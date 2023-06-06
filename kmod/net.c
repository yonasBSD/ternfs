#include "net.h"

#include <linux/version.h>

#include "log.h"
#include "trace.h"

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
    u64 request_id;

    read_lock_bh(&sk->sk_callback_lock);
    s = (struct eggsfs_shard_socket*)sk->sk_user_data;
    BUG_ON(!s);
    while (1) {
        skb = skb_recv_udp(sk, 0, 1, &err);
        if (!skb) {
            read_unlock_bh(&sk->sk_callback_lock);
            return;
        }
                              
        if (unlikely(skb_is_nonlinear(skb))) {
            // TODO: need top handle this
            eggsfs_warn("dropping nonlinear request");
            goto drop_skb;
        } else if (unlikely(skb_checksum_complete(skb))) {
            eggsfs_warn("dropping request with bad checksum");
            goto drop_skb;
        }

        // u32 protocol + u64 req_id
        if (unlikely(skb->len < 12)) {
            eggsfs_warn("dropping runt eggsfs request");
            goto drop_skb;
        }
        request_id = get_unaligned_le64(skb->data + 4);

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
            eggsfs_warn("could not find request id %llu", request_id);
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


static_assert(HZ / 100 > 0);

#if 0
#define SHARD_ATTEMPTS 10
const static u64 shard_timeouts_10ms[SHARD_ATTEMPTS] = {
    1, 2, 4, 8, 16, 32, 64, 100, 200, 200
};

// 5 times the shard numbers, given that we roughly do 5 shard requests per CDC request

#define CDC_ATTEMPTS 10
const static u64 cdc_timeouts_10ms[CDC_ATTEMPTS] = {
    5, 10, 20, 40, 80, 160, 320, 640, 1000, 1000
};
#endif

// higher timeouts since I'm testing in qemu with kasan

#define SHARD_ATTEMPTS 10
const static u64 shard_timeouts_10ms[SHARD_ATTEMPTS] = {
    5, 10, 20, 40, 80, 160, 320, 640, 1000, 1000
};

#define CDC_ATTEMPTS 10
const static u64 cdc_timeouts_10ms[CDC_ATTEMPTS] = {
    10, 20, 40, 80, 160, 320, 640, 1000, 1000, 1000
};

// Returns:
//
// * n == 0 for success
// * n < 0 for error (notable ones being -ETIMEOUT and -ERESTARTSYS)
//
// Once this is done, the request is guaranteed to not be in the tree anymore.
static int __must_check wait_for_request(struct eggsfs_shard_socket* s, struct eggsfs_shard_request* req, u64 timeout_10ms) {
    int ret = wait_for_completion_killable_timeout(&req->comp, (timeout_10ms * 10 * HZ) / 1000);
    // We got a response, this is the happy path.
    if (likely(ret > 0)) {
        BUG_ON(!req->skb);
        return 0;
    }
    // We didn't get a response, it's our job to clean up the request.
    spin_lock_bh(&s->lock);
    // We might have filled the request in the time it took us to get here, in which
    // case `sock_readable` will have already erased it.
    if (likely(!req->skb)) {
        rb_erase(&req->node, &s->requests);
    }
    spin_unlock_bh(&s->lock);
    // If the request was filled in the meantime, we also need to free the skb.
    if (unlikely(req->skb)) {
        kfree_skb(req->skb);
        req->skb = NULL;
    }
    return ret ? ret : -ETIMEDOUT;
}

void eggsfs_net_shard_free_socket(struct eggsfs_shard_socket* s) {
    // TODO anything else?
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
    s->sock->sk->sk_data_ready = sock_readable; // FIXME: keep around the default one
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

// shard_id is just used for debugging/event tracing, and should be -1 if we're going to the CDC
struct sk_buff* eggsfs_metadata_request(
    struct eggsfs_shard_socket* sock,
    s16 shard_id,
    struct msghdr* msg,
    u64 req_id,
    void* p,
    u32 len,
    u32* attempts
) {
    struct sockaddr_in* addr = (struct sockaddr_in*)msg->msg_name;
    struct eggsfs_shard_request req;
    struct kvec vec;
    int err = -EIO;

    u8 kind = *((u8*)p + 12);

    req.skb = NULL;
    req.request_id = req_id;
    *attempts = 0;

    vec.iov_base = p;
    vec.iov_len = len;

    int max_attempts = shard_id < 0 ? CDC_ATTEMPTS : SHARD_ATTEMPTS;
    const u64* timeouts_10ms = shard_id < 0 ? cdc_timeouts_10ms : shard_timeouts_10ms;

    do {
        trace_eggsfs_metadata_request(msg, req_id, len, shard_id, kind, *attempts, 0, EGGSFS_METADATA_REQUEST_ATTEMPT, 0); // which socket?
        init_completion(&req.comp);

        BUG_ON(req.skb);
        spin_lock_bh(&sock->lock);
        insert_shard_request(sock, &req);
        spin_unlock_bh(&sock->lock);

        eggsfs_debug("sending request of kind 0x%02x, len %d to %pI4:%d, after %d attempts", (int)kind, len, &addr->sin_addr, ntohs(addr->sin_port), *attempts);
        eggsfs_debug("sending to sock=%p iov=%p len=%u", sock->sock, p, len);
        err = kernel_sendmsg(sock->sock, msg, &vec, 1, len);
        if (err < 0) {
            eggsfs_info("could not send req %llu of kind 0x%02x to %pI4:%d: %d", req_id, (int)kind, &addr->sin_addr, ntohs(addr->sin_port), err);
            goto out_unregister;
        }

        err = wait_for_request(sock, &req, timeouts_10ms[*attempts]);
        (*attempts)++;
        if (!err) {
            eggsfs_debug("got response");
            BUG_ON(!req.skb);
            // extract the the error for the benefit of the tracing
            int trace_err = 0;
            if (req.skb->len >= (4 + 8 + 1)) {
                uint8_t kind = *(u8*)(req.skb->data + 4 + 8);
                if (kind == 0 && req.skb->len >= (4 + 8 + 1 + 2)) {
                    trace_err = get_unaligned_le16(req.skb->data + 4 + 8 + 1);
                }
            }
            trace_eggsfs_metadata_request(msg, req_id, len, shard_id, kind, *attempts, req.skb->len, EGGSFS_METADATA_REQUEST_DONE, trace_err);
            return req.skb;
        }

        eggsfs_debug("err=%d", err);
        if (err != -ETIMEDOUT || *attempts >= max_attempts) {
            eggsfs_info("giving up after %d attempts due to err %d", *attempts, err);
            goto out_err;
        }
    } while (1);

out_unregister:
    spin_lock_bh(&sock->lock);
    // TODO what are the circumstances where this can be true? That is, kernel_sendmsg returns
    // an error, but we get a fill from the shard?
    BUG_ON(req.skb);
    rb_erase(&req.node, &sock->requests);
    spin_unlock_bh(&sock->lock);

out_err:
    trace_eggsfs_metadata_request(msg, req_id, len, shard_id, kind, *attempts, 0, EGGSFS_METADATA_REQUEST_DONE, err);
    eggsfs_info("err=%d", err);
    return ERR_PTR(err);
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
