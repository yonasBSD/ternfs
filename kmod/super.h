#ifndef _EGGSFS_SUPER_H
#define _EGGSFS_SUPER_H

#include <linux/inet.h>

#include "net.h"

struct eggsfs_fs_info {
    struct eggsfs_shard_socket sock;

    // NB: ->msg_iter is used by `kernel_sendmsg`, so when you want to use the `struct msghhdr`
    // stored here you should copy it to some local structure, and then use it, otherwise
    // multiple users might step on each others' toes.

    struct sockaddr_in shards_addrs[256];
    struct msghdr shards_msghdrs[256];
    struct sockaddr_in cdc_addr;
    struct msghdr cdc_msghdr;
};

int __init eggsfs_fs_init(void);
void __cold eggsfs_fs_exit(void);

#endif

