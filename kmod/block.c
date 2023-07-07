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

// Static data
// --------------------------------------------------------------------

#define EGGSFS_BLOCKS_REQ_HEADER_SIZE (4 + 8 + 1) // protocol + block service id + kind
#define EGGSFS_BLOCKS_RESP_HEADER_SIZE (4 + 1) // protocol + kind

#define MSECS_TO_JIFFIES(_ms) ((_ms * HZ) / 1000)

int eggsfs_fetch_block_timeout_jiffies = MSECS_TO_JIFFIES(10 * 1000);
int eggsfs_write_block_timeout_jiffies = MSECS_TO_JIFFIES(10 * 1000);

static u64 WHICH_BLOCK_IP = 0;

static struct kmem_cache* fetch_request_cachep;
static struct kmem_cache* write_request_cachep;

// two 128 elements arrays (we have ~100 disks per block service, so
// with one bucket per element we can hold ~10k disks comfortably)
#define BLOCK_SOCKET_BITS 7
#define BLOCK_SOCKET_BUCKETS (1<<BLOCK_SOCKET_BITS)

static u64 block_socket_key(struct sockaddr_in* addr) {
    return ((u64)addr->sin_addr.s_addr << 16) | addr->sin_port;
}

static DEFINE_HASHTABLE(fetch_sockets, BLOCK_SOCKET_BITS);
static spinlock_t fetch_sockets_locks[BLOCK_SOCKET_BUCKETS]; // to sync writes to hashmap
static wait_queue_head_t fetch_sockets_wqs[BLOCK_SOCKET_BUCKETS]; // TODO init
static atomic_t fetch_sockets_len[BLOCK_SOCKET_BUCKETS]; // TODO init

#if 0
static DEFINE_HASHTABLE(write_sockets, BLOCK_SOCKET_BITS);
static spinlock_t write_sockets_locks[BLOCK_SOCKET_BUCKETS];
#endif

static void timeout_sockets(struct work_struct* w);
static DECLARE_DELAYED_WORK(timeout_work, timeout_sockets);

// Fetch
// --------------------------------------------------------------------

struct fetch_socket {
    struct socket* sock;
    struct sockaddr_in addr;
    // If non-zero, we'll check that not more than `eggsfs_fetch_block_timeout_jiffies`
    // has passed since this.
    // For this reason, we set this timeout when:
    // * We read anything
    // * We write anything
    // * We add an element to the write/read list
    // And we only remove it when both lists become are empty.
    u64 timeout_start;
    atomic_t err;
    // To store in the hashmap.
    struct hlist_node hnode;
    // Write end. write_work does all the work by peeking at the head, new requests
    // get added to the tail. We write in a work queue function both because
    // kernel_sendmsg probably doesn't work so well in bottom halves, and also because
    // we want to immediatly try to start writing after scheduling a request.
    struct list_head write;
    spinlock_t write_lock;
    // This does both write work and also cleaning up timed out / errored sockets.
    struct work_struct work;
    // Read end. "data available" callback does all the work.
    struct list_head read;
    spinlock_t read_lock;
    // Saved callbacks
    void (*saved_state_change)(struct sock *sk);
    void (*saved_data_ready)(struct sock *sk);
    void (*saved_write_space)(struct sock *sk);
};

struct fetch_request {
    void (*callback)(void* data, u64 block_id, struct list_head* pages, int err);
    void* data;

    // Completion data.
    atomic_t err;
    struct list_head pages;

    // What we use to call back into the callback from BH
    struct work_struct complete_work;

    // Either in the write or in the read end of the socket.
    struct list_head list;

    // Req info
    u64 block_service_id;
    u64 block_id;
    u32 offset;
    u32 count;

    // How much we've written of the request
    u32 written_bytes;

    // How many bytes we have left to read.
    u32 read_bytes_left;
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

static void fetch_data_ready(struct sock* sk);
static void fetch_state_change(struct sock* sk);
static void fetch_write_space(struct sock* sk);
static void fetch_work(struct work_struct* work);

#if 0
static void fetch_block_socket_trace(struct fetch_socket* st, u8 event) {
    u32 ip = 
    trace_eggsfs_fetch_block_socket(
        st->enode->inode.i_ino,
        st->span->span.start,
        st->stripe,
        st->span->parity,
        st->prefetching,
        event,
        block,
        err
    );
}
#endif

// Gets the socket, and acquires a reference to it.
static struct fetch_socket* get_fetch_block_socket(struct sockaddr_in* addr) {
    u64 key = block_socket_key(addr);

