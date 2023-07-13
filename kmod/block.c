#include <linux/inet.h>
#include <net/tcp.h>
#include <net/sock.h>
#include <linux/workqueue.h>

#include "block.h"
#include "log.h"
#include "err.h"
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

// two 256 elements arrays (we have ~100 disks per block service, so
// with one bucket per element we can hold ~25k disks comfortably)
#define BLOCK_SOCKET_BITS 8
#define BLOCK_SOCKET_BUCKETS (1<<BLOCK_SOCKET_BITS)

static u64 block_socket_key(struct sockaddr_in* addr) {
    return ((u64)addr->sin_addr.s_addr << 16) | addr->sin_port;
}

struct block_socket;
struct block_request;

struct block_ops {
    DECLARE_HASHTABLE(sockets, BLOCK_SOCKET_BITS);
    spinlock_t locks[BLOCK_SOCKET_BUCKETS];
    // these two are just to detect when a socket is gone
    wait_queue_head_t wqs[BLOCK_SOCKET_BUCKETS];
    atomic_t len[BLOCK_SOCKET_BUCKETS];

    int* timeout_jiffies;

    // functions
    void (*data_ready)(struct sock *sk);
    void (*work)(struct work_struct* work);
    void (*complete)(struct block_request* req);
    int (*write)(struct block_socket* socket, struct block_request* req);
    int (*receive)(read_descriptor_t* rd_desc, struct sk_buff* skb, unsigned int offset, size_t len);
};

static void fetch_data_ready(struct sock *sk);
static void fetch_work(struct work_struct* work);
static void fetch_complete(struct block_request* req);
static int fetch_write(struct block_socket* socket, struct block_request* breq);
static int fetch_receive(read_descriptor_t* rd_desc, struct sk_buff* skb, unsigned int offset, size_t len);

static struct block_ops fetch_ops = {
    .data_ready = fetch_data_ready,
    .work = fetch_work,
    .complete = fetch_complete,
    .write = fetch_write,
    .receive = fetch_receive,
    .timeout_jiffies = &eggsfs_fetch_block_timeout_jiffies,
};

static void write_data_ready(struct sock *sk);
static void write_work(struct work_struct* work);
static void write_complete(struct block_request* req);
static int write_write(struct block_socket* socket, struct block_request* breq);
static int write_receive(read_descriptor_t* rd_desc, struct sk_buff* skb, unsigned int offset, size_t len);

static struct block_ops write_ops = {
    .data_ready = write_data_ready,
    .work = write_work,
    .complete = write_complete,
    .write = write_write,
    .receive = write_receive,
    .timeout_jiffies = &eggsfs_write_block_timeout_jiffies,
};

static void do_timeout_sockets(struct work_struct* w);
static DECLARE_DELAYED_WORK(timeout_work, do_timeout_sockets);

// Utils
// --------------------------------------------------------------------

static u32 eggsfs_skb_copy(void* dstp, struct sk_buff* skb, u32 offset, u32 len) {
    struct skb_seq_state seq;
    u32 consumed = 0;
    char* dst = dstp;

    skb_prepare_seq_read(skb, offset, min(offset + len, skb->len), &seq);
    for (;;) {
        const u8* ptr;
        int avail = skb_seq_read(consumed, &ptr, &seq);
        if (avail == 0) { return consumed; }
        BUG_ON(avail < 0);
        // avail _can_ exceed the upper bound here
        int actual_avail = min((u32)avail, len-consumed);
        memcpy(dst + consumed, ptr, actual_avail);
        consumed += actual_avail;
        if (actual_avail < avail) {
            skb_abort_seq_read(&seq);
            return consumed;
        }
    }
    BUG();
}

// Generic socket/request infra
// --------------------------------------------------------------------

static void block_ops_init(struct block_ops* ops) {
    int i;
    hash_init(ops->sockets);    
    for (i = 0; i < BLOCK_SOCKET_BUCKETS; i++) {
        spin_lock_init(&ops->locks[i]);
        atomic_set(&ops->len[i], 0);
        init_waitqueue_head(&ops->wqs[i]);
    }
}

