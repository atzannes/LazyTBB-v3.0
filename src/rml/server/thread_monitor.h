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

// All platform-specific threading support is encapsulated here. */
 
#ifndef __RML_thread_monitor_H
#define __RML_thread_monitor_H

#if USE_WINTHREAD
#include <windows.h>
#include <process.h>
#include <malloc.h> //_alloca
#elif USE_PTHREAD
#include <pthread.h>
#include <string.h>
#include <stdlib.h>
#else
#error Unsupported platform
#endif 
#include <stdio.h>
#include "tbb/itt_notify.h"

#if __LRB__
#ifndef __RML_USE_XNMETASCHEDULER
#define __RML_USE_XNMETASCHEDULER 1
#include "common/XN0_Common.h"
#include "common/XN0MetaScheduler_common.h"
#endif
#endif /* __LRB__ */

// All platform-specific threading support is in this header.

#if (_WIN32||_WIN64)&&!__TBB_ipf
// Deal with 64K aliasing.  The formula for "offset" is a Fibonacci hash function,
// which has the desirable feature of spreading out the offsets fairly evenly
// without knowing the total number of offsets, and furthermore unlikely to
// accidentally cancel out other 64K aliasing schemes that Microsoft might implement later.
// See Knuth Vol 3. "Theorem S" for details on Fibonacci hashing.
// The second statement is really does need "volatile", otherwise the compiler might remove the _alloca.
#define AVOID_64K_ALIASING(idx)                       \
    size_t offset = (idx+1) * 40503U % (1U<<16);      \
    void* volatile sink_for_alloca = _alloca(offset); \
    __TBB_ASSERT_EX(sink_for_alloca, "_alloca failed");
#else
// Linux thread allocators avoid 64K aliasing.
#define AVOID_64K_ALIASING(idx)
#endif /* _WIN32||_WIN64 */

namespace rml {

namespace internal {

#if DO_ITT_NOTIFY
static const ::tbb::tchar *SyncType_RML = _T("%Constant");
static const ::tbb::tchar *SyncObj_ThreadMonitorLock = _T("RML Lock"),
                          *SyncObj_ThreadMonitor = _T("RML Thr Monitor");
#endif /* DO_ITT_NOTIFY */

//! Monitor with limited two-phase commit form of wait.  
/** At most one thread should wait on an instance at a time. */
class thread_monitor {
public:
    class cookie {
        friend class thread_monitor;
        unsigned long long my_version;
    };
    thread_monitor();
    ~thread_monitor();

    //! If a thread is waiting or started a two-phase wait, notify it.
    /** Can be called by any thread. */
    void notify();

    //! Begin two-phase wait.
    /** Should only be called by thread that owns the monitor. 
        The caller must either complete the wait or cancel it. */
    void prepare_wait( cookie& c );

    //! Complete a two-phase wait and wait until notification occurs after the earlier prepare_wait.
    void commit_wait( cookie& c );

    //! Cancel a two-phase wait.
    void cancel_wait();

#if USE_WINTHREAD
#define __RML_DECL_THREAD_ROUTINE unsigned WINAPI
    typedef unsigned (WINAPI *thread_routine_type)(void*);
#endif /* USE_WINTHREAD */

#if USE_PTHREAD
#define __RML_DECL_THREAD_ROUTINE void*
    typedef void*(*thread_routine_type)(void*);
#endif /* USE_PTHREAD */

