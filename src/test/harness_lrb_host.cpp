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

/** @file harness_lrb_host.cpp     
    This is the launcher for TBB tests compiled for native or NetSim environments.
**/

#include <windows.h>

#define __TBB_LRB_HOST 1
#include "harness_lrb.h"
#define HARNESS_NO_PARSE_COMMAND_LINE 1
#define HARNESS_CUSTOM_MAIN 1
#include "harness.h"
#include <assert.h>

bool IsSupportedTest ( int argc, char* argv[] ) {
    if( xnVerbose ) return true;
    const char* test[] = {
        "test_openmp", // omp library must be loaded manually
        "test_model_plugin", // usage model is not supported by TBB on LRB
        "malloc_overload", // ?
        "test_task_scheduler",  // all those tests halt or work long
        NULL
        };
    for ( size_t i = 0; test[i]; ++i ) {
        for ( size_t j = 1; j < argc; ++j ) {
            if ( strstr(argv[j], test[i]) )
                return false;
        }
    }
    return true;
}

#if __TBB_LRB_R10_LAUNCHER
#include <vector>

HANDLE alive;
bool done = false;
void WatchDog(void*) {
    while( !done ) if( WaitForSingleObject( alive, 30000 ) ) { // failure or timeout
        printf("ERROR: XN does not respond\n"); // LRB BUG: it must not block forever
        fflush(stdout);
        TerminateProcess( GetCurrentProcess(), 13 );
    }
}

char* ConfigAndExt();

XNERROR LoadModule(
    XNCONTEXT  Context,
    const char *pName,
    XNLIBRARY  *out_pLibrary)
{
    char fullName[MAX_PATH];
#if __TBB_LRB_COMPLETE_PATH
    char path[MAX_PATH];
    DWORD pathLength = GetModuleFileNameA( NULL, path, MAX_PATH );
    while ( path[--pathLength] != '\\' ) {};
    path[++pathLength] = 0; // Terminate the string just after the path
#else
    const char *path = "";
#endif
    
    if( pName[0] == '.' && pName[1] == '/' ) pName += 2; // LRB BUG: loads with ./ but doesn't work
    if( pName[0] == '/' || pName[0] == '\\' || (pName[0] && pName[1] == ':')) // full path
        strcpy( fullName, pName );
    else sprintf_s( fullName, "%s%s", path, pName );
    XNERROR result = XN0ContextLoadLib3(Context, fullName, 0, out_pLibrary);
    ASSERT(SetEvent(alive), 0);
    if( xnVerbose ) printf("Module: %s: %s\n", fullName, XN0ErrorGetName(result));
    return result;
}

XNERROR LoadNativeAppWrapper(
    const char* pTestName,
    XNCONTEXT*  out_pContext,
    std::vector<XNLIBRARY>  &out_Libraries)
{
    XNERROR result = XN_SUCCESS;

    UINT32 NumberOfEngines = XN0EngineGetCount1( XN_ISA_LRB1, NULL );
    if (0 == NumberOfEngines)
    {
        printf("ERROR: No Larrabee card detected\n");
        result = XN_NOT_INITIALIZED;
        goto end;
    }

    XNENGINE engine;
    XNCALL(XN0EngineGetHandle1( XN_ISA_LRB1, NULL, 0, &engine ));

    XNCALL(XN0ContextCreate1(engine, out_pContext));

    XNLIBRARY out_Library;

    if( getenv("LRB_PRELOAD") ) {
        char *lrb_preload = strdup(getenv("LRB_PRELOAD")), *lib = lrb_preload, *next;
        do {
            next = strchr(lib,';');
            if(next) *next++ = 0;
            if(*lib) {
                if( XN_SUCCESS == LoadModule(*out_pContext, lib, &out_Library) )
                    out_Libraries.push_back(out_Library);
            }
        } while( (lib = next) );
        free(lrb_preload);
    }

    XNCALL(LoadModule(*out_pContext, pTestName, &out_Library));
    out_Libraries.push_back(out_Library);

end:
    return result;
} /* LoadNativeAppWrapper() */


