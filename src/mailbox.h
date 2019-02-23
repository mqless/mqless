#ifndef MAILBOX_H_INCLUDED
#define MAILBOX_H_INCLUDED

#include "mql_classes.h"

typedef void (mailbox_callback_fn) (void *server, void **connection, zhttp_response_t* response);

typedef struct _mailbox_t mailbox_t;

mailbox_t* mailbox_new (const char* address, aws_t *aws, void *server, mailbox_callback_fn *callback);

void mailbox_destroy (mailbox_t  **self_p);

int mailbox_send (mailbox_t *self,
                  const char *function,
                  int invocation_type,
                  char **payload,
                  void* connection);

#endif

