#ifndef _EGGSFS_DIR_H
#define _EGGSFS_DIR_H

#include <linux/fs.h>

#include "inode.h"
#include "log.h"

extern struct file_operations eggsfs_dir_operations;

extern int eggsfs_dir_refresh_time_jiffies;

int eggsfs_dir_readdir_entry_cb(void* ptr, const char* name, int name_len, u64 hash, u64 edge_creation_time, u64 ino);

void eggsfs_dir_drop_cache(struct eggsfs_inode* enode);

static inline int eggsfs_dir_revalidate(struct eggsfs_inode* enode) {
    return eggsfs_do_getattr(enode);
}

static inline int eggsfs_dir_needs_reval(struct eggsfs_inode* dir, struct dentry* dentry) {
    // 0 => invalid
    // -ECHILD => revalidate parent dir
    // 1 => valid
    u64 dentry_dir_mtime = dentry->d_time;
    int ret;
    u64 t = get_jiffies_64();
    if (dentry_dir_mtime != dir->mtime) { ret = 0; }
    else if (t >= dir->mtime_expiry) { ret = -ECHILD; }
    else { ret = 1; }

    eggsfs_debug(
        "t=%llu dir=%p dir_id=0x%016lx dir_mtime=%llu dir_mtime_expiry=%llu dentry=%pd dentry_dir_mtime=%llu -> ret=%d",
        t, dir, dir->inode.i_ino, dir->mtime, dir->mtime_expiry, dentry, dentry_dir_mtime, ret
    );

    return ret;
}

int __init eggsfs_dir_init(void);
void __cold eggsfs_dir_exit(void);

#endif

