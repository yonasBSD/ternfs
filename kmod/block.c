// Plan for socket reclamation:
// * Have a hashmap or anyway some sort of mapping from port/ip to socket
// * Every n seconds in get_socket function purge inactive ones
//
// Plan for writing. At any given point, we are:
// * Writing the current data blocks and the previous parity blocks.
// * Computing the current parity blocks.
// We can do this by just firing all the requests, start computing the
// parity blocks, and then waiting on all the responses.
// This should ideally keep us writing close to 100% of the time, with
// a small tail of writing the parity blocks for the last span.
#include <linux/inet.h>
#include <net/tcp.h>
#include <net/sock.h>

#include "block.h"
#include "log.h"
#include "err.h"
#include "skb.h"
#include "super.h"
#include "crc.h"
#include "trace.h"
#include "wq.h"

#define EGGSFS_BLOCKS_REQ_HEADER_SIZE (4 + 8 + 1) // protocol + block service id + kind

struct eggsfs_block_socket {
    struct socket* sock;
    atomic_t sock_err;
    struct list_head requests;
    spinlock_t requests_lock;
    struct sockaddr_in addr;
    u64 block_service_id;
    // saved callbacks
    void (*saved_state_change)(struct sock *sk);
    void (*saved_data_ready)(struct sock *sk);
    void (*saved_write_space)(struct sock *sk);
    // only used for block writes
    struct work_struct write_work;
};

static u64 WHICH_BLOCK_IP = 0;

static struct kmem_cache* eggsfs_fetch_block_request_cachep;
static struct kmem_cache* eggsfs_write_block_request_cachep;

static void eggsfs_fetch_block_state_check(struct sock* sk) {
    // TODO check if connection has been closed before its time and error out
    // eggsfs_debug_print("socket state check triggered: %d", sk->sk_state);
}

static void eggsfs_fetch_block_state_change(struct sock* sk) {
    read_lock_bh(&sk->sk_callback_lock);
    eggsfs_fetch_block_state_check(sk);
    read_unlock_bh(&sk->sk_callback_lock);
}

// Returns a negative result if the socket has to be considered corrupted.
// If a "recoverable" error occurs (e.g. the server just replies with an error)
// `req->err` will be filled in and receiving can continue. If an unrecoverable
// error occurs `req->err` will be filled in also, but it's expected that the
// caller stops operating on this socket.
static int eggsfs_fetch_block_receive_single_req(
    struct eggsfs_fetch_block_request* req,
    struct sk_buff* skb,
    unsigned int offset, size_t len0
) {
    size_t len = len0;

#define HEADER_COPY(buf, count) ({ \
        int read = count > 0 ? eggsfs_skb_copy(buf, skb, offset, count) : 0; \
        offset += read; \
        len -= read; \
        req->header_read += read; \
        read; \
    })

    // Header
    {
        int header_left = EGGSFS_BLOCKS_RESP_HEADER_SIZE - (int)req->header_read;
        int read = HEADER_COPY(req->header.buf + req->header_read, header_left);
        header_left -= read;
        if (header_left > 0) { return len0-len; }
    }

    // Protocol check
    if (unlikely(le32_to_cpu(req->header.data.protocol) != EGGSFS_BLOCKS_RESP_PROTOCOL_VERSION)) {
        eggsfs_info_print("bad blocks resp protocol, expected %*pE, got %*pE", 4, &EGGSFS_BLOCKS_RESP_PROTOCOL_VERSION, 4, &req->header.data.protocol);
        return eggsfs_error_to_linux(EGGSFS_ERR_MALFORMED_RESPONSE);
    }

    // Error check
    if (unlikely(req->header.data.kind == 0)) {
        int error_left = (EGGSFS_BLOCKS_RESP_HEADER_SIZE + 2) - (int)req->header_read;
        int read = HEADER_COPY(req->header.buf + req->header_read, error_left);
        error_left -= read;
        if (error_left > 0) { return len0-len; }
        eggsfs_info_print("failed=%u", req->header.data.error);
        // we got an error "nicely", the socket can carry on living.
        req->err = eggsfs_error_to_linux(le16_to_cpu(req->header.data.error));
        return len0-len;
    }

