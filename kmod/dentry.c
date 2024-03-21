#include "dentry.h"

#include "log.h"
#include "dir.h"
#include "err.h"
#include "inode.h"
#include "metadata.h"
#include "trace.h"

EGGSFS_DEFINE_COUNTER(eggsfs_stat_dir_revalidations);

static int eggsfs_d_revalidate(struct dentry* dentry, unsigned int flags) {
    struct dentry* parent;
    struct inode* dir;
    int ret;

    eggsfs_debug("dentry=%pd flags=%x[rcu=%d]", dentry, flags, !!(flags & LOOKUP_RCU));

    if (IS_ROOT(dentry)) { return 1; }

    if (dentry->d_name.len > EGGSFS_MAX_FILENAME) {
        return -ENAMETOOLONG;
    }

    if (flags & LOOKUP_RCU) {
        parent = READ_ONCE(dentry->d_parent);
        dir = d_inode_rcu(parent);
        if (!dir) { return -ECHILD; }
        ret = eggsfs_dir_needs_reval(EGGSFS_I(dir), dentry);
        if (parent != READ_ONCE(dentry->d_parent)) { return -ECHILD; }
        return ret;
    } else {
        parent = dget_parent(dentry);
        dir = d_inode(parent);
        ret = eggsfs_dir_needs_reval(EGGSFS_I(dir), dentry);
        if (ret == -ECHILD) {
            eggsfs_counter_inc(eggsfs_stat_dir_revalidations);
            ret = eggsfs_dir_revalidate(EGGSFS_I(dir));
            if (ret) { goto out; }
            ret = eggsfs_dir_needs_reval(EGGSFS_I(dir), dentry);
            if (ret == -ECHILD) { ret = 1; }
        }
out:
        dput(parent);
    }
    return ret;
}

struct dentry_operations eggsfs_dentry_ops = {
    .d_revalidate = eggsfs_d_revalidate,
};


static void eggsfs_dcache_handle_error(struct inode* dir, struct dentry* dentry, int err) {
    eggsfs_debug("err=%d", err);
    switch (err) {
    case EGGSFS_ERR_CANNOT_OVERRIDE_NAME:
        WARN_ON(dentry->d_inode);
        trace_eggsfs_dcache_invalidate_neg_entry(dir, dentry);
        WRITE_ONCE(EGGSFS_I(dir)->dir.mtime_expiry, 0);
        d_invalidate(dentry);
        break;
    // TODO is this correct? verify, I've changed the code from an earlier error
    // which does not exist anymore.
    case EGGSFS_ERR_DIRECTORY_NOT_FOUND:
        WARN_ON(dentry->d_inode);
        trace_eggsfs_dcache_delete_inode(dir);
        WRITE_ONCE(EGGSFS_I(dir)->dir.mtime_expiry, 0);
        clear_nlink(dir);
        d_invalidate(dentry->d_parent);
        break;
    case EGGSFS_ERR_DIRECTORY_NOT_EMPTY:
        WARN_ON(!dentry->d_inode);
        if (dentry->d_inode) {
            trace_eggsfs_dcache_invalidate_dir(dentry->d_inode);
            WRITE_ONCE(EGGSFS_I(dentry->d_inode)->dir.mtime_expiry, 0);
        }
        break;
    case EGGSFS_ERR_NAME_NOT_FOUND:
        WARN_ON(!dentry->d_inode);
        if (dentry->d_inode) {
            trace_eggsfs_dcache_delete_entry(dir, dentry, dentry->d_inode);
            WRITE_ONCE(EGGSFS_I(dentry->d_inode)->dir.mtime_expiry, 0);
            clear_nlink(dentry->d_inode);
            d_invalidate(dentry);
        }
        break;
    case EGGSFS_ERR_MISMATCHING_TARGET:
    case EGGSFS_ERR_MISMATCHING_CREATION_TIME:
        WARN_ON(!dentry->d_inode);
        if (dentry->d_inode) {
            trace_eggsfs_dcache_invalidate_entry(dir, dentry, dentry->d_inode);
            WRITE_ONCE(EGGSFS_I(dentry->d_inode)->dir.mtime_expiry, 0);
            d_invalidate(dentry);
        }
        break;
    }
}

// vfs: shared dir.i_rwsem
struct dentry* eggsfs_lookup(struct inode* dir, struct dentry* dentry, unsigned int flags) {
    eggsfs_debug("dir=0x%016lx, name=%*pE", dir->i_ino, dentry->d_name.len, dentry->d_name.name);
    int err;

