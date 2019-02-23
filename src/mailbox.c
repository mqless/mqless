#include "mql_classes.h"
#include <string.h>

struct _mailbox_t {
    char *address;
    zlistx_t *queue;
    void *server;
    mailbox_callback_fn *callback;
    aws_t *aws;
    bool inprogress;
};

typedef struct {
    mailbox_t *parent;
    char *function;
    int invocation_type;
    char* payload;
    void* connection;
} mailbox_item_t;

static void mailbox_item_callback (mailbox_item_t *self, zhttp_response_t *response);

static mailbox_item_t *lambda_request_new (mailbox_t *parent,
                                           const char *function,
                                           int invocation_type,
                                           char* payload, void* connection) {
    mailbox_item_t *self = (mailbox_item_t *) zmalloc (sizeof (mailbox_item_t));
    self->parent = parent;
    self->function = strdup (function);
    self->invocation_type = invocation_type;
    self->payload = payload;
    self->connection = connection;

    return self;
}

static void mailbox_item_destroy (mailbox_item_t **self_p) {
    mailbox_item_t *self = *self_p;
    zstr_free (&self->function);
    zstr_free (&self->payload);
    free (self);
    *self_p = NULL;
}

static char *
mailbox_item_create_content (mailbox_item_t *self) {
    static const char* address_key = "{\"address\":\""; //
    static const char* payload_key = "\",\"payload\":";
    static const char* close_bracket = "}";
    size_t address_key_size = strlen(address_key);
    size_t payload_key_size = strlen(payload_key);
    size_t close_bracket_size = strlen(close_bracket);

    size_t payload_size = strlen(self->payload);

    size_t content_size =
            address_key_size +
            strlen(self->parent->address) +
            payload_key_size +
            payload_size +
            close_bracket_size;

    char* content = (char*) malloc (content_size + 1);
    char* needle = content;

    memcpy (needle, address_key, address_key_size);
    needle += address_key_size;

    memcpy (needle, self->parent->address, strlen (self->parent->address));
    needle += strlen (self->parent->address);

    memcpy (needle, payload_key, payload_key_size);
    needle += payload_key_size;

    memcpy (needle, self->payload, payload_size);
    needle += payload_size;

    memcpy (needle, close_bracket, close_bracket_size);
    needle += close_bracket_size;

    *needle = '\0';

    assert (needle - content == content_size);

    return content;
}

mailbox_t *
mailbox_new (const char *address, aws_t *aws, void *server, mailbox_callback_fn *callback) {
    mailbox_t *self = (mailbox_t *) zmalloc (sizeof (mailbox_t));
    assert (self);
    self->address = strdup (address);

    self->queue = zlistx_new ();
    zlistx_set_destructor (self->queue, (zlistx_destructor_fn *) mailbox_item_destroy);

    self->server = server;
    self->callback = callback;
    self->aws = aws;
    self->inprogress = false;

    return self;
}

void mailbox_destroy (mailbox_t **self_p) {
    mailbox_t *self = *self_p;
    zstr_free (&self->address);
    zlistx_destroy (&self->queue);

    free (self);
    *self_p = NULL;
}

static void mailbox_next (mailbox_t *self) {
    // Dequeue the next request
    zlistx_first (self->queue);
    mailbox_item_t *next = (mailbox_item_t *) zlistx_detach_cur (self->queue);

    if (next) {
        zsys_info ("mailbox: invoking function. address = %s, function: %s", self->address, next->function);
        self->inprogress = true;
        char* content = mailbox_item_create_content (next);

        aws_invoke_lambda (self->aws, next->function, &content,
                (aws_lambda_callback_fn*)mailbox_item_callback, next);
    } else {
        self->inprogress = false;
    }
}

static void mailbox_item_callback (mailbox_item_t *self, zhttp_response_t *response) {
    zsys_info ("mailbox: function completed. address: %s, status code: %d, function: %s",
               self->parent->address, zhttp_response_status_code (response),
               self->function);

    if (self->invocation_type == MQL_INVOCATION_TYPE_REQUEST_RESPONSE) {
        byte source;

        uint32_t status_code = zhttp_response_status_code (response);
        if (status_code >= 200 && status_code < 300)
            source = MQL_SOURCE_FUNCTION;
        else {
            // TODO: we need to fetch the FunctionError header from the response in order to know
            // if the source is provider or function
            source = MQL_SOURCE_PLATFORM;
        }

        // TODO: dont pass the request as is, make msg object and pass it. In order to support other transport

        self->parent->callback (self->parent->server, &self->connection, response);
    }

    mailbox_next (self->parent);
    mailbox_item_destroy (&self);
}

int mailbox_send (
        mailbox_t *self,
        const char *function,
        int invocation_type,
        char **payload,
        void* connection) {

    mailbox_item_t *item = lambda_request_new (self, function, invocation_type, *payload, connection);
    zlistx_add_end (self->queue, item);

    zsys_info ("mailbox: new function request. address: %s, function: %s", self->address, function);

    if (!self->inprogress)
        mailbox_next (self);

    *payload = NULL;

    return 0;
}