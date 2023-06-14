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
#include "file.h"

#define EGGSFS_BLOCKS_REQ_HEADER_SIZE (4 + 8 + 1) // protocol + block service id + kind

struct eggsfs_block_socket {
    struct socket* sock;
    struct sockaddr_in addr;
    // saved callbacks
    void (*saved_state_change)(struct sock *sk);
    void (*saved_data_ready)(struct sock *sk);
    void (*saved_write_space)(struct sock *sk);
    // only used for block writes
    struct work_struct write_work;
};

static void eggsfs_put_socket(struct eggsfs_block_socket *socket) {
    read_lock(&socket->sock->sk->sk_callback_lock);
    socket->sock->sk->sk_data_ready = socket->saved_data_ready;
    socket->sock->sk->sk_state_change = socket->saved_state_change;
    socket->sock->sk->sk_write_space = socket->saved_write_space;
    read_unlock(&socket->sock->sk->sk_callback_lock);

    sock_release(socket->sock);
}

static u64 WHICH_BLOCK_IP = 0;

static struct kmem_cache* eggsfs_fetch_block_request_cachep;
static struct kmem_cache* eggsfs_write_block_request_cachep;

struct eggsfs_fetch_block_request {
    void (*callback)(void* data, u64 block_id, struct list_head* pages, int err);
    void* data;
    atomic_t called; // whether we've already called the callback or not

    // Completion data.
    atomic_t err;
    u64 block_id;
    struct list_head pages;

    // What we used to call the callback
    atomic_t complete_queued;
    struct work_struct complete_work;

    // In the future, this will be a list
    struct eggsfs_block_socket socket;

    // How many bytes we have left to read.
    u32 bytes_left;
    // The cursor inside the current page.
    u16 page_offset;
    // How much we've read of the header.
    u8 header_read;
    static_assert(EGGSFS_FETCH_BLOCK_RESP_SIZE == 0);
    union {
        char buf[EGGSFS_BLOCKS_RESP_HEADER_SIZE + 2];
        struct {
            __le32 protocol;
            u8 kind;
            __le16 error;
        } __attribute__((packed)) data;
    } header;
};

static void eggsfs_fetch_complete(struct work_struct* work) {
    // Given that we have `complete_queued`, we know this is only called once

    struct eggsfs_fetch_block_request* req = container_of(work, struct eggsfs_fetch_block_request, complete_work);
    eggsfs_debug("block fetch complete block_id=%016llx err=%d", req->block_id, atomic_read(&req->err));
    req->callback(req->data, req->block_id, &req->pages, atomic_read(&req->err));

    // TODO what if the socket is being read right now? There's probably a race with
    // that and the free here.
    eggsfs_put_socket(&req->socket);

    eggsfs_debug("block released socket block_id=%016llx err=%d", req->block_id, atomic_read(&req->err));

    put_pages_list(&req->pages);
    kmem_cache_free(eggsfs_fetch_block_request_cachep, req);
}

static void eggsfs_fetch_block_state_check(struct sock* sk) {
    // TODO check if connection has been closed before its time and error out
    // eggsfs_debug("socket state check triggered: %d", sk->sk_state);
}

static void eggsfs_fetch_block_state_change(struct sock* sk) {
    read_lock_bh(&sk->sk_callback_lock);
    eggsfs_fetch_block_state_check(sk);
    read_unlock_bh(&sk->sk_callback_lock);
}