XNERROR
SpawnMain(
    XNLIBRARY   in_pWrapperLibrary,
    int         in_argc,
    char**      in_ppArgv)
{
    XNERROR result = XN_SUCCESS;

    XN_BUFFER_LIST bufList;
    bufList.numBufs = 0;
    bufList.pBufArray = NULL;

    XNFUNCTION SpawnMainHandle;
    XNCALL(XN0ContextGetLibFunctionHandle(in_pWrapperLibrary, "SpawnMain", &SpawnMainHandle));

    // Argc and argv by the time they are passed into us count and point to
    // the arguments to pass to the LRB native app's main.
    // Calculate size of buffer needed to hold all args
    int argBufLen = sizeof(int);
    for (int i = 0; i < in_argc; i++)
    {
        argBufLen += (int) strnlen(in_ppArgv[i], 512);
        argBufLen++; // Leave space for null termination
    }
    void* pArgBuffer = malloc(argBufLen);
    assert(NULL != pArgBuffer);

    // Place argc at the start of the buffer
    *(int*) pArgBuffer = in_argc;
    // Copy in the argvs to the rest of it
    char* pCurrentArgBufPosition = &((static_cast<char*>(pArgBuffer))[4]);
    int currentArgBufPosition = 4;
    for (int i = 0; i < in_argc; i++)
    {
        strcpy_s(pCurrentArgBufPosition, 
                 argBufLen - currentArgBufPosition,
                 in_ppArgv[i]);
        currentArgBufPosition += (int) strlen(in_ppArgv[i]);
        currentArgBufPosition += 1; // For null termination byte
        pCurrentArgBufPosition = pCurrentArgBufPosition + (int) strlen(in_ppArgv[i]) + 1;
    }

    XNCALL(XN0ContextRunFunction(SpawnMainHandle, bufList, pArgBuffer, (uint16_t) currentArgBufPosition));

    free(pArgBuffer);

    return result;
} /* LoadNativeApp() */

XNERROR
WaitForNativeMainToExit(
    int &out_Result,
    XNLIBRARY   in_wrapperLibrary)
{
    XNFUNCTION RunForAQuantum;
    XNCALL(XN0ContextGetLibFunctionHandle(in_wrapperLibrary, "RunForAQuantum", &RunForAQuantum));
    /*
    printf("Sending quanta of duration 10ms at frequency 10hz to allow main\n");
    printf("to execute while waiting for it to exit\n\n");
    printf("\tSending quanta:/");
    */

    // Look up the sync object the LRB side will set when main has exited
    XNERROR xnResult;
    XNSYNCOBJECT mainExitedSyncObj;
    while(XN_DOES_NOT_EXIST ==
        XN0SyncObjectLookup( 
            "/Intel/Larrabee SDK Core/mainExitedSyncObj", &mainExitedSyncObj ) ) {};

    uint32_t quantaExecuted = 0;
    do
    {
        ASSERT(SetEvent(alive), 0);

        XN_BUFFER_LIST bufList = { 0, NULL };
        uint32_t quantum = 10; // Let native app run for 10ms at a time. LRB BUG: it is ugly workaround
        XNCALL(XN0ContextRunFunction(RunForAQuantum, bufList, &quantum, sizeof(quantum)));
        
        if( xnVerbose ) switch(quantaExecuted++ % 4)
        {
            case 0: printf("-\b");break;
            case 1: printf("\\\b");break;
            case 2: printf("|\b");break;
            case 3: printf("/\b");break;
        }
        fflush(stdout);
        
        Sleep(100); // Send 10 quanta per second
        xnResult = XN0SyncObjectWaitWithTimeout(mainExitedSyncObj, 0);
    } while (XN_TIME_OUT_REACHED == xnResult);

    int64_t mainResult;
    XNCALL(XN0SyncObjectGetUserData(mainExitedSyncObj, &mainResult));
    XNCALL(XN0SyncObjectRelease(&mainExitedSyncObj, 1));

    out_Result = int(mainResult);
    if( xnVerbose > 1 ) printf("Module return code: %d\n", out_Result);

    return XN_SUCCESS;
}

XNERROR 
Cleanup(
    XNCONTEXT in_context,
    std::vector<XNLIBRARY>  &in_Libraries)
{
    for(; !in_Libraries.empty(); in_Libraries.pop_back())
        XNCALL(XN0ContextUnloadLib1(in_Libraries.back()));
    XNCALL(XN0ContextDestroy(in_context));
    return XN_SUCCESS;
} /* Cleanup() */


int main(int argc, char** argv)
{
    int Result = 0;

    if (argc < 2) {
        printf( "Usage: %s test_name [test_args]\n", argv[0] );
        return -1;
    }
    for ( size_t j = 2; j < argc; ++j ) {
        if ( argv[j][0] == '-' && argv[j][1] == 'v' ) {
            if( argv[j][2] >= '0' && argv[j][2] <= '9' )
                xnVerbose = int(argv[j][2] - '0');
            else xnVerbose = 1;
        }
    }
    if ( !IsSupportedTest(argc, argv) ) {
        printf(__TBB_MSG_SKIP);
        return 0;
    }
    alive = CreateEvent(NULL, FALSE, FALSE, NULL);
    ASSERT(alive,0);
    _beginthread(WatchDog, 0, NULL);

    XNCONTEXT context;
    std::vector<XNLIBRARY> in_Libraries;
    XNCALL(LoadNativeAppWrapper(argv[1], &context, in_Libraries));

    XNCALL(SpawnMain(in_Libraries.back(), argc-1, argv+1));

    XNCALL(WaitForNativeMainToExit(Result, in_Libraries.back()));

    XNCALL(Cleanup(context, in_Libraries));

    done = true;
    ASSERT(SetEvent(alive), 0);
    fflush(stdout);
#if TBB_USE_DEBUG
    // We do not need a dump of memory leaks statistics
    TerminateProcess( GetCurrentProcess(), 0 );
#endif
    return Result;
} /* main() */

#else // !__TBB_LRB_R10_LAUNCHER
#define __TBB_HOST_EXIT(status)  exitStatus = status; goto hard_stop;

bool IsCompletionMsg ( const char* msg ) {
    return strncmp(msg, __TBB_MSG_DONE, __TBB_LRB_COMM_MSG_SIZE_MAX) == 0 ||
           strncmp(msg, __TBB_MSG_SKIP, __TBB_LRB_COMM_MSG_SIZE_MAX) == 0;
}

int main( int argc, char* argv[] ) {
    int exitStatus = 0;

    if (argc < 2) {
        printf( "Usage: %s test_name test_args\n", argv[0] );
        __TBB_HOST_EXIT(-1);
    }
    if ( !IsSupportedTest(argc, argv) ) {
        printf(__TBB_MSG_SKIP);
        __TBB_HOST_EXIT(0);
    }

    XNENGINE engine;
    XNERROR result = XN0EngineGetHandle(0, &engine);
    assert( XN_SUCCESS == result );

    // Try with a run schedule of one second
    XN_RUN_SCHEDULE runSchedule;
    runSchedule.executionQuantumInUsecs = 500000;
    runSchedule.frequencyInHz = 1;

    XNCONTEXT ctxHandle;
    result = XN0ContextCreate(engine, &runSchedule, &ctxHandle);
    assert( XN_SUCCESS == result );

    XNCOMMUNICATOR communicator;
    result = XN0MessageCreateCommunicator( __TBB_LRB_COMMUNICATOR_NAME, __TBB_LRB_COMM_MSG_SIZE_MAX, &communicator );
    assert( XN_SUCCESS == result );

    XNLIBRARY libHandle;
    if ( argc == 2 )
        result = XN0ContextLoadLib(ctxHandle, argv[1], &libHandle);
    else
        result = XN0ContextLoadLib1(ctxHandle, argv[1], argc - 1, argv + 1, &libHandle);
    if( result != XN_SUCCESS ) {
        printf( "ERROR: Loading module \"%s\" failed", argv[1] );
        __TBB_HOST_EXIT(-2);
    }
    puts("Executing");

    char msg[__TBB_LRB_COMM_MSG_SIZE_MAX + 1] = { 0 };
    bool abort_signalled = false;
    for( ; !IsCompletionMsg(msg); ) {
        XN0MessageReceive( communicator, msg, __TBB_LRB_COMM_MSG_SIZE_MAX, NULL );
        if ( strncmp(msg, __TBB_MSG_ABORT, __TBB_LRB_COMM_MSG_SIZE_MAX ) == 0 ) {
            abort_signalled = true;
            // The next message should provide the reason
            continue;
        }
        printf("%s\n", msg); fflush(stdout);
        if ( abort_signalled ) {
            // After exit() or abort() was invoked in a LRB library, it cannot be 
            // unloaded, and the host hangs in XN0ContextDestroy. Thus we have to 
            // bypass the graceful termination code.
            __TBB_HOST_EXIT(1);
        }
    }
    XN0MessageDestroyCommunicator( communicator );

    result = XN0ContextUnloadLib(libHandle, 10 * 1000, &exitStatus);
    if( result == XN_TIME_OUT_REACHED ) {
        printf("ERROR: timed out waiting for LRB module unload\n");
    }
    else {
        result = XN0ContextDestroy(ctxHandle);
        assert( XN_SUCCESS == result );
    }
    if ( exitStatus != 0 )
        printf("ERROR: %s returned failure status %d", argv[1], exitStatus);
hard_stop:
    fflush(stdout);
    // We do not need a dump of memory leaks statistics
    TerminateProcess( GetCurrentProcess(), 0 );
    return 0;
}
#endif // __TBB_LRB_R10_LAUNCHER