    rcu_read_lock();
    struct fetch_socket* sock;
    hash_for_each_possible_rcu(fetch_sockets, sock, hnode, key) {
        if (block_socket_key(&sock->addr) == key) {
            rcu_read_unlock();
            return sock;
        }
    }

    eggsfs_debug("socket to %pI4:%d not found, will create", &addr->sin_addr, ntohs(addr->sin_port));

    sock = kmalloc(sizeof(struct fetch_socket), GFP_KERNEL);
    if (sock == NULL) { return ERR_PTR(-ENOMEM); }

    memcpy(&sock->addr, addr, sizeof(struct sockaddr_in));

    sock->timeout_start = 0;
    atomic_set(&sock->err, 0);

    int err = sock_create_kern(&init_net, PF_INET, SOCK_STREAM, IPPROTO_TCP, &sock->sock);
    if (err != 0) { goto out_err; }

    // TFO would be nice, but it doesn't really matter given that we aggressively recycle
    // connections, and also given that we have our own callbacks which do not play well
    // at connection opening stage. So open the connection
    err = kernel_connect(sock->sock, (struct sockaddr*)&sock->addr, sizeof(sock->addr), 0);
    if (err < 0) {
        if (err == -ERESTARTSYS || err == -ERESTARTNOINTR) {
            eggsfs_debug("could not connect to block service at %pI4:%d: %d", &sock->addr.sin_addr, ntohs(sock->addr.sin_port), err);
        } else {
            eggsfs_warn("could not connect to block service at %pI4:%d: %d", &sock->addr.sin_addr, ntohs(sock->addr.sin_port), err);
        }
        goto out_err;
    }

    INIT_LIST_HEAD(&sock->write);
    spin_lock_init(&sock->write_lock);
    INIT_WORK(&sock->work, fetch_work);
    INIT_LIST_HEAD(&sock->read);
    spin_lock_init(&sock->read_lock);

    // now insert
    int bucket = hash_min(key, HASH_BITS(fetch_sockets));
    struct fetch_socket* other_sock;

    spin_lock(&fetch_sockets_locks[bucket]);
    // first check if somebody else got there first
    hash_for_each_possible_rcu(fetch_sockets, other_sock, hnode, key) { // first we check if somebody didn't get to it first
        if (block_socket_key(&other_sock->addr) == key) {
            // somebody got here before us
            spin_unlock(&fetch_sockets_locks[bucket]);
            sock_release(sock->sock);
            kfree(sock);
            return other_sock;
        }
    }
    // now actually insert. before exiting the critical section,
    // re-acquire the RCU lock, so that we won't get it removed
    // now because of timeouts or such.
    hash_add_rcu(fetch_sockets, &sock->hnode, key);
    rcu_read_lock();
    spin_unlock(&fetch_sockets_locks[bucket]);

    // Important for the callbacks to happen after the connect, see comment about
    // fastopen above. Also, we want the callbacks to set last, after we've setup
    // all the state, lest they run before the state is indeed setup.
    write_lock(&sock->sock->sk->sk_callback_lock);
    sock->saved_data_ready = sock->sock->sk->sk_data_ready;
    sock->saved_state_change = sock->sock->sk->sk_state_change;
    sock->saved_write_space = sock->sock->sk->sk_write_space;
    sock->sock->sk->sk_user_data = sock;
    sock->sock->sk->sk_data_ready = fetch_data_ready;
    sock->sock->sk->sk_state_change = fetch_state_change;
    sock->sock->sk->sk_write_space = fetch_write_space;
    write_unlock(&sock->sock->sk->sk_callback_lock);

    atomic_inc(&fetch_sockets_len[bucket]);