#undef HEADER_COPY

    // Actual data copy

    BUG_ON(req->bytes_left == 0);

    struct page* page = list_first_entry(&req->pages, struct page, lru);
    BUG_ON(!page);
    char* page_ptr = kmap_atomic(page);

    struct skb_seq_state seq;
    skb_prepare_seq_read(skb, offset, offset + min(req->bytes_left, (u32)len), &seq);

    u32 block_bytes_read = 0;
    while (block_bytes_read < req->bytes_left) {
        const u8* data;
        u32 avail = skb_seq_read(block_bytes_read, &data, &seq);
        if (avail == 0) { break; }

        kernel_fpu_begin(); // crc
        while (avail) {
            u32 this_len = min3(avail, (u32)PAGE_SIZE - req->page_offset, req->bytes_left - block_bytes_read);
            req->crc = eggsfs_crc32c(req->crc, data, this_len);
            memcpy(page_ptr + req->page_offset, data, this_len);
            data += this_len;
            req->page_offset += this_len;
            avail -= this_len;
            block_bytes_read += this_len;

            if (req->page_offset >= PAGE_SIZE) {
                BUG_ON(req->page_offset > PAGE_SIZE);

                kunmap_atomic(page_ptr);
                list_rotate_left(&req->pages);
                page = list_first_entry(&req->pages, struct page, lru);
                page_ptr = kmap_atomic(page);
                req->page_offset = 0;
            }
        }
        kernel_fpu_end();
    }

    // We might have not terminated with avail == 0, but because we're done with this request
    skb_abort_seq_read(&seq);

    req->bytes_left -= block_bytes_read;

    // The last page is partially filled, zero it and return to first page
    if (!req->bytes_left && req->page_offset) {
        memset(page_ptr + req->page_offset, 0, PAGE_SIZE - req->page_offset);
        list_rotate_left(&req->pages);
        req->page_offset = 0;
    }

    kunmap_atomic(page_ptr);

    return (len0-len) + block_bytes_read;
}

static void eggsfs_put_socket(struct eggsfs_block_socket *socket) {
    read_lock(&socket->sock->sk->sk_callback_lock);
    socket->sock->sk->sk_data_ready = socket->saved_data_ready;
    socket->sock->sk->sk_state_change = socket->saved_state_change;
    socket->sock->sk->sk_write_space = socket->saved_write_space;
    read_unlock(&socket->sock->sk->sk_callback_lock);

    sock_release(socket->sock);
    kfree(socket);
}

static int eggsfs_fetch_block_receive(read_descriptor_t* rd_desc, struct sk_buff* skb, unsigned int offset, size_t len) {
    struct eggsfs_block_socket* socket = rd_desc->arg.data;

    int initial_len = len;
    int sock_err;
    for (;;) {
        sock_err = atomic_read(&socket->sock_err);
        if (sock_err) { break; }
        spin_lock_bh(&socket->requests_lock);
        struct eggsfs_fetch_block_request* req = list_first_entry_or_null(&socket->requests, struct eggsfs_fetch_block_request, socket_requests);
        if (req != NULL) { list_del(&req->socket_requests); } // to avoid races with eggsfs_put_fetch_block_socket
        spin_unlock_bh(&socket->requests_lock);
        if (req == NULL) { break; }
        int consumed = sock_err;
        if (consumed == 0) {
            consumed = eggsfs_fetch_block_receive_single_req(req, skb, offset, len);
            if (consumed < 0) {
                sock_err = consumed;
                atomic_cmpxchg(&socket->sock_err, 0, sock_err);
            } else {
                offset += consumed;
                len -= consumed;
            }
        } else {
            req->err = consumed;
        }
        if (req->err || req->bytes_left == 0) {
            spin_lock_bh(&socket->requests_lock);
            BUG_ON(atomic_read(&req->refcount) < 1);
            bool should_free = atomic_dec_and_test(&req->refcount);
            spin_unlock_bh(&socket->requests_lock);
            complete_all(&req->comp);
            // Important to do the actual freeing after the completion: otherwise
            // there might be races between this and a consumer taking ownership
            // of the pages.
            if (should_free) {
                put_pages_list(&req->pages);
                kmem_cache_free(eggsfs_fetch_block_request_cachep, req);
            }
        } else {
            // Not done yet, add back again to the list
            spin_lock_bh(&socket->requests_lock);
            list_add(&req->socket_requests, &socket->requests);
            spin_unlock_bh(&socket->requests_lock);
            break;
        }
    }
    if (sock_err == 0) {
        BUG_ON(len != 0); // can't have leftovers
    }
    return initial_len;
}

