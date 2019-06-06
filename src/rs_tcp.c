// SPDX-License-Identifier: MIT
// Copyright © 2019 William Budd

#include "rs_tcp.h"
#include "rs_socket.h" // free_peer_slot()
#include "rs_tls.h" // init_tls_session()
#include "rs_util.h" // get_peer_str()

rs_ret read_tcp(
    union rs_peer * peer,
    void * rbuf,
    size_t rbuf_size,
    size_t * rsize
) {
    ssize_t ret = read(peer->socket_fd, rbuf, rbuf_size);
    if (ret > 0) {
        *rsize = ret;
        return RS_OK;
    }
    *rsize = 0;
    if (!ret) {
        RS_LOG(LOG_NOTICE, "read(%d, rbuf, %zu) from %s returned 0.",
            peer->socket_fd, rbuf_size, get_peer_str(peer));
        return RS_CLOSE_PEER;
    }
    if (errno == EAGAIN) {
        peer->is_writing = false;
        return RS_AGAIN;
    }
    RS_LOG_ERRNO(LOG_ERR, "Unsuccessful read(%d, rbuf, %zu) from %s",
        peer->socket_fd, rbuf_size, get_peer_str(peer));
    return RS_CLOSE_PEER;
}

rs_ret write_tcp(
    union rs_peer * peer,
    void const * wbuf,
    size_t wbuf_size
) {
    // The main reason that this function takes a wbuf pointer to the start of
    // the original write message even when resuming partial writes, is to
    // mimic the signature of write_tls() (because SSL_write(_ex)() requires
    // receiving the same data on retries).
    size_t remaining_wsize = wbuf_size - peer->old_wsize;
    ssize_t ret = write(peer->socket_fd, (uint8_t *) wbuf + peer->old_wsize,
        remaining_wsize);
    if (ret > 0) {
        size_t wsize = ret;
        // write_tcp() and write_tls() only return RS_OK when the entire
        // message has been written out.
        if (wsize == remaining_wsize) {
            peer->old_wsize = 0;
            return RS_OK;
        }
        peer->old_wsize += wsize;
        peer->is_writing = true;
        return RS_AGAIN;
    }
    if (errno = EAGAIN) {
        peer->is_writing = true;
        return RS_AGAIN;
    }
    RS_LOG_ERRNO(LOG_ERR, "Unsuccessful write(%d, wbuf + %zu, %zu) to %s",
        peer->socket_fd, peer->old_wsize, remaining_wsize, get_peer_str(peer));
    return RS_CLOSE_PEER;
}

rs_ret write_bidirectional_tcp_shutdown(
    union rs_peer * peer
) {
    // Send a TCP FIN flag to the peer, signalling that there will be no more
    // writes from this side. read_bidirectional_tcp_shutdown() should be called
    // at some point later than this function.
    if (shutdown(peer->socket_fd, SHUT_WR) == -1) {
        RS_LOG_ERRNO(LOG_CRIT, "Unsuccessful shutdown(%d, SHUT_WR) of %s",
            peer->socket_fd, get_peer_str(peer));
        return RS_FATAL;
    }
    peer->mortality = RS_MORTALITY_SHUTDOWN_READ;
    return RS_OK;
}

static rs_ret read_bidirectional_tcp_shutdown(
    union rs_peer * peer,
    uint8_t * rbuf,
    size_t rbuf_size
) {
    // write_bidirectional_tcp_shutdown() should have been called already.
    // read() until 0 is returned, signifying the completion of a bidirectional
    // shutdown. As apparent from the fact that peer->layer has dropped to
    // RS_LAYER_TCP, any bytes read() at this stage are not considered usable,
    // so they are ignored by repeatedly reading them into the start of rbuf,
    // to be readily overwritten during any next read().
    ssize_t rsize = 0;
    do {
        rsize = read(peer->socket_fd, rbuf, rbuf_size);
        RS_LOG(LOG_DEBUG, "Read(%d, ...) %ld bytes of ignored TCP data from "
            "%s", peer->socket_fd, rsize, get_peer_str(peer));
    } while (rsize > 0);
    if (!rsize) {
        peer->mortality = RS_MORTALITY_DEAD;
        return RS_CLOSE_PEER;
    }
    if (errno == EAGAIN) {
        peer->is_writing = false;
        return RS_AGAIN;
    }
    RS_LOG_ERRNO(LOG_WARNING, "Unsuccessful read(%d, rbuf, %zu) from %s in "
        "RS_IO_STATE_CLOSING_READ_ONLY while at the TCP layer",
        peer->socket_fd, rbuf_size, get_peer_str(peer));
    peer->mortality = RS_MORTALITY_DEAD;
    return RS_CLOSE_PEER;
}

rs_ret handle_tcp_io(
    union rs_peer * peer,
    uint8_t * rbuf,
    size_t rbuf_size,
    int peer_i
) {
    switch (peer->mortality) {
    case RS_MORTALITY_LIVE: // This is a new peer
        if (peer->is_encrypted) {
            peer->layer = RS_LAYER_TLS;
            RS_GUARD(init_tls_session(peer));
        } else {
            peer->layer = RS_LAYER_HTTP;
        }
        // loop_over_events() will now call either handle_tls_events() or
        // handle_http_events(), depending on the value of peer->layer
        return RS_OK;
    case RS_MORTALITY_SHUTDOWN_WRITE:
        RS_GUARD(write_bidirectional_tcp_shutdown(peer));
        // fall through
    case RS_MORTALITY_SHUTDOWN_READ:
        switch (read_bidirectional_tcp_shutdown(peer, rbuf, rbuf_size)) {
        case RS_AGAIN:
            return RS_OK;
        case RS_FATAL:
            return RS_FATAL;
        default:
            break;
        } // fall through
    case RS_MORTALITY_DEAD: default:
        break;
    }
    if (close(peer->socket_fd) == -1) {
        RS_LOG_ERRNO(LOG_ERR, "Unsuccessful socket close(%d)",
            peer->socket_fd);
    }
    // epoll_ctl(epoll_fd, EPOLL_CTL_DEL, peer->socket_fd, NULL) is not
    // necessary: given that socket_fd was only ever seen by this worker thread,
    // there are/were no other file descriptors referring to the file
    // description to which it belonged, which means that that file description
    // is now guaranteed to be gone, along with any events it may otherwise have
    // continued to trigger. (See Q&A #6 of man 7 epoll.)
    memset(peer, 0, sizeof(union rs_peer));
    free_peer_slot(peer_i);
    return RS_OK;
}