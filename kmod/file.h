#ifndef _EGGSFS_FILE_H
#define _EGGSFS_FILE_H

#include "inode.h"

extern unsigned eggsfs_max_write_span_attempts;
extern int eggsfs_file_refresh_time_jiffies; // this is only relevant for mtime/atime updates

ssize_t eggsfs_file_write(struct eggsfs_inode* enode, int flags, loff_t* ppos, struct iov_iter* from);
int eggsfs_file_flush(struct eggsfs_inode* enode, struct dentry* dentry);

void eggsfs_link_destructor(void*);
char* eggsfs_read_link(struct eggsfs_inode* enode);

extern const struct file_operations eggsfs_file_operations;

int __init eggsfs_file_init(void);
void __cold eggsfs_file_exit(void);

#endif
