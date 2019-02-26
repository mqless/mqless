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


struct _mql_server_t {
    zsock_t* pipe;
    zhttp_server_options_t *http_options;
    zhttp_server_t *http_server;
    zhttp_request_t *request;
    zhttp_response_t *response;
    zsock_t* http_worker;
    char endpoint[256];

    zhashx_t *mailboxes;
    aws_t    *aws;
    zpoller_t *poller;
    ztimerset_t *timerset;

    bool terminated;
};

static void s_refresh_credentials_interval (int timer_id, mql_server_t *self);

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

static void
server_send_response (mql_server_t *self, void **connection, zhttp_response_t *response) {
    zhttp_response_send (response, self->http_worker, connection);
}

static void
server_recv_http (mql_server_t* self) {
    void *connection = zhttp_request_recv (self->request, self->http_worker);

    const char* method = zhttp_request_method (self->request);
    const char *url = zhttp_request_url (self->request);

    char* address;
    char* function;

    if (!zhttp_request_match (self->request, "POST", "/send/%s/%s", &function, &address)) {
        zsys_warning ("Server: not found %s %s", method, url);
        zhttp_response_set_status_code (self->response, 404);
        zhttp_response_set_content_const (self->response, "Not found");
        zhttp_response_send (self->response, self->http_worker, &connection);

        return;
    }

    char *payload = zhttp_request_get_content (self->request);
    zsys_info ("Server: new request %s %s", method, url);

    mailbox_t *mailbox = (mailbox_t *) zhashx_lookup (self->mailboxes, address);
    if (!mailbox) {
        mailbox = mailbox_new (address, self->aws, self, (mailbox_callback_fn *)server_send_response);
        assert (mailbox);
        zhashx_insert (self->mailboxes, address, mailbox);
    }

    //  Queuing the message on the worker, the worker is responsible to reply to the client through the return address
    mailbox_send (
            mailbox,
            function,
            MQL_INVOCATION_TYPE_REQUEST_RESPONSE, // TODO: figure out the invocacation type,
            &payload,
            connection);
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

const char *
mql_server_endpoint (mql_server_t *self) {
    return self->endpoint;
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