void eggsfs_put_fetch_block_socket(struct eggsfs_block_socket *socket) {
    atomic_set(&socket->sock_err, -EINTR);
    // We assume that at this point nobody else is traversing these
    // (this will change when we start sharing sockets). TODO this is
    // quite nasty: come up with something better when we start sharing
    // sockets. The only reason why I'm doing this is that the fetching
    // code in span.c would be pretty annoying to write if we were to
    // have to wait on each request on failure.
    for (;;) {
        spin_lock_bh(&socket->requests_lock);
        struct eggsfs_fetch_block_request* req = list_first_entry_or_null(&socket->requests, struct eggsfs_fetch_block_request, socket_requests);
        if (req != NULL) { list_del(&req->socket_requests); }
        spin_unlock_bh(&socket->requests_lock);
        if (req == NULL) { break; }
        put_pages_list(&req->pages);
        kmem_cache_free(eggsfs_fetch_block_request_cachep, req);
    }

    eggsfs_put_socket(socket);
}

static void eggsfs_fetch_block_data_ready(struct sock* sk) {
    read_lock_bh(&sk->sk_callback_lock);
    read_descriptor_t rd_desc;
    // Taken from iscsi -- we set count to 1 because we want the network layer to
    // hand us all the skbs that are available.
    rd_desc.arg.data = sk->sk_user_data;
    rd_desc.count = 1;
    tcp_read_sock(sk, &rd_desc, eggsfs_fetch_block_receive);
    eggsfs_fetch_block_state_check(sk);
    read_unlock_bh(&sk->sk_callback_lock);
}

struct eggsfs_block_socket* eggsfs_get_fetch_block_socket(struct eggsfs_block_service *block_service) {
    struct eggsfs_block_socket* socket = kzalloc(sizeof(struct eggsfs_block_socket), GFP_KERNEL);
    if (socket == NULL) { return ERR_PTR(-ENOMEM); }

    int err = sock_create_kern(&init_net, PF_INET, SOCK_STREAM, IPPROTO_TCP, &socket->sock);
    if (err != 0) { goto out_err; }

    socket->block_service_id = block_service->id;

    atomic_set(&socket->sock_err, 0);
    
    socket->saved_data_ready = socket->sock->sk->sk_data_ready;
    socket->saved_state_change = socket->sock->sk->sk_state_change;
    socket->saved_write_space = socket->sock->sk->sk_write_space;

    // Try both ips (if we have them) before giving up
    int block_ip = WHICH_BLOCK_IP++;

    int i;
    for (i = 0; i < 1 + (block_service->port2 != 0); i++, block_ip++) { // we might not have a second address
        socket->addr.sin_family = AF_INET;
        if (block_service->port2 == 0 || block_ip & 1) {
            socket->addr.sin_addr.s_addr = htonl(block_service->ip1);
            socket->addr.sin_port = htons(block_service->port1);
        } else {
            socket->addr.sin_addr.s_addr = htonl(block_service->ip2);
            socket->addr.sin_port = htons(block_service->port2);
        }

        // TODO we should probably use TFO, but it's annoying because our custom callbacks
        // break the connection, so we'd need to coordinate with that in a more annoying way.
        // Also this makes it easier to try both IPs immediately.
        err = kernel_connect(socket->sock, (struct sockaddr*)&socket->addr, sizeof(socket->addr), 0);
        if (err < 0) {
            if (unlikely(err == -ERESTARTSYS)) {
                eggsfs_debug_print("could not connect to block service at %pI4:%d: %d", &socket->addr.sin_addr, ntohs(socket->addr.sin_port), err);
            } else {
                eggsfs_warn_print("could not connect to block service at %pI4:%d: %d", &socket->addr.sin_addr, ntohs(socket->addr.sin_port), err);
            }
        } else {
            break;
        }
    }
    if (err < 0) { goto out_err_sock; }

    INIT_LIST_HEAD(&socket->requests);
    spin_lock_init(&socket->requests_lock);

    // Important for the callbacks to happen after the connect, see TODO above.
    read_lock(&socket->sock->sk->sk_callback_lock);
    socket->sock->sk->sk_user_data = socket;
    socket->sock->sk->sk_data_ready = eggsfs_fetch_block_data_ready;
    socket->sock->sk->sk_state_change = eggsfs_fetch_block_state_change;
    read_unlock(&socket->sock->sk->sk_callback_lock);

