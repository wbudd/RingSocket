// SPDX-License-Identifier: MIT
// Copyright © 2019 William Budd

#pragma once

// Due to their dependency relationships, all RingSocket system headers other
// than ringsocket_conf.h and ringsocket_variadic.h each include one other
// RingSocket system header, forming a chain in the following order:
//
//       <ringsocket.h>: RingSocket helper function API
//       <ringsocket_app.h>: Definition of RS_APP() and its descendent macros
//       <ringsocket_queue.h>: Struct rs_ring_queue and queuing/waking functions
//       <ringsocket_ring.h>: Single producer single consumer ring buffer API
// ----> <ringsocket_api.h>: Basic RingSocket API macros and typedefs
#include <ringsocket_variadic.h> // Arity-based macro expansion helper macros
//
// Their contents are therefore easier to understand when read in reverse order.

#include <errno.h> // errno for RS_LOG_ERRNO()
#include <stdalign.h> // C11: aligned_alloc() for RS_CACHE_ALIGNED_CALLOC()
#include <stdatomic.h> // C11: atomic_[load|store]_explicit()
#include <stddef.h> // size_t and NULL for heap memory macros below
#include <stdlib.h> // calloc(), realloc(), free() for heap memory macros below
#include <string.h> // memset() for RS_CACHE_ALIGNED_CALLOC()
#include <syslog.h> // syslog() for RS_LOG()
#include <threads.h> // C11: thread_local for RS_LOG()

// #############################################################################
// # CPU cache line size #######################################################

// It is highly recommended that the correct cache line size of the target
// architecture be passed to the compiler. For example:
// > gcc -DRS_CACHE_LINE_SIZE=$(shell getconf LEVEL1_DCACHE_LINESIZE)
// When not defined, guess a cache line size of 64, which may cause significant
// performance penalities when inaccurate.
#ifndef RS_CACHE_LINE_SIZE
#define RS_CACHE_LINE_SIZE 64
#endif
#if RS_CACHE_LINE_SIZE == 0
#define RS_CACHE_LINE_SIZE 64
#endif

// #############################################################################
// # rs_t opaque typedef #######################################################

// Every RingSocket app callback function receives the same (rs_t * rs) opaque
// pointer type as its first 1st argument. (Or maybe just pretend it's opaque?)
typedef struct rs_app_cb_args rs_t;

// #############################################################################
// # rs_ret & Co. ##############################################################

typedef enum {
    RS_OK = 0,

    // RS_CLOSE_PEER advises the calling function to initiate peer shutdown
    // because some condition ocurred that makes the called function want to
    // say goodbye to the peer.
    RS_CLOSE_PEER = -1,

    // A fatal error occurred. RS_FATAL will cause the RS process to exit
    // (across all apps and worker threads).
    RS_FATAL = -2,

    // All IO is performed in non-blocking modes. If continuing an operation
    // would cause an IO function to block, they instead return RS_AGAIN.
    // If TLS: returned on SSL_ERROR_WANT_READ or SSL_ERROR_WANT_WRITE
    // If plain TCP: returned on EAGAIN, or if any unwritten bytes still remain
    RS_AGAIN = -3
} rs_ret;

// Return if the called child function was not successful, propagating its
// returned status code to the parent function.
#define RS_GUARD(call) \
do { \
    rs_ret ret = (call); \
    if (ret != RS_OK) { \
        return ret; \
    } \
} while (0)

// #############################################################################
// # Miscellaneous macros ######################################################

// While not essential, the following macros are made available for use by apps.

// Evaluate the number of elements in an array.
#define RS_ELEM_C(arr) (sizeof(arr) / sizeof((arr)[0]))

#define RS_MIN(a, b) ((a) < (b) ? (a) : (b))
#define RS_MAX(a, b) ((a) > (b) ? (a) : (b))

// Size in bytes of a fixed length char array, excluding the trailing NULL byte.
#define RS_CONST_STRLEN(const_str) (sizeof(const_str) - 1)

#define RS_STRINGIFIED(str) #str
#define RS_STRINGIFY(str) RS_STRINGIFIED(str)

// Is this machine little-endian or big-endian?
#define RS_IS_LITTLE_ENDIAN (*((uint8_t *) (uint32_t []){1}))

// Swap between network byte order and host byte order: reverse the bytes if
// the machine is little-endian, else do nothing.
#define RS_NTOH16(h16) (RS_IS_LITTLE_ENDIAN ? \
    __builtin_bswap16(h16) : (h16))
#define RS_NTOH32(h32) (RS_IS_LITTLE_ENDIAN ? \
    __builtin_bswap32(h32) : (h32))
