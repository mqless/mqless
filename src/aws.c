#include "mql_classes.h"
#include <jansson.h>

#define LAMBDA_SERVICE_NAME "lambda"
#define DATETIME_LEN 17

typedef enum {
    REGION,
    ROLE,
    CREDENTIALS,
    DONE,
    ERROR
} credentials_state_t;

struct _aws_t {
    zhttp_client_t *http_client;
    aws_sign_t *sign;

    bool secure;
    char host[256];
    char access_key[256];
    char secret[256];
    char region[256];
    char role[256];
    char *session_token;

    zhttp_request_t *request;
    zhttp_response_t *response;
    credentials_state_t credentials_state;
};

static void get_datetime (char *str) {
    time_t t = time (NULL);
    struct tm *datetime = gmtime (&t);

    sprintf (str, "%d%02d%02dT%02d%02d%02dZ",
                    datetime->tm_year + 1900, datetime->tm_mon + 1, datetime->tm_mday,
                    datetime->tm_hour, datetime->tm_min, datetime->tm_sec);
}

aws_t *aws_new () {
    aws_t *self = (aws_t *) zmalloc (sizeof (aws_t));
    assert (self);

    self->http_client = zhttp_client_new (false);
    self->secure = true;
    self->request = zhttp_request_new ();
    self->response = zhttp_response_new ();
    self->credentials_state = DONE;

    strcpy (self->region, "");

    return self;
}

void aws_set (aws_t *self, const char* region, const char *access_key,
              const char *secret) {
    assert (self);

    aws_sign_destroy (&self->sign);
    self->sign = aws_sign_new (access_key, secret, region, LAMBDA_SERVICE_NAME);
    sprintf (self->host, "%s.%s.amazonaws.com", LAMBDA_SERVICE_NAME, region);
    strcpy (self->access_key, access_key);
    strcpy (self->secret, secret);
    strcpy (self->region, region);
}

void aws_destroy (aws_t **self_p) {
    assert (self_p);
    aws_t *self = *self_p;

    if (self) {
        zhttp_client_destroy (&self->http_client);
        zhttp_request_destroy (&self->request);
        zhttp_response_destroy (&self->response);
        aws_sign_destroy (&self->sign);
        zstr_free (&self->session_token);

        memset (self->secret, 0, strlen (self->secret));

        free (self);

        *self_p = NULL;
    }
}

static void aws_security_credentials_callback (aws_t *self, zhttp_response_t *response);
static void aws_security_credentials_role_callback (aws_t *self, zhttp_response_t *response);

static void aws_security_credentials_region_callback (aws_t *self, zhttp_response_t *response) {
    if (zhttp_response_status_code (response) != 200) {
        zsys_error ("AWS: fail to retrieve region %d %s",
                    zhttp_response_status_code (response),
                    zhttp_response_content (response));

        self->credentials_state = ERROR;
        return;
    }

    json_error_t error;
    json_t *root = json_loads (zhttp_response_content (response), 0, &error);

    if (root == NULL) {
        zsys_error ("AWS: fail to parse json %s", error.text);
        self->credentials_state = ERROR;
        return;
    }

    if (!json_is_object (root)) {
        zsys_error ("AWS: json object expected");
        json_decref (root);
        self->credentials_state = ERROR;
        return;
    }

    json_t *region = json_object_get (root, "region");
    if (!json_is_string (region)) {
        zsys_error ("AWS: region expected to be string");
        json_decref (root);
        self->credentials_state = ERROR;
        return;
    }

    strcpy (self->region, json_string_value (region));
    json_decref (root);

    self->credentials_state = ROLE;

    char *url = "http://169.254.169.254/latest/meta-data/iam/security-credentials/";
    zhttp_request_set_url (self->request, url);
    zhttp_request_set_method (self->request, "GET");
    zhttp_request_send (self->request, self->http_client, 10000, aws_security_credentials_role_callback, self);
}

