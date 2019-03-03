#ifndef MAILBOX_H_INCLUDED
#define MAILBOX_H_INCLUDED

#include "mql_classes.h"

typedef struct _mailbox_t mailbox_t;

mailbox_t* mailbox_new (const char *address, aws_t *aws, mql_server_t *server);

void mailbox_destroy (mailbox_t  **self_p);

int mailbox_send (mailbox_t *self,
                  const char *from,
                  const char *subject,
                  json_t **body);

#endif

