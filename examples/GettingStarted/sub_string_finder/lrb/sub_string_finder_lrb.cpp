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

#include "lrb/XN0_lrb.h"
#include <stdio.h>

#define main sub_string_finder_main
#include "../sub_string_finder_extended.cpp"

// We must declare the sub_string_finder function with external linkage so that 
// the CPU-side XN0ContextGetLibFunctionHandle() call can get a handle to it.
XNNATIVELIBEXPORT 
void sub_string_finder(XN_BUFFER_LIST,void *, uint16_t)
{
    main(0, 0);
    fflush( stdout );
}