    return socket;

out_err_sock:
    sock_release(socket->sock);
out_err:
    kfree(socket);
    return ERR_PTR(err);
}

struct eggsfs_fetch_block_request* eggsfs_fetch_block(
    struct eggsfs_block_socket* socket,
    u64 block_service_id,
    u64 block_id,
    u32 offset,
    u32 count
) {
    int err;

    struct eggsfs_fetch_block_request* req = kmem_cache_alloc(eggsfs_fetch_block_request_cachep, GFP_KERNEL);
    if (!req) { err = -ENOMEM; goto out_err; }

    INIT_LIST_HEAD(&req->pages);
    int i;
    for (i = 0; i < (count + PAGE_SIZE - 1)/PAGE_SIZE; i++) {
        struct page* page = alloc_page(GFP_KERNEL);
        if (!page) { err = -ENOMEM; goto out_err_pages; }
        list_add_tail(&page->lru, &req->pages);
    }

    req->bytes_left = count;
    req->page_offset = 0;
    req->header_read = 0;
    req->crc = 0;
    req->err = 0;
    // one for the socket, one for the caller
    atomic_set(&req->refcount, 2);
    reinit_completion(&req->comp);

    spin_lock_bh(&socket->requests_lock);
    list_add_tail(&req->socket_requests, &socket->requests);
    spin_unlock_bh(&socket->requests_lock);

    // fill in req
    char req_msg_buf[EGGSFS_BLOCKS_REQ_HEADER_SIZE + EGGSFS_FETCH_BLOCK_REQ_SIZE];
    char* req_msg = req_msg_buf;
    put_unaligned_le32(EGGSFS_BLOCKS_REQ_PROTOCOL_VERSION, req_msg); req_msg += 4;
    put_unaligned_le64(block_service_id, req_msg); req_msg += 8;
    *(u8*)req_msg = EGGSFS_BLOCKS_FETCH_BLOCK; req_msg += 1;
    {
        struct eggsfs_bincode_put_ctx ctx = {
            .start = req_msg,
            .cursor = req_msg,
            .end = req_msg_buf + sizeof(req_msg_buf),
        };
        eggsfs_fetch_block_req_put_start(&ctx, start);
        eggsfs_fetch_block_req_put_block_id(&ctx, start, req_block_id, block_id);
        eggsfs_fetch_block_req_put_offset(&ctx, req_block_id, req_offset, offset);
        eggsfs_fetch_block_req_put_count(&ctx, req_offset, req_count, count);
        eggsfs_fetch_block_req_put_end(ctx, req_count, end);
        BUG_ON(ctx.cursor != ctx.end);
    }

    // send message
    struct msghdr msg = { NULL, };
    eggsfs_debug_print("sending fetch block req to %pI4:%d", &socket->addr.sin_addr, ntohs(socket->addr.sin_port));
    struct kvec iov = {
        .iov_base = req_msg_buf,
        .iov_len = sizeof(req_msg_buf),
    };
    err = kernel_sendmsg(socket->sock, &msg, &iov, 1, iov.iov_len);
    if (err < 0) { goto out_err_pages; }

    return req;

out_err_pages:
    put_pages_list(&req->pages);
    kmem_cache_free(eggsfs_fetch_block_request_cachep, req);
out_err:
    eggsfs_info_print("couldn't start fetch block request, err=%d", err);
    return ERR_PTR(err);
}

void eggsfs_put_fetch_block_request(struct eggsfs_fetch_block_request* req) {
    BUG_ON(atomic_read(&req->refcount) < 1);
    if (atomic_dec_and_test(&req->refcount)) {
        put_pages_list(&req->pages);
        kmem_cache_free(eggsfs_fetch_block_request_cachep, req);
    }
}

static void eggsfs_write_block_state_check(struct sock* sk) {
    // TODO check if connection has been closed before its time and error out
    // eggsfs_debug_print("socket state check triggered: %d", sk->sk_state);
}

static void eggsfs_write_block_state_change(struct sock* sk) {
    read_lock_bh(&sk->sk_callback_lock);
    eggsfs_write_block_state_check(sk);
    read_unlock_bh(&sk->sk_callback_lock);
}

static inline int atomic_set_return(atomic_t *v, int i) {
    atomic_set(v, i);
    return i;
}