// Returns a negative result if the socket has to be considered corrupted.
// If a "recoverable" error occurs (e.g. the server just replies with an error)
// `req->err` will be filled in and receiving can continue. If an unrecoverable
// error occurs `req->complete.err` will be filled in also, but it's expected that the
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
        eggsfs_info("bad blocks resp protocol, expected %*pE, got %*pE", 4, &EGGSFS_BLOCKS_RESP_PROTOCOL_VERSION, 4, &req->header.data.protocol);
        return eggsfs_error_to_linux(EGGSFS_ERR_MALFORMED_RESPONSE);
    }

    // Error check
    if (unlikely(req->header.data.kind == 0)) {
        int error_left = (EGGSFS_BLOCKS_RESP_HEADER_SIZE + 2) - (int)req->header_read;
        int read = HEADER_COPY(req->header.buf + req->header_read, error_left);
        error_left -= read;
        if (error_left > 0) { return len0-len; }
        // we got an error "nicely", the socket can carry on living.
        atomic_cmpxchg(&req->err, 0, eggsfs_error_to_linux(le16_to_cpu(req->header.data.error)));
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

        while (avail) {
            u32 this_len = min3(avail, (u32)PAGE_SIZE - req->page_offset, req->bytes_left - block_bytes_read);
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

static int eggsfs_fetch_block_receive(read_descriptor_t* rd_desc, struct sk_buff* skb, unsigned int offset, size_t len) {
    struct eggsfs_block_socket* socket = rd_desc->arg.data;
    struct eggsfs_fetch_block_request* req = container_of(socket, struct eggsfs_fetch_block_request, socket);

    int err = atomic_read(&req->err);
    int consumed = 0;

    eggsfs_debug("block fetch receive enter, block_id=%016llx err=%d", req->block_id, err);
    if (!err) {
        consumed = eggsfs_fetch_block_receive_single_req(req, skb, offset, len);
        if (atomic_read(&req->err) || req->bytes_left == 0) { // we're done or errored
            if (atomic_cmpxchg(&req->complete_queued, 0, 1) == 0) {
                BUG_ON(!queue_work(eggsfs_wq, &req->complete_work));
            }
        }
    }

    eggsfs_debug("block fetch receive exit, block_id=%016llx bytes_left=%u err=%d", req->block_id, req->bytes_left, atomic_read(&req->err));

    // If we have an error, we want to drop what's in the socket anyway.
    // If we do not, we must have consumed everything.
    if (atomic_read(&req->err) == 0) {
        BUG_ON(consumed != len);
    }
    return len;
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

static int eggsfs_get_fetch_block_socket(struct eggsfs_block_service* block_service, struct eggsfs_block_socket* socket) {
    int err = sock_create_kern(&init_net, PF_INET, SOCK_STREAM, IPPROTO_TCP, &socket->sock);
    if (err != 0) { goto out_err; }

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
            if (err == -ERESTARTSYS || err == -ERESTARTNOINTR) {
                eggsfs_debug("could not connect to block service at %pI4:%d: %d", &socket->addr.sin_addr, ntohs(socket->addr.sin_port), err);
            } else {
                eggsfs_warn("could not connect to block service at %pI4:%d: %d", &socket->addr.sin_addr, ntohs(socket->addr.sin_port), err);
            }
        } else {
            break;
        }
    }
    if (err < 0) { goto out_err_sock; }

    // Important for the callbacks to happen after the connect, see TODO above.
    read_lock(&socket->sock->sk->sk_callback_lock);
    socket->sock->sk->sk_user_data = socket;
    socket->sock->sk->sk_data_ready = eggsfs_fetch_block_data_ready;
    socket->sock->sk->sk_state_change = eggsfs_fetch_block_state_change;
    read_unlock(&socket->sock->sk->sk_callback_lock);

    return 0;

out_err_sock:
    sock_release(socket->sock);
out_err:
    return err;
}

int eggsfs_fetch_block(
    void (*callback)(void* data, u64 block_id, struct list_head* pages, int err),
    void* data,
    struct eggsfs_block_service* bs,
    u64 block_id,
    u32 offset,
    u32 count
) {
    int err, i;

    // can't read
    if (unlikely(bs->flags & EGGSFS_BLOCK_SERVICE_DONT_READ)) {
        eggsfs_debug("could not fetch block given block flags %02x", bs->flags);
        return -EIO;
    }

    struct eggsfs_fetch_block_request* req = kmem_cache_alloc(eggsfs_fetch_block_request_cachep, GFP_KERNEL);
    if (!req) { err = -ENOMEM; goto out_err; }

    err = eggsfs_get_fetch_block_socket(bs, &req->socket);
    if (err) { goto out_err; }

    struct eggsfs_block_socket* socket = &req->socket;

    req->callback = callback;
    req->data = data;

    atomic_set(&req->err, 0);
    req->block_id = block_id;
    INIT_LIST_HEAD(&req->pages);
    for (i = 0; i < (count + PAGE_SIZE - 1)/PAGE_SIZE; i++) {
        struct page* page = alloc_page(GFP_KERNEL);
        if (!page) { err = -ENOMEM; goto out_err_pages; }
        list_add_tail(&page->lru, &req->pages);
    }

    INIT_WORK(&req->complete_work, eggsfs_fetch_complete);
    atomic_set(&req->complete_queued, 0);

    req->bytes_left = count;
    req->page_offset = 0;
    req->header_read = 0;

    // fill in req
    char req_msg_buf[EGGSFS_BLOCKS_REQ_HEADER_SIZE + EGGSFS_FETCH_BLOCK_REQ_SIZE];
    char* req_msg = req_msg_buf;
    put_unaligned_le32(EGGSFS_BLOCKS_REQ_PROTOCOL_VERSION, req_msg); req_msg += 4;
    put_unaligned_le64(bs->id, req_msg); req_msg += 8;
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
    eggsfs_debug("sending fetch block req to %pI4:%d, bs=%016llx block_id=%016llx", &socket->addr.sin_addr, ntohs(socket->addr.sin_port), bs->id, block_id);
    struct kvec iov = {
        .iov_base = req_msg_buf,
        .iov_len = sizeof(req_msg_buf),
    };
    err = kernel_sendmsg(socket->sock, &msg, &iov, 1, iov.iov_len);
    if (err < 0) { goto out_err_pages; }

    return 0;

out_err_pages:
    put_pages_list(&req->pages);
    kmem_cache_free(eggsfs_fetch_block_request_cachep, req);
out_err:
    eggsfs_info("couldn't start fetch block request, err=%d", err);
    return err;
}

static void eggsfs_write_block_state_check(struct sock* sk) {
    // TODO check if connection has been closed before its time and error out
    // eggsfs_debug("socket state check triggered: %d", sk->sk_state);
}

static void eggsfs_write_block_state_change(struct sock* sk) {
    read_lock_bh(&sk->sk_callback_lock);
    eggsfs_write_block_state_check(sk);
    read_unlock_bh(&sk->sk_callback_lock);
}

struct eggsfs_write_block_request {
    // Called when request is done
    void (*callback)(void* data, u64 block_id, u64 proof, int err);
    void* data;

    struct list_head pages;

    // What we used to call the callback
    atomic_t complete_queued;
    struct work_struct complete_work;

    // This will be a list soon.
    struct eggsfs_block_socket socket;

    // Req info
    u64 block_service_id;
    u64 block_id;
    u32 crc;
    u32 size;
    u64 certificate;

    atomic_t err;

    u32 bytes_left; // how many bytes left to write (just the block, excluding the request)

    bool done;

    // How much we've written of the write request
    u8 write_req_written;
    // How much we're read of the first response (after we ask to write), and of the
    // second (after we've written the block).
    u8 write_resp_read;
    static_assert(EGGSFS_WRITE_BLOCK_RESP_SIZE > 2);
    union {
        char buf[EGGSFS_BLOCKS_RESP_HEADER_SIZE+EGGSFS_WRITE_BLOCK_RESP_SIZE];
        struct {
            __le32 protocol;
            u8 kind;
            union {
                __le16 error;
                __be64 proof;
            };
        } __attribute__((packed)) data;
    } write_resp;
};

static inline void eggsfs_write_block_trace(struct eggsfs_write_block_request* req, u8 event) {
    trace_eggsfs_block_write(req->block_service_id, req->block_id, event, req->size, req->write_req_written, req->size - req->bytes_left, req->write_resp_read, atomic_read(&req->err));
}

static void eggsfs_write_complete(struct work_struct* work) {
    struct eggsfs_write_block_request* req = container_of(work, struct eggsfs_write_block_request, complete_work);

    eggsfs_write_block_trace(req, EGGSFS_BLOCK_WRITE_DONE);

    req->callback(req->data, req->block_id, be64_to_cpu(req->write_resp.data.proof), atomic_read(&req->err));

    eggsfs_put_socket(&req->socket);

    // TODO what if the socket is being read right now? There's probably a race with
    // that and the free here.

    BUG_ON(!list_empty(&req->pages));
    kmem_cache_free(eggsfs_write_block_request_cachep, req);
}

static inline int atomic_set_return(atomic_t *v, int i) {
    atomic_set(v, i);
    return i;
}

static int eggsfs_write_block_receive_single_req(
    struct eggsfs_write_block_request* req,
    struct sk_buff* skb,
    unsigned int offset, size_t len0
) {
    size_t len = len0;

    eggsfs_write_block_trace(req, EGGSFS_BLOCK_WRITE_RECV_ENTER);

#define BLOCK_WRITE_EXIT(i) do { \
        if (i < 0) { \
            atomic_cmpxchg(&req->err, 0, i); \
        } \
        eggsfs_write_block_trace(req, EGGSFS_BLOCK_WRITE_RECV_EXIT); \
        return i; \
    } while (0)

    // Write resp

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
        eggsfs_info("bad blocks resp protocol, expected %*pE, got %*pE", 4, &EGGSFS_BLOCKS_RESP_PROTOCOL_VERSION, 4, &req->write_resp.data.protocol);
        BLOCK_WRITE_EXIT(eggsfs_error_to_linux(EGGSFS_ERR_MALFORMED_RESPONSE));
    }

    // Error check
    if (unlikely(req->write_resp.data.kind == 0)) {
        int error_left = (EGGSFS_BLOCKS_RESP_HEADER_SIZE + 2) - (int)req->write_resp_read;
        int read = HEADER_COPY(req->write_resp.buf + req->write_resp_read, error_left);
        error_left -= read;
        if (error_left > 0) { BLOCK_WRITE_EXIT(len0-len); }
        eggsfs_info("writing block to bs=%016llx block_id=%016llx failed=%u", req->block_service_id, req->block_id, req->write_resp.data.error);
        // We immediately start writing, so any error here means that the socket is kaput
        int err = eggsfs_error_to_linux(le16_to_cpu(req->write_resp.data.error));
        BLOCK_WRITE_EXIT(err);
    }

    // We can finally get the proof
    {
        int proof_left = (EGGSFS_BLOCKS_RESP_HEADER_SIZE + 8) - (int)req->write_resp_read;
        int read = HEADER_COPY(req->write_resp.buf + req->write_resp_read, proof_left);
        proof_left -= read;
        if (proof_left > 0) {
            BLOCK_WRITE_EXIT(len0-len);
        }
    }

#undef HEADER_COPY

    // we can't get here unless we've fully written the block, the channel is broken
    if (smp_load_acquire(&req->bytes_left)) {
        eggsfs_warn("unexpected written resp before writing out block");
        BLOCK_WRITE_EXIT(-EIO);
    }

    // We're good!
    req->done = true;

    BLOCK_WRITE_EXIT(len0-len);

#undef BLOCK_WRITE_EXIT
}

static int eggsfs_write_block_receive_inner(struct eggsfs_block_socket* socket, struct sk_buff* skb, unsigned int offset, size_t len) {
    struct eggsfs_write_block_request* req = container_of(socket, struct eggsfs_write_block_request, socket);

    int err = atomic_read(&req->err);
    int consumed = 0;

    if (!err) {
        consumed = eggsfs_write_block_receive_single_req(req, skb, offset, len);
        if (atomic_read(&req->err) || req->done) { // we're done or errored
            if (atomic_cmpxchg(&req->complete_queued, 0, 1) == 0) {
                eggsfs_write_block_trace(req, EGGSFS_BLOCK_WRITE_COMPLETE_QUEUED);
                BUG_ON(!queue_work(eggsfs_wq, &req->complete_work));
            }
        }
    }

    // If we have an error, we want to drop what's in the socket anyway.
    // If we do not, we must have consumed everything.
    if (atomic_read(&req->err) == 0) {
        BUG_ON(consumed != len);
    }
    return len;
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
    struct eggsfs_write_block_request* req = container_of(socket, struct eggsfs_write_block_request, socket);
    if (sk_stream_is_writeable(sk)) {
        clear_bit(SOCK_NOSPACE, &sk->sk_socket->flags);
        eggsfs_write_block_trace(req, EGGSFS_BLOCK_WRITE_WRITE_QUEUED);
        queue_work(eggsfs_wq, &socket->write_work);
    }
    read_unlock_bh(&sk->sk_callback_lock);
}

static void eggsfs_write_block_work(struct work_struct* work) {
    struct eggsfs_block_socket* socket = container_of(work, struct eggsfs_block_socket, write_work);
    struct eggsfs_write_block_request* req = container_of(socket, struct eggsfs_write_block_request, socket);

    eggsfs_write_block_trace(req, EGGSFS_BLOCK_WRITE_WRITE_ENTER);

    int err = 0;

#define BLOCK_WRITE_EXIT do { \
        eggsfs_write_block_trace(req, EGGSFS_BLOCK_WRITE_WRITE_EXIT); \
        return; \
    } while (0)


    // Still writing request -- we just serialize the buffer each time and send it out
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
        eggsfs_debug("sending write block req to %pI4:%d", &socket->addr.sin_addr, ntohs(socket->addr.sin_port));
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
        struct page* page = list_first_entry(&req->pages, struct page, lru);
        int sent = kernel_sendpage(
            socket->sock, page,
            page->index,
            min((u32)(PAGE_SIZE - page->index), bytes_left),
            MSG_MORE | MSG_DONTWAIT
        );
        if (sent == -EAGAIN) {
            // done for now, will be rescheduled by `sk_write_space`
            break;
        }
        if (sent < 0) { err = sent; goto out_err; }
        page->index += sent;
        bytes_left -= sent;
        if (page->index >= PAGE_SIZE) {
            BUG_ON(page->index != PAGE_SIZE);
            list_del(&page->lru);
            put_page(page);
            if (bytes_left) { // otherwise this will be null
                list_first_entry(&req->pages, struct page, lru)->index = 0;
            }
        }
    }
    smp_store_release(&req->bytes_left, bytes_left);
    BLOCK_WRITE_EXIT;

