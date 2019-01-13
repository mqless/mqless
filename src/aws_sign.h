#ifndef AWS_SIGN_H_INCLUDED
#define AWS_SIGN_H_INCLUDED

#include "mql_classes.h"

// TODO: validate this number
#define MAX_AUTHORIZATION_LEN 2000

typedef struct _aws_sign_t aws_sign_t;

aws_sign_t *aws_sign_new (const char *access_key, const char *secret, const char *region, const char *service_name);

void aws_sign_destroy (aws_sign_t **aws_sign_p);

void aws_sign (
        aws_sign_t *self,
        char * output,
        const char *method,
        const char *host,
        const char *path,
        const char *query,
        const char *datetime,
        const char *request_payload);

void aws_sign_test();

#endif