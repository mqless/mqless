#ifndef AWS_H_INCLUDED
#define AWS_H_INCLUDED

#include "mql_classes.h"

typedef struct _aws_t aws_t;

typedef void (aws_lambda_callback_fn) (void* arg, zhttp_response_t *response);

aws_t * aws_new ();
void aws_destroy (aws_t ** aws_p);

void aws_set (aws_t *self, const char* region, const char *access_key, const char *secret, const char *endpoint);

int aws_invoke_lambda (aws_t *self, const char* function_name, char **content, aws_lambda_callback_fn callback, void* arg);

int aws_execute (aws_t *aws);

zsock_t* aws_get_socket (aws_t *aws);

void aws_refresh_credentials (aws_t *self);

int aws_refresh_credentials_sync (aws_t *self);

const char *aws_private_ip_address (aws_t *self);

void aws_test ();

#endif