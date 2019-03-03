#include "mql_classes.h"
#include <string.h>
#include <jansson.h>

typedef struct {
    mailbox_t *parent;
    char *from;
    char *subject;
    json_t *body;
    void *connection;
} mailbox_item_t;

struct _mailbox_t {
    char *address;
    char *actor_type;
    zlistx_t *queue;
    mql_server_t *server;
    aws_t *aws;
    bool inprogress;
};

static void mailbox_item_callback (mailbox_item_t *self, zhttp_response_t *response);

static mailbox_item_t *lambda_request_new (mailbox_t *parent,
                                           const char *from,
                                           const char *subject,
                                           json_t *body) {
    mailbox_item_t *self = (mailbox_item_t *) zmalloc (sizeof (mailbox_item_t));
    self->parent = parent;
    self->from = strdup (from);
    self->subject = strdup (subject);
    self->body = body;

    return self;
}

static void mailbox_item_destroy (mailbox_item_t **self_p) {
    mailbox_item_t *self = *self_p;
    zstr_free (&self->from);
    zstr_free (&self->subject);

    if (self->body)
        json_decref (self->body);
    self->body = NULL;

    free (self);
    *self_p = NULL;
}

static char *
mailbox_item_create_content (mailbox_item_t *self) {

    json_t *root = json_pack ("{ssssssso?}", "subject",
        self->subject, "from", self->from, "address", self->parent->address, "body", self->body);
    self->body = NULL;

    char *content = json_dumps (root, JSON_COMPACT);
    json_decref (root);

    return content;
}

mailbox_t *
mailbox_new (const char *address, aws_t *aws, mql_server_t *server) {
    mailbox_t *self = (mailbox_t *) zmalloc (sizeof (mailbox_t));
    assert (self);
    self->address = strdup (address);

    const char *delimiter = strchr (address, '/');
    assert (delimiter);

    self->actor_type = (char *) zmalloc (delimiter - address + 1);
    memcpy (self->actor_type, address, delimiter - address);

    self->queue = zlistx_new ();
    zlistx_set_destructor (self->queue, (zlistx_destructor_fn *) mailbox_item_destroy);

    self->server = server;
    self->aws = aws;
    self->inprogress = false;

    return self;
}

void mailbox_destroy (mailbox_t **self_p) {
    mailbox_t *self = *self_p;
    zstr_free (&self->address);
    zstr_free (&self->actor_type);
    zlistx_destroy (&self->queue);

    free (self);
    *self_p = NULL;
}

static void mailbox_next (mailbox_t *self) {
    // Dequeue the next request
    zlistx_first (self->queue);
    mailbox_item_t *next = (mailbox_item_t *) zlistx_detach_cur (self->queue);

    if (next) {
        zsys_info ("mailbox: invoking function. address: %s, subject: %s", self->address, next->subject);
        self->inprogress = true;
        char *content = mailbox_item_create_content (next);

        aws_invoke_lambda (self->aws, self->actor_type, &content,
                           (aws_lambda_callback_fn *) mailbox_item_callback, next);
    } else
        self->inprogress = false;
}

static int mailbox_item_send_message (mailbox_item_t *self, json_t *message, const char *from) {
    if (!json_is_object (message)) {
        zsys_warning ("Mailbox: Actor %s returned invalid message. subject = %s", self->parent->actor_type, self->subject);
        mql_server_send_error (self->parent->server, self->from, 400, "{\"body\": \"Invalid message\"}");
        return -1;
    }

    json_t *to = json_object_get (message, "to");
    json_t *body = json_object_get (message, "body");
    json_t *subject = json_object_get (message, "subject");

    if (to == NULL || !json_is_string (to) || subject == NULL || !json_is_string (subject)) {
        zsys_warning ("Mailbox: Actor %s returned invalid message. Subject = %s", self->parent->actor_type, self->subject);
        mql_server_send_error (self->parent->server, self->from, 400, "{\"body\": \"Invalid message\"}");
        return -1;
    }

    const char *to_str = json_string_value (to);
    const char *subject_str = json_string_value (subject);

    if (body)
        json_incref (body);

    mql_server_send (self->parent->server, to_str, from, subject_str, &body);

    return 0;
}

