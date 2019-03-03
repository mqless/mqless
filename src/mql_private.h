#ifndef MQL_PRIVATE_H_INCLUDED
#define MQL_PRIVATE_H_INCLUDED

#include "mql_classes.h"

MQL_PRIVATE int
    mql_server_send (mql_server_t *self, const char *to, const char *from, const char *subject, json_t **body);

MQL_PRIVATE int
    mql_server_send_error (mql_server_t *self, const char *to, uint32_t status_code, const char* body);

#endif