static void aws_security_credentials_role_callback (aws_t *self, zhttp_response_t *response) {

    const char* role = zhttp_response_content (response);

    if (zhttp_response_status_code (response) != 200 || role == NULL || streq (role, "")) {
        zsys_error ("AWS: fail to retrieve role %d %s",
                    zhttp_response_status_code (response),
                    zhttp_response_content (response));

        self->credentials_state = ERROR;
        return;
    }

    char *url = zsys_sprintf (
            "http://169.254.169.254/latest/meta-data/iam/security-credentials/%s", role);

    self->credentials_state = CREDENTIALS;
    strcpy (self->role, role);

    zhttp_request_set_url (self->request, url);
    zhttp_request_set_method (self->request, "GET");
    zhttp_request_send (self->request, self->http_client, 10000, aws_security_credentials_callback, self);
}

static void aws_security_credentials_callback (aws_t *self, zhttp_response_t *response) {
    if (zhttp_response_status_code (response) != 200) {
        zsys_error ("AWS: fail to retrieve credentials %d %s",
                zhttp_response_status_code (response),
                zhttp_response_content (response));

        self->credentials_state = ERROR;
        return;
    }

    json_error_t error;
    json_t *root = json_loads (zhttp_response_content (response), 0, &error);

    if (root == NULL) {
        zsys_error ("AWS: fail to parse json %s", error.text);
        self->credentials_state = ERROR;
        return;
    }

    if (!json_is_object (root)) {
        zsys_error ("AWS: json object expected");
        json_decref (root);
        self->credentials_state = ERROR;
        return;
    }

    json_t *code = json_object_get (root, "Code");
    if (!json_is_string (code)) {
        zsys_error ("AWS: code expected to be string");
        json_decref (root);
        self->credentials_state = ERROR;
        return;
    }

    if (strneq ("Success", json_string_value (code))) {
        zsys_error ("AWS: failed to retrieve security credentials %s", json_string_value (code));
        json_decref (root);
        self->credentials_state = ERROR;
        return;
    }

    json_t *access_key = json_object_get (root, "AccessKeyId");
    json_t *secret = json_object_get (root, "SecretAccessKey");
    json_t *token = json_object_get (root, "Token");

    if (!json_is_string (access_key) || !json_is_string (secret) || !json_is_string (token)) {
        zsys_error ("AWS: access_key or secret are missing");
        json_decref (root);
        self->credentials_state = ERROR;
        return;
    }

    zstr_free (&self->session_token);
    self->session_token = strdup (json_string_value (token));
    aws_set (self, self->region, json_string_value (access_key), json_string_value (secret));

    json_decref (root);

    self->credentials_state = DONE;
}

void aws_refresh_credentials (aws_t *self) {

    if (strneq(self->region, "")) {
        char *url = "http://169.254.169.254/latest/meta-data/iam/security-credentials/";

        self->credentials_state = ROLE;

        zhttp_request_set_url (self->request, url);
        zhttp_request_set_method (self->request, "GET");
        zhttp_request_send (self->request, self->http_client, 10000, aws_security_credentials_role_callback, self);
    }
    else {
        const char *url = "http://169.254.169.254/latest/dynamic/instance-identity/document";

        self->credentials_state = REGION;

        zhttp_request_set_url (self->request, url);
        zhttp_request_set_method (self->request, "GET");
        zhttp_request_send (self->request, self->http_client, 10000, aws_security_credentials_region_callback, self);
    }
}

int aws_refresh_credentials_sync (aws_t *self) {

    // Invoke the async version
    aws_refresh_credentials (self);

    // Now waiting for the response
    while (self->credentials_state != DONE && self->credentials_state != ERROR) {
        aws_lambda_callback_fn *callback;
        void *arg;
        int rc = zhttp_response_recv (self->response, self->http_client, (void **) &callback, &arg);
        if (rc == -1) {
            zsys_error ("AWS: fail to retrieve credentials %s", zmq_strerror (errno));
            return rc;
        }

        callback(arg, self->response);
    }

    zsys_info ("AWS: credentials fetched for region %s, role %s", self->region, self->role);

    return self->credentials_state == DONE ? 0 : -1;
}

