#include "net.h"

#include <linux/version.h>

#include "log.h"
#include "super.h"
#include "trace.h"
#include "err.h"
#include "debugfs.h"
#include "metadata.h"

#define MSECS_TO_JIFFIES(_ms) ((_ms * HZ) / 1000)

unsigned eggsfs_initial_shard_timeout_jiffies = MSECS_TO_JIFFIES(250);
unsigned eggsfs_max_shard_timeout_jiffies = MSECS_TO_JIFFIES(2000);
unsigned eggsfs_overall_shard_timeout_jiffies = MSECS_TO_JIFFIES(10000);
unsigned eggsfs_initial_cdc_timeout_jiffies = MSECS_TO_JIFFIES(1000);
unsigned eggsfs_max_cdc_timeout_jiffies = MSECS_TO_JIFFIES(10000);
unsigned eggsfs_overall_cdc_timeout_jiffies = MSECS_TO_JIFFIES(120000);

static struct eggsfs_shard_request* get_shard_request(struct eggsfs_shard_socket* s, u64 request_id) __must_hold(s->lock) {
    struct rb_node* node = s->requests.rb_node;
    while (node) {
        struct eggsfs_shard_request* req = container_of(node, struct eggsfs_shard_request, node);
        if (request_id < req->request_id) { node = node->rb_left; }
        else if (request_id > req->request_id) { node = node->rb_right; }
        else { return req; }
    }
    return NULL;
}

static void sock_readable(struct sock* sk) {
    struct eggsfs_shard_socket* s;
    struct sk_buff* skb;
    int err;
    __le64 request_id_le;
    u64 request_id;

    read_lock_bh(&sk->sk_callback_lock);
    s = (struct eggsfs_shard_socket*)sk->sk_user_data;
    BUG_ON(!s);
    for (;;) {
        skb = skb_recv_udp(sk, 0, 1, &err);
        if (!skb) {
            read_unlock_bh(&sk->sk_callback_lock);
            return;
        }
                              
        if (unlikely(skb_checksum_complete(skb))) {
            eggsfs_warn("dropping request with bad checksum");
            goto drop_skb;
        }

        // u32 protocol + u64 req_id. Note that we check the protocol later on.
        // We just need the request id here.
        if (unlikely(skb->len < 12)) {
            eggsfs_warn("dropping runt eggsfs request");
            goto drop_skb;
        }
        if (unlikely(skb->len > EGGSFS_MAX_MTU)) {
            eggsfs_warn("dropping overlong eggsfs request");
            goto drop_skb;
        }
        BUG_ON(skb_copy_bits(skb, 4, &request_id_le, 8) != 0);
        request_id = le64_to_cpu(request_id_le);

        struct eggsfs_shard_request* req;
        spin_lock_bh(&s->lock); {
            req = get_shard_request(s, request_id);
            if (req) {
                BUG_ON(req->skb);
                rb_erase(&req->node, &s->requests);
                req->skb = skb;
                skb = NULL;
                complete(&req->comp);
#if 0
                if (!(req->flags & EGGSFS_SHARD_ASYNC)) {
                } else {
                    struct eggsfs_shard_async_request* areq = container_of(req, struct eggsfs_shard_async_request, request);
                    cancel_delayed_work(&areq->work);
                    queue_work(eggsfs_wq, &areq->callback);
                }
#endif
            }
        } spin_unlock_bh(&s->lock);
        if (!req) {
            eggsfs_debug("could not find request id %llu (probably interrupted or late after multiple attempts)", request_id);
        }

        if (skb) {
drop_skb:
            kfree_skb(skb);
        }
    }
}

// timer async request expired
// if skb then retrun
// requeue 
// whoever wants to remove request needs to handle timer and workqueue

static void insert_shard_request(struct eggsfs_shard_socket* s, struct eggsfs_shard_request* req) __must_hold(s->lock) {
    u64 request_id = req->request_id;
    struct rb_node** new = &s->requests.rb_node;
    struct rb_node* parent = NULL;
    while (*new) {
        struct eggsfs_shard_request* this = container_of(*new, struct eggsfs_shard_request, node);
        parent = *new;
        if (request_id < this->request_id) { new = &((*new)->rb_left); }
        else if (request_id > this->request_id) { new = &((*new)->rb_right); }
        else { printk(KERN_ERR "dup id %lld vs. %lld %p vs. %p\n", request_id, this->request_id, req, this); BUG_ON(1); return; }
    }

    rb_link_node(&req->node, parent, new);
    rb_insert_color(&req->node, &s->requests);
}


