#ifndef _EGGSFS_NAMEI_H
#define _EGGSFS_NAMEI_H

#include <linux/fs.h>

#include "counter.h"

EGGSFS_DECLARE_COUNTER(eggsfs_stat_dir_revalidations);

struct dentry* eggsfs_lookup(struct inode* dir, struct dentry* dentry, unsigned int flags);
int eggsfs_mkdir(struct inode* dir, struct dentry* dentry, umode_t mode);
int eggsfs_rmdir(struct inode* dir, struct dentry* dentry);
int eggsfs_unlink(struct inode* dir, struct dentry* dentry);
int eggsfs_rename(
    struct inode* old_dir, struct dentry* old_dentry,
    struct inode* new_dir, struct dentry* new_dentry,
    unsigned int flags
);

extern struct dentry_operations eggsfs_dentry_ops;

#endif