    trace_eggsfs_vfs_lookup_enter(dir, dentry);

    if (dentry->d_name.len > EGGSFS_MAX_FILENAME) {
        err = -ENAMETOOLONG;
        goto out_err;
    }

    u64 ino, creation_time;
    // need to keep the eggs error around for the EGGSFS_ERR_NAME_NOT_FOUND
    err = eggsfs_shard_lookup(
        dir->i_sb->s_fs_info, dir->i_ino, dentry->d_name.name, dentry->d_name.len, &ino, &creation_time
    );
    if (unlikely(err < 0)) {
        goto out_err;
    }
    if (err) { // not unlikely since vfs will do lookups for entries likely to not exist
        if (err == EGGSFS_ERR_NAME_NOT_FOUND) {
            dentry->d_time = EGGSFS_I(dir)->mtime;
            trace_eggsfs_vfs_lookup_exit(dir, dentry, NULL, 0);
            return d_splice_alias(NULL, dentry);
        }
        err = eggsfs_error_to_linux(err);
        goto out_err;
    }

    struct inode* inode = eggsfs_get_inode_normal(dentry->d_sb, EGGSFS_I(dir), ino);
    if (IS_ERR(inode)) {
        err = PTR_ERR(inode);
        goto out_err;
    }
    inode->i_ino = ino;
    EGGSFS_I(inode)->edge_creation_time = creation_time;

    dentry->d_time = EGGSFS_I(dir)->mtime;

    trace_eggsfs_vfs_lookup_exit(dir, dentry, inode, 0);
    return d_splice_alias(inode, dentry);

out_err:
    trace_eggsfs_vfs_lookup_exit(dir, dentry, NULL, err);
    return ERR_PTR(err);
}

// vfs: exclusive dir.i_rwsem, lockref dentry
int eggsfs_mkdir(struct inode* dir, struct dentry* dentry, umode_t mode) {
    int err;

    struct eggsfs_inode* dir_enode = EGGSFS_I(dir);

    trace_eggsfs_vfs_mkdir_enter(dir, dentry);
    BUG_ON(dentry->d_inode);

    if (dentry->d_name.len > EGGSFS_MAX_FILENAME) {
        err = -ENAMETOOLONG;
        goto out_err;
    }

    u64 ino, creation_time;
    err = eggsfs_cdc_mkdir(
        dir->i_sb->s_fs_info,
        dir->i_ino,
        dentry->d_name.name,
        dentry->d_name.len,
        &ino,
        &creation_time
    );
    if (unlikely(err)) {
        if (err < 0) { goto out_err; }
        eggsfs_dcache_handle_error(dir, dentry, err);
        err = eggsfs_error_to_linux(err);
        goto out_err;
    }
    eggsfs_dir_drop_cache(EGGSFS_I(dir));

    struct inode* inode = eggsfs_get_inode_normal(dentry->d_sb, EGGSFS_I(dir), ino);
    if (IS_ERR(inode)) { err = PTR_ERR(inode); goto out_err; }
    struct eggsfs_inode* enode = EGGSFS_I(inode);
    enode->edge_creation_time = creation_time;
    dentry->d_time = dir_enode->mtime;
    d_instantiate(dentry, inode);

    trace_eggsfs_vfs_mkdir_exit(dir, dentry, inode, 0);
    return 0;

out_err:
    trace_eggsfs_vfs_mkdir_exit(dir, dentry, NULL, err);
    return err;
}

// vfs: exclusive dir,d_entry->d_inode.i_rwsem
int eggsfs_rmdir(struct inode* dir, struct dentry* dentry) {
    int err;

    trace_eggsfs_vfs_rmdir_enter(dir, dentry, dentry->d_inode);
    BUG_ON(!dentry->d_inode);

    if (unlikely(dentry->d_name.len > EGGSFS_MAX_FILENAME)) {
        pr_err("eggsfs: eggsfs_rmdir dir=%lu dentry=%pd exceed max filename length %d\n", dir->i_ino, dentry, EGGSFS_MAX_FILENAME);
        err = -ENAMETOOLONG;
        goto out_err;
    }

    err = eggsfs_cdc_rmdir((struct eggsfs_fs_info*)dir->i_sb->s_fs_info, dir->i_ino, dentry->d_inode->i_ino, EGGSFS_I(dentry->d_inode)->edge_creation_time, dentry->d_name.name, dentry->d_name.len);
    if (unlikely(err)) {
        if (err < 0) { goto out_err; }
        eggsfs_dcache_handle_error(dir, dentry, err);
        err = eggsfs_error_to_linux(err);
        goto out_err;
    } else {
        eggsfs_dir_drop_cache(EGGSFS_I(dir));
        clear_nlink(dentry->d_inode);
    }

out_err:
    trace_eggsfs_vfs_rmdir_exit(dir, dentry, err);
    return err;
}

// vfs: exclusive dir,d_entry->d_inode.i_rwsem
int eggsfs_unlink(struct inode* dir, struct dentry* dentry) {
    int err;

    trace_eggsfs_vfs_unlink_enter(dir, dentry, dentry->d_inode);
    BUG_ON(!dentry->d_inode);

    if (unlikely(dentry->d_name.len > EGGSFS_MAX_FILENAME)) {
        pr_err("eggsfs: eggsfs_unlink dir=%lu dentry=%pd exceed max filename length %d\n", dir->i_ino, dentry, EGGSFS_MAX_FILENAME);
        err = -ENAMETOOLONG;
        goto out_err;
    }

    struct eggsfs_inode* enode = EGGSFS_I(dentry->d_inode);
    u64 delete_creation_time;
    err = eggsfs_shard_soft_unlink_file(
        (struct eggsfs_fs_info*)dir->i_sb->s_fs_info,
        dir->i_ino,
        dentry->d_inode->i_ino,
        dentry->d_name.name,
        dentry->d_name.len,
        enode->edge_creation_time,
        &delete_creation_time
    );
    if (unlikely(err)) {
        if (err < 0) { goto out_err; }
        eggsfs_dcache_handle_error(dir, dentry, err);
        err = eggsfs_error_to_linux(err);
        goto out_err;
    } else {
        eggsfs_dir_drop_cache(EGGSFS_I(dir));
        // no hard links, inode is gone
        clear_nlink(dentry->d_inode);
    }

out_err:
    trace_eggsfs_vfs_unlink_exit(dir, dentry, err);
    return err;
}

// vfs: exclusive i_rwsem for all
int eggsfs_rename(struct inode* old_dir, struct dentry* old_dentry, struct inode* new_dir, struct dentry* new_dentry, unsigned int flags) {
    int err;

    trace_eggsfs_vfs_rename_enter(old_dir, old_dentry, new_dir, new_dentry);

    if (!old_dentry->d_inode) { // TODO can this ever happen?
        eggsfs_warn("got no inodes for old dentry!");
        return -EIO;
    }
    struct eggsfs_inode* enode = EGGSFS_I(old_dentry->d_inode);
    u64 ino = enode->inode.i_ino;

    if (flags) { err = -EINVAL; goto out; }

    u64 old_creation_time = enode->edge_creation_time;
    u64 new_creation_time;
    struct eggsfs_fs_info* info = old_dir->i_sb->s_fs_info;
    if (old_dir == new_dir) {
        err = eggsfs_error_to_linux(eggsfs_shard_rename(
            info,
            old_dir->i_ino,
            ino, old_dentry->d_name.name, old_dentry->d_name.len,
            old_creation_time,
            new_dentry->d_name.name, new_dentry->d_name.len,
            &new_creation_time
        ));
    } else if (S_ISDIR(old_dentry->d_inode->i_mode)) {
        err = eggsfs_error_to_linux(eggsfs_cdc_rename_directory(
            info,
            ino,
            old_dir->i_ino, new_dir->i_ino,
            old_dentry->d_name.name, old_dentry->d_name.len, old_creation_time,
            new_dentry->d_name.name, new_dentry->d_name.len,
            &new_creation_time
        ));
    } else {
        err = eggsfs_error_to_linux(eggsfs_cdc_rename_file(
            info,
            ino,
            old_dir->i_ino, new_dir->i_ino,
            old_dentry->d_name.name, old_dentry->d_name.len, old_creation_time,
            new_dentry->d_name.name, new_dentry->d_name.len,
            &new_creation_time
        ));
    }
    // TODO do we need to drop the dentries in case of error? The old code
    // thought so, but I'm not sure.
    if (!err) {
        old_dentry->d_time = new_creation_time; // used by d_revalidate
        enode->edge_creation_time = new_creation_time;
        struct eggsfs_inode* new_edir = EGGSFS_I(new_dir);
        enode->block_policy = new_edir->block_policy;
        enode->span_policy = new_edir->span_policy;
        enode->stripe_policy = new_edir->stripe_policy;
        d_move(old_dentry, new_dentry);
    }

out:
    trace_eggsfs_vfs_rename_exit(old_dir, old_dentry, new_dir, new_dentry, err);
    eggsfs_debug("err=%d", err);
    return err;
}
