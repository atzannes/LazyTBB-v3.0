/*
    Copyright 2005-2010 Intel Corporation.  All Rights Reserved.

    This file is part of Threading Building Blocks.

    Threading Building Blocks is free software; you can redistribute it
    and/or modify it under the terms of the GNU General Public License
    version 2 as published by the Free Software Foundation.

    Threading Building Blocks is distributed in the hope that it will be
    useful, but WITHOUT ANY WARRANTY; without even the implied warranty
    of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with Threading Building Blocks; if not, write to the Free Software
    Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA

    As a special exception, you may use this file as part of a free software
    library without restriction.  Specifically, if other files instantiate
    templates or use macros or inline functions from this file, or you compile
    this file and link it with other files to produce an executable, this
    file does not by itself cause the resulting executable to be covered by
    the GNU General Public License.  This exception does not however
    invalidate any other reasons why the executable file might be covered by
    the GNU General Public License.
*/

#ifndef tbb_test_harness_lrb_H
#define tbb_test_harness_lrb_H

#if !(__LRB__||__TBB_LRB_HOST)
    #error test/harness_lrb.h should be included only when building for LRB platform
#endif

#include <stdlib.h>
#include <stdio.h>

#define __TBB_LRB_COMM_MSG_SIZE_MAX 1024
#define __TBB_LRB_COMMUNICATOR_NAME "__TBB_LRB_COMMUNICATOR"

#define __TBB_MSG_DONE "done\n"
#define __TBB_MSG_SKIP "skip\n"
#define __TBB_MSG_ABORT "__TBB_abort__"

static int xnVerbose = 0;

#define XNCALL(X) \
    { \
        if(xnVerbose > 1) {printf("XNCALL#%d: %s...\n", __LINE__, # X); fflush(0);} \
        XNERROR result = (X); \
        if (XN_SUCCESS != result) \
        { \
        printf("ERROR: Call to %s\n\t at line %d of %s\n\t failed with code %s\n", # X, __LINE__, __FILE__, XN0ErrorGetName(result)); fflush(0); \
            ASSERT(false, 0); \
        } \
    }


#define INTCALL(X) \
    { \
        int result = (X); \
        if (0 != result) \
        { \
        printf("ERROR: Call to %s\n\t at line %d of %s\n\t failed with value %d\n", # X, __LINE__, __FILE__, result); fflush(0); \
            ASSERT(false, 0); \
        } \
    }

#if __TBB_LRB_HOST

#include "host/XN0_host.h"

#else /* !__TBB_LRB_HOST */

#if __TBB_LRB_NO_XN // no LRB launcher needed
#define REPORT_FATAL_ERROR  REPORT
#define __TBB_TEST_EXPORT
#else
#include "lrb/XN0_lrb.h"
#include "harness_assert.h"
#endif

#if __TBB_LRB_NATIVE
    #define TBB_EXIT_ON_ASSERT 1
    #define __TBB_PLACEMENT_NEW_EXCEPTION_SAFETY_BROKEN 1
#else
    #define TBB_TERMINATE_ON_ASSERT 1
#endif

// Suppress warnings caused by windows.h during NetSim build
#pragma warning (disable: 4005)

#endif /* __TBB_LRB_HOST */

#if XN_ISA_LRB1 // defined in R10
#define __TBB_LRB_R10_LAUNCHER 1
#else
#define __TBB_LRB_R10_LAUNCHER 0
#endif

#if !(__TBB_LRB_HOST || __TBB_LRB_NO_XN)
#if __TBB_LRB_R10_LAUNCHER // new launcher from LRB Tutorial

#define __TBB_TEST_EXPORT

#ifndef _WIN32
#include <unistd.h> // Needed for usleep
#include <dlfcn.h>
#include <string.h>
#endif

#ifndef __TBB_WAIT_FOR_MAIN
#define __TBB_WAIT_FOR_MAIN __TBB_LRB_NATIVE
#endif

#ifndef HARNESS_NO_MAIN_ARGS
#define HARNESS_NO_MAIN_ARGS HARNESS_NO_PARSE_COMMAND_LINE
#endif
#if HARNESS_NO_MAIN_ARGS
int main();
#else
int main(int, char**);
#endif

typedef int 
(*Main2FunctionPtr_t)(
    int     argc,
    char**  argv);

typedef struct 
{
    int                 argc;
    char**              argv;
    Main2FunctionPtr_t  mainPtr;
} main_wrapper_t;

static pthread_t g_mainThread;
XNEVENT g_mainExitedSyncObj;

#if __TBB_WAIT_FOR_MAIN
static volatile int mainResult = -1;
static volatile int mainDone = 0;
#endif


#define REPORT_FATAL_ERROR  signalExited(-1); REPORT

static void signalHost(int Res) {
    // Store the value main returned in the sync object used to
    // signal main has exited, then signal the sync object
    XNCALL(XN0SyncObjectSetUserData(g_mainExitedSyncObj, (int64_t) Res));
    XNCALL(XN0EventSet(g_mainExitedSyncObj));
}

void signalExited(int Res) {
    fflush(0); // workaraund for LRB printf bug
#if !__TBB_WAIT_FOR_MAIN
    signalHost(Res);
#else
    UNREFERENCED_PARAM(Res);
    mainDone = 1;
#endif
}

// Wrapper function used to keep track of whether main has returned or not.
void* mainWrapperFn(void* in_mainInput)
{
    main_wrapper_t* pWrapper = static_cast<main_wrapper_t*>(in_mainInput);
#if !__TBB_WAIT_FOR_MAIN
    int mainResult;
#endif
    mainResult = pWrapper->mainPtr(pWrapper->argc, pWrapper->argv);
    signalExited( mainResult );

    return NULL;
} /* mainWrapperFn() */

// Argc and argv are encoded in the in_pMiscData - the argc is at the start,
// the rest of it is argv.
extern "C" XNNATIVELIBEXPORT void 
SpawnMain(
    XN_BUFFER_LIST  in_bufList,
    void*           in_pMiscData, 
    uint16_t        in_pMiscDataSize)
{
    UNREFERENCED_PARAM(in_bufList);

    // First thing we do is register the sync object that will
    // be used to signal when main exits
    XNCALL(XN0EventCreate( XN_NOT_SIGNALED, XN_AUTO_RESET, &g_mainExitedSyncObj));
    XNCALL(XN0SyncObjectRegister( g_mainExitedSyncObj, 
        "/Intel/Larrabee SDK Core/mainExitedSyncObj"));

    main_wrapper_t* pMainWrapper = static_cast<main_wrapper_t*>(
        malloc(sizeof(main_wrapper_t)));
    ASSERT(NULL != pMainWrapper, 0);

    // Figure out how many/how long the argv arguments are and build up
    // an argv buffer.
    pMainWrapper->argc = *(static_cast<int32_t*>(in_pMiscData));
    pMainWrapper->argv = static_cast<char**>(malloc(pMainWrapper->argc * sizeof(char*)));
    char* pCurrentArg = 4 + static_cast<char*>(in_pMiscData);
    for (int i = 0; i < pMainWrapper->argc; i++)
    {
        int currentArgLen =  1 + (int) strlen(pCurrentArg); // Add 1 for null termination
        pMainWrapper->argv[i] = static_cast<char*>(malloc(currentArgLen));
#ifdef _WIN32
        strcpy_s(pMainWrapper->argv[i], currentArgLen, pCurrentArg);
#else
        strncpy(pMainWrapper->argv[i], pCurrentArg, currentArgLen);
#endif
        pCurrentArg = pCurrentArg + currentArgLen;
    }

    // Sanity check to make sure length of buffer matches what we found via strlens.
    ASSERT((pCurrentArg - static_cast<char*>(in_pMiscData)) == in_pMiscDataSize, 0);

    pMainWrapper->mainPtr = reinterpret_cast<Main2FunctionPtr_t>(main);

    pthread_attr_t attr;
    INTCALL(pthread_attr_init(&attr));
    // We want to be able to join the main thread to prove it is gone
    INTCALL(pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE));

    INTCALL(pthread_create(&g_mainThread, &attr, mainWrapperFn, pMainWrapper));

    pthread_set_name_np(g_mainThread, "main TBB test thread");
    INTCALL(pthread_attr_destroy(&attr)); // Cleanup
} /* StartMain() */

extern "C" XNNATIVELIBEXPORT void
RunForAQuantum(
    XN_BUFFER_LIST  in_bufList,
    void*           in_pMiscData, 
    uint16_t        in_pMiscDataSize)
{
    UNREFERENCED_PARAM(in_bufList);

    ASSERT(sizeof(uint32_t) == in_pMiscDataSize, 0);
    uint32_t msToSleep = *static_cast<uint32_t*>(in_pMiscData);

#ifdef _WIN32
    // Windows emulation does not implement uSleep
    Sleep((DWORD) msToSleep);
#else
    usleep(1000 * msToSleep); // usleep is in microseconds
#endif

#if __TBB_WAIT_FOR_MAIN
    if( mainDone == 1 ) {
        int status = pthread_kill( g_mainThread, 0 );
        if ( status == ESRCH ) {
            mainDone = 2; // Do not call signalHost repeatedly.
            signalHost(mainResult);
        }
    }
#endif

} /* RunForAQuantum() */

#else // !__TBB_LRB_R10_LAUNCHER

#define __TBB_STDARGS_BROKEN 1
#define __TBB_TEST_EXPORT XNNATIVELIBEXPORT
namespace Harness {
    namespace internal {

    class LrbReporter {
        XNCOMMUNICATOR  m_communicator;

    public:
        LrbReporter () {
            XNERROR res = XN0MessageCreateCommunicator( __TBB_LRB_COMMUNICATOR_NAME, 
                                                        __TBB_LRB_COMM_MSG_SIZE_MAX, 
                                                        &m_communicator );
            ASSERT( XN_SUCCESS == res , NULL);
        }

        ~LrbReporter () {
            XN0MessageDestroyCommunicator( m_communicator );
        }

        void Report ( const char* msg ) {
            XN0MessageSend( m_communicator, msg, __TBB_LRB_COMM_MSG_SIZE_MAX );
        }
    }; // class LrbReporter

    } // namespace internal
} // namespace Harness

#define TbbHarnessReporter LrbReporter
#define REPORT_FATAL_ERROR  REPORT(__TBB_MSG_ABORT); REPORT

#endif // __TBB_LRB_R10_LAUNCHER

#endif /* !(__TBB_LRB_HOST || __TBB_LRB_NO_XN) */

#endif /* tbb_test_harness_lrb_H */
