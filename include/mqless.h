/*  =========================================================================
    mqless - Serverless Message Broker

    Copyright (c) the Contributors as noted in the AUTHORS file.
    This file is part of the Malamute Project.

    This Source Code Form is subject to the terms of the Mozilla Public
    License, v. 2.0. If a copy of the MPL was not distributed with this
    file, You can obtain one at http://mozilla.org/MPL/2.0/.
    =========================================================================
*/

#ifndef MQLESS_H_H_INCLUDED
#define MQLESS_H_H_INCLUDED

//  Include the project library file
#include "mql_library.h"

//  Add your own public definitions here, if you need them

#define MQL_ROUTING_KEY_MAX_LEN 255

#define MQL_INVOCATION_TYPE_REQUEST_RESPONSE    0
#define MQL_INVOCATION_TYPE_EVENT               1

#define MQL_SOURCE_PLATFORM 0
#define MQL_SOURCE_FUNCTION 1
#define MQL_SOURCE_MQL      2

#endif
