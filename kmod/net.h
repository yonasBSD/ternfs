#ifndef _EGGSFS_NET_H
#define _EGGSFS_NET_H

#include <linux/net.h>
#include <net/udp.h>
#include <net/sock.h>
#include <net/inet_common.h>

#define EGGSFS_UDP_MTU 1472

extern unsigned eggsfs_initial_shard_timeout_jiffies;
extern unsigned eggsfs_max_shard_timeout_jiffies;
extern unsigned eggsfs_overall_shard_timeout_jiffies;
extern unsigned eggsfs_initial_cdc_timeout_jiffies;
extern unsigned eggsfs_max_cdc_timeout_jiffies;
extern unsigned eggsfs_overall_cdc_timeout_jiffies;

struct eggsfs_shard_socket {
    struct socket* sock;
    struct rb_root requests;
    spinlock_t lock;
};

#define EGGSFS_SHARD_ASYNC 1

struct eggsfs_shard_request {
    struct rb_node node;
    u64 request_id;
    struct sk_buff* skb;
    struct completion comp;
};

int eggsfs_init_shard_socket(struct eggsfs_shard_socket* sock);
void eggsfs_net_shard_free_socket(struct eggsfs_shard_socket* sock);

struct sk_buff* eggsfs_metadata_request(
    struct eggsfs_shard_socket* sock, s16 shard_id, struct msghdr* msg, u64 req_id, void* p, u32 len, u32* attempts
);

struct eggsfs_shard_async_request {
    struct eggsfs_shard_request request;
    struct eggsfs_shard_socket* socket;
    struct msghdr msg;
    struct delayed_work work;
    struct work_struct callback;
    u32 length;
};

#if 0

void eggsfs_shard_async_request(struct eggsfs_shard_async_request* req, struct eggsfs_shard_socket* sock, struct msghdr* msg, u64 req_id, u32 len);
void eggsfs_shard_async_cleanup(struct eggsfs_shard_async_request* req);

#endif

#endif
