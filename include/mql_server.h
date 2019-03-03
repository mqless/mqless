/*  =========================================================================
    mql_server - mqless server implementation

    This Source Code Form is subject to the terms of the Mozilla Public
    License, v. 2.0. If a copy of the MPL was not distributed with this
    file, You can obtain one at http://mozilla.org/MPL/2.0/.
    =========================================================================
*/

#ifndef MQL_SERVER_H_INCLUDED
#define MQL_SERVER_H_INCLUDED

#ifdef __cplusplus
extern "C" {
#endif

//  This is the mql_server constructor as a zactor_fn;
MQL_EXPORT void
    mql_server_actor (zsock_t *pipe, void *args);

MQL_EXPORT zactor_t *
    mql_server_new (zconfig_t *config);

MQL_EXPORT void
    mql_server_destroy (zactor_t **self_p);

//  Self test of this actor
MQL_EXPORT void
    mql_server_test (bool verbose);
//  @end

#ifdef __cplusplus
}
#endif

#endif
