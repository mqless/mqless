#include "mql_classes.h"

int main (int argc, char **argv) {
    zargs_t *args = zargs_new (argc, argv);

    if (zargs_hasx (args, "--help", "-h", NULL) || zargs_arguments (args) != 3) {
        puts ("Usage: mqless-client [--s mqless-address] function address message");
        puts (" Message must be valid json.");
        return 0;
    }

    const char *function = zargs_next (args);
    const char *address = zargs_first (args);
    const char *message = zargs_next (args);

    const char *server_address = "http://127.0.0.1:34543";
    if (zargs_has (args,"-s"))
        server_address = zargs_get (args, "-s");

    zhttp_client_t* client = zhttp_client_new (false);
    zhttp_request_t *request = zhttp_request_new ();

    char* url = zsys_sprintf ("%s/send/%s/%s", server_address, function, address);
    printf ("%s\n", url);

    zhttp_request_set_url (request, url);
    zhttp_request_set_method (request, "POST");
    zhttp_request_set_content_const (request, message);

    int rc = zhttp_request_send (request, client, -1, NULL, NULL);

    if (rc == -1) {
        fprintf (stdout, "Error: fail to send a message to server. %s", zmq_strerror (errno));
        zstr_free (&url);
        zhttp_request_destroy (&request);
        zhttp_client_destroy (&client);

        return 1;
    }

    void *arg1;
    void *arg2;
    zhttp_response_t *response = zhttp_response_new ();
    rc = zhttp_response_recv (response, client, &arg1, &arg2);

    if (rc == -1) {
        fprintf (stdout, "Error: fail to receive a message from server. %s", zmq_strerror (errno));
        zhttp_response_destroy (&response);
        zstr_free (&url);
        zhttp_request_destroy (&request);
        zhttp_client_destroy (&client);

        return 1;
    }

    printf ("Status Code: %d\n%s\n", zhttp_response_status_code (response),
            zhttp_response_content (response));

    zhttp_response_destroy (&response);
    zstr_free (&url);
    zhttp_request_destroy (&request);
    zhttp_client_destroy (&client);

    return 0;
}