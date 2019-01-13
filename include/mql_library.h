/*  =========================================================================
    mqless - generated layer of public API

    This Source Code Form is subject to the terms of the Mozilla Public
    License, v. 2.0. If a copy of the MPL was not distributed with this
    file, You can obtain one at http://mozilla.org/MPL/2.0/.

################################################################################
#  THIS FILE IS 100% GENERATED BY ZPROJECT; DO NOT EDIT EXCEPT EXPERIMENTALLY  #
#  Read the zproject/README.md for information about making permanent changes. #
################################################################################
    =========================================================================
*/

#ifndef MQL_LIBRARY_H_INCLUDED
#define MQL_LIBRARY_H_INCLUDED

//  Set up environment for the application

//  External dependencies
#include <czmq.h>

//  MQL version macros for compile-time API detection
#define MQL_VERSION_MAJOR 0
#define MQL_VERSION_MINOR 1
#define MQL_VERSION_PATCH 0

#define MQL_MAKE_VERSION(major, minor, patch) \
    ((major) * 10000 + (minor) * 100 + (patch))
#define MQL_VERSION \
    MQL_MAKE_VERSION(MQL_VERSION_MAJOR, MQL_VERSION_MINOR, MQL_VERSION_PATCH)

#if defined (__WINDOWS__)
#   if defined MQL_STATIC
#       define MQL_EXPORT
#   elif defined MQL_INTERNAL_BUILD
#       if defined DLL_EXPORT
#           define MQL_EXPORT __declspec(dllexport)
#       else
#           define MQL_EXPORT
#       endif
#   elif defined MQL_EXPORTS
#       define MQL_EXPORT __declspec(dllexport)
#   else
#       define MQL_EXPORT __declspec(dllimport)
#   endif
#   define MQL_PRIVATE
#elif defined (__CYGWIN__)
#   define MQL_EXPORT
#   define MQL_PRIVATE
#else
#   if (defined __GNUC__ && __GNUC__ >= 4) || defined __INTEL_COMPILER
#       define MQL_PRIVATE __attribute__ ((visibility ("hidden")))
#       define MQL_EXPORT __attribute__ ((visibility ("default")))
#   else
#       define MQL_PRIVATE
#       define MQL_EXPORT
#   endif
#endif

//  Project has no stable classes, so we build the draft API
#undef  MQL_BUILD_DRAFT_API
#define MQL_BUILD_DRAFT_API

//  Opaque class structures to allow forward references
//  These classes are stable or legacy and built in all releases
//  Draft classes are by default not built in stable releases
#ifdef MQL_BUILD_DRAFT_API
typedef struct _mql_server_t mql_server_t;
#define MQL_SERVER_T_DEFINED
#endif // MQL_BUILD_DRAFT_API


//  Public classes, each with its own header file
#ifdef MQL_BUILD_DRAFT_API
#include "mql_server.h"
#endif // MQL_BUILD_DRAFT_API

#ifdef MQL_BUILD_DRAFT_API

#ifdef __cplusplus
extern "C" {
#endif

//  Self test for private classes
MQL_EXPORT void
    mql_private_selftest (bool verbose, const char *subtest);

#ifdef __cplusplus
}
#endif
#endif // MQL_BUILD_DRAFT_API

#endif
/*
################################################################################
#  THIS FILE IS 100% GENERATED BY ZPROJECT; DO NOT EDIT EXCEPT EXPERIMENTALLY  #
#  Read the zproject/README.md for information about making permanent changes. #
################################################################################
*/