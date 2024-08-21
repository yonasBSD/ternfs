#ifndef _EGGSFS_SUPER_H
#define _EGGSFS_SUPER_H

#include <linux/inet.h>

#include "net.h"

extern int eggsfs_shuckle_refresh_time_jiffies;
extern unsigned int eggsfs_readahead_pages;

// We store addresses as atomics so that we can
// easily refresh them.
struct eggsfs_fs_info {
    atomic64_t shuckle_addr1;
    atomic64_t shuckle_addr2;

    struct eggsfs_metadata_socket sock;

    atomic64_t shard_addrs1[256];
    atomic64_t shard_addrs2[256];
    atomic64_t cdc_addr1;
    atomic64_t cdc_addr2;

    u64 block_services_last_changed_time;

    struct delayed_work shuckle_refresh_work;

    kuid_t uid;
    kgid_t gid;
    umode_t fmask;
    umode_t dmask;
};

int __init eggsfs_fs_init(void);
void __cold eggsfs_fs_exit(void);

static inline u64 eggsfs_mk_addr(u32 ip, u16 port) {
    return ((u64)port << 32) | (u64)ip;
}

static inline __be32 eggsfs_get_addr_ip(u64 v) {
    return htonl(v&((1ull<<32)-1));
}
static inline __be16 eggsfs_get_addr_port(u64 v) {
    return htons(v >> 32);
}

#endif

