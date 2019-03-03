/*  =========================================================================
    mql_server - class description

    This Source Code Form is subject to the terms of the Mozilla Public
    License, v. 2.0. If a copy of the MPL was not distributed with this
    file, You can obtain one at http://mozilla.org/MPL/2.0/.
    =========================================================================
*/

/*
@header
    mql_server -
@discuss
@end
*/

#include "mql_classes.h"
#include <jansson.h>

struct _mql_server_t {
    zsock_t* pipe;
    zhttp_server_options_t *http_options;
    zhttp_server_t *http_server;
    zhttp_request_t *request;
    zhttp_response_t *response;
    uint64_t next_id;
    zhashx_t *connections;
    zsock_t* http_worker;
    char endpoint[256];

    zhashx_t *mailboxes;
    aws_t    *aws;
    zpoller_t *poller;
    ztimerset_t *timerset;

    bool terminated;
};

static void s_refresh_credentials_interval (int timer_id, mql_server_t *self);

static mailbox_t *
s_get_mailbox (mql_server_t *self, const char *address);

static mql_server_t *
server_new (zconfig_t* config, zsock_t *pipe) {
    assert (config);

    mql_server_t *self = (mql_server_t*) zmalloc (sizeof (mql_server_t));
    assert (self);

    self->pipe = pipe;
    self->http_options = zhttp_server_options_new ();
    char* port_str = zconfig_get (config, "server/port", "34543");
    int port = atoi (port_str);
    zhttp_server_options_set_port (self->http_options, port);
    self->http_server = zhttp_server_new (self->http_options);
    self->http_worker = zsock_new_dealer (NULL);
    zsock_connect (self->http_worker, "%s", zhttp_server_options_backend_address (self->http_options));
    self->request = zhttp_request_new ();
    self->response = zhttp_response_new ();
    self->next_id =  (((uint64_t) rand() <<  0) & 0x00000000FFFFFFFFull) |
                     (((uint64_t) rand() << 32) & 0xFFFFFFFF00000000ull);
    self->connections = zhashx_new ();
    zhashx_set_key_duplicator (self->connections, NULL); // Connection takes ownership of the key
    self->mailboxes = zhashx_new ();
    zhashx_set_destructor (self->mailboxes, (czmq_destructor *) mailbox_destroy);
    self->timerset = ztimerset_new ();

    self->aws = aws_new ();

    char* access_key = zconfig_get (config, "aws/access_key", NULL);
    char* secret = zconfig_get (config, "aws/secret", NULL);
    char* region = zconfig_get (config, "aws/region", NULL);
    char* aws_endpoint = zconfig_get (config, "aws/endpoint", NULL);

    if (region && access_key && secret) {
        aws_set (self->aws, region, access_key, secret, aws_endpoint);

        ziflist_t *iflist = ziflist_new ();
        ziflist_first (iflist);

        snprintf (self->endpoint, 255, "http://%s:%d", ziflist_address (iflist), port);

        ziflist_destroy (&iflist);
    }
    else {
        // Request credentials from aws metadata
        int rc = aws_refresh_credentials_sync (self->aws);
        assert (rc == 0);

        // We will refresh the credentials every four minutes
        // TODO: instead of a fix interval we should refresh 4 minutes before expiry
        ztimerset_add (self->timerset, 1000 * 60 * 4, (ztimerset_fn *) s_refresh_credentials_interval, self);

        snprintf (self->endpoint, 255, "http://%s:%d", aws_private_ip_address (self->aws), port);
    }

    zsys_info ("Server: server endpoint is %s", self->endpoint);

    self->poller = zpoller_new (pipe, self->http_worker, aws_get_socket (self->aws), NULL);
    zpoller_set_nonstop (self->poller, true);
    self->terminated = false;

    return self;
}

static void
server_destroy (mql_server_t **self_p) {
    assert (self_p);
    mql_server_t *self = *self_p;

    if (self) {
        zhttp_request_destroy (&self->request);
        zhttp_response_destroy (&self->response);
        zsock_destroy (&self->http_worker);
        zhttp_server_destroy (&self->http_server);
        zhttp_server_options_destroy (&self->http_options);
        zhashx_destroy (&self->connections);

        ztimerset_destroy (&self->timerset);
        zhashx_destroy (&self->mailboxes);
        aws_destroy (&self->aws);
        zpoller_destroy (&self->poller);
    }
}

void s_refresh_credentials_interval (int timer_id, mql_server_t *self) {
    zsys_info ("Server: refreshing credentials");

    aws_refresh_credentials (self->aws);
}

static void
server_recv_api (mql_server_t* self) {
    char* command = zstr_recv (self->pipe);

    // Interrupted
    if (!command)
        return;

    if (streq (command, "$TERM"))
        self->terminated = true;
}


int
mql_server_send_error (mql_server_t *self, const char *to, uint32_t status_code, const char* body) {
    const char *delimiter = strchr (to, '/');

    // We only forward errors to http requests
    if (delimiter == NULL) {
        void *connection = zhashx_lookup (self->connections, to);

        if (connection == NULL) {
            return -1;
        }

        // TODO: build the response

        zhttp_response_set_status_code (self->response, status_code);
        zhttp_response_set_content_const (self->response, body);
        zhttp_response_send (self->response, self->http_worker, &connection);

        zhashx_delete (self->connections, to);
    }
}