out_err:
    eggsfs_debug("block write failed err=%d", err);
    queue_work(eggsfs_wq, &req->complete_work);

    BLOCK_WRITE_EXIT;

#undef BLOCK_WRITE_EXIT
}

static int eggsfs_get_write_block_socket(struct eggsfs_block_service* block_service, struct eggsfs_block_socket* socket) {
    int err = eggsfs_get_fetch_block_socket(block_service, socket);
    if (err) { return err; }

    socket->sock->sk->sk_data_ready = eggsfs_write_block_data_ready;
    socket->sock->sk->sk_state_change = eggsfs_write_block_state_change;
    socket->sock->sk->sk_write_space = eggsfs_write_block_write_space;

    INIT_WORK(&socket->write_work, eggsfs_write_block_work);

    return 0;
}

int eggsfs_write_block(
    void (*callback)(void* data, u64 block_id, u64 proof, int err),
    void* data,
    struct eggsfs_block_service* bs,
    u64 block_id,
    u64 certificate,
    u32 size,
    u32 crc,
    struct list_head* pages
) {
    int err;

    trace_eggsfs_block_write(bs->id, block_id, EGGSFS_BLOCK_WRITE_START, size, 0, 0, 0, 0);

    BUG_ON(list_empty(pages));
    list_first_entry(pages, struct page, lru)->index = 0;

    struct eggsfs_write_block_request* req = kmem_cache_alloc(eggsfs_write_block_request_cachep, GFP_KERNEL);
    if (!req) { err = -ENOMEM; goto out_err; }

    err = eggsfs_get_write_block_socket(bs, &req->socket);
    if (err) {
        kmem_cache_free(eggsfs_write_block_request_cachep, req);
        goto out_err;
    }

    eggsfs_debug("writing to bs=%016llx block_id=%016llx crc=%08x", bs->id, block_id, crc);

    req->callback = callback;
    req->data = data;
    INIT_WORK(&req->complete_work, eggsfs_write_complete);
    atomic_set(&req->complete_queued, 0);

    req->block_service_id = bs->id;
    req->block_id = block_id;
    req->crc = crc;
    req->size = size;
    req->certificate = certificate;
    atomic_set(&req->err, 0);
    req->bytes_left = size;
    req->done = false;
    req->write_req_written = 0;
    req->write_resp_read = 0;
    memset(&req->write_resp.buf, 0, sizeof(req->write_resp.buf));
    list_replace_init(pages, &req->pages);

    queue_work(eggsfs_wq, &req->socket.write_work);

    return 0;

out_err:
    eggsfs_info("couldn't start write block request, err=%d", err);
    trace_eggsfs_block_write(bs->id, block_id, EGGSFS_BLOCK_WRITE_START, size, 0, 0, 0, err);
    return err;
}

int __init eggsfs_block_init(void) {
    int err;
    eggsfs_fetch_block_request_cachep = kmem_cache_create(
        "eggsfs_fetch_block_request_cache",
        sizeof(struct eggsfs_fetch_block_request),
        0,
        SLAB_RECLAIM_ACCOUNT | SLAB_MEM_SPREAD,
        NULL
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