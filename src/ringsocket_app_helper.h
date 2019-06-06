// SPDX-License-Identifier: MIT
// Copyright © 2019 William Budd

#pragma once

#include <ringsocket.h>

// Inline functions allowing apps to only include a single
// #include <ringsocket.h>, while avoiding function call overhead.

// API functions

inline uint64_t rs_get_client_id(
    struct rs * n
) {
    return *((uint64_t *) (uint32_t []){n->src_worker_thread_i, n->src_peer_i});
}

inline rs_ret rs_check_app_wsize(
    struct rs * n,
    size_t incr_size
) {
    if (!*n->wbuf) {
        RS_CALLOC(*n->wbuf, *n->wbuf_size);
    }
    if (n->wbuf_i + incr_size >= *n->wbuf_size) {
        *n->wbuf_size = n->conf->realloc_multiplier * (n->wbuf_i + incr_size);
        RS_REALLOC(*n->wbuf, *n->wbuf_size);
    }
    return RS_OK;
}

inline void rs_w_p(
    struct rs * n,
    void const * src,
    size_t size
) {
    RS_GUARD_APP(rs_check_app_wsize(n, size));
    memcpy(*n->wbuf + n->wbuf_i, src, size);
    n->wbuf_i += size;
}

inline void rs_w_uint8(
    struct rs * n,
    uint8_t u8
) {
    RS_GUARD_APP(rs_check_app_wsize(n, 1));
    (*n->wbuf)[n->wbuf_i++] = u8;
}

inline void rs_w_uint16(
    struct rs * n,
    uint16_t u16
) {
    RS_GUARD_APP(rs_check_app_wsize(n, 2));
    *((uint16_t *) (*n->wbuf + n->wbuf_i)) = u16;
    n->wbuf_i += 2;
}

inline void rs_w_uint32(
    struct rs * n,
    uint32_t u32
) {
    RS_GUARD_APP(rs_check_app_wsize(n, 4));
    *((uint32_t *) (*n->wbuf + n->wbuf_i)) = u32;
    n->wbuf_i += 4;
}

inline void rs_w_uint64(
    struct rs * n,
    uint32_t u64
) {
    RS_GUARD_APP(rs_check_app_wsize(n, 8));
    *((uint64_t *) (*n->wbuf + n->wbuf_i)) = u64;
    n->wbuf_i += 8;
}

inline void rs_w_uint16_hton(
    struct rs * n,
    uint16_t u16
) {
    rs_w_uint16(n, RS_HTON16(u16));
}

inline void rs_w_uint32_hton(
    struct rs * n,
    uint32_t u32
) {
    rs_w_uint32(n, RS_HTON32(u32));
}

inline void rs_w_uint64_hton(
    struct rs * n,
    uint64_t u64
) {
    rs_w_uint64(n, RS_HTON64(u64));
}

inline void rs_w_int8(
    struct rs * n,
    int8_t i8
) {
    rs_w_uint8(n, i8);
}

inline void rs_w_int16(
    struct rs * n,
    int16_t i16
) {
    rs_w_uint16(n, i16);
}

inline void rs_w_int32(
    struct rs * n,
    int32_t i32
) {
    rs_w_uint32(n, i32);
}

inline void rs_w_int64(
    struct rs * n,
    int64_t i64
) {
    rs_w_uint64(n, i64);
}

inline void rs_w_int16_hton(
    struct rs * n,
    int16_t i16
) {
    rs_w_uint16_hton(n, i16);
}

inline void rs_w_int32_hton(
    struct rs * n,
    int32_t i32
) {
    rs_w_uint32_hton(n, i32);
}

inline void rs_w_int64_hton(
    struct rs * n,
    int64_t i64
) {
    rs_w_uint64_hton(n, i64);
}