// Returns:
//
// * n == 0 for success
// * n < 0 for error (notable ones being -ETIMEOUT and -ERESTARTSYS)
//
// Once this is done, the request is guaranteed to not be in the tree anymore.
static int __must_check wait_for_request(struct eggsfs_shard_socket* s, struct eggsfs_shard_request* req, u64 timeout_jiffies) {
    u64 timeout_s = timeout_jiffies/HZ;
    if (timeout_s > 10) {
        eggsfs_warn("about to wait for request for a long time (%llu seconds)", timeout_s);
    }
    int ret = wait_for_completion_killable_timeout(&req->comp, timeout_jiffies);
    // We got a response, this is the happy path.
    if (likely(ret > 0)) {
        BUG_ON(!req->skb);
        return 0;
    }
    // We didn't get a response, it's our job to clean up the request.
    spin_lock_bh(&s->lock);
    // We might have filled the request in the time it took us to get here, in which
    // case `sock_readable` will have already erased it and we're actually good to
    // go.
    bool completed = !!req->skb;
    if (likely(!completed)) {
        rb_erase(&req->node, &s->requests);
    }
    spin_unlock_bh(&s->lock);
    // If the request was filled in the meantime, we also need to free the skb.
    if (completed) {
        eggsfs_info("got response after waiting for it returned an error or timed out: %d", ret);
        return 0;
    }
    return ret ? ret : -ETIMEDOUT;
}

void eggsfs_net_shard_free_socket(struct eggsfs_shard_socket* s) {
    // TODO anything else?
    write_lock_bh(&s->sock->sk->sk_callback_lock);
    s->sock->sk->sk_data_ready = s->original_data_ready;
    s->original_data_ready = NULL;
    write_unlock_bh(&s->sock->sk->sk_callback_lock);
    sock_release(s->sock);
}

int eggsfs_init_shard_socket(struct eggsfs_shard_socket* s) {
    struct sockaddr_in addr;
    int err;

    err = sock_create_kern(&init_net, PF_INET, SOCK_DGRAM, IPPROTO_UDP, &s->sock);
    if (err) { goto out_err; }

    write_lock_bh(&s->sock->sk->sk_callback_lock);
    BUG_ON(s->sock->sk->sk_user_data);
    s->sock->sk->sk_user_data = s;
    s->original_data_ready = s->sock->sk->sk_data_ready;
    s->sock->sk->sk_data_ready = sock_readable;
    write_unlock_bh(&s->sock->sk->sk_callback_lock);

    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = 0;
    addr.sin_port = 0;
    err = s->sock->ops->bind(s->sock, (struct sockaddr*)&addr, sizeof(addr));
    if (err) { goto out_socket; }

    s->requests = RB_ROOT;
    spin_lock_init(&s->lock);

    return 0;

out_socket:
    sock_release(s->sock);
out_err:
    return err;
}

// TODO: congestion control

static bool eggsfs_shard_fill_msghdr(struct msghdr *msg, struct sockaddr_in* addr, const atomic64_t* addr_data) {
    u64 v1 = atomic64_read(addr_data);

    __be32 ip = eggsfs_get_addr_ip(v1);
    if (unlikely(ip == 0)) {
        return false;
    }
    __be16 port = eggsfs_get_addr_port(v1);

    addr->sin_addr.s_addr = ip;
    addr->sin_port = port;
    addr->sin_family = AF_INET;

    msg->msg_name = addr;
    msg->msg_namelen = sizeof(struct sockaddr_in);
    msg->msg_control = NULL;
    msg->msg_controllen = 0;
    msg->msg_flags = 0;
    return true;
}

static u64 WHICH_SHARD_IP = 0;