    return sock;

out_err:
    kfree(sock);
    return ERR_PTR(err);
}

static void fetch_complete(struct fetch_request* req) {
    eggsfs_debug("block fetch complete block_id=%016llx err=%d", req->block_id, atomic_read(&req->err));
    req->callback(req->data, req->block_id, &req->pages, atomic_read(&req->err));

    eggsfs_debug("block released socket block_id=%016llx err=%d", req->block_id, atomic_read(&req->err));

    put_pages_list(&req->pages);
    kmem_cache_free(fetch_request_cachep, req);
}

static void fetch_complete_work(struct work_struct* work) {
    fetch_complete(container_of(work, struct fetch_request, complete_work));
}

static void fetch_request_constructor(void* ptr) {
    struct fetch_request* req = (struct fetch_request*)ptr;

    INIT_LIST_HEAD(&req->pages);
    INIT_WORK(&req->complete_work, fetch_complete_work);
}

int eggsfs_drop_fetch_block_sockets(void) {
    int dropped = 0;
    int bucket;
    struct fetch_socket* sock;

    rcu_read_lock();
    hash_for_each_rcu(fetch_sockets, bucket, sock, hnode) {
        dropped++;
        // We put EAGAIN here so that the caller can restart the
        // request if it wants to (since we didn't really encounter
        // any error). We don't currently do this though.
        atomic_cmpxchg(&sock->err, 0, -EAGAIN);
        queue_work(eggsfs_wq, &sock->work);
    }
    rcu_read_unlock();

    eggsfs_info("dropped %d sockets", dropped);

    return dropped;
}

// Can only be called from fetch_block_work! Otherwise it'll race with it.
static void remove_fetch_socket(struct fetch_socket* socket) {
    int err = atomic_read(&socket->err);
    BUG_ON(!err);

    eggsfs_debug("winding down socket to %pI4:%d due to %d", &socket->addr.sin_addr, ntohs(socket->addr.sin_port), err);

    // First, remove socket from hashmap. After we're done with this,
    // we know nobody's going to add stuff to the write queue.
    u64 key = block_socket_key(&socket->addr);
    int bucket = hash_min(key, HASH_BITS(fetch_sockets));
    spin_lock(&fetch_sockets_locks[bucket]);
    hash_del_rcu(&socket->hnode);
    spin_unlock(&fetch_sockets_locks[bucket]);
    synchronize_rcu();

    // Then, change back the callbacks: this will also ensure that no
    // callback is currently running, i.e. that nobody's using the
    // socket.
    write_lock_bh(&socket->sock->sk->sk_callback_lock);
    socket->sock->sk->sk_data_ready = socket->saved_data_ready;
    socket->sock->sk->sk_state_change = socket->saved_state_change;
    socket->sock->sk->sk_write_space = socket->saved_write_space;
    write_unlock_bh(&socket->sock->sk->sk_callback_lock);
    
    // Now complete all the remaining requests with the error.
    // Note that we're the only one remaining, but we still
    // take locks for hygiene.

    struct fetch_request* req;
    struct fetch_request* tmp;

    struct list_head all_reqs;
    INIT_LIST_HEAD(&all_reqs);

    spin_lock_bh(&socket->write_lock);
    list_for_each_entry_safe(req, tmp, &socket->write, list) {
        list_del(&req->list);
        list_add(&req->list, &all_reqs);
    }
    spin_unlock_bh(&socket->write_lock);

    spin_lock_bh(&socket->read_lock);
    list_for_each_entry_safe(req, tmp, &socket->read, list) {
        list_del(&req->list);
        list_add(&req->list, &all_reqs);
    }
    spin_unlock_bh(&socket->read_lock);

    list_for_each_entry_safe(req, tmp, &all_reqs, list) {
        atomic_cmpxchg(&req->err, 0, err);
        fetch_complete(req); // no need to go through wq, we're not in BH
    }

    // Finally, kill the socket.
    sock_release(socket->sock);
    kfree(socket);

    // Adjust len, notify waiters
    smp_mb__before_atomic();
    atomic_dec(&fetch_sockets_len[bucket]);
    wake_up_all(&fetch_sockets_wqs[bucket]);
}

// This has three purposes:
//
// * Writing out fetch requests;
// * If the socket is to be decommissioned, fail all the requests.
void fetch_work(struct work_struct* work) {
    struct fetch_socket* socket = container_of(work, struct fetch_socket, work);

    eggsfs_debug("%pI4:%d", &socket->addr.sin_addr, ntohs(socket->addr.sin_port));

    // if we've failed, then we need to clean up the mess
    int err = atomic_read(&socket->err);
    if (err) {
        remove_fetch_socket(socket);
        return;
    }

    // Otherwise we need to write out requests.

    for (;;) {
        // Get request, if any
        struct fetch_request* req;
        spin_lock_bh(&socket->write_lock);
        req = list_first_entry_or_null(&socket->write, struct fetch_request, list);
        spin_unlock_bh(&socket->write_lock);

        char req_msg_buf[EGGSFS_BLOCKS_REQ_HEADER_SIZE + EGGSFS_FETCH_BLOCK_REQ_SIZE];

        // Check if we've got nothing to do
        if (req == NULL) { return; }

        // Can't be done already, we just got the req
        BUG_ON(req->written_bytes == sizeof(req_msg_buf));

        // we're not done, fill in the req
        char* req_msg = req_msg_buf;
        put_unaligned_le32(EGGSFS_BLOCKS_REQ_PROTOCOL_VERSION, req_msg); req_msg += 4;
        put_unaligned_le64(req->block_service_id, req_msg); req_msg += 8;
        *(u8*)req_msg = EGGSFS_BLOCKS_FETCH_BLOCK; req_msg += 1;
        {
            struct eggsfs_bincode_put_ctx ctx = {
                .start = req_msg,
                .cursor = req_msg,
                .end = req_msg_buf + sizeof(req_msg_buf),
            };
            eggsfs_fetch_block_req_put_start(&ctx, start);
            eggsfs_fetch_block_req_put_block_id(&ctx, start, req_block_id, req->block_id);
            eggsfs_fetch_block_req_put_offset(&ctx, req_block_id, req_offset, req->offset);
            eggsfs_fetch_block_req_put_count(&ctx, req_offset, req_count, req->count);
            eggsfs_fetch_block_req_put_end(ctx, req_count, end);
            BUG_ON(ctx.cursor != ctx.end);
        }

        // send message
        struct msghdr msg = { .msg_flags = MSG_DONTWAIT };
        eggsfs_debug("sending fetch block req to %pI4:%d, bs=%016llx block_id=%016llx", &socket->addr.sin_addr, ntohs(socket->addr.sin_port), req->block_service_id, req->block_id);
        struct kvec iov = {
            .iov_base = req_msg_buf + req->written_bytes,
            .iov_len = sizeof(req_msg_buf) - req->written_bytes,
        };
        int sent = kernel_sendmsg(socket->sock, &msg, &iov, 1, iov.iov_len);
        if (sent == -EAGAIN) { return; } // done for now, will be rescheduled by `sk_write_space`
        if (sent < 0) { 
            atomic_cmpxchg(&socket->err, 0, sent);
            remove_fetch_socket(socket); // bail
            return;
        }
        req->written_bytes += sent;
        if (sent > 0) {
            socket->timeout_start = get_jiffies_64();
        }

        if (req->written_bytes == sizeof(req_msg_buf)) {
            // we're done with this, move it to read list and keep going
            spin_lock_bh(&socket->write_lock);
            list_del(&req->list);
            spin_unlock_bh(&socket->write_lock);
            spin_lock_bh(&socket->read_lock);
            list_add_tail(&req->list, &socket->read);
            socket->timeout_start = get_jiffies_64();
            spin_unlock_bh(&socket->read_lock);
        } else {
            // we didn't manage to write out, stop
            break;
        }
    }
}

static void fetch_block_state_check(struct sock* sk) {
    struct fetch_socket* socket = sk->sk_user_data;

    if (sk->sk_state == TCP_ESTABLISHED) { return; } // the only good one

    // Right now we connect beforehand, so every change is trouble. Just
    // kill the socket.
    eggsfs_debug("socket state check triggered: %d, will fail reqs", sk->sk_state);
    atomic_cmpxchg(&socket->err, 0, -ECONNABORTED); // TODO we could have nicer errors
    queue_work(eggsfs_wq, &socket->work);
}

void fetch_state_change(struct sock* sk) {
    read_lock_bh(&sk->sk_callback_lock);
    fetch_block_state_check(sk);
    read_unlock_bh(&sk->sk_callback_lock);
}

void fetch_write_space(struct sock* sk) {
    read_lock_bh(&sk->sk_callback_lock);
    struct fetch_socket* socket = (struct fetch_socket*)sk->sk_user_data;
    if (sk_stream_is_writeable(sk)) {
        clear_bit(SOCK_NOSPACE, &sk->sk_socket->flags);
        queue_work(eggsfs_wq, &socket->work);
    }
    read_unlock_bh(&sk->sk_callback_lock);
}

// Returns a negative result if the socket has to be considered corrupted.
// If a "recoverable" error occurs (e.g. the server just replies with an error)
// `req->err` will be filled in and receiving can continue. If an unrecoverable
// error occurs `req->complete.err` will be filled in also, but it's expected that the
// caller stops operating on this socket.
static int fetch_receive_single_req(
    struct fetch_request* req,
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

    BUG_ON(req->read_bytes_left == 0);

    struct page* page = list_first_entry(&req->pages, struct page, lru);
    BUG_ON(!page);
    char* page_ptr = kmap_atomic(page);

    struct skb_seq_state seq;
    skb_prepare_seq_read(skb, offset, offset + min(req->read_bytes_left, (u32)len), &seq);

    u32 block_bytes_read = 0;
    while (block_bytes_read < req->read_bytes_left) {
        const u8* data;
        u32 avail = skb_seq_read(block_bytes_read, &data, &seq);
        if (avail == 0) { break; }

        while (avail) {
            u32 this_len = min3(avail, (u32)PAGE_SIZE - req->page_offset, req->read_bytes_left - block_bytes_read);
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

    req->read_bytes_left -= block_bytes_read;

    // The last page is partially filled, zero it and return to first page
    if (!req->read_bytes_left && req->page_offset) {
        memset(page_ptr + req->page_offset, 0, PAGE_SIZE - req->page_offset);
        list_rotate_left(&req->pages);
        req->page_offset = 0;
    }

    kunmap_atomic(page_ptr);

    return (len0-len) + block_bytes_read;
}

static int fetch_receive(read_descriptor_t* rd_desc, struct sk_buff* skb, unsigned int offset, size_t len) {
    struct fetch_socket* socket = rd_desc->arg.data;

    eggsfs_debug("%pI4:%d offset=%u len=%lu", &socket->addr.sin_addr, ntohs(socket->addr.sin_port), offset, len);

    size_t len0 = len;

    if (atomic_read(&socket->err)) {
        // Socket is scuppered, exit immediately.
        // Note that whoever sets the error first must also schedule the work to take care of
        // it.
        return len0;
    }

    socket->timeout_start = get_jiffies_64();

    // eggsfs_info("enter sock=%p", socket);

    // line up first req
    struct fetch_request* req;
    spin_lock_bh(&socket->read_lock);
    req = list_first_entry_or_null(&socket->read, struct fetch_request, list);
    spin_unlock_bh(&socket->read_lock);

    for (;;) {
        if (req == NULL) { break; }
        int consumed = fetch_receive_single_req(req, skb, offset, len);
        if (consumed < 0) {
            // socket is scuppered, let the work handle this
            atomic_cmpxchg(&socket->err, 0, consumed);
            queue_work(eggsfs_wq, &socket->work);
            return len0;
        }
        // eggsfs_info("sock=%p req=%p consumed=%d len=%llu offset=%u err=%d", socket, req, consumed, len, offset, req->err);
        len -= consumed;
        offset += consumed;
        if (req->read_bytes_left == 0 || atomic_read(&req->err)) {
            // this request is done
            spin_lock_bh(&socket->read_lock);
            list_del(&req->list);
            queue_work(eggsfs_wq, &req->complete_work);
            req = list_first_entry_or_null(&socket->read, struct fetch_request, list);
            if (req == NULL) {
                // Non-blockingly reset timeout if we're the last ones here
                // (note that not resetting it in this case it's fine, the other
                // critical section will update it anyway, and it wouldn't matter
                // either way)
                if (spin_trylock_bh(&socket->write_lock)) {
                    socket->timeout_start = 0;
                    spin_unlock_bh(&socket->write_lock);
                }
            }
            spin_unlock_bh(&socket->read_lock);
        } else {
            break; // we're not done yet
        }
    }

    // Intuitively we'd think that we always consume everything, but there's
    // a time between sending the message and adding the req to the read list
    // where we might have leftovers.
    return len0 - len;
}

void fetch_data_ready(struct sock* sk) {
    read_lock_bh(&sk->sk_callback_lock);
    read_descriptor_t rd_desc;
    // Taken from iscsi -- we set count to 1 because we want the network layer to
    // hand us all the skbs that are available.
    rd_desc.arg.data = sk->sk_user_data;
    rd_desc.count = 1;
    tcp_read_sock(sk, &rd_desc, fetch_receive);
    fetch_block_state_check(sk);
    read_unlock_bh(&sk->sk_callback_lock);
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

    struct fetch_request* req = kmem_cache_alloc(fetch_request_cachep, GFP_KERNEL);
    if (!req) { err = -ENOMEM; goto out_err; }

    req->callback = callback;
    req->data = data;
    atomic_set(&req->err, 0);

    BUG_ON(!list_empty(&req->pages));
    for (i = 0; i < (count + PAGE_SIZE - 1)/PAGE_SIZE; i++) {
        struct page* page = alloc_page(GFP_KERNEL);
        if (!page) { err = -ENOMEM; goto out_err_pages; }
        list_add_tail(&page->lru, &req->pages);
    }

    req->block_service_id = bs->id;
    req->block_id = block_id;
    req->offset = offset;
    req->count = count;

    req->written_bytes = 0;

    req->read_bytes_left = count;
    req->page_offset = 0;
    req->header_read = 0;

    // As the very last thing, try to get the socket (will get RCU lock).
    // Try both ips (if we have them) before giving up
    int block_ip = WHICH_BLOCK_IP++;
    struct sockaddr_in addr;
    struct fetch_socket* sock;
    for (i = 0; i < 1 + (bs->port2 != 0); i++, block_ip++) { // we might not have a second address
        addr.sin_family = AF_INET;
        if (bs->port2 == 0 || block_ip&1) {
            addr.sin_addr.s_addr = htonl(bs->ip1);
            addr.sin_port = htons(bs->port1);
        } else {
            addr.sin_addr.s_addr = htonl(bs->ip2);
            addr.sin_port = htons(bs->port2);
        }

        sock = get_fetch_block_socket(&addr);
        if (!IS_ERR(sock)) {
            goto sock_found;
        } else {
            err = PTR_ERR(sock);
        }
    }
    if (err < 0) { goto out_err_pages; }
sock_found:
    err = 0;

    // We have a socket, and we also have the RCU lock. We need to hurry
    // and place the request in the queue, and schedule work. Everything
    // in the RCU section, since otherwise the socket might be cleared
    // under our feet.
    spin_lock_bh(&sock->write_lock);
    list_add_tail(&req->list, &sock->write);
    sock->timeout_start = get_jiffies_64();
    spin_unlock_bh(&sock->write_lock);

    queue_work(eggsfs_wq, &sock->work);

    // we're done
    rcu_read_unlock();

    return 0;

out_err_pages:
    put_pages_list(&req->pages);
    kmem_cache_free(fetch_request_cachep, req);
out_err:
    eggsfs_info("couldn't start fetch block request, err=%d", err);
    return err;
}

// Write
// --------------------------------------------------------------------

struct write_socket {
    struct socket* sock;
    struct sockaddr_in addr;
    // for the hashmap
    struct hlist_node node;
    // block writes
    struct work_struct write_work;
    // saved callbacks
    void (*saved_state_change)(struct sock *sk);
    void (*saved_data_ready)(struct sock *sk);
    void (*saved_write_space)(struct sock *sk);
};

static void put_write_socket(struct write_socket *socket) {
    read_lock(&socket->sock->sk->sk_callback_lock);
    socket->sock->sk->sk_data_ready = socket->saved_data_ready;
    socket->sock->sk->sk_state_change = socket->saved_state_change;
    socket->sock->sk->sk_write_space = socket->saved_write_space;
    read_unlock(&socket->sock->sk->sk_callback_lock);

    sock_release(socket->sock);
}

struct write_request {
    // Called when request is done
    void (*callback)(void* data, struct list_head* pages, u64 block_id, u64 proof, int err);
    void* data;

    struct list_head pages;

    // What we used to call the callback
    atomic_t complete_queued;
    struct work_struct complete_work;

    // This will be a list soon.
    struct write_socket socket;

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

static inline void write_block_trace(struct write_request* req, u8 event) {
    trace_eggsfs_block_write(req->block_service_id, req->block_id, event, req->size, req->write_req_written, req->size - req->bytes_left, req->write_resp_read, atomic_read(&req->err));
}

static void write_complete(struct work_struct* work) {
    struct write_request* req = container_of(work, struct write_request, complete_work);

    write_block_trace(req, EGGSFS_BLOCK_WRITE_DONE);

    // might be that we didn't consume all the pages -- in which case we need
    // to keep rotating until we're there.
    while (list_first_entry(&req->pages, struct page, lru)->private == 0) {
        list_rotate_left(&req->pages);
    }
    req->callback(req->data, &req->pages, req->block_id, be64_to_cpu(req->write_resp.data.proof), atomic_read(&req->err));
    BUG_ON(!list_empty(&req->pages)); // the callback must acquire this

    put_write_socket(&req->socket);

    // TODO what if the socket is being read right now? There's probably a race with
    // that and the free here.

    BUG_ON(!list_empty(&req->pages));
    kmem_cache_free(write_request_cachep, req);
}

static void write_finalize(struct write_request* req) {
    if (atomic_read(&req->err) || req->bytes_left == 0) { // we're done or errored
        if (atomic_cmpxchg(&req->complete_queued, 0, 1) == 0) {
            BUG_ON(!queue_work(eggsfs_wq, &req->complete_work));
        }
    }
}

static void write_block_state_check(struct sock* sk) {
    struct write_socket* socket = sk->sk_user_data;
    struct write_request* req = container_of(socket, struct write_request, socket);

    // Right now we connect beforehand, so every change = failure
    eggsfs_debug("socket state check triggered: %d", sk->sk_state);

    if (sk->sk_state == TCP_ESTABLISHED) { return; } // the only good one

    atomic_cmpxchg(&req->err, 0, -EIO);
    write_finalize(req);
}

static void write_block_state_change(struct sock* sk) {
    read_lock_bh(&sk->sk_callback_lock);
    write_block_state_check(sk);
    read_unlock_bh(&sk->sk_callback_lock);
}

static inline int atomic_set_return(atomic_t *v, int i) {
    atomic_set(v, i);
    return i;
}

static int write_block_receive_single_req(
    struct write_request* req,
    struct sk_buff* skb,
    unsigned int offset, size_t len0
) {
    size_t len = len0;

    write_block_trace(req, EGGSFS_BLOCK_WRITE_RECV_ENTER);

#define BLOCK_WRITE_EXIT(i) do { \
        if (i < 0) { \
            atomic_cmpxchg(&req->err, 0, i); \
        } \
        write_block_trace(req, EGGSFS_BLOCK_WRITE_RECV_EXIT); \
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

static int write_block_receive_inner(struct write_socket* socket, struct sk_buff* skb, unsigned int offset, size_t len) {
    struct write_request* req = container_of(socket, struct write_request, socket);

    int err = atomic_read(&req->err);
    int consumed = 0;

    if (!err) {
        consumed = write_block_receive_single_req(req, skb, offset, len);
        if (atomic_read(&req->err) || req->done) { // we're done or errored
            if (atomic_cmpxchg(&req->complete_queued, 0, 1) == 0) {
                write_block_trace(req, EGGSFS_BLOCK_WRITE_COMPLETE_QUEUED);
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

static int write_block_receive(read_descriptor_t* rd_desc, struct sk_buff* skb, unsigned int offset, size_t len) {
    struct write_socket* socket = rd_desc->arg.data;
    return write_block_receive_inner(socket, skb, offset, len);
}

static void write_block_data_ready(struct sock* sk) {
    read_lock_bh(&sk->sk_callback_lock);
    read_descriptor_t rd_desc;
    rd_desc.arg.data = sk->sk_user_data;
    rd_desc.count = 1;
    tcp_read_sock(sk, &rd_desc, write_block_receive);
    write_block_state_check(sk);
    read_unlock_bh(&sk->sk_callback_lock);
}

static void write_block_write_space(struct sock* sk) {
    read_lock_bh(&sk->sk_callback_lock);
    struct write_socket* socket = (struct write_socket*)sk->sk_user_data;
    struct write_request* req = container_of(socket, struct write_request, socket);
    if (sk_stream_is_writeable(sk)) {
        clear_bit(SOCK_NOSPACE, &sk->sk_socket->flags);
        write_block_trace(req, EGGSFS_BLOCK_WRITE_WRITE_QUEUED);
        queue_work(eggsfs_wq, &socket->write_work);
    }
    read_unlock_bh(&sk->sk_callback_lock);
}

static void write_block_work(struct work_struct* work) {
    struct write_socket* socket = container_of(work, struct write_socket, write_work);
    struct write_request* req = container_of(socket, struct write_request, socket);

    write_block_trace(req, EGGSFS_BLOCK_WRITE_WRITE_ENTER);

    int err = 0;

#define BLOCK_WRITE_EXIT do { \
        write_block_trace(req, EGGSFS_BLOCK_WRITE_WRITE_EXIT); \
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
            list_rotate_left(&req->pages);
            if (bytes_left) { // otherwise this will be null
                list_first_entry(&req->pages, struct page, lru)->index = 0;
                list_first_entry(&req->pages, struct page, lru)->private = 0;
            }
        }
    }
    smp_store_release(&req->bytes_left, bytes_left);
    BLOCK_WRITE_EXIT;

out_err:
    eggsfs_debug("block write failed err=%d", err);
    BUG_ON(err == 0);
    atomic_cmpxchg(&req->err, 0, err);
    queue_work(eggsfs_wq, &req->complete_work);

    BLOCK_WRITE_EXIT;

#undef BLOCK_WRITE_EXIT
}

static int get_write_block_socket(struct eggsfs_block_service* block_service, struct write_socket* socket) {
    int err = sock_create_kern(&init_net, PF_INET, SOCK_STREAM, IPPROTO_TCP, &socket->sock);
    if (err != 0) { goto out_err; }

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
    write_lock(&socket->sock->sk->sk_callback_lock);
    socket->saved_data_ready = socket->sock->sk->sk_data_ready;
    socket->saved_state_change = socket->sock->sk->sk_state_change;
    socket->saved_write_space = socket->sock->sk->sk_write_space;
    socket->sock->sk->sk_user_data = socket;
    socket->sock->sk->sk_data_ready = write_block_data_ready;
    socket->sock->sk->sk_state_change = write_block_state_change;
    socket->sock->sk->sk_write_space = write_block_write_space;
    write_unlock(&socket->sock->sk->sk_callback_lock);

    INIT_WORK(&socket->write_work, write_block_work);

    return 0;

out_err_sock:
    sock_release(socket->sock);
out_err:
    return err;
}

int eggsfs_write_block(
    void (*callback)(void* data, struct list_head* pages, u64 block_id, u64 proof, int err),
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

    struct write_request* req = kmem_cache_alloc(write_request_cachep, GFP_KERNEL);
    if (!req) { err = -ENOMEM; goto out_err; }

    err = get_write_block_socket(bs, &req->socket);
    if (err) {
        kmem_cache_free(write_request_cachep, req);
        goto out_err;
    }

    eggsfs_debug("writing to bs=%016llx block_id=%016llx crc=%08x", bs->id, block_id, crc);

    req->callback = callback;
    req->data = data;
    INIT_WORK(&req->complete_work, write_complete);
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
    list_first_entry(&req->pages, struct page, lru)->index = 0;
    list_first_entry(&req->pages, struct page, lru)->private = 1; // we use this to mark the first page

    queue_work(eggsfs_wq, &req->socket.write_work);

    return 0;

out_err:
    eggsfs_info("couldn't start write block request, err=%d", err);
    trace_eggsfs_block_write(bs->id, block_id, EGGSFS_BLOCK_WRITE_START, size, 0, 0, 0, err);
    return err;
}
// Timeout
// --------------------------------------------------------------------

// Periodically checks all sockets and schedules the timed out ones for deletion
void timeout_sockets(struct work_struct* w) {
    int bucket;
    struct fetch_socket* sock;
    u64 now = get_jiffies_64();

    rcu_read_lock();
    hash_for_each_rcu(fetch_sockets, bucket, sock, hnode) {
        if (sock->timeout_start == 0) { continue; }
        // this loop is relatively long, this could happen
        if (unlikely(sock->timeout_start > now)) {
            eggsfs_info("timeout start apparently in the future, skipping (%llums > %llums)", jiffies64_to_msecs(sock->timeout_start), now);
            continue;
        }
        u64 dt = now - sock->timeout_start;
        if (dt > eggsfs_fetch_block_timeout_jiffies) {
            eggsfs_info("timing out socket to %pI4:%d (%llums > %llums)", &sock->addr.sin_addr, ntohs(sock->addr.sin_port), jiffies64_to_msecs(dt), jiffies64_to_msecs(eggsfs_fetch_block_timeout_jiffies));
            atomic_cmpxchg(&sock->err, 0, -ETIMEDOUT);
            queue_work(eggsfs_wq, &sock->work);
        }
    }
    rcu_read_unlock();

    queue_delayed_work(eggsfs_wq, &timeout_work, eggsfs_fetch_block_timeout_jiffies);
}

// init/exit
// --------------------------------------------------------------------

int __init eggsfs_block_init(void) {
    int err, i;
    fetch_request_cachep = kmem_cache_create(
        "eggsfs_fetch_block_request_cache",
        sizeof(struct fetch_request),
        0,
        SLAB_RECLAIM_ACCOUNT | SLAB_MEM_SPREAD,
        fetch_request_constructor
    );
    if (!fetch_request_cachep) { err = -ENOMEM; goto out_err; }

    write_request_cachep = kmem_cache_create(
        "eggsfs_write_block_request_cache",
        sizeof(struct write_request),
        0,
        SLAB_RECLAIM_ACCOUNT | SLAB_MEM_SPREAD,
        NULL
    );
    if (!write_request_cachep) { err = -ENOMEM; goto out_fetch_request; }

    queue_work(eggsfs_wq, &timeout_work.work);
    for (i = 0; i < BLOCK_SOCKET_BUCKETS; i++) {
        atomic_set(&fetch_sockets_len[i], 0);
        init_waitqueue_head(&fetch_sockets_wqs[i]);
    }

    return 0;

out_fetch_request:
    kmem_cache_destroy(fetch_request_cachep);
out_err:
    return err;   
}

void __cold eggsfs_block_exit(void) {
    eggsfs_debug("block exit");

    // stop timing out sockets
    cancel_delayed_work_sync(&timeout_work);

    // If we're here, it means that all requests
    // must have finished. However we might still
    // be getting data down the sockets. So we let
    // the normal cleanup procedure run its course
    // by erroring out each socket and waiting
    // for all sockets to be released.

    // error out all the sockets
    struct fetch_socket* sock;
    int bucket;
    rcu_read_lock();
    hash_for_each_rcu(fetch_sockets, bucket, sock, hnode) {
        atomic_cmpxchg(&sock->err, 0, -ECONNABORTED);
        queue_work(eggsfs_wq, &sock->work);
    }
    rcu_read_unlock();

    // wait for all of them to be freed by work
    for (bucket = 0; bucket < BLOCK_SOCKET_BUCKETS; bucket++) {
        wait_event(fetch_sockets_wqs[bucket], atomic_read(&fetch_sockets_len[bucket]) == 0);
    }

    // clear caches
    kmem_cache_destroy(fetch_request_cachep);
    kmem_cache_destroy(write_request_cachep);
}