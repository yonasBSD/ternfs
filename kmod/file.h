#ifndef _EGGSFS_FILE_H
#define _EGGSFS_FILE_H

#include "inode.h"

ssize_t eggsfs_file_write(struct eggsfs_inode* enode, int flags, loff_t* ppos, struct iov_iter* from);
int eggsfs_file_flush(struct eggsfs_inode* enode, struct dentry* dentry);

void eggsfs_link_destructor(void*);
char* eggsfs_write_link(struct eggsfs_inode* enode);

int eggsfs_shard_add_span_initiate_block_cb(
    void* data, int block, u32 ip1, u16 port1, u32 ip2, u16 port2, u64 block_service_id, const u8* failure_domain, u64 block_id, u64 certificate
);

extern const struct file_operations eggsfs_file_operations;

int __init eggsfs_file_init(void);
void __cold eggsfs_file_exit(void);

#endif