int
mql_server_send (mql_server_t *self, const char *to, const char *from, const char *subject, json_t **body) {

    // Check if an http connection
    if (strncmp ("$http/", to, 6) == 0) {
        void *connection = zhashx_lookup (self->connections, to);

        if (connection == NULL) {
            zsys_warning ("Sever: reply to dead http connection from %s", from);
            json_decref (*body);
            return -1;
        }

        json_t *root = json_pack ("{ssssso?}", "from", from, "subject", subject, "body", *body);
        *body = NULL;

        char *content = json_dumps (root, JSON_COMPACT);
        json_decref (root);

        zhttp_response_set_status_code (self->response, 200);
        zhttp_response_set_content (self->response, &content);
        zhttp_response_send (self->response, self->http_worker, &connection);

        zhashx_delete (self->connections, to);
    }
    else {
        mailbox_t *mailbox = s_get_mailbox (self, to);

        return mailbox_send (mailbox, from, subject, body);
    }
}

static void
server_recv_http (mql_server_t* self) {
    void *connection = zhttp_request_recv (self->request, self->http_worker);

    const char* method = zhttp_request_method (self->request);
    const char *url = zhttp_request_url (self->request);

    char* actor_id;
    char* actor_type;
    char* subject;

    zsys_info ("Server: new request %s %s", method, url);

    if (zhttp_request_match (self->request, "POST", "/send/%s/%s/%s", &actor_type, &actor_id, &subject)) {

        json_error_t error;
        json_t *body = json_loads (zhttp_request_content (self->request), 0, &error);

        if (body == NULL) {
            zsys_warning ("Server: invalid json received");
            zhttp_response_set_status_code (self->response, 400);
            zhttp_response_set_content_const (self->response, "{\"error\": \"invalid json\"}");
            zhttp_response_send (self->response, self->http_worker, &connection);
            return;
        }

        char *address = zsys_sprintf ("%s/%s", actor_type, actor_id);
        mailbox_t *mailbox = s_get_mailbox (self, address);

        char *from = zsys_sprintf ("$http/%" PRIu64, self->next_id);
        self->next_id++;

        // Insert the new connection, hash take ownership of the new from
        zhashx_insert (self->connections, from, connection);

        //  Queuing the message on the worker, the worker is responsible to reply to the client through the return address
        mailbox_send (
                mailbox,
                from,
                subject,
                &body);

        zstr_free (&address);
    }
    else {
        zsys_warning ("Server: not found %s %s", method, url);
        zhttp_response_set_status_code (self->response, 404);
        zhttp_response_set_content_const (self->response, "Not found");
        zhttp_response_send (self->response, self->http_worker, &connection);
    }
}

void
mql_server_actor (zsock_t *pipe, void *arg) {
    mql_server_t *self = server_new (arg, pipe);
    zsock_signal (pipe, 0);

    zsys_info ("Server: listening on port %d", zhttp_server_port (self->http_server));

    while (!self->terminated) {
        void* which = zpoller_wait (self->poller, ztimerset_timeout (self->timerset));
        ztimerset_execute (self->timerset);

        if (which == pipe)
            server_recv_api (self);
        else if (which == self->http_worker)
            server_recv_http (self);
        else if (which == aws_get_socket (self->aws))
            aws_execute (self->aws);
    }

    server_destroy (&self);
}

static mailbox_t *
s_get_mailbox (mql_server_t *self, const char *address) {
    mailbox_t *mailbox = (mailbox_t *) zhashx_lookup (self->mailboxes, address);
    if (!mailbox) {
        mailbox = mailbox_new (address, self->aws, self);
        assert (mailbox);
        zhashx_insert (self->mailboxes, address, mailbox);
    }

    return mailbox;
}


//  --------------------------------------------------------------------------
//  Create a new mql_server

zactor_t *
mql_server_new (zconfig_t *config)
{
    return zactor_new (mql_server_actor, config);
}


//  --------------------------------------------------------------------------
//  Destroy the mql_server

void
mql_server_destroy (zactor_t **self_p)
{
    zactor_destroy (self_p);
}

//  --------------------------------------------------------------------------
//  Self test of this class

// If your selftest reads SCMed fixture data, please keep it in
// src/selftest-ro; if your test creates filesystem objects, please
// do so under src/selftest-rw.
// The following pattern is suggested for C selftest code:
//    char *filename = NULL;
//    filename = zsys_sprintf ("%s/%s", SELFTEST_DIR_RO, "mytemplate.file");
//    assert (filename);
//    ... use the "filename" for I/O ...
//    zstr_free (&filename);
// This way the same "filename" variable can be reused for many subtests.
#define SELFTEST_DIR_RO "src/selftest-ro"
#define SELFTEST_DIR_RW "src/selftest-rw"

void
mql_server_test (bool verbose)
{
    printf (" * mql_server: ");

    //  @selftest
    //  Simple create/destroy test
//    mql_server_t *self = mql_server_new ();
//    assert (self);
//    mql_server_destroy (&self);
    //  @end
    printf ("OK\n");
}
