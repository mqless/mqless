#ifndef AWS_H_INCLUDED
#define AWS_H_INCLUDED

#include "mql_classes.h"

typedef struct _aws_t aws_t;

typedef void (aws_lambda_callback_fn) (zhttp_response_t *response, void* arg);

// TODO: we should also accept a ctor without a secret and access_key and retrieve from metadata

aws_t * aws_new ();
void aws_destroy (aws_t ** aws_p);

void aws_set (aws_t *self, const char *access_key,
             const char *secret,
             const char *region);

int aws_invoke_lambda (aws_t *self, const char* function_name, char **content, aws_lambda_callback_fn callback, void* arg);

int aws_execute (aws_t *aws);

zsock_t* aws_get_socket (aws_t *aws);

void aws_test ();

#endif