    //! Launch a thread
    static void launch( thread_routine_type thread_routine, void* arg, size_t stack_size );
    static void yield();

#if __RML_USE_XNMETASCHEDULER
    static void xncheck( XNERROR xn_error_code, const char* routine_name );
    static void xnMetaSchedulerInitialize( void );
    static void xnMetaInsertScheduler( thread_monitor::thread_routine_type thread_routine_addr, void* arg, size_t stack_size );
    static void xnMetaStartScheduler( XNHANDLE sched_handle );
    static void xnMetaRemoveScheduler( XNHANDLE sched_handle );
    static void xnMetaStopScheduler( XNHANDLE sched_handle );
    static void xnMetaSchedulerShutdown( void );
#endif /* __RML_USE_XNMETASCHEDULER */

#if __RML_USE_XNMETASCHEDULER
    XNHANDLE sched_handle;
    thread_monitor::thread_routine_type thread_routine_addr;
#endif /* __RML_USE_XNMETASCHEDULER */

private:
    cookie my_cookie;
#if USE_WINTHREAD
    CRITICAL_SECTION critical_section;
    HANDLE event;
#endif /* USE_WINTHREAD */
#if USE_PTHREAD
    pthread_mutex_t my_mutex;
    pthread_cond_t my_cond;
    static void check( int error_code, const char* routine );
#endif /* USE_PTHREAD */
};

#if __RML_USE_XNMETASCHEDULER
inline void thread_monitor::xncheck( XNERROR xn_error_code, const char* routine ) {
    if( XN_SUCCESS != xn_error_code ) {
        fprintf( stderr,"thread_monitor::xncheck : routine=%s xn_error=%s\n", routine, XN0ErrorGetName( xn_error_code ) );
        exit(1);
    }
}

// For general purpose usage,
// let's model a server thread as if it were a single pthread.
// A server thread represents a workload with 1 gang. The gang consists of 1 thread.
const static uint16_t threadsPerGang = 1; // Number of threads in a gang.
const static uint16_t maxGangs       = 1; // Number of gangs in a workload.

static inline unsigned get_recommended_max_L2_cache_count() {
    unsigned L2CacheCount = XN0SysGetL2CacheCount();
    unsigned do_not_use = 1; // Do not use one L2 cache.
    return ( L2CacheCount - do_not_use );
}

// Number of workloads a connection can use.
// server->default_concurrency() uses this value as a base.
static inline unsigned get_hardware_concurrency() {
    return get_recommended_max_L2_cache_count() * threadsPerGang;
}

static XNERROR metaRunFunction( void* in_pPerThreadData );
static void xnSetupMonitor( XNHANDLE sched_handle, thread_monitor::thread_routine_type thread_routine_addr, void* arg, size_t stack_size );

static XNERROR metaInitFunction(
        uint16_t /*in_gangIndex*/,
        uint16_t /*in_gangCount*/,
        uint16_t /*in_threadIndex*/,
        uint16_t /*in_threadCount*/,
        void*    in_pCookie,
        void**   out_pPerThreadData ) {
    *out_pPerThreadData = in_pCookie; // Will be passed as in_pPerThreadData to init, run and shutdown functions.
    return XN_SUCCESS;
}

static XNERROR metaShutdownFunction( void* /*in_pPerThreadData*/ ) {
    return XN_SUCCESS;
}

static const XN_SCHEDULER_FUNCTIONS schedFunctions = {
    metaInitFunction,
    metaRunFunction,
    metaShutdownFunction
};

void thread_monitor::xnMetaSchedulerInitialize( void ) {
    xncheck( XN0MetaConfig1( get_recommended_max_L2_cache_count() ), "XN0MetaConfig1" );
}

void thread_monitor::xnMetaInsertScheduler( thread_monitor::thread_routine_type thread_routine_addr, void* arg, size_t stack_size ) {
    XNHANDLE sched_handle_var;
    xnMetaSchedulerInitialize(); // Workaround. Implement a one-time initialization instead.
    XNERROR r = XN0MetaInsertScheduler(
        schedFunctions,     // const XN_SCHEDULER_FUNCTIONS in_newSchedFunctions,
        threadsPerGang,     // const uint16_t               in_threadsPerGang,
        maxGangs,           // const uint16_t               in_maxGangs,
        0,                  // const uint8_t                in_relativePriority,
        arg,                // void*                        in_pSchedCookie,
        &sched_handle_var   // XNHANDLE* const              out_pNewSchedHandle
    );
    xncheck( r, "XN0MetaInsertScheduler" );
    xnSetupMonitor( sched_handle_var, thread_routine_addr, arg, stack_size );
    xnMetaStartScheduler( sched_handle_var );
}

void thread_monitor::xnMetaStartScheduler( XNHANDLE sched_handle ) {
    xncheck( XN0MetaStartScheduler( sched_handle ), "XN0MetaStartScheduler" );
}

void thread_monitor::xnMetaStopScheduler( XNHANDLE sched_handle ) {
    xncheck( XN0MetaStopScheduler( sched_handle ), "XN0MetaStopScheduler" );
}

void thread_monitor::xnMetaRemoveScheduler( XNHANDLE sched_handle ) {
    xncheck( XN0MetaRemoveScheduler( sched_handle ), "XN0MetaRemoveScheduler" );
}

void thread_monitor::xnMetaSchedulerShutdown( void ) {
    xncheck( XN0MetaShutdown(), "XN0MetaShutdown" );
}
#endif /* __RML_USE_XNMETASCHEDULER */


#if USE_WINTHREAD
#ifndef STACK_SIZE_PARAM_IS_A_RESERVATION
#define STACK_SIZE_PARAM_IS_A_RESERVATION 0x00010000
#endif
inline void thread_monitor::launch( thread_routine_type thread_routine, void* arg, size_t stack_size ) {
#if __RML_USE_XNMETASCHEDULER
    xnMetaInsertScheduler( thread_routine, arg, stack_size );
#else
    unsigned thread_id;
    uintptr_t status = _beginthreadex( NULL, unsigned(stack_size), thread_routine, arg, STACK_SIZE_PARAM_IS_A_RESERVATION, &thread_id );
    if( status==0 ) {
        fprintf(stderr,"thread_monitor::launch: _beginthreadex failed\n");
        exit(1); 
    } else {
        CloseHandle((HANDLE)status);
    }
#endif /* __RML_USE_XNMETASCHEDULER */
}

inline void thread_monitor::yield() {
    SwitchToThread();
}

inline thread_monitor::thread_monitor() {
    event = CreateEvent( NULL, /*manualReset=*/true, /*initialState=*/false, NULL );
    InitializeCriticalSection( &critical_section );
    ITT_SYNC_CREATE(&event, SyncType_RML, SyncObj_ThreadMonitor);
    ITT_SYNC_CREATE(&critical_section, SyncType_RML, SyncObj_ThreadMonitorLock);
    my_cookie.my_version = 0;
}

inline thread_monitor::~thread_monitor() {
    // Fake prepare/acquired pair for Intel(R) Parallel Amplifier to correctly attribute the operations below
    ITT_NOTIFY( sync_prepare, &event );
    CloseHandle( event );
    DeleteCriticalSection( &critical_section );
    ITT_NOTIFY( sync_acquired, &event );
}
     
inline void thread_monitor::notify() {
    EnterCriticalSection( &critical_section );
    ++my_cookie.my_version;
    SetEvent( event );
    LeaveCriticalSection( &critical_section );
}

inline void thread_monitor::prepare_wait( cookie& c ) {
    EnterCriticalSection( &critical_section );
    c = my_cookie;
}

inline void thread_monitor::commit_wait( cookie& c ) {
    ResetEvent( event );
    LeaveCriticalSection( &critical_section );
    while( my_cookie.my_version==c.my_version ) {
        WaitForSingleObject( event, INFINITE );
        ResetEvent( event );
    }
}

inline void thread_monitor::cancel_wait() {
    LeaveCriticalSection( &critical_section );
}
#endif /* USE_WINTHREAD */

#if USE_PTHREAD
inline void thread_monitor::check( int error_code, const char* routine ) {
    if( error_code ) {
        fprintf(stderr,"thread_monitor %s\n", strerror(error_code) );
        exit(1);
    }
}

inline void thread_monitor::launch( void* (*thread_routine)(void*), void* arg, size_t stack_size ) {
    // FIXME - consider more graceful recovery than just exiting if a thread cannot be launched.
    // Note that there are some tricky situations to deal with, such that the thread is already 
    // grabbed as part of an OpenMP team. 
#if __RML_USE_XNMETASCHEDULER
    xnMetaInsertScheduler( thread_routine, arg, stack_size );
#else
    pthread_attr_t s;
    check(pthread_attr_init( &s ), "pthread_attr_init");
    if( stack_size>0 ) {
        check(pthread_attr_setstacksize( &s, stack_size ),"pthread_attr_setstack_size");
    }
    pthread_t handle;
    check( pthread_create( &handle, &s, thread_routine, arg ), "pthread_create" );
    check( pthread_detach( handle ), "pthread_detach" );
#endif /* __RML_USE_XNMETASCHEDULER */
}

inline void thread_monitor::yield() {
    sched_yield();
}

inline thread_monitor::thread_monitor() {
    check( pthread_cond_init(&my_cond,NULL), "pthread_cond_init" );
    check( pthread_mutex_init(&my_mutex,NULL), "pthread_mutex_init" );
    ITT_SYNC_CREATE(&my_cond, SyncType_RML, SyncObj_ThreadMonitor);
    ITT_SYNC_CREATE(&my_mutex, SyncType_RML, SyncObj_ThreadMonitorLock);
    my_cookie.my_version = 0;
}

inline thread_monitor::~thread_monitor() {
    pthread_cond_destroy(&my_cond);
    pthread_mutex_destroy(&my_mutex);
}

inline void thread_monitor::notify() {
    check( pthread_mutex_lock( &my_mutex ), "pthread_mutex_lock" );
    ++my_cookie.my_version;
    check( pthread_mutex_unlock( &my_mutex ), "pthread_mutex_unlock" );
    check( pthread_cond_signal(&my_cond), "pthread_cond_signal" );
}

inline void thread_monitor::prepare_wait( cookie& c ) {
    check( pthread_mutex_lock( &my_mutex ), "pthread_mutex_lock" );
    c = my_cookie;
}

inline void thread_monitor::commit_wait( cookie& c ) {
    while( my_cookie.my_version==c.my_version ) {
        pthread_cond_wait( &my_cond, &my_mutex );
    }
    check( pthread_mutex_unlock( &my_mutex ), "pthread_mutex_unlock" );
}

inline void thread_monitor::cancel_wait() {
    check( pthread_mutex_unlock( &my_mutex ), "pthread_mutex_unlock" );
}
#endif /* USE_PTHREAD */

} // namespace internal
} // namespace rml

#endif /* __RML_thread_monitor_H */