static int mailbox_item_parse_json (mailbox_item_t *self, zhttp_response_t *response) {
    json_error_t error;
    json_t *root = json_loads (zhttp_response_content (response), 0, &error);

    if (root == NULL) {
        json_decref (root);
        return -1;
    }

    int rc;

    json_t *send = json_object_get (root, "send");
    if (send) {
        // Send can either be an object or array
        if (json_is_object (send)) {
            rc = mailbox_item_send_message (self, send, self->parent->address);
            if (rc != 0) {
                json_decref (root);
                return rc;
            }
        } else if (json_is_array (send)) {
            size_t index;
            json_t *value;
            json_array_foreach (send, index, value) {
                rc = mailbox_item_send_message (self, value, self->parent->address);
                if (rc != 0) {
                    json_decref (root);
                    return rc;
                }
            }
        } else {
            zsys_error ("Mailbox: Invalid send returned from actor. address: %s, subject: %s", self->parent->address,
                        self->subject);
            json_decref (root);
            return -1;
        }
    }

    json_t *forward = json_object_get (root, "forward");

    // Returned json can be forward or a reply, not both
    if (forward) {
        rc = mailbox_item_send_message (self, forward, self->from);
        if (rc != 0) {
            json_decref (root);
            return rc;
        }
    }
    else {
        json_t *body = json_object_get (root, "body");
        json_t *subject = json_object_get (root, "subject");

        // If body or subject it is an immediate reply
        if (subject) {
            const char *subject_str = json_string_value (subject);

            if (body)
                json_incref (body);

            mql_server_send (self->parent->server, self->from, self->parent->address, subject_str, &body);
        }

        if (body && !subject) {
            zsys_error ("Mailbox: subject is mandatory. address: %s", self->parent->address);
            json_decref (root);
            return -1;
        }
    }

    json_decref (root);

    return 0;
}


static void mailbox_item_callback (mailbox_item_t *self, zhttp_response_t *response) {
    zsys_info ("mailbox: function completed. address: %s, subject: %s, status code: %d",
               self->parent->address,
               self->subject,
               zhttp_response_status_code (response));

    zhash_t *headers = zhttp_response_headers (response);
    bool has_error = zhash_lookup (headers, "X-Amz-Function-Error") != NULL || zhash_lookup (headers, "x-amz-function-error");

    uint32_t status_code = zhttp_response_status_code (response);
    if (status_code >= 300 || has_error) {
        if (status_code >= 200 && status_code < 300)
            status_code = 400;

        mql_server_send_error (self->parent->server, self->from, status_code, zhttp_response_content (response));

        mailbox_next (self->parent);
        mailbox_item_destroy (&self);
        return;
    }

    int rc = mailbox_item_parse_json (self, response);

    if (rc != 0) {
        zsys_error ("Mailbox: Invalid json returned from actor. address: %s, from: %s, subject: %s",
                    self->parent->address, self->from, self->subject);
        mql_server_send_error (self->parent->server, self->from, 400, "{\"body\": \"Invalid json\"}");
    }

    mailbox_next (self->parent);
    mailbox_item_destroy (&self);
}

int mailbox_send (
        mailbox_t *self,
        const char *from,
        const char *subject,
        json_t **body) {

    mailbox_item_t *item = lambda_request_new (self, from, subject, *body);
    zlistx_add_end (self->queue, item);

    zsys_info ("mailbox: new message. address: %s, from: %s, subject: %s", self->address, from, subject);

    if (!self->inprogress)
        mailbox_next (self);

    *body = NULL;

    return 0;
}