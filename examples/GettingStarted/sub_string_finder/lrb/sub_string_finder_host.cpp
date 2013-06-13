/*
    Copyright 2005-2010 Intel Corporation.  All Rights Reserved.

    The source code contained or described herein and all documents related
    to the source code ("Material") are owned by Intel Corporation or its
    suppliers or licensors.  Title to the Material remains with Intel
    Corporation or its suppliers and licensors.  The Material is protected
    by worldwide copyright laws and treaty provisions.  No part of the
    Material may be used, copied, reproduced, modified, published, uploaded,
    posted, transmitted, distributed, or disclosed in any way without
    Intel's prior express written permission.

    No license under any patent, copyright, trade secret or other
    intellectual property right is granted to or conferred upon you by
    disclosure or delivery of the Materials, either expressly, by
    implication, inducement, estoppel or otherwise.  Any license under such
    intellectual property rights must be express and approved by Intel in
    writing.
*/

#include "host/XN0_host.h"
#include <stdio.h>
#include <stdlib.h>

// Declare the ConfigAndExt function (defined below) to support
// multiple build and platform configurations.
const char* ConfigName(const char *names[]);

const char *tbbNames[] = { "libtbb.so", "libtbb_debug.so", "tbb.dll", "tbb_debug.dll" };
const char *programNames[] = { "sub_string_finder_lrb64r.so", "sub_string_finder_lrb64d.so",
                              "sub_string_finder_lrb64r.dll", "sub_string_finder_lrb64d.dll" };

int main()
{
    XNERROR result;

    // Determine the number of Larrabee engines which are installed on the 
    // system by calling XN0EngineGetCount1()
    if ( XN0EngineGetCount1( XN_ISA_LRB1, NULL ) == 0 ) 
    {
        printf( "No Larrabee deivces were found. Aborting.\n" );
        return -1;
    }

    // Get a handle to the first Larrabee device so that we can use it later
    XNENGINE engine;
    result = XN0EngineGetHandle1( XN_ISA_LRB1, NULL, 0, &engine );
    if ( XN_SUCCESS != result )
    {
        printf( "XN0EngineGetHandle1 returned %s. Aborting.\n", 
            XN0ErrorGetName(result) );
        return -1;
    }

    // Create a context to hold the Larrabee program that we want to run
    // by calling XN0ContextCreate1()
    XNCONTEXT context;
    result = XN0ContextCreate1( engine, &context );
    if ( XN_SUCCESS != result )
    {
        printf( "XN0ContextCreate1 returned %s. Aborting.\n", 
            XN0ErrorGetName(result) );
        return -1;
    }

    // Load the TBB library into the context
    char libName[MAX_PATH];
    size_t sz; char *tbbpath;
    _dupenv_s(&tbbpath, &sz, "TBB30_INSTALL_DIR");
    if( !tbbpath )
        tbbpath = _strdup("..\\..\\..\\.."); // default directories layout
    sprintf_s( libName, "%s\\lib\\%s", tbbpath, ConfigName(tbbNames) );
    free(tbbpath);
    printf( "Loading %s.\n", libName ); 
    XNLIBRARY tbblibrary;
    result = XN0ContextLoadLib3( context, libName, 0, &tbblibrary );
    if ( XN_SUCCESS != result )
    {
        printf( "Another try with plain %s.\n", ConfigName(tbbNames) );
        // if specified in LD_LIBRARY_PATH or copied locally try plain name
        result = XN0ContextLoadLib3( context, ConfigName(tbbNames), 0, &tbblibrary );
        if ( XN_SUCCESS != result )
        {
            printf( "XN0ContextLoadLib3 returned %s. Aborting.\n", 
                XN0ErrorGetName(result) );
            XN0ContextDestroy( context );
            return -1;
        }
    }

    // Load the program into the context using XN0ContextLoadLib3()
    strcpy_s( libName, MAX_PATH, ConfigName(programNames) );
    printf( "Loading %s.\n", libName ); 
    XNLIBRARY library;
    result = XN0ContextLoadLib3( context, libName, 0, &library );
    if ( XN_SUCCESS != result )
    {
        printf( "XN0ContextLoadLib3 returned %s. Aborting.\n", 
            XN0ErrorGetName(result) );
        XN0ContextUnloadLib1( tbblibrary );
        XN0ContextDestroy( context );
        return -1;
    }

    // Obtain the handle to the function within the Larrabee program.
    XNFUNCTION sub_string_finder;
    result = XN0ContextGetLibFunctionHandle( library, "sub_string_finder", &sub_string_finder );
    if ( XN_SUCCESS != result )
    {
        printf( "XN0ContextGetLibFunctionHandle returned %s. Aborting.\n", 
            XN0ErrorGetName(result) );
        XN0ContextUnloadLib1( library );
        XN0ContextUnloadLib1( tbblibrary );
        XN0ContextDestroy( context );
        return -1;
    }

    // Schedule the function to be run on the Larrabee device by 
    // calling XN0ContextRunFunction
    printf( "Running %s on Larrabee.\n", libName );
    XN_BUFFER_LIST bufferList = { 0, NULL };
    result = XN0ContextRunFunction( sub_string_finder, bufferList, NULL, 0 );
    if ( XN_SUCCESS != result )
    {
        printf( "XN0ContextRunFunction returned %s. Aborting.\n", 
            XN0ErrorGetName(result) );
        XN0ContextUnloadLib1( library );
        XN0ContextUnloadLib1( tbblibrary );
        XN0ContextDestroy( context );
        return -1;
    }

    // Flush the command buffer to ensure that our program runs.
    XN0ContextFlush( context );

    // Now that we are done running the program, we unload it
    // from the context
    result = XN0ContextUnloadLib1( library );
    if ( XN_SUCCESS != result )
    {
        printf( "XN0ContextUnloadLib1 returned %s. Aborting.\n", 
            XN0ErrorGetName(result) );
        XN0ContextDestroy( context );
        return -1;
    }
    //XN0ContextUnloadLib1( tbblibrary ); -- may cause segfault, known issue

    // Cleanup the context
    result = XN0ContextDestroy( context );
    if ( XN_SUCCESS != result )
    {
        printf( "XN0ContextDestroy returned %s. Aborting.\n", 
            XN0ErrorGetName(result) );
        return -1;
    }

    return 0;
}

// This helper function returns the platform configuration and extension.
const char* ConfigName(const char *names[])
{
    const char* result = NULL;
    char* pLarrabeeEmulation = NULL;
    size_t unused;
    _dupenv_s( &pLarrabeeEmulation, &unused, "LARRABEE_EMULATION" );
    if ( pLarrabeeEmulation )
    {
        if ( 0 == strncmp(pLarrabeeEmulation, "WIN64", strlen("WIN64")) )
        {
#ifdef _DEBUG
            result = names[3];
#else
            result = names[2];
#endif
        }

        free(pLarrabeeEmulation);
    }

    if (!result)
    {
#ifdef _DEBUG
        result = names[1];
#else
        result = names[0];
#endif
    }

    return result;
}