static inline void eggsfs_write_block_trace(struct eggsfs_write_block_request* req, u8 event, int err) {
    trace_eggsfs_block_write(req->block_service_id, req->block_id, req->write_resp_read+req->written_resp_read, req->write_req_written + (req->size - req->bytes_left), event, err);
}

static int eggsfs_write_block_receive_single_req(
    struct eggsfs_block_socket* socket,
    struct eggsfs_write_block_request* req,
    struct sk_buff* skb,
    unsigned int offset, size_t len0
) {
    size_t len = len0;

    eggsfs_write_block_trace(req, EGGSFS_BLOCK_WRITE_RECV_ENTER, 0);

#define BLOCK_WRITE_EXIT(i) do { \
        eggsfs_write_block_trace(req, EGGSFS_BLOCK_WRITE_RECV_EXIT, i >= 0 ? 0 : i); \
        return i; \
    } while (0)

    // Write resp (after write request)

#define HEADER_COPY(buf, count) ({ \
        int read = count > 0 ? eggsfs_skb_copy(buf, skb, offset, count) : 0; \
        offset += read; \
        len -= read; \
        req->write_resp_read += read; \
        read; \
    })

    // Header
    {
        int header_left = EGGSFS_BLOCKS_RESP_HEADER_SIZE - (int)req->write_resp_read;
        int read = HEADER_COPY(req->write_resp.buf + req->write_resp_read, header_left);
        header_left -= read;
        if (header_left > 0) { BLOCK_WRITE_EXIT(len0-len); }
    }

    // Protocol check
    if (unlikely(le32_to_cpu(req->write_resp.data.protocol) != EGGSFS_BLOCKS_RESP_PROTOCOL_VERSION)) {
        eggsfs_info_print("bad blocks resp protocol, expected %*pE, got %*pE", 4, &EGGSFS_BLOCKS_RESP_PROTOCOL_VERSION, 4, &req->write_resp.data.protocol);
        BLOCK_WRITE_EXIT(eggsfs_error_to_linux(EGGSFS_ERR_MALFORMED_RESPONSE));
    }

    // Error check
    if (unlikely(req->write_resp.data.kind == 0)) {
        int error_left = (EGGSFS_BLOCKS_RESP_HEADER_SIZE + 2) - (int)req->write_resp_read;
        int read = HEADER_COPY(req->write_resp.buf + req->write_resp_read, error_left);
        error_left -= read;
        if (error_left > 0) { BLOCK_WRITE_EXIT(len0-len); }
        eggsfs_info_print("failed=%u", req->write_resp.data.error);
        // We immediately start writing, so any error here means that the socket is kaput
        BLOCK_WRITE_EXIT(atomic_set_return(&req->status, eggsfs_error_to_linux(le16_to_cpu(req->write_resp.data.error))));
    }

#undef HEADER_COPY

    // Written resp (after the block has been fully written)

#define HEADER_COPY(buf, count) ({ \
        int read = count > 0 ? eggsfs_skb_copy(buf, skb, offset, count) : 0; \
        offset += read; \
        len -= read; \
        req->written_resp_read += read; \
        read; \
    })

    // Header
    {
        int header_left = EGGSFS_BLOCKS_RESP_HEADER_SIZE - (int)req->written_resp_read;
        int read = HEADER_COPY(req->written_resp.buf + req->written_resp_read, header_left);
        header_left -= read;
        if (header_left > 0) {
            BLOCK_WRITE_EXIT(len0-len);
        }
    }

    // Protocol check
    if (unlikely(le32_to_cpu(req->written_resp.data.protocol) != EGGSFS_BLOCKS_RESP_PROTOCOL_VERSION)) {
        eggsfs_info_print("bad blocks resp protocol, expected %*pE, got %*pE", 4, &EGGSFS_BLOCKS_RESP_PROTOCOL_VERSION, 4, &req->written_resp.data.protocol);
        BLOCK_WRITE_EXIT(eggsfs_error_to_linux(EGGSFS_ERR_MALFORMED_RESPONSE));
    }

    // Error check
    if (unlikely(req->written_resp.data.kind == 0)) {
        int error_left = (EGGSFS_BLOCKS_RESP_HEADER_SIZE + 2) - (int)req->written_resp_read;
        int read = HEADER_COPY(req->written_resp.buf + req->written_resp_read, error_left);
        error_left -= read;
        if (error_left > 0) {
            BLOCK_WRITE_EXIT(len0-len);
        }
        eggsfs_info_print("failed=%u", req->written_resp.data.error);
        // We immediately start writing, so any error here means that the socket is kaput
        // TODO here it might actually be fine, but better not risk it
        BLOCK_WRITE_EXIT(atomic_set_return(&req->status, eggsfs_error_to_linux(le16_to_cpu(req->written_resp.data.error))));
    }

    // We can finally get the proof
    {
        int proof_left = (EGGSFS_BLOCKS_RESP_HEADER_SIZE + 8) - (int)req->written_resp_read;
        int read = HEADER_COPY(req->written_resp.buf + req->written_resp_read, proof_left);
        proof_left -= read;
        if (proof_left > 0) {
            BLOCK_WRITE_EXIT(len0-len);
        }
    }