inline void rs_send(
    struct rs * n,
    size_t worker_i,
    enum rs_outbound_kind outbound_kind,
    uint32_t const * recipients,
    uint32_t recipient_c,
    bool is_utf8,
    void const * p,
    size_t size
) {
    size_t payload_size = n->wbuf_i + size;
    if (payload_size > n->conf->max_ws_msg_size) {
        return RS_FATAL;
    }
    size_t msg_size = payload_size;
    if (msg_size > UINT16_MAX) {
        msg_size += 10;
    } else if (msg_size > 125) {
        msg_size += 4;
    } else {
        msg_size += 2;
    }
    msg_size++;
    struct rs_ring * ring = n->outbound_rings + worker_i;
    RS_GUARD_APP(rs_prepare_ring_write(&n->io_pairs[worker_i].outbound,
        ring, msg_size));
    *ring->writer++ = (uint8_t) outbound_kind;
    if (recipient_c) {
        if (recipient_c > 1) {
            *((uint32_t *) ring->writer) = recipient_c;
            ring->writer += 4;
        }
        do {
            *((uint32_t *) ring->writer) = *recipients++;
            ring->writer += 4;
        } while (--recipient_c);
    }
    *ring->writer++ = is_utf8 ? 0x81 : 0x82;
    if (msg_size > UINT16_MAX) {
        *ring->writer++ = 127;
        RS_W_HTON64(ring->writer, payload_size);
        ring->writer += 8;
    } else if (msg_size > 125) {
        *ring->writer++ = 126;
        RS_W_HTON16(ring->writer, payload_size);
        ring->writer += 2;
    }
    if (n->wbuf_i) {
        memcpy(ring->writer, *n->wbuf, n->wbuf_i);
        ring->writer += n->wbuf_i;
    }
    if (size) {
        memcpy(ring->writer, p, size);
        ring->writer += size;
    }
    RS_GUARD_APP(rs_enqueue_ring_update(n->ring_update_queue, n->io_pairs,
        n->worker_sleep_states, n->worker_eventfds, ring->writer, worker_i,
        true));
}

inline void rs_to_single(
    struct rs * n,
    bool is_utf8,
    uint64_t client_id,
    void const * p,
    size_t size
) {
    uint32_t * u32 = (uint32_t *) &client_id;
    rs_send(n, *u32, RS_OUTBOUND_SINGLE, u32 + 1, 1, is_utf8, p, size);
    n->wbuf_i = 0;
}

inline void rs_to_multi(
    struct rs * n,
    bool is_utf8,
    uint64_t const * client_ids,
    size_t client_c,
    void const * p,
    size_t size
) {
    for (size_t i = 0; i < n->conf->worker_c; i++) {
        uint32_t cur_clients[client_c];
        size_t cur_client_c = 0;
        for (size_t j = 0; j < client_c; j++) {
            uint32_t * u32 = (uint32_t *) (client_ids + j);
            if (*u32++ == i) {
                cur_clients[cur_client_c++] = *u32;
            }
        }
        switch (cur_client_c) {
        case 0:
            continue;
        case 1:
            rs_send(n, i, RS_OUTBOUND_SINGLE, cur_clients, 1, is_utf8, p, size);
            continue;
        default:
            rs_send(n, i, RS_OUTBOUND_ARRAY, cur_clients, cur_client_c, is_utf8,
                p, size);
            continue;
        }
    }
    n->wbuf_i = 0;
}

inline void rs_to_cur(
    struct rs * n,
    bool is_utf8,
    void const * p,
    size_t size
) {
    rs_send(n, n->src_worker_thread_i, RS_OUTBOUND_SINGLE,
        (uint32_t []){n->src_peer_i}, 1, is_utf8, p, size);
    n->wbuf_i = 0;
}

inline void rs_to_every(
    struct rs * n,
    bool is_utf8,
    void const * p,
    size_t size
) {
    for (size_t i = 0; i < n->conf->worker_c; i++) {
        rs_send(n, i, RS_OUTBOUND_EVERY, NULL, 0, is_utf8, p, size);
    }
    n->wbuf_i = 0;
}

inline void rs_to_every_except_single(
    struct rs * n,
    bool is_utf8,
    uint64_t client_id,
    void const * p,
    size_t size
) {
    uint32_t * u32 = (uint32_t *) &client_id;
    for (size_t i = 0; i < n->conf->worker_c; i++) {
        if (i == *u32) {
            rs_send(n, i, RS_OUTBOUND_EVERY_EXCEPT_SINGLE, u32 + 1, 1, is_utf8,
                p, size);
        } else {
            rs_send(n, i, RS_OUTBOUND_EVERY, NULL, 0, is_utf8, p, size);
        }
    }
    n->wbuf_i = 0;
}