#define RS_NTOH64(h64) (RS_IS_LITTLE_ENDIAN ? \
    __builtin_bswap64(h64) : (h64))
#define RS_HTON16(n16) RS_NTOH16(n16)
#define RS_HTON32(n32) RS_NTOH32(n32)
#define RS_HTON64(n64) RS_NTOH64(n64)

// Dereference an int in network byte order as an int in host byte order.
#define RS_R_NTOH16(ptr) RS_NTOH16(*((uint16_t *) (ptr)))
#define RS_R_NTOH32(ptr) RS_NTOH32(*((uint32_t *) (ptr)))
#define RS_R_NTOH64(ptr) RS_NTOH64(*((uint64_t *) (ptr)))

// Assign an int in host byte order to a referenced int in network byte order.
#define RS_W_HTON16(ptr, uint16) *((uint16_t *) (ptr)) = RS_HTON16(uint16)
#define RS_W_HTON32(ptr, uint32) *((uint32_t *) (ptr)) = RS_HTON32(uint32)
#define RS_W_HTON64(ptr, uint64) *((uint64_t *) (ptr)) = RS_HTON64(uint64)

// #############################################################################
// # RS_LOG & Co. ##############################################################

// These macros are syslog() wrappers that prepend the translation unit's
// filename, function name, and line_number of the location where the macro is
// invoked.

// 1st arg: syslog priority level (required)
// 2nd arg: format string (optional)
// 3rd arg, etc: any parameters corresponding to the format string (optional)
#define RS_LOG(...) \
RS_MACRIFY_LOG( /* Arity-based macro expansion: see ringsocket_variadic.h */ \
    RS_256_3( \
        _RS_LOG_1, \
        _RS_LOG_2, \
        _RS_LOG_MORE, \
        __VA_ARGS__ \
    ), \
    __VA_ARGS__ \
)

// Same as RS_LOG, except that it also appends strerror(errno)
#define RS_LOG_ERRNO(...) \
RS_MACRIFY_LOG( \
    RS_256_3( \
        _RS_LOG_ERRNO_1, \
        _RS_LOG_ERRNO_2, \
        _RS_LOG_ERRNO_MORE, \
        __VA_ARGS__ \
    ), \
    __VA_ARGS__ \
)

#define _RS_SYSLOG(lvl, ...) \
do { \
    if ((lvl) <= _rs_log_max) { \
        syslog((lvl), "%s" __FILE__ ":%s():" RS_STRINGIFY(__LINE__) \
            __VA_ARGS__); \
    } \
} while (0)

#define _RS_LOG_1(lvl) \
    _RS_SYSLOG((lvl), , _rs_thread_id_str, __func__)
#define _RS_LOG_2(lvl, fmt) \
    _RS_SYSLOG((lvl), ": " fmt, _rs_thread_id_str, __func__)
#define _RS_LOG_MORE(lvl, fmt, ...) \
    _RS_SYSLOG((lvl), ": " fmt, _rs_thread_id_str, __func__, __VA_ARGS__)

#define _RS_LOG_ERRNO_1(lvl) \
    _RS_SYSLOG((lvl), ": %s", _rs_thread_id_str, __func__, strerror(errno))
#define _RS_LOG_ERRNO_2(lvl, fmt) \
    _RS_SYSLOG((lvl), ": " fmt ": %s", _rs_thread_id_str, __func__, \
        strerror(errno))
#define _RS_LOG_ERRNO_MORE(lvl, fmt, ...) \
    _RS_SYSLOG((lvl), ": " fmt ": %s", _rs_thread_id_str, __func__, \
        __VA_ARGS__, strerror(errno))

// Same as RS_LOG, except that the 3rd arg is expected to be a non-0-terminated
// string buffer, of which the size is expected to be the 4th arg.
#define RS_LOG_CHBUF(lvl, fmt, chbuf, ...) \
RS_MACRIFY_LOG( \
    RS_256_2( \
        _RS_LOG_CHBUF_1, \
        _RS_LOG_CHBUF_MORE, \
        __VA_ARGS__ \
    ), \
    lvl, \
    fmt, \
    chbuf, \
    __VA_ARGS__ \
)

#define _RS_LOG_CHBUF_VLA(chbuf, size) \
    char str[(size) + 1]; \
    memcpy(str, chbuf, size); \
    str[size] = '\0' \

#define _RS_LOG_CHBUF_1(lvl, fmt, chbuf, size) \
do { \
    _RS_LOG_CHBUF_VLA(chbuf, size); \
    _RS_LOG_MORE((lvl), fmt ": %s", (str)); \
} while (0)

#define _RS_LOG_CHBUF_MORE(lvl, fmt, chbuf, size, ...) \
do { \
    _RS_LOG_CHBUF_VLA(chbuf, size); \
    _RS_LOG_MORE((lvl), fmt ": %s", __VA_ARGS__, (str)); \
} while (0)

// The only two variables with external linkage in rs:

// Used instead of setlogmask() in order to optimize out the overhead of calling
// syslog() and evaluating its arguments for each logging statement beyond the
// run-time-determined mask level (e.g., LOG_DEBUG).
extern int _rs_log_max;

// Unique thread_local string such as "Worker #7: " or "App Foo: ".
// Its value is an empty "" during the early single-threaded startup phase.
extern thread_local char _rs_thread_id_str[];

#define RS_APP_NAME_MAX_STRLEN 32
#define RS_THREAD_ID_MAX_STRLEN (RS_APP_NAME_MAX_STRLEN + RS_CONST_STRLEN(": "))

// The following macro must be invoked in exactly one translation unit, in order
// to define the two variables above. In the case of apps, this is taken care of
// by the RS_APP() macro.
#define RS_LOG_VARS \
int _rs_log_max = LOG_NOTICE; \
thread_local char _rs_thread_id_str[RS_THREAD_ID_MAX_STRLEN + 1] = {0}

// #############################################################################
// # Heap memory management macros #############################################

#define RS_CALLOC(pointer, elem_c) do { \
    if (pointer) { \
        RS_LOG(LOG_CRIT, "Pointer argument of RS_CALLOC(pointer, elem_c) " \
            "must be NULL."); \
        return RS_FATAL; \
    } \
    (pointer) = calloc((elem_c), sizeof(*(pointer))); \
    if (!(pointer)) { \
        RS_LOG(LOG_ALERT, "Failed to calloc()."); \
        return RS_FATAL; \
    } \
} while (0)

#define RS_CACHE_ALIGNED_CALLOC(pointer, elem_c) do { \
    if (pointer) { \
        RS_LOG(LOG_CRIT, "Pointer argument of " \
            "RS_CACHE_ALIGNED_CALLOC(pointer, elem_c) must be NULL."); \
        return RS_FATAL; \
    } \
    size_t alloc_size = (elem_c) * sizeof(*(pointer)); \
    (pointer) = aligned_alloc(RS_CACHE_LINE_SIZE, alloc_size); \
    if (!(pointer)) { \
        RS_LOG(LOG_ALERT, "Failed to aligned_alloc()."); \
        return RS_FATAL; \
    } \
    memset((pointer), 0, alloc_size); \
} while (0)

#define RS_REALLOC(pointer, elem_c) do { \
    if (!(pointer)) { \
        RS_LOG(LOG_CRIT, "Pointer argument of RS_REALLOC(pointer, " \
            "elem_c) must not be NULL."); \
        return RS_FATAL; \
    } \
    size_t type_size = sizeof(*(pointer)); \
    (pointer) = realloc((pointer), (elem_c) * type_size); \
    if (!(pointer)) { \
        RS_LOG(LOG_ALERT, "Failed to realloc()."); \
        return RS_FATAL; \
    } \
} while (0)

#define RS_FREE(pointer) do { \
    free(pointer); \
    (pointer) = NULL; \
} while (0)

// #############################################################################
// # C11 atomic loads and stores ###############################################

// See the lengthy comment at the top of ringsocket_queue.h for explanation.

#define RS_PREVENT_COMPILER_REORDERING __asm__ volatile("" ::: "memory")

#define RS_ATOMIC_STORE_RELAXED(store, val) \
do { \
    /* Don't let the compiler move atomic_store_explicit() to earlier code */ \
    RS_PREVENT_COMPILER_REORDERING; \
    atomic_store_explicit((store), (val), memory_order_relaxed); \
    /* Don't let the compiler move atomic_store_explicit() to later code */ \
    RS_PREVENT_COMPILER_REORDERING; \
} while (0)

#define RS_ATOMIC_LOAD_RELAXED(store, val) \
    RS_CASTED_ATOMIC_LOAD_RELAXED(store, val,)
#define RS_CASTED_ATOMIC_LOAD_RELAXED(store, val, cast) \
do { \
    /* Don't let the compiler move atomic_load_explicit() to earlier code */ \
    RS_PREVENT_COMPILER_REORDERING; \
    /* The cast is needed to silence GCC when assigning an atomic_uintptr_t */ \
    /* to a uint8_t pointer (despite that being the type's intended usage). */ \
    (val) = cast atomic_load_explicit((store), memory_order_relaxed); \
    /* Don't let the compiler move atomic_load_explicit() to later code */ \
    RS_PREVENT_COMPILER_REORDERING; \
} while (0)