#undef HEADER_COPY

    // we can't get here unless we've fully written the block, the channel is broken
    if (smp_load_acquire(&req->bytes_left)) {
        eggsfs_warn_print("unexpected written resp before writing out block");
        BLOCK_WRITE_EXIT(atomic_set_return(&req->status, -EIO));
    }

    // We're good!
    atomic_set_return(&req->status, 1);

    BLOCK_WRITE_EXIT(len0-len);

#undef BLOCK_WRITE_EXIT
}

static int eggsfs_write_block_receive_inner(struct eggsfs_block_socket* socket, struct sk_buff* skb, unsigned int offset, size_t len) {
    int initial_len = len;
    int sock_err = atomic_read(&socket->sock_err);
    while (true) {
        spin_lock_bh(&socket->requests_lock);
        struct eggsfs_write_block_request* req = list_empty(&socket->requests) ?
            NULL :
            list_first_entry(&socket->requests, struct eggsfs_write_block_request, socket_requests);
        spin_unlock_bh(&socket->requests_lock);
        if (req == NULL) { break; }
        int consumed = sock_err;
        if (consumed == 0) {
            consumed = eggsfs_write_block_receive_single_req(socket, req, skb, offset, len);
            if (consumed < 0) {
                sock_err = consumed;
            } else {
                offset += consumed;
                len -= consumed;
            }
        } else {
            atomic_set(&req->status, consumed);
        }
        int status = atomic_read(&req->status);
        if (status != 0) {
            spin_lock_bh(&socket->requests_lock);
            list_del(&req->socket_requests);
            spin_unlock_bh(&socket->requests_lock);
            eggsfs_write_block_trace(req, EGGSFS_BLOCK_WRITE_DONE, status > 0 ? 0 : status);
            queue_work(eggsfs_wq, req->callback);
            // start writing the next request
            queue_work(eggsfs_wq, &socket->write_work);
        } else {
            break;
        }
    }
    if (sock_err == 0) {
        BUG_ON(len != 0); // can't have leftovers
    }
    return initial_len;
}

static int eggsfs_write_block_receive(read_descriptor_t* rd_desc, struct sk_buff* skb, unsigned int offset, size_t len) {
    struct eggsfs_block_socket* socket = rd_desc->arg.data;
    return eggsfs_write_block_receive_inner(socket, skb, offset, len);
}

static void eggsfs_write_block_data_ready(struct sock* sk) {
    read_lock_bh(&sk->sk_callback_lock);
    read_descriptor_t rd_desc;
    rd_desc.arg.data = sk->sk_user_data;
    rd_desc.count = 1;
    tcp_read_sock(sk, &rd_desc, eggsfs_write_block_receive);
    eggsfs_fetch_block_state_check(sk);
    read_unlock_bh(&sk->sk_callback_lock);
}

static void eggsfs_write_block_write_space(struct sock* sk) {
    read_lock_bh(&sk->sk_callback_lock);
    struct eggsfs_block_socket* socket = (struct eggsfs_block_socket*)sk->sk_user_data;
    if (sk_stream_is_writeable(sk)) {
        clear_bit(SOCK_NOSPACE, &sk->sk_socket->flags);
        trace_eggsfs_block_write(socket->block_service_id, 0, 0, 0, EGGSFS_BLOCK_WRITE_WRITE_QUEUED, 0);
        queue_work(eggsfs_wq, &socket->write_work);
    }
    read_unlock_bh(&sk->sk_callback_lock);
}

