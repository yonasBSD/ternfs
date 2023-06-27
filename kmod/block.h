#ifndef _EGGSFS_BLOCK_H
#define _EGGSFS_BLOCK_H

#include <linux/kernel.h>
#include <linux/completion.h>
#include <linux/net.h>
#include <net/tcp.h>

#include "bincode.h"
#include "inode.h"

#define EGGSFS_MAX_BLOCK_SIZE (100 << 20) // 100MiB

#define EGGSFS_BLOCKS_RESP_HEADER_SIZE (4 + 1) // protocol + kind

struct eggsfs_block_service {
    u64 id;
    u32 ip1;
    u32 ip2;
    u16 port1;
    u16 port2;
    u8 flags;
    u8 failure_domain[16];
};

// Calls `eggsfs_fetch_block_complete_cb` when done.
// Returns an error immediately if it can't connect to the block service or anyway
// if it thinks the block service is no good.
//
// It's _very_ important that you remember to get a reference to whatever
// the callback needs! E.g. ihold on the inode.
int eggsfs_fetch_block(
    void (*callback)(void* data, u64 block_id, struct list_head* pages, int err),
    void* data,
    struct eggsfs_block_service* bs,
    u64 block_id,
    u32 offset,
    u32 count
);

// Will call back into `eggsfs_write_block_complete_cb` when done.
// Returns an error immediately if it can't connect to the block service or anyway
// if it thinks the block service is no good.
int eggsfs_write_block(
    void (*callback)(void* data, u64 block_id, u64 proof, int err),
    void* data,
    struct eggsfs_block_service* bs,
    u64 block_id,
    u64 certificate,
    u32 size,
    u32 crc,
    // There must be enough pages to write everything.
    // After this call, ownership of these pages is passed onto the block
    // writing (if the call succeeds)
    struct list_head* pages
);

int __init eggsfs_block_init(void);
void __cold eggsfs_block_exit(void);

#endif