int eggsfs_metadata_request_nowait(struct eggsfs_shard_socket* sock, u64 req_id, void *p, u32 len, const atomic64_t* addr_data1, const atomic64_t* addr_data2) {
    struct msghdr msg;
    struct sockaddr_in addr;
    if (WHICH_SHARD_IP++ % 2) {
        eggsfs_shard_fill_msghdr(&msg, &addr, addr_data1);
    } else {
        if (unlikely(!eggsfs_shard_fill_msghdr(&msg, &addr, addr_data2))) {
            BUG_ON(!eggsfs_shard_fill_msghdr(&msg, &addr, addr_data1));
        }
    }
    struct kvec vec;
    vec.iov_base = p;
    vec.iov_len = len;
    int err = kernel_sendmsg(sock->sock, &msg, &vec, 1, vec.iov_len);
    if (err < 0) {
        eggsfs_info("could not send, err=%d", err);
        return err;
    }
    return 0;
}

// shard_id is just used for debugging/event tracing, and should be -1 if we're going to the CDC
struct sk_buff* eggsfs_metadata_request(
    struct eggsfs_shard_socket* sock,
    s16 shard_id,
    u64 req_id,
    void* p,
    u32 len,
    u32* attempts,
    const atomic64_t* addr_data1,
    const atomic64_t* addr_data2
) {
    u64 which_shard_ip = WHICH_SHARD_IP++;
    const atomic64_t* addr_data[2] = {addr_data1, addr_data2};

    struct eggsfs_shard_request req;
    struct kvec vec;
    int err = -EIO;

    u8 kind = *((u8*)p + 12);
    const char* kind_str = (shard_id < 0) ? eggsfs_cdc_kind_str(kind) : eggsfs_shard_kind_str(kind);
    // We can think of the contiguous stats array as list of areas for each shard.
    // Shard area is then split to 4 entries (EGGSFS_COUNTERS_NUM_COUNTERS) per request kind (EGGSFS_SHARD_KIND_MAX).
    // The expression above is left unminimised to make it easier to understand and match the explanation above.
    u64 stats_idx = (shard_id < 0) ?
        ((u64)__eggsfs_cdc_kind_index_mappings[kind] * EGGSFS_COUNTERS_NUM_COUNTERS) :
        ((u64)__eggsfs_shard_kind_index_mappings[kind] * EGGSFS_COUNTERS_NUM_COUNTERS + (u64)shard_id * EGGSFS_SHARD_KIND_MAX * EGGSFS_COUNTERS_NUM_COUNTERS);

    u64 latencies_idx = (shard_id < 0) ?
        ((u64)__eggsfs_cdc_kind_index_mappings[kind] * EGGSFS_LATENCIES_NUM_BUCKETS) :
        ((u64)__eggsfs_shard_kind_index_mappings[kind] * EGGSFS_LATENCIES_NUM_BUCKETS + (u64)shard_id * EGGSFS_SHARD_KIND_MAX * EGGSFS_LATENCIES_NUM_BUCKETS);

    req.skb = NULL;
    req.request_id = req_id;
    *attempts = 0;

    vec.iov_base = p;
    vec.iov_len = len;

    unsigned timeout = shard_id < 0 ? eggsfs_initial_cdc_timeout_jiffies : eggsfs_initial_shard_timeout_jiffies;
    unsigned max_timeout = shard_id < 0 ? eggsfs_max_cdc_timeout_jiffies : eggsfs_max_shard_timeout_jiffies;
    unsigned overall_timeout = shard_id < 0 ? eggsfs_overall_cdc_timeout_jiffies : eggsfs_overall_shard_timeout_jiffies;
    u64 start_t = get_jiffies_64();
    u64 elapsed = 0;

    u64 late_threshold = (shard_id < 0) ? MSECS_TO_JIFFIES(60000) : MSECS_TO_JIFFIES(1000);

#define LOG_STR_NO_ADDR "req_id=%llu shard_id=%d kind_str=%s kind=%d attempts=%d elapsed=%llums"
#define LOG_STR LOG_STR_NO_ADDR " addr=%pI4:%d"
#define LOG_ARGS_NO_ADDR req_id, shard_id, kind_str, kind, *attempts, jiffies64_to_msecs(elapsed)
#define LOG_ARGS LOG_ARGS_NO_ADDR, &addr.sin_addr, ntohs(addr.sin_port)
#define WARN_LATE if (elapsed > late_threshold) { eggsfs_info("late request: " LOG_STR_NO_ADDR, LOG_ARGS_NO_ADDR); }

    // This should be atomic64_add to be correct, but given that these are
    // stats, we leave it like this. Almost certainly wouldn't matter.
#define INC_STAT_COUNTER(_name) \
    if (shard_id < 0) { \
        atomic64_add(1, (atomic64_t*)&cdc_counters[stats_idx + EGGSFS_COUNTERS_IDX_##_name]); \
    } else { \
        atomic64_add(1, (atomic64_t*)&shard_counters[stats_idx + EGGSFS_COUNTERS_IDX_##_name]); \
    }

// jiffies only have millisecond precision, but we still use nanoseconds as the
// format is nicer and makes the buckets a bit more precise.
// |1 below drops the 0 value to the smallest bucket
#define INC_LATENCY_BUCKET ({ \
        u64 hist_bucket = 0; \
        u64 elapsed_nsecs = jiffies_to_nsecs(elapsed) | 1; \
        __asm__("lzcnt %1,%0" : "=r"(hist_bucket) : "r"(elapsed_nsecs)); \
        if (shard_id < 0) { \
            atomic64_add(1, (atomic64_t*)&cdc_latencies[latencies_idx + hist_bucket]); \
        } else { \
            atomic64_add(1, (atomic64_t*)&shard_latencies[latencies_idx + hist_bucket]); \
        } \
    });

    for (;;) {
        struct sockaddr_in addr;
        struct msghdr msg;

        if (unlikely(!eggsfs_shard_fill_msghdr(&msg, &addr, addr_data[which_shard_ip%2]))) {
            if (!eggsfs_shard_fill_msghdr(&msg, &addr, addr_data[0])) {
                eggsfs_warn("could not find any shard addresses! does everything look good in shuckle?");
                err = -EIO;
                goto out;
            }
        }
        which_shard_ip++;

        trace_eggsfs_metadata_request(&addr, req_id, len, shard_id, kind, *attempts, 0, EGGSFS_METADATA_REQUEST_ATTEMPT, 0); // which socket?
        init_completion(&req.comp);

        BUG_ON(req.skb);
        spin_lock_bh(&sock->lock);
        insert_shard_request(sock, &req);
        spin_unlock_bh(&sock->lock);

        timeout = min(max_timeout, (timeout * 3) / 2); // 1.5 exponential backoff
        eggsfs_debug("sending: " LOG_STR, LOG_ARGS)
        eggsfs_debug("sending to sock=%p iov=%p len=%u", sock->sock, p, len);
        err = kernel_sendmsg(sock->sock, &msg, &vec, 1, len);
        if (err < 0) {
            INC_STAT_COUNTER(NET_FAILURES)
            eggsfs_info("could not send: " LOG_STR " err=%d", LOG_ARGS, err);
            spin_lock_bh(&sock->lock);
            // TODO what are the circumstances where this can be true? That is, kernel_sendmsg returns
            // an error, but we get a fill from the shard?
            BUG_ON(req.skb);
            rb_erase(&req.node, &sock->requests);
            spin_unlock_bh(&sock->lock);
            elapsed = get_jiffies_64() - start_t;

            if (err == -ENETUNREACH && elapsed < overall_timeout) {
                eggsfs_warn(LOG_STR " got retryable net error when sending: %d", LOG_ARGS, err);
                msleep(timeout);
                continue;
            }
            goto out;
        }

        err = wait_for_request(sock, &req, timeout);
        u64 t = get_jiffies_64();
        elapsed = t - start_t;
        (*attempts)++;
        INC_STAT_COUNTER(ATTEMPTED)
        if (!err) {
            eggsfs_debug("got response");
            BUG_ON(!req.skb);
            // extract the the error for the benefit of logging
            int bincode_err = 0;
            if (req.skb->len >= (4 + 8 + 1)) {
                uint8_t kind;
                BUG_ON(skb_copy_bits(req.skb, 4 + 8, &kind, 1) != 0);
                if (kind == 0 && req.skb->len >= (4 + 8 + 1 + 2)) {
                    __le16 err;
                    BUG_ON(skb_copy_bits(req.skb, 4 + 8 + 1, &err, 2) != 0);
                    bincode_err = le16_to_cpu(err);
                }
            }
            if (bincode_err && eggsfs_unexpected_error(bincode_err)) {
                INC_STAT_COUNTER(FAILURES)
                eggsfs_warn("unexpected error: " LOG_STR " err_str=%s err=%d", LOG_ARGS, eggsfs_err_str(bincode_err), bincode_err);
            }
            WARN_LATE
            INC_STAT_COUNTER(COMPLETED)
            INC_LATENCY_BUCKET
            trace_eggsfs_metadata_request(&addr, req_id, len, shard_id, kind, *attempts, req.skb->len, EGGSFS_METADATA_REQUEST_DONE, bincode_err);
            return req.skb;
        }

        eggsfs_debug("err=%d", err);
        if (err != -ETIMEDOUT || elapsed >= overall_timeout) {
            if (err != -ERESTARTSYS) {
                INC_STAT_COUNTER(TIMEOUTS)
                eggsfs_warn("giving up (might be that too much time passed): " LOG_STR " overall_timeout=%ums err=%d", LOG_ARGS, overall_timeout, err);
            }
            goto out_err;
        } else {
            WARN_LATE
        }
    }

out_err:
    WARN_LATE
    INC_LATENCY_BUCKET

out:
    {
        struct sockaddr_in addr = {0};
        trace_eggsfs_metadata_request(&addr, req_id, len, shard_id, kind, *attempts, 0, EGGSFS_METADATA_REQUEST_DONE, err);
    }
    if (err != -ERESTARTSYS) {
        eggsfs_info("err=%d", err);
    }
    return ERR_PTR(err);

#undef LOG_STR
#undef LOG_ARGS
#undef WARN_LATE
#undef INC_STAT_COUNTER
#undef INC_LATENCY_BUCKET
}

#if 0

static void eggsfs_shard_async_request_worker(struct work_struct* work) {
    struct eggsfs_shard_async_request* req = container_of(to_delayed_work(work), struct eggsfs_shard_async_request, work);
    struct kvec vec;
    int err;

    eggsfs_debug();

    spin_lock_bh(&req->socket->lock);
    if (req->request.skb) { goto out_done; }
    if (req->request.attempt == 0) {
        insert_shard_request(req->socket, &req->request);
    } else if (req->request.attempt >= eggsfs_max_request_retries) {
        rb_erase(&req->request.node, &req->socket->requests);
        req->request.skb = ERR_PTR(-ETIMEDOUT);
        goto out_failed;
    }
    ++req->request.attempt;
    spin_unlock_bh(&req->socket->lock);

    vec.iov_base = req + 1;
    vec.iov_len = req->length;

    err = kernel_sendmsg(req->socket->sock, &req->msg, &vec, 1, req->length);
    if (err < 0) {
        spin_lock_bh(&req->socket->lock);
        if (!req->request.skb) {
            rb_erase(&req->request.node, &req->socket->requests);
            req->request.skb = ERR_PTR(err);
            goto out_failed;
        }
        spin_unlock_bh(&req->socket->lock);
    } else {
        queue_delayed_work(eggsfs_wq, &req->work, HZ);
    }

    return;

out_failed:
    spin_unlock_bh(&req->socket->lock);
    queue_work(system_wq, &req->callback);
    return;

out_done:
    spin_unlock_bh(&req->socket->lock);
    return;
}

// this layer needs to be capable of picking sockets / though we still need a list of them
void eggsfs_shard_async_request(struct eggsfs_shard_async_request* req, struct eggsfs_shard_socket* sock, struct msghdr* hdr, u64 req_id, u32 len) {
    eggsfs_debug();

    req->socket = sock;
    req->request.request_id = req_id;
    req->request.flags = EGGSFS_SHARD_ASYNC;
    req->request.skb = NULL;
    req->request.attempt = 0;
    req->length = len;
    req->msg = *hdr;
    INIT_DELAYED_WORK(&req->work, eggsfs_shard_async_request_worker);
    queue_delayed_work(eggsfs_wq, &req->work, 0);
}

void eggsfs_shard_async_cleanup(struct eggsfs_shard_async_request* req) {
    cancel_delayed_work_sync(&req->work);
}

#endif

/*
void eggsfs_shard_async_cancel(struct eggsfs_shard_async_request* req) {
    spin_lock_bh(&req->socket->lock);
    if (!req->request.skb) {
        rb_erase(&req.node, &sock->requests);
    }
    spin_unlock_bh(&req->socket->lock);
    cancel_delayed_work_sync(&req->work);
}
*/