static void eggsfs_write_block_work(struct work_struct* work) {
    struct eggsfs_block_socket* socket = container_of(work, struct eggsfs_block_socket, write_work);

    // The way this function works means that we will not start writing a request
    // before we've finished reading the proof for the previous one. We could be
    // slightly lower latency by writing ahead, but probably not a big difference.

    spin_lock_bh(&socket->requests_lock);
    struct eggsfs_write_block_request* req = list_first_entry_or_null(&socket->requests, struct eggsfs_write_block_request, socket_requests);
    spin_unlock_bh(&socket->requests_lock);
    if (req == NULL) {
        eggsfs_debug_print("no request to work on");
        return;
    }

    eggsfs_write_block_trace(req, EGGSFS_BLOCK_WRITE_WRITE_ENTER, 0);

    int err = 0;

#define BLOCK_WRITE_EXIT do { \
        eggsfs_write_block_trace(req, EGGSFS_BLOCK_WRITE_WRITE_EXIT, err); \
        return; \
    } while (0)


    // Still writing request
    while (req->write_req_written < EGGSFS_BLOCKS_REQ_HEADER_SIZE+EGGSFS_WRITE_BLOCK_REQ_SIZE) {
        char req_msg_buf[EGGSFS_BLOCKS_REQ_HEADER_SIZE+EGGSFS_WRITE_BLOCK_REQ_SIZE];
        char* req_msg = req_msg_buf;
        put_unaligned_le32(EGGSFS_BLOCKS_REQ_PROTOCOL_VERSION, req_msg); req_msg += 4;
        put_unaligned_le64(req->block_service_id, req_msg); req_msg += 8;
        *(u8*)req_msg = EGGSFS_BLOCKS_WRITE_BLOCK; req_msg += 1;
        {
            struct eggsfs_bincode_put_ctx ctx = {
                .start = req_msg,
                .cursor = req_msg,
                .end = req_msg_buf + sizeof(req_msg_buf),
            };
            eggsfs_write_block_req_put_start(&ctx, start);
            eggsfs_write_block_req_put_block_id(&ctx, start, req_block_id, req->block_id);
            eggsfs_write_block_req_put_crc(&ctx, req_block_id, req_crc, req->crc);
            eggsfs_write_block_req_put_size(&ctx, req_crc, req_size, req->size);
            eggsfs_write_block_req_put_certificate(&ctx, req_size, req_certificate, req->certificate);
            eggsfs_write_block_req_put_end(ctx, req_certificate, end);
            BUG_ON(ctx.cursor != ctx.end);
        }

        // send message
        struct msghdr msg = {
            .msg_flags = MSG_MORE | MSG_DONTWAIT,
        };
        eggsfs_debug_print("sending write block req to %pI4:%d", &socket->addr.sin_addr, ntohs(socket->addr.sin_port));
        struct kvec iov = {
            .iov_base = req_msg_buf + req->write_req_written,
            .iov_len = sizeof(req_msg_buf) - req->write_req_written,
        };
        int sent = kernel_sendmsg(socket->sock, &msg, &iov, 1, iov.iov_len);
        if (sent == -EAGAIN) { BLOCK_WRITE_EXIT; } // done for now, will be rescheduled by `sk_write_space`
        if (sent < 0) { err = sent; goto out_err; }
        req->write_req_written += sent;
    }

    // Write the pages out
    u32 bytes_left = req->bytes_left;
    while (bytes_left > 0) {
        struct page* page = req->page;
        int sent = kernel_sendpage(
            socket->sock, page,
            req->page_offset,
            min((u32)(PAGE_SIZE - req->page_offset), bytes_left),
            MSG_DONTWAIT
        );
        if (sent == -EAGAIN) {
            // done for now, will be rescheduled by `sk_write_space`
            break;
        }
        if (sent < 0) { err = sent; goto out_err; }
        req->page_offset += sent;
        bytes_left -= sent;
        if (req->page_offset >= PAGE_SIZE) {
            BUG_ON(req->page_offset != PAGE_SIZE);
            req->page_offset = 0;
            req->page = (struct page*)req->page->private;
        }
    }
    smp_store_release(&req->bytes_left, bytes_left);
    BLOCK_WRITE_EXIT;

out_err:
    eggsfs_debug_print("block write failed err=%d", err);
    read_lock(&socket->sock->sk->sk_callback_lock);
    atomic_set(&socket->sock_err, err);
    // this will just drain the request list erroring them all because of the
    // socket error.
    eggsfs_write_block_receive_inner(socket, NULL, 0, 0);
    read_unlock(&socket->sock->sk->sk_callback_lock);

    BLOCK_WRITE_EXIT;