int aws_invoke_lambda (
        aws_t *self,
        const char *function_name,
        char **content,
        aws_lambda_callback_fn callback,
        void *arg) {

    char datetime[DATETIME_LEN];
    get_datetime (datetime);

    char path[256];
    sprintf (path, "/2015-03-31/functions/%s/invocations", function_name);

    char authorization_header[MAX_AUTHORIZATION_LEN];
    aws_sign (self->sign, authorization_header, "POST", self->host, path, "", datetime, *content);

    zhash_t *headers = zhttp_request_headers (self->request);

    zhash_insert (headers, "X-Amz-Invocation-Type", "RequestResponse");
    zhash_insert (headers, "X-Amz-Log-Type", "None");
    zhash_insert (headers, "Content-Type", "application/json");
    zhash_insert (headers, "Authorization", authorization_header);
    zhash_insert (headers, "X-Amz-Date", datetime);

    if (self->session_token)
        zhash_insert (headers, "X-Amz-Security-Token", self->session_token);

    char url[2000];
    sprintf(url, "%s://%s%s", self->secure ? "https" : "http", self->host, path);

    // TODO: get timeout from configuration
    zhttp_request_set_content (self->request, content);
    zhttp_request_set_method (self->request, "POST");
    zhttp_request_set_url (self->request, url);
    zhttp_request_send (self->request, self->http_client, -1, callback, arg);

    return 0;
}

int aws_execute (aws_t *self) {
    zsock_t* sock = aws_get_socket (self);

    while (zsock_has_in (sock)) {
        aws_lambda_callback_fn *callback;
        void* arg;
        int rc = zhttp_response_recv (self->response, self->http_client, (void**) &callback, &arg);
        if (rc == -1)
            return rc;

        callback(arg, self->response);
    }

    return 0;
}

zsock_t *aws_get_socket (aws_t *aws) {
    return zactor_sock ((zactor_t*) aws->http_client);
}

static void aws_test_callback (void *arg, int response_code, zchunk_t *payload) {
    assert (response_code == 200);
    bool *event = (bool *)arg;
    *event = true;

    char *response = zchunk_strdup (payload);

    zchunk_destroy (&payload);
    zstr_free (&response);
}

static zchunk_t *
recv_http_request(void* server) {
    zchunk_t *routing_id;
    char *request;
    int rc = zsock_recv (server, "cs", &routing_id, &request);
    assert (rc == 0);

    while (strlen (request) == 0) {
        zchunk_destroy (&routing_id);
        zstr_free (&request);
        zsock_recv (server, "cs", &routing_id, &request);
        assert (rc == 0);
    }

    zstr_free (&request);

    return routing_id;
}

void aws_test () {
//    //  Creating http server for local tests
//    zsock_t *server = zsock_new_stream (NULL);
//    int port = zsock_bind (server, "tcp://127.0.0.1:*");
//    char host[256];
//    sprintf (host, "127.0.0.1:%d", port);
//
//    //  Create the aws client
//    aws_t *self = aws_new ();
//    aws_set (self, "AKIDEXAMPLE", "wJalrXUtnFEMI/K7MDENG+bPxRfiCYEXAMPLEKEY", "us-west-2");
//    strcpy (self->host, host);
//    self->secure = false;
//
//    //  Invoke the lambda
//    bool event = false;
//    zchunk_t *data = zchunk_new ("\"hello\"", 7);
//    aws_invoke_lambda (self, "hello", data, &event, aws_test_callback);
//
//    //  Receive the request
//    zchunk_t *routing_id = recv_http_request (server);
//
//    //  Send the response
//    zsock_send (server, "cs", routing_id, "HTTP/1.1 200 OK\r\nContent-Length: 7\r\n\r\n\"World\"");
//
//    //  Waiting for the response
//    int rc = zhttp_client_wait (self->http_client, -1);
//    assert (rc == 0);
//    rc = aws_execute (self);
//    assert (rc == 0);
//    assert (event);
//
//    //  Cleanup
//    zchunk_destroy (&routing_id);
//    zchunk_destroy (&data);
//    aws_destroy (&self);
//    zsock_destroy (&server);
}