struct block_socket {
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
    bool terminal;
    // Read end. "data available" callback does all the work.
    struct list_head read;
    spinlock_t read_lock;
    // Saved callbacks
    void (*saved_state_change)(struct sock *sk);
    void (*saved_data_ready)(struct sock *sk);
    void (*saved_write_space)(struct sock *sk);
};

static void block_ops_exit(struct block_ops* ops) {
    eggsfs_debug("waiting for all sockets to be done");

    struct block_socket* sock;
    int bucket;
    rcu_read_lock();
    hash_for_each_rcu(ops->sockets, bucket, sock, hnode) {
        eggsfs_debug("scheduling winddown for %d", ntohs(sock->addr.sin_port));
        atomic_cmpxchg(&sock->err, 0, -ECONNABORTED);
        queue_work(eggsfs_wq, &sock->work);
    }
    rcu_read_unlock();

    // wait for all of them to be freed by work
    for (bucket = 0; bucket < BLOCK_SOCKET_BUCKETS; bucket++) {
        eggsfs_debug("waiting for bucket %d (len %d)", bucket, atomic_read(&ops->len[bucket]));
        wait_event(ops->wqs[bucket], atomic_read(&ops->len[bucket]) == 0);
    }
}

struct block_request {
    atomic_t err;

    // What we use to call back into the callback from BH
    struct work_struct complete_work;

    // To store the request in the read/write lists of the socket
    struct list_head list;

    // How much is left to write (including the block body if writin)
    u32 left_to_write;
    // How much is left to read (including block body if reading)
    u32 left_to_read;
};

static void block_state_check(struct sock* sk) {
    struct block_socket* socket = sk->sk_user_data;

    if (sk->sk_state == TCP_ESTABLISHED) { return; } // the only good one

    // Right now we connect beforehand, so every change is trouble. Just
    // kill the socket.
    eggsfs_debug("socket state check triggered: %d, will fail reqs", sk->sk_state);
    atomic_cmpxchg(&socket->err, 0, -ECONNABORTED); // TODO we could have nicer errors
    queue_work(eggsfs_wq, &socket->work);
}

static void block_state_change(struct sock* sk) {
    read_lock_bh(&sk->sk_callback_lock);
    struct block_socket* socket = sk->sk_user_data;
    void (*saved_state_change)(struct sock *) = socket->saved_state_change;
    block_state_check(sk);
    read_unlock_bh(&sk->sk_callback_lock);

    saved_state_change(sk);
}

static void block_write_space(struct sock* sk) {
    read_lock_bh(&sk->sk_callback_lock);
    struct block_socket* socket = (struct block_socket*)sk->sk_user_data;
    void (*old_write_space)(struct sock *) = socket->saved_write_space;
    queue_work(eggsfs_wq, &socket->work);
    read_unlock_bh(&sk->sk_callback_lock);

    old_write_space(sk);
}

// Gets the socket, and acquires a reference to it.
static struct block_socket* get_block_socket(
    struct block_ops* ops,
    struct sockaddr_in* addr
) __acquires(RCU) {
    u64 key = block_socket_key(addr);
    int bucket = hash_min(key, BLOCK_SOCKET_BITS);

    rcu_read_lock();
    struct block_socket* sock;
    hlist_for_each_entry_rcu(sock, &ops->sockets[bucket], hnode) {
        if (block_socket_key(&sock->addr) == key) {
            return sock;
        }
    }
    rcu_read_unlock();

    eggsfs_debug("socket to %pI4:%d not found, will create", &addr->sin_addr, ntohs(addr->sin_port));

    sock = kmalloc(sizeof(struct block_socket), GFP_KERNEL);
    if (sock == NULL) { return ERR_PTR(-ENOMEM); }

    memcpy(&sock->addr, addr, sizeof(struct sockaddr_in));

    sock->timeout_start = 0;
    atomic_set(&sock->err, 0);

    int err = sock_create_kern(&init_net, PF_INET, SOCK_STREAM, IPPROTO_TCP, &sock->sock);
    if (err != 0) { goto out_err; }

    // TFO would be nice, but it doesn't really matter given that we aggressively recycle
    // connections, and also given that we have our own callbacks which do not play well
    // at connection opening stage. So open the connection
    //
    // TODO actually this might not be relevant anymore, because we now call the original
    // callbacks in the state change callback (like iscsi does).
    err = kernel_connect(sock->sock, (struct sockaddr*)&sock->addr, sizeof(sock->addr), 0);
    if (err < 0) {
        if (err == -ERESTARTSYS || err == -ERESTARTNOINTR) {
            eggsfs_debug("could not connect to block service at %pI4:%d: %d", &sock->addr.sin_addr, ntohs(sock->addr.sin_port), err);
        } else {
            eggsfs_warn("could not connect to block service at %pI4:%d: %d", &sock->addr.sin_addr, ntohs(sock->addr.sin_port), err);
        }
        goto out_err;
    }

    // Important for the callbacks to happen after the connect, see comment about
    // fastopen above. Also, we want the callbacks to set last, after we've setup
    // all the state, lest they run before the state is indeed setup.
    write_lock(&sock->sock->sk->sk_callback_lock);
    sock->saved_data_ready = sock->sock->sk->sk_data_ready;
    sock->saved_state_change = sock->sock->sk->sk_state_change;
    sock->saved_write_space = sock->sock->sk->sk_write_space;
    sock->sock->sk->sk_user_data = sock;
    sock->sock->sk->sk_data_ready = ops->data_ready;
    sock->sock->sk->sk_state_change = block_state_change;
    sock->sock->sk->sk_write_space = block_write_space;
    write_unlock(&sock->sock->sk->sk_callback_lock);

    INIT_LIST_HEAD(&sock->write);
    spin_lock_init(&sock->write_lock);
    INIT_WORK(&sock->work, ops->work);
    INIT_LIST_HEAD(&sock->read);
    spin_lock_init(&sock->read_lock);

    sock->terminal = false;

    // now insert
    struct block_socket* other_sock;

    spin_lock(&ops->locks[bucket]);
    // Take the RCU lock while holding the socket lock so that
    // we know it won't get removed by timeouts or such in the
    // span of time between unlocking the socket lock and acquiring
    // the RCU lock.
    rcu_read_lock();
    // first check if somebody else got there first
    hlist_for_each_entry_rcu(other_sock, &ops->sockets[bucket], hnode) { // first we check if somebody didn't get to it first
        if (block_socket_key(&other_sock->addr) == key) {
            // somebody got here before us
            spin_unlock(&ops->locks[bucket]);
            sock_release(sock->sock);
            kfree(sock);
            return other_sock; // exit with read lock held
        }
    }
    // Now actually insert.
    hlist_add_head_rcu(&sock->hnode, &ops->sockets[bucket]);
    spin_unlock(&ops->locks[bucket]);

    atomic_inc(&ops->len[bucket]);

    return sock;

out_err:
    kfree(sock);
    return ERR_PTR(err);
}

// Can only be called from the socket work! Otherwise it'll race with it.
static void remove_block_socket(
    struct block_ops* ops,
    struct block_socket* socket
) {
    int err = atomic_read(&socket->err);
    BUG_ON(!err);

    eggsfs_debug("winding down socket to %pI4:%d due to %d", &socket->addr.sin_addr, ntohs(socket->addr.sin_port), err);

    u64 key = block_socket_key(&socket->addr);
    int bucket = hash_min(key, BLOCK_SOCKET_BITS);

    if (!socket->terminal) {
        socket->terminal = true;
    
        // First, remove socket from hashmap. After we're done with this,
        // we know nobody's going to add new requests to this.
        spin_lock(&ops->locks[bucket]);
        hash_del_rcu(&socket->hnode); // tied to atomic_dec below
        spin_unlock(&ops->locks[bucket]);
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

        struct block_request* req;
        struct block_request* tmp;

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
            eggsfs_debug("completing request because of a socket winddown");
            ops->complete(req); // no need to go through wq, we're in process context already
        }
    }

    // Finally, kill the socket, unless there's another work item remaining, in which
    // case it'll kill the socket but won't execute the cleanup operation above.
    if (!(test_bit(WORK_STRUCT_PENDING_BIT, work_data_bits(&socket->work)))) {
        sock_release(socket->sock);
        kfree(socket);

        // Adjust len, notify waiters
        smp_mb__before_atomic();
        atomic_dec(&ops->len[bucket]);
        wake_up_all(&ops->wqs[bucket]);
    }
}