#undef BLOCK_WRITE_EXIT
}

struct eggsfs_block_socket* eggsfs_get_write_block_socket(struct eggsfs_block_service* block_service) {
    struct eggsfs_block_socket* socket = eggsfs_get_fetch_block_socket(block_service);
    if (IS_ERR(socket)) { return socket; }

    socket->sock->sk->sk_data_ready = eggsfs_write_block_data_ready;
    socket->sock->sk->sk_state_change = eggsfs_write_block_state_change;
    socket->sock->sk->sk_write_space = eggsfs_write_block_write_space;

    INIT_WORK(&socket->write_work, eggsfs_write_block_work);

    return socket;
}

void eggsfs_put_write_block_socket(struct eggsfs_block_socket* socket) {
    cancel_work_sync(&socket->write_work);

    spin_lock_bh(&socket->requests_lock);
    BUG_ON(!list_empty(&socket->requests));
    spin_unlock_bh(&socket->requests_lock);

    eggsfs_put_socket(socket);
}

struct eggsfs_write_block_request* eggsfs_write_block(
    struct eggsfs_block_socket* socket,
    struct work_struct* callback,
    // This must match with what you passed into `eggsfs_get_fetch_block_socket`
    u64 block_service_id,
    u64 block_id,
    u64 certificate,
    u32 size,
    u32 crc,
    // There must be enough pages to write everything. The next page should
    // be in ->private (the reasons for not using ->lru are purely because
    // how some other code works out)
    struct page* page
) {
    int err;

    struct eggsfs_write_block_request* req = kmem_cache_alloc(eggsfs_write_block_request_cachep, GFP_KERNEL);
    if (!req) { err = -ENOMEM; goto out_err; }

    req->page = page;
    atomic_set(&req->status, 0);
    req->bytes_left = size;
    req->page_offset = 0;
    req->write_resp_read = 0;
    req->write_req_written = 0;
    req->written_resp_read = 0;
    req->block_service_id = block_service_id;
    req->block_id = block_id;
    req->crc = crc;
    req->size = size;
    req->certificate = certificate;
    req->callback = callback;
    // we rely on the proof becoming non-zero to find out when the request is finished
    memset(&req->written_resp.buf, 0, sizeof(req->written_resp.buf));

    spin_lock_bh(&socket->requests_lock);
    list_add_tail(&req->socket_requests, &socket->requests);
    spin_unlock_bh(&socket->requests_lock);

    // the request is filled in by the normal writer, since we need to ensure ordering
    queue_work(eggsfs_wq, &socket->write_work);

    eggsfs_write_block_trace(req, EGGSFS_BLOCK_WRITE_QUEUED, 0);

    return req;

out_err:
    eggsfs_info_print("couldn't start write block request, err=%d", err);
    return ERR_PTR(err);
}

void eggsfs_put_write_block_request(struct eggsfs_write_block_request* req) {
    kmem_cache_free(eggsfs_write_block_request_cachep, req);
}

static void eggsfs_init_fetch_block_request_once(void *p) {
    struct eggsfs_fetch_block_request* req = p;
    init_completion(&req->comp);
}

int __init eggsfs_block_init(void) {
    int err;
    eggsfs_fetch_block_request_cachep = kmem_cache_create(
        "eggsfs_fetch_block_request_cache",
        sizeof(struct eggsfs_fetch_block_request),
        0,
        SLAB_RECLAIM_ACCOUNT | SLAB_MEM_SPREAD,
        eggsfs_init_fetch_block_request_once
    );
    if (!eggsfs_fetch_block_request_cachep) { err = -ENOMEM; goto out_err; }

    eggsfs_write_block_request_cachep = kmem_cache_create(
        "eggsfs_write_block_request_cache",
        sizeof(struct eggsfs_write_block_request),
        0,
        SLAB_RECLAIM_ACCOUNT | SLAB_MEM_SPREAD,
        NULL
    );
    if (!eggsfs_write_block_request_cachep) { err = -ENOMEM; goto out_fetch_request; }

    return 0;

out_fetch_request:
    kmem_cache_destroy(eggsfs_fetch_block_request_cachep);
out_err:
    return err;   
}

void __cold eggsfs_block_exit(void) {
    // TODO: handle case where there still are requests in flight.
    kmem_cache_destroy(eggsfs_fetch_block_request_cachep);
    kmem_cache_destroy(eggsfs_write_block_request_cachep);
}