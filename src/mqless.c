#include "mql_classes.h"

int main (int argc, char **argv) {
    zsys_init ();
    zsys_set_pipehwm (0);
    zsys_set_sndhwm (0);
    zsys_set_rcvhwm (0);

    zargs_t *args = zargs_new (argc, argv);

    //  Collect configuration file name
    const char *config_file = "mqless.cfg";
    const char *aws_access_key = NULL;
    const char *aws_secret = NULL;
    const char *aws_region = NULL;
    int port = -1;

    for (const char* value = zargs_param_first (args); value != NULL; value = zargs_param_next (args)) {
        const char* name = zargs_param_name (args);

        if (streq (name, "--config") || streq(name, "-c"))
            config_file = value;
        else if (streq (name, "--aws-access-key"))
            aws_access_key = value;
        else if (streq (name, "--aws-secret"))
            aws_secret = value;
        else if (streq (name, "--aws-region"))
            aws_region = value;
        else if (streq (name, "--port") || streq(name, "-p"))
            port = atoi (value);
        else if (streq (name, "--help") || streq (name, "-h")) {
            puts ("Usage: mqless [options...]");
            puts (" -c, --config <file>\t\t\tLoad config-file");
            puts (" -p, --port <port>\t\t\tListening port");
            puts ("     --aws-access-key <access-key>\tSet aws access key");
            puts ("     --aws-secret <secret>\t\tSet aws secret");
            puts ("     --aws-region <region>\t\tSet aws region");
            puts (" -h, --help\t\t\t\tThis help text");
            puts (" Default config-file is 'mqless.cfg'");

            return 0;
        }
        else {
            fprintf (stderr, "invalid parameter %s\n", name);
            zargs_destroy (&args);
            return -1;
        }
    }

    if (!(aws_access_key && aws_region && aws_secret) && !(!aws_access_key && !aws_region && !aws_secret)) {
        fprintf (stderr, "you must provide all aws parameters\n");
        zargs_destroy (&args);
        return -1;
    }

    //  Load config file for our own use here
    zsys_info ("loading configuration from '%s'...", config_file);
    zconfig_t *config = zconfig_load (config_file);
    if (!config) {
        config = zconfig_new ("root", NULL);
        zconfig_put (config, "server/verbose", "0");
        zconfig_put (config, "server/timeout", "5000");
        zconfig_put (config, "server/bind", "tcp://*:34543");
        zconfig_put (config, "aws/access_key", "access_key");
        zconfig_put (config, "aws/secret", "secret");
        zconfig_put (config, "aws/region", "us-west-2");

        return 0;
    }

    mql_server_t *server = mql_server_new (config);
    //zstr_send (server, "VERBOSE");

//    if (aws_access_key && aws_region && aws_secret)
//        zstr_sendx (server, "SET_AWS", aws_access_key, aws_secret, aws_region, NULL);
//
//    if (!bind)
//        bind = zconfig_get (config, "server/bind", "tcp://*:34543");
//
//    zstr_sendx (server, "BIND", bind, NULL);

    while (true) {
        char *message = zstr_recv (server);
        if (message) {
            puts (message);
            free (message);
        }
        else {
            puts ("interrupted");
            break;
        }
    }

    zargs_destroy (&args);
    mql_server_destroy (&server);
    zconfig_destroy (&config);

	return 0;
}