inline void rs_to_every_except_multi(
    struct rs * n,
    bool is_utf8,
    uint64_t const * client_ids,
    size_t client_c,
    void const * p,
    size_t size
) {
    for (size_t i = 0; i < n->conf->worker_c; i++) {
        uint32_t cur_clients[client_c];
        size_t cur_client_c = 0;
        for (size_t j = 0; j < client_c; j++) {
            uint32_t * u32 = (uint32_t *) (client_ids + j);
            if (*u32++ == i) {
                cur_clients[cur_client_c++] = *u32;
            }
        }
        switch (cur_client_c) {
        case 0:
            rs_send(n, i, RS_OUTBOUND_EVERY, NULL, 0, is_utf8, p, size);
            continue;
        case 1:
            rs_send(n, i, RS_OUTBOUND_EVERY_EXCEPT_SINGLE, cur_clients, 1,
                is_utf8, p, size);
            continue;
        default:
            rs_send(n, i, RS_OUTBOUND_EVERY_EXCEPT_ARRAY, cur_clients,
                cur_client_c, is_utf8, p, size);
            continue;
        }
    }
    n->wbuf_i = 0;
}

inline void rs_to_every_except_cur(
    struct rs * n,
    bool is_utf8,
    void const * p,
    size_t size
) {
    for (size_t i = 0; i < n->conf->worker_c; i++) {
        if (i == n->src_worker_thread_i) {
            rs_send(n, i, RS_OUTBOUND_EVERY_EXCEPT_SINGLE,
                (uint32_t []){n->src_peer_i}, 1, is_utf8, p, size);
        } else {
            rs_send(n, i, RS_OUTBOUND_EVERY, NULL, 0, is_utf8, p, size);
        }
    }
    n->wbuf_i = 0;
}

// Non-API functions

inline rs_ret rs_init_rings(
    struct rs_conf const * conf,
    struct rs_app const * app,
    struct rs_thread_io_pairs * * io_pairs,
    struct rs_ring * * outbound_rings,
    uint8_t * * inbound_readers,
    struct rs_ring_update_queue * ring_update_queue
) {
    // Initialize the outbound write side and inbound read side of this app
    // thread's ring buffers. Mirrors init_rings() in ring.c which initializes
    // the inbound write side and outbound read side of the worker thread
    // calling it, except that init_rings() doesn't initialize io_pairs.
    RS_CACHE_ALIGNED_CALLOC(*io_pairs, conf->worker_c);
    RS_CALLOC(*outbound_rings, conf->worker_c);
    RS_CALLOC(*inbound_readers, conf->worker_c);
    for (size_t i = 0; i < conf->worker_c; i++) {
        struct rs_ring * out_ring = *outbound_rings + i;
        RS_CACHE_ALIGNED_CALLOC(out_ring->buf, conf->outbound_ring_buf_size);
        out_ring->writer = out_ring->buf;
        out_ring->alloc_multiplier = conf->realloc_multiplier;
        RS_ATOMIC_STORE_RELAXED(&(*io_pairs)[i].outbound.writer,
            (atomic_uintptr_t) out_ring->buf);
        RS_ATOMIC_STORE_RELAXED(&(*io_pairs)[i].outbound.reader,
            (atomic_uintptr_t) out_ring->buf);
        RS_ATOMIC_LOAD_RELAXED_CASTED(&(*io_pairs)[i].inbound.reader,
            inbound_readers[i], (uint8_t *));
    }
    ring_update_queue->size = app->update_queue_size;
    RS_CALLOC(ring_update_queue->queue, ring_update_queue->size);
    return RS_OK;
}

inline rs_ret rs_get_time_in_milliseconds(
    uint64_t * time_ms
) {
    struct timespec ts = {0};
    if (clock_gettime(CLOCK_MONOTONIC_COARSE, &ts) == -1) {
        RS_LOG_ERRNO(LOG_CRIT,
            "Unsuccessful clock_gettime(CLOCK_MONOTONIC_COARSE, &ts)");
        return RS_FATAL;
    }
    *time_ms = 1000 * ts.tv_sec + ts.tv_nsec / 1000000;
    return RS_OK;
}