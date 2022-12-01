#ifndef _EGGSFS_FILESIMPLE_H
#define _EGGSFS_FILESIMPLE_H

#include <linux/list.h>
#include <linux/workqueue.h>

// Must be called with `spans_lock` taken.
struct eggsfs_transient_span* eggsfs_add_new_span(struct list_head* spans);

void eggsfs_flush_transient_spans(struct work_struct* work);

int eggsfs_shard_add_span_initiate_block_cb(
    void* data, int block, u32 ip1, u16 port1, u32 ip2, u16 port2, u64 block_service_id, u64 block_id, u64 certificate
);

extern const struct file_operations eggsfs_filesimple_operations;

#endif
