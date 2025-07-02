#ifndef _EGGSFS_DENTRY_H
#define _EGGSFS_DENTRY_H

#include <linux/fs.h>

#include "counter.h"
#include "inode_compat.h"

EGGSFS_DECLARE_COUNTER(eggsfs_stat_dir_revalidations);

extern struct dentry_operations eggsfs_dentry_ops;

struct dentry* eggsfs_lookup(struct inode* dir, struct dentry* dentry, unsigned int flags);
int eggsfs_rmdir(struct inode* dir, struct dentry* dentry);
int eggsfs_unlink(struct inode* dir, struct dentry* dentry);
int COMPAT_FUNC_UNS(eggsfs_mkdir, struct inode* dir, struct dentry* dentry, umode_t mode);
int COMPAT_FUNC_UNS(
    eggsfs_rename,
    struct inode* old_dir, struct dentry* old_dentry,
    struct inode* new_dir, struct dentry* new_dentry,
    unsigned int flags
);

#endif /* _EGGSFS_DENTRY_H */