// This has three purposes:
//
// * Writing out stuff to socket;
// * If the socket is to be decommissioned, fail all the remaining requests.
static void block_work(
    struct block_ops* ops,
    struct block_socket* socket
) {
    eggsfs_debug("%pI4:%d", &socket->addr.sin_addr, ntohs(socket->addr.sin_port));

    // if we've failed, then we need to clean up the mess
    int err = atomic_read(&socket->err);
    if (err) {
        remove_block_socket(ops, socket);
        return;
    }

    // Otherwise we need to write out requests.

    for (;;) {
        // Get request, if any
        struct block_request* req;
        spin_lock_bh(&socket->write_lock);
        req = list_first_entry_or_null(&socket->write, struct block_request, list);
        spin_unlock_bh(&socket->write_lock);

        // Check if we've got nothing to do
        if (req == NULL) { return; }

        // Can't be done already, we just got the req
        BUG_ON(req->left_to_write == 0);

        int sent = ops->write(socket, req);
        if (sent < 0) { 
            atomic_cmpxchg(&socket->err, 0, sent);
            remove_block_socket(ops, socket); // bail
            return;
        }
        BUG_ON(sent > req->left_to_write);
        req->left_to_write -= sent;
        if (sent > 0) {
            socket->timeout_start = get_jiffies_64();
        }

        if (req->left_to_write == 0) {
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

static int block_receive(
    int (*receive_single_req)(struct block_request* req, struct sk_buff* skb, unsigned int offset, size_t len),
    read_descriptor_t* rd_desc,
    struct sk_buff* skb,
    unsigned int offset,
    size_t len
) {
    struct block_socket* socket = rd_desc->arg.data;

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
    struct block_request* req;
    spin_lock_bh(&socket->read_lock);
    req = list_first_entry_or_null(&socket->read, struct block_request, list);
    spin_unlock_bh(&socket->read_lock);

    for (;;) {
        if (req == NULL) { break; }
        int consumed = receive_single_req(req, skb, offset, len);
        if (consumed < 0) {
            // socket is scuppered, let the work handle this
            atomic_cmpxchg(&socket->err, 0, consumed);
            queue_work(eggsfs_wq, &socket->work);
            return len0;
        }
        BUG_ON(consumed > req->left_to_read);
        req->left_to_read -= consumed;
        eggsfs_debug("left_to_read=%u consumed=%d", req->left_to_read, consumed);
        // eggsfs_info("sock=%p req=%p consumed=%d len=%llu offset=%u err=%d", socket, req, consumed, len, offset, req->err);
        len -= consumed;
        offset += consumed;
        if (req->left_to_read == 0 || atomic_read(&req->err)) {
            // this request is done
            spin_lock_bh(&socket->read_lock);
            list_del(&req->list);
            queue_work(eggsfs_wq, &req->complete_work);
            req = list_first_entry_or_null(&socket->read, struct block_request, list);
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

static void block_data_ready(
    struct block_ops* ops,
    struct sock* sk
) {
    read_lock_bh(&sk->sk_callback_lock);
    read_descriptor_t rd_desc;
    // Taken from iscsi -- we set count to 1 because we want the network layer to
    // hand us all the skbs that are available.
    rd_desc.arg.data = sk->sk_user_data;
    rd_desc.count = 1;
    tcp_read_sock(sk, &rd_desc, ops->receive);
    block_state_check(sk);
    read_unlock_bh(&sk->sk_callback_lock);
}

static int block_start(
    struct block_ops* ops,
    struct block_request* req,
    struct eggsfs_block_service* bs,
    u32 left_to_write,
    u32 left_to_read
) {
    int err, i;

    atomic_set(&req->err, 0);

    req->left_to_write = left_to_write;
    req->left_to_read = left_to_read;

    // As the very last thing, try to get the socket (will get RCU lock).
    // Try both ips (if we have them) before giving up
    int block_ip = WHICH_BLOCK_IP++;
    struct sockaddr_in addr;
    struct block_socket* sock;
    for (i = 0; i < 1 + (bs->port2 != 0); i++, block_ip++) { // we might not have a second address
        addr.sin_family = AF_INET;
        if (bs->port2 == 0 || block_ip&1) {
            addr.sin_addr.s_addr = htonl(bs->ip1);
            addr.sin_port = htons(bs->port1);
        } else {
            addr.sin_addr.s_addr = htonl(bs->ip2);
            addr.sin_port = htons(bs->port2);
        }

        sock = get_block_socket(ops, &addr);
        if (!IS_ERR(sock)) {
            goto sock_found;
        } else {
            err = PTR_ERR(sock);
        }
    }
    if (err < 0) { goto out_err; }
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

out_err:
    eggsfs_info("couldn't start block request, err=%d", err);
    return err;
}

// Periodically checks all sockets and schedules the timed out ones for deletion
static void timeout_sockets(struct block_ops* ops) {
    int bucket;
    struct block_socket* sock;
    u64 now = get_jiffies_64();

    rcu_read_lock();
    hash_for_each_rcu(ops->sockets, bucket, sock, hnode) {
        if (sock->timeout_start == 0) { continue; }
        // this loop is relatively long, this could happen
        if (unlikely(sock->timeout_start > now)) {
            eggsfs_info("timeout start apparently in the future, skipping (%llums > %llums)", jiffies64_to_msecs(sock->timeout_start), now);
            continue;
        }
        u64 dt = now - sock->timeout_start;
        if (dt > *ops->timeout_jiffies) {
            eggsfs_info("timing out socket to %pI4:%d (%llums > %llums)", &sock->addr.sin_addr, ntohs(sock->addr.sin_port), jiffies64_to_msecs(dt), jiffies64_to_msecs(*ops->timeout_jiffies));
            atomic_cmpxchg(&sock->err, 0, -ETIMEDOUT);
            queue_work(eggsfs_wq, &sock->work);
        }
    }
    rcu_read_unlock();
}

static int drop_sockets(struct block_ops* ops) {
    int dropped = 0;
    int bucket;
    struct block_socket* sock;

    rcu_read_lock();
    hash_for_each_rcu(ops->sockets, bucket, sock, hnode) {
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

// Fetch
// --------------------------------------------------------------------

struct fetch_request {
    struct block_request breq;

    void (*callback)(void* data, u64 block_id, struct list_head* pages, int err);
    void *data;
    struct list_head pages;

    // Req info
    u64 block_service_id;
    u64 block_id;
    u32 offset;
    u32 count;

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

#define get_fetch_request(req) container_of(req, struct fetch_request, breq)

static void fetch_complete(struct block_request* breq) {
    struct fetch_request* req = get_fetch_request(breq);
    eggsfs_debug("block fetch complete block_id=%016llx err=%d", req->block_id, atomic_read(&req->breq.err));
    req->callback(req->data, req->block_id, &req->pages, atomic_read(&req->breq.err));

    eggsfs_debug("block released socket block_id=%016llx err=%d", req->block_id, atomic_read(&req->breq.err));

    put_pages_list(&req->pages);
    kmem_cache_free(fetch_request_cachep, req);
}

static void fetch_complete_work(struct work_struct* work) {
    fetch_complete(container_of(work, struct block_request, complete_work));
}

static void fetch_request_constructor(void* ptr) {
    struct fetch_request* req = (struct fetch_request*)ptr;

    INIT_LIST_HEAD(&req->pages);
    INIT_WORK(&req->breq.complete_work, fetch_complete_work);
}

int eggsfs_drop_fetch_block_sockets(void) {
    return drop_sockets(&fetch_ops);
}

static int fetch_write(struct block_socket* socket, struct block_request* breq) {
    struct fetch_request* req = get_fetch_request(breq);

    char req_msg_buf[EGGSFS_BLOCKS_REQ_HEADER_SIZE + EGGSFS_FETCH_BLOCK_REQ_SIZE];
    BUG_ON(breq->left_to_write > sizeof(req_msg_buf));

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
        .iov_base = req_msg_buf + (sizeof(req_msg_buf) - breq->left_to_write),
        .iov_len = breq->left_to_write,
    };
    int err = kernel_sendmsg(socket->sock, &msg, &iov, 1, iov.iov_len);
    return err == -EAGAIN ? 0 : err;
}

static void fetch_work(struct work_struct* work) {
    struct block_socket* socket = container_of(work, struct block_socket, work);
    block_work(&fetch_ops, socket);
}

// Returns a negative result if the socket has to be considered corrupted.
// If a "recoverable" error occurs (e.g. the server just replies with an error)
// `req->err` will be filled in and receiving can continue. If an unrecoverable
// error occurs `req->complete.err` will be filled in also, but it's expected that the
// caller stops operating on this socket.
static int fetch_receive_single_req(
    struct block_request* breq,
    struct sk_buff* skb,
    unsigned int offset, size_t len0
) {
    struct fetch_request* req = get_fetch_request(breq);
    size_t len = len0;

    BUG_ON(req->breq.left_to_read == 0);

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
        atomic_cmpxchg(&req->breq.err, 0, eggsfs_error_to_linux(le16_to_cpu(req->header.data.error)));
        return len0-len;
    }

#undef HEADER_COPY

    // Actual data copy

    struct page* page = list_first_entry(&req->pages, struct page, lru);
    BUG_ON(!page);
    char* page_ptr = kmap_atomic(page);

    struct skb_seq_state seq;
    u32 read_len = min(req->breq.left_to_read, (u32)len);
    skb_prepare_seq_read(skb, offset, offset + read_len, &seq);

    u32 block_bytes_read = 0;
    while (block_bytes_read < req->breq.left_to_read) {
        const u8* data;
        u32 avail = skb_seq_read(block_bytes_read, &data, &seq);
        if (avail == 0) { break; }
        avail = min(avail, read_len - block_bytes_read); // avail can exceed the len upper bound

        while (avail) {
            u32 this_len = min3(avail, (u32)PAGE_SIZE - req->page_offset, req->breq.left_to_read - block_bytes_read);
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

    BUG_ON(block_bytes_read > req->breq.left_to_read);

    // The last page is partially filled, zero it and return to first page
    if (!req->breq.left_to_read && req->page_offset) {
        memset(page_ptr + req->page_offset, 0, PAGE_SIZE - req->page_offset);
        list_rotate_left(&req->pages);
        req->page_offset = 0;
    }

    kunmap_atomic(page_ptr);

    return (len0-len) + block_bytes_read;
}

static int fetch_receive(read_descriptor_t* rd_desc, struct sk_buff* skb, unsigned int offset, size_t len) {
    return block_receive(fetch_receive_single_req, rd_desc, skb, offset, len);
}

static void fetch_data_ready(struct sock* sk) {
    block_data_ready(&fetch_ops, sk);
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
    req->block_service_id = bs->id;
    req->block_id = block_id;
    req->offset = offset;
    req->count = count;
    req->page_offset = 0;
    req->header_read = 0;

    BUG_ON(!list_empty(&req->pages));
    for (i = 0; i < (count + PAGE_SIZE - 1)/PAGE_SIZE; i++) {
        struct page* page = alloc_page(GFP_KERNEL);
        if (!page) { err = -ENOMEM; goto out_err_pages; }
        list_add_tail(&page->lru, &req->pages);
    }

    err = block_start(
        &fetch_ops,
        &req->breq,
        bs,
        EGGSFS_BLOCKS_REQ_HEADER_SIZE + EGGSFS_FETCH_BLOCK_REQ_SIZE,
        EGGSFS_BLOCKS_RESP_HEADER_SIZE + EGGSFS_FETCH_BLOCK_RESP_SIZE + count
    );
    if (err) {
        goto out_err_pages;
    }

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

struct write_request {
    struct block_request breq;

    // Called when request is done
    void (*callback)(void* data, struct list_head* pages, u64 block_id, u64 proof, int err);
    void* data;

    struct list_head pages;

    // Req info
    u64 block_service_id;
    u64 block_id;
    u32 crc;
    u32 size;
    u64 certificate;

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

#define get_write_request(req) container_of(req, struct write_request, breq)

static void write_complete(struct block_request* breq) {
    struct write_request* req = get_write_request(breq);


    // might be that we didn't consume all the pages -- in which case we need
    // to keep rotating until we're there.
    while (list_first_entry(&req->pages, struct page, lru)->private == 0) {
        list_rotate_left(&req->pages);
    }
    req->callback(req->data, &req->pages, req->block_id, be64_to_cpu(req->write_resp.data.proof), atomic_read(&req->breq.err));
    BUG_ON(!list_empty(&req->pages)); // the callback must acquire this

    // TODO what if the socket is being read right now? There's probably a race with
    // that and the free here.

    BUG_ON(!list_empty(&req->pages));
    kmem_cache_free(write_request_cachep, req);
}

static void write_complete_work(struct work_struct* work) {
    write_complete(container_of(work, struct block_request, complete_work));
}

static void write_request_constructor(void* ptr) {
    struct write_request* req = (struct write_request*)ptr;

    INIT_LIST_HEAD(&req->pages);
    INIT_WORK(&req->breq.complete_work, write_complete_work);
}

int eggsfs_drop_write_block_sockets(void) {
    return drop_sockets(&write_ops);
}

static int write_write(struct block_socket* socket, struct block_request* breq) {
    struct write_request* req = get_write_request(breq);
    int err = 0;

    u32 write_size = EGGSFS_BLOCKS_REQ_HEADER_SIZE + EGGSFS_WRITE_BLOCK_REQ_SIZE + req->size;
    u32 write_req_written0 = write_size - breq->left_to_write;
    u32 write_req_written = write_req_written0;

#define BLOCK_WRITE_EXIT return write_req_written - write_req_written0;

    // Still writing request -- we just serialize the buffer each time and send it out
    while (write_req_written < EGGSFS_BLOCKS_REQ_HEADER_SIZE+EGGSFS_WRITE_BLOCK_REQ_SIZE) {
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
            .iov_base = req_msg_buf + write_req_written,
            .iov_len = sizeof(req_msg_buf) - write_req_written,
        };
        int sent = kernel_sendmsg(socket->sock, &msg, &iov, 1, iov.iov_len);
        if (sent == -EAGAIN) { BLOCK_WRITE_EXIT } // done for now, will be rescheduled by `sk_write_space`
        if (sent < 0) { err = sent; goto out_err; }
        write_req_written += sent;
    }

    // Write the pages out
    while (write_req_written < write_size) {
        struct page* page = list_first_entry(&req->pages, struct page, lru);
        int sent = kernel_sendpage(
            socket->sock, page,
            page->index,
            min((u32)(PAGE_SIZE - page->index), write_size - write_req_written),
            MSG_MORE | MSG_DONTWAIT
        );
        if (sent == -EAGAIN) {
            // done for now, will be rescheduled by `sk_write_space`
            break;
        }
        if (sent < 0) { err = sent; goto out_err; }
        page->index += sent;
        write_req_written += sent;
        if (page->index >= PAGE_SIZE) {
            BUG_ON(page->index != PAGE_SIZE);
            list_rotate_left(&req->pages);
            if (write_size - write_req_written) { // otherwise this will be null
                list_first_entry(&req->pages, struct page, lru)->index = 0;
                list_first_entry(&req->pages, struct page, lru)->private = 0;
            }
        }
    }
    BLOCK_WRITE_EXIT;

out_err:
    eggsfs_debug("block write failed err=%d", err);
    BUG_ON(err == 0);
    atomic_cmpxchg(&breq->err, 0, err);
    return err;

#undef BLOCK_WRITE_EXIT
}

static void write_work(struct work_struct* work) {
    struct block_socket* socket = container_of(work, struct block_socket, work);
    block_work(&write_ops, socket);
}

static int write_receive_single_req(
    struct block_request* breq,
    struct sk_buff* skb,
    unsigned int offset, size_t len0
) {
    struct write_request* req = get_write_request(breq);
    size_t len = len0;
    u32 write_resp_read = (EGGSFS_BLOCKS_RESP_HEADER_SIZE + EGGSFS_WRITE_BLOCK_RESP_SIZE) - req->breq.left_to_read;

#define BLOCK_WRITE_EXIT(i) do { \
        if (i < 0) { \
            atomic_cmpxchg(&req->breq.err, 0, i); \
        } \
        return i; \
    } while (0)

    // Write resp

#define HEADER_COPY(buf, count) ({ \
        int read = count > 0 ? eggsfs_skb_copy(buf, skb, offset, count) : 0; \
        offset += read; \
        len -= read; \
        write_resp_read += read; \
        read; \
    })

    // Header
    {
        int header_left = EGGSFS_BLOCKS_RESP_HEADER_SIZE - (int)write_resp_read;
        int read = HEADER_COPY(req->write_resp.buf + write_resp_read, header_left);
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
        int error_left = (EGGSFS_BLOCKS_RESP_HEADER_SIZE + 2) - (int)write_resp_read;
        int read = HEADER_COPY(req->write_resp.buf + write_resp_read, error_left);
        error_left -= read;
        if (error_left > 0) { BLOCK_WRITE_EXIT(len0-len); }
        eggsfs_info("writing block to bs=%016llx block_id=%016llx failed=%u", req->block_service_id, req->block_id, req->write_resp.data.error);
        // We immediately start writing, so any error here means that the socket is kaput
        int err = eggsfs_error_to_linux(le16_to_cpu(req->write_resp.data.error));
        BLOCK_WRITE_EXIT(err);
    }

    // We can finally get the proof
    {
        int proof_left = (EGGSFS_BLOCKS_RESP_HEADER_SIZE + 8) - (int)write_resp_read;
        int read = HEADER_COPY(req->write_resp.buf + write_resp_read, proof_left);
        proof_left -= read;
        if (proof_left > 0) {
            BLOCK_WRITE_EXIT(len0-len);
        }
    }

#undef HEADER_COPY

    BLOCK_WRITE_EXIT(len0-len);

#undef BLOCK_WRITE_EXIT
}

static int write_receive(read_descriptor_t* rd_desc, struct sk_buff* skb, unsigned int offset, size_t len) {
    return block_receive(write_receive_single_req, rd_desc, skb, offset, len);
}

static void write_data_ready(struct sock* sk) {
    block_data_ready(&write_ops, sk);
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

    BUG_ON(list_empty(pages));

    // can't write
    if (unlikely(bs->flags & EGGSFS_BLOCK_SERVICE_DONT_WRITE)) {
        eggsfs_debug("could not write block given block flags %02x", bs->flags);
        return -EIO;
    }

    struct write_request* req = kmem_cache_alloc(write_request_cachep, GFP_KERNEL);
    if (!req) { err = -ENOMEM; goto out_err; }

    req->callback = callback;
    req->data = data;
    req->block_service_id = bs->id;
    req->block_id = block_id;
    req->crc = crc;
    req->size = size;
    req->certificate = certificate;
    memset(&req->write_resp.buf, 0, sizeof(req->write_resp.buf));
    list_replace_init(pages, &req->pages);
    list_first_entry(&req->pages, struct page, lru)->index = 0;
    list_first_entry(&req->pages, struct page, lru)->private = 1; // we use this to mark the first page

    err = block_start(
        &write_ops,
        &req->breq,
        bs,
        EGGSFS_BLOCKS_REQ_HEADER_SIZE + EGGSFS_WRITE_BLOCK_REQ_SIZE + size,
        EGGSFS_BLOCKS_RESP_HEADER_SIZE + EGGSFS_WRITE_BLOCK_RESP_SIZE
    );
    if (err) {
        goto out_err_req;
    }

    return 0;

out_err_req:
    list_replace_init(&req->pages, pages);
    kmem_cache_free(write_request_cachep, req);
out_err:
    eggsfs_info("couldn't start write block request, err=%d", err);
    return err;
}

// Timeout
// --------------------------------------------------------------------

// Periodically checks all sockets and schedules the timed out ones for deletion
static void do_timeout_sockets(struct work_struct* w) {
    timeout_sockets(&fetch_ops);
    timeout_sockets(&write_ops);
    queue_delayed_work(eggsfs_wq, &timeout_work, min(*fetch_ops.timeout_jiffies, *write_ops.timeout_jiffies));
}

// init/exit
// --------------------------------------------------------------------

int __init eggsfs_block_init(void) {
    int err;
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
        write_request_constructor
    );
    if (!write_request_cachep) { err = -ENOMEM; goto out_fetch_request; }

    block_ops_init(&fetch_ops);
    block_ops_init(&write_ops);

    queue_work(eggsfs_wq, &timeout_work.work);

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

    block_ops_exit(&fetch_ops);
    block_ops_exit(&write_ops);

    // clear caches
    kmem_cache_destroy(fetch_request_cachep);
    kmem_cache_destroy(write_request_cachep);
}
