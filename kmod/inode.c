#include "inode.h"

#include <linux/sched/mm.h>

#include "log.h"
#include "dir.h"
#include "metadata.h"
#include "dentry.h"
#include "trace.h"
#include "err.h"
#include "file.h"
#include "wq.h"
#include "span.h"

static struct kmem_cache* eggsfs_inode_cachep;

struct inode* eggsfs_inode_alloc(struct super_block* sb) {
    struct eggsfs_inode* enode;

    eggsfs_debug("sb=%p", sb);

    enode = (struct eggsfs_inode*)kmem_cache_alloc(eggsfs_inode_cachep, GFP_NOFS);
    if (!enode) {
        return NULL;
    }

    enode->mtime = 0;
    enode->mtime_expiry = 0;
    enode->edge_creation_time = 0;
    eggsfs_latch_init(&enode->getattr_update_latch);

    eggsfs_debug("done enode=%p", enode);
    return &enode->inode;
}

void eggsfs_inode_evict(struct inode* inode) {
    struct eggsfs_inode* enode = EGGSFS_I(inode);
    eggsfs_debug("enode=%p", enode);
    if (S_ISDIR(inode->i_mode)) {
        eggsfs_dir_drop_cache(enode);
    } else if (S_ISREG(inode->i_mode)) {
        eggsfs_drop_file_spans(enode);
    }
    truncate_inode_pages(&inode->i_data, 0);
    clear_inode(inode);
}

void eggsfs_inode_free(struct inode* inode) {
    // We need to clear the spans
    struct eggsfs_inode* enode = EGGSFS_I(inode);
    eggsfs_debug("enode=%p", enode);
    kmem_cache_free(eggsfs_inode_cachep, enode);
}

static void eggsfs_inode_init_once(void* ptr) {
    struct eggsfs_inode* enode = (struct eggsfs_inode*)ptr;

    inode_init_once(&enode->inode);
}

int __init eggsfs_inode_init(void) {
    eggsfs_inode_cachep = kmem_cache_create(
        "eggsfs_inode_cache", sizeof(struct eggsfs_inode),
        0, SLAB_RECLAIM_ACCOUNT | SLAB_MEM_SPREAD, eggsfs_inode_init_once
    );
    if (!eggsfs_inode_cachep) { return -ENOMEM; }

    return 0;
}

void __cold eggsfs_inode_exit(void) {
    eggsfs_debug("inode exit");
    rcu_barrier();
    kmem_cache_destroy(eggsfs_inode_cachep);
}


int eggsfs_do_getattr(struct eggsfs_inode* enode) {
    int err;
    s64 seqno;

again: // progress: whoever wins the lock won't try again
    eggsfs_debug("enode=%p id=0x%016lx mtime=%lld mtime_expiry=%lld", enode, enode->inode.i_ino, enode->mtime, enode->mtime_expiry);

    if (eggsfs_latch_try_acquire(&enode->getattr_update_latch, seqno)) {
        u64 ts = get_jiffies_64();
        if (smp_load_acquire(&enode->mtime_expiry) > ts) { err = 0; goto out; }

        u64 mtime;
        u64 expiry;
        if (S_ISDIR(enode->inode.i_mode)) {
            struct eggsfs_block_policies block_policies;
            struct eggsfs_span_policies span_policies;
            u32 target_stripe_size;
            err = eggsfs_shard_getattr_dir(
                (struct eggsfs_fs_info*)enode->inode.i_sb->s_fs_info,
                enode->inode.i_ino,
                &mtime,
                &block_policies,
                &span_policies,
                &target_stripe_size
            );
            if (err == 0) {
                if (block_policies.len) {
                    memcpy(&enode->block_policies, &block_policies, sizeof(block_policies));
                }
                if (span_policies.len) {
                    memcpy(&enode->span_policies, &span_policies, sizeof(span_policies));
                }
                if (target_stripe_size) {
                    enode->target_stripe_size = target_stripe_size;
                }
                expiry = ts + eggsfs_dir_refresh_time_jiffies;
            }
        } else {
            // Make it so that this function is only called once. Afterwards
            // we always keep this up to date internally, until the file is
            // linked in which case it never changes size nor mtime TODO
            // this will change soon as we add utime.
            if (enode->file.status == EGGSFS_FILE_STATUS_READING || enode->file.status == EGGSFS_FILE_STATUS_NONE) {
                eggsfs_debug("updating getattr for reading file");
                u64 size;
                err = eggsfs_shard_getattr_file(
                    (struct eggsfs_fs_info*)enode->inode.i_sb->s_fs_info, 
                    enode->inode.i_ino,
                    &mtime,
                    &size
                );
                if (err == EGGSFS_ERR_FILE_NOT_FOUND && enode->file.status == EGGSFS_FILE_STATUS_NONE) { // probably just created
                    enode->inode.i_size = 0;
                    expiry = 0;
                    mtime = 0;
                } else if (err == 0) {
                    enode->inode.i_size = size;
                    expiry = ~(uint64_t)0;
                }
            } else {
                BUG_ON(enode->file.status != EGGSFS_FILE_STATUS_WRITING);
                expiry = ~(uint64_t)0; // we take care of this from now on
                enode->inode.i_size = 0;
                mtime = 0;
            }
        }
        if (err) { err = eggsfs_error_to_linux(err); goto out; }
        else {
            WRITE_ONCE(enode->mtime, mtime);
            enode->inode.i_mtime.tv_sec = mtime / 1000000000;
            enode->inode.i_mtime.tv_nsec = mtime % 1000000000;
        }

        smp_store_release(&enode->mtime_expiry, expiry);

out:
        WRITE_ONCE(enode->getattr_err, err);
        eggsfs_latch_release(&enode->getattr_update_latch, seqno);
        eggsfs_debug("out mtime=%llu mtime_expiry=%llu", enode->mtime, enode->mtime_expiry);
        return err;
    } else {
        err = eggsfs_latch_wait_killable(&enode->getattr_update_latch, seqno);
        if (err) { return err; }
        if (READ_ONCE(enode->getattr_err)) { goto again; }
        return 0;
    }
}

static struct eggsfs_inode* eggsfs_create_internal(struct inode* parent, int itype, struct dentry* dentry) {
    int err;

    BUG_ON(dentry->d_inode);

    struct eggsfs_inode* parent_enode = EGGSFS_I(parent);

    if (dentry->d_name.len > EGGSFS_MAX_FILENAME) { return ERR_PTR(-ENAMETOOLONG); }

    eggsfs_debug("creating %*s in %016lx", dentry->d_name.len, dentry->d_name.name, parent->i_ino);

    u64 ino, cookie;
    err = eggsfs_error_to_linux(eggsfs_shard_create_file(
        (struct eggsfs_fs_info*)parent->i_sb->s_fs_info, eggsfs_inode_shard(parent->i_ino),
        itype, dentry->d_name.name, dentry->d_name.len, &ino, &cookie
    ));
    if (err) { return ERR_PTR(eggsfs_error_to_linux(err)); }
    // make this dentry stick around?

    struct inode* inode = eggsfs_get_inode(parent->i_sb, parent_enode, ino); // new_inode
    if (IS_ERR(inode)) { return ERR_PTR(PTR_ERR(inode)); }
    struct eggsfs_inode* enode = EGGSFS_I(inode);

    enode->file.status = EGGSFS_FILE_STATUS_WRITING;

    // Initialize all the transient specific fields
    enode->file.cookie = cookie;
    atomic_set(&enode->file.transient_err, 0);
    enode->file.writing_span = NULL;
    sema_init(&enode->file.flushing_span_sema, 1); // 1 = free to flush
    enode->file.owner = current->group_leader;
    enode->file.mm = current->group_leader->mm;
    // We might need the mm beyond the file lifetime, to clear up block write pages MM_FILEPAGES
    mmgrab(enode->file.mm);

    return enode;
}

static int eggsfs_create(struct inode* parent, struct dentry* dentry, umode_t mode, bool excl) {
    struct eggsfs_inode* enode = eggsfs_create_internal(parent, EGGSFS_INODE_FILE, dentry);
    if (IS_ERR(enode)) { return PTR_ERR(enode); }

    // This is not valid yet, we need to fill it in first
    d_instantiate(dentry, &enode->inode);
    d_invalidate(dentry);

    return 0;
}

// vfs: refcount of path->dentry
static int eggsfs_getattr(const struct path* path, struct kstat* stat, u32 request_mask, unsigned int query_flags) {
    struct inode* inode = d_inode(path->dentry);
    struct eggsfs_inode* enode = EGGSFS_I(inode);

    trace_eggsfs_vfs_getattr_enter(inode);
    
    // >= so that eggsfs_dir_refresh_time=0 causes revalidation at every call to this function
    if (get_jiffies_64() >= smp_load_acquire(&enode->mtime_expiry)) {
        int err;

        // FIXME: symlinks
        if (!(request_mask & STATX_MTIME) && (S_ISDIR(inode->i_mode) || !(request_mask & STATX_SIZE))) {
            goto done;
        }

        trace_eggsfs_vfs_getattr_lock(inode);

        // dentry refcount also protects the inode (e.g. d_delete will not turn used dentry into a negative one),
        // so no need to grab anything before we start waiting for stuff
        err = eggsfs_do_getattr(enode);
        if (err) {
            trace_eggsfs_vfs_getattr_exit(inode, err);
            return err;
        }
    }

done:
    generic_fillattr(inode, stat);
    trace_eggsfs_vfs_getattr_exit(inode, 0);
    return 0;
}

static int eggsfs_symlink(struct inode* dir, struct dentry* dentry, const char* path) {
    struct eggsfs_inode* enode = eggsfs_create_internal(dir, EGGSFS_INODE_SYMLINK, dentry);
    if (IS_ERR(enode)) { return PTR_ERR(enode); }

    enode->file.status = EGGSFS_FILE_STATUS_WRITING; // needed for flush to flush below

    size_t len = strlen(path);

    // We now need to write out the symlink contents...
    loff_t ppos = 0;
    struct kvec vec;
    struct iov_iter from;
    vec.iov_base = (void*)path;
    vec.iov_len = len;
    iov_iter_kvec(&from, READ, &vec, 1, vec.iov_len);
    inode_lock(&enode->inode);
    int err = eggsfs_file_write(enode, 0, &ppos, &from);
    inode_unlock(&enode->inode);
    if (err < 0) { return err; }
    // ...and flush them
    err = eggsfs_file_flush(enode, dentry);
    if (err < 0) { return err; }

    // Now we link the dentry in
    d_instantiate(dentry, &enode->inode);

    return 0;
}

static const char* eggsfs_get_link(struct dentry* dentry, struct inode* inode, struct delayed_call* destructor) {
    // Can't be bothered to think about RCU
    if (dentry == NULL) { return ERR_PTR(-ECHILD); }

    struct eggsfs_inode* enode = EGGSFS_I(inode);
    char* buf = eggsfs_read_link(enode);

    if (IS_ERR(buf)) { return buf; }

    destructor->fn = eggsfs_link_destructor;
    destructor->arg = buf;

    return buf;
}

static const struct inode_operations eggsfs_dir_inode_ops = {
    .create = eggsfs_create,
    .lookup = eggsfs_lookup,
    .unlink = eggsfs_unlink,
    .mkdir = eggsfs_mkdir,
    .rmdir = eggsfs_rmdir,
    .rename = eggsfs_rename,
    .getattr = eggsfs_getattr,
    .symlink = eggsfs_symlink,
};

static const struct inode_operations eggsfs_file_inode_ops = {
    .getattr = eggsfs_getattr,
};

static const struct inode_operations eggsfs_symlink_inode_ops = {
    .getattr = eggsfs_getattr,
    .get_link = eggsfs_get_link,
};

extern struct file_operations eggsfs_dir_operations;

struct inode* eggsfs_get_inode(struct super_block* sb, struct eggsfs_inode* parent, u64 ino) {
    trace_eggsfs_get_inode_enter(ino);

    struct inode* inode = iget_locked(sb, ino); // new_inode when possible?
    if (!inode) {
        trace_eggsfs_get_inode_exit(ino, NULL, false, 0);
        return ERR_PTR(-ENOMEM);
    }
    struct eggsfs_inode* enode = EGGSFS_I(inode);

    bool new = inode->i_state & I_NEW;
    if (new) {
        switch (eggsfs_inode_type(ino)) {
        case EGGSFS_INODE_DIRECTORY: inode->i_mode = S_IFDIR; break;
        case EGGSFS_INODE_FILE: inode->i_mode = S_IFREG; break;
        case EGGSFS_INODE_SYMLINK: inode->i_mode = S_IFLNK; break;
        default: BUG();
        }
        inode->i_mode |= S_ISDIR(inode->i_mode) ? 0777 : 0666;
        inode->i_blocks = 0;
        inode->i_size = 0;

        inode->i_op = NULL;
        inode->i_fop = NULL;
        if (S_ISDIR(inode->i_mode)) {
            inode->i_op = &eggsfs_dir_inode_ops;
            inode->i_fop = &eggsfs_dir_operations;

            rcu_assign_pointer(enode->dir.pages, NULL);
            eggsfs_latch_init(&enode->dir.pages_latch);
        } else if (S_ISREG(inode->i_mode) || S_ISLNK(inode->i_mode)) {
            if (unlikely(S_ISLNK(inode->i_mode))) {
                inode->i_op = &eggsfs_symlink_inode_ops;
            } else {
                inode->i_op = &eggsfs_file_inode_ops;
            }
            inode->i_fop = &eggsfs_file_operations;

            enode->file.status = EGGSFS_FILE_STATUS_NONE;

            // Init normal file stuff -- that's always there. The transient
            // file stuff is only filled in if needed.
            enode->file.spans = RB_ROOT;
            init_rwsem(&enode->file.spans_lock);
            atomic64_set(&enode->file.prefetch_section, 0);
            atomic_set(&enode->file.in_flight, 0);
            init_waitqueue_head(&enode->file.in_flight_wq);
        }

        // FIXME
        inode->i_uid = make_kuid(&init_user_ns, 1000);//65534);
        inode->i_gid = make_kgid(&init_user_ns, 1000);//65534);

        // Only == NULL when we're getting the root inode    
        if (parent) {
            memcpy(&enode->block_policies, &parent->block_policies, sizeof(parent->block_policies));
            memcpy(&enode->span_policies, &parent->span_policies, sizeof(parent->span_policies));
            enode->target_stripe_size = parent->target_stripe_size;
            BUG_ON(!enode->block_policies.len || !enode->span_policies.len || !enode->target_stripe_size);
        } else {
            BUG_ON(ino != EGGSFS_ROOT_INODE);
        }

        unlock_new_inode(inode);
    }

    trace_eggsfs_get_inode_exit(ino, inode, new, 0);
    return inode;
}

void eggsfs_wait_in_flight(struct eggsfs_inode* enode) {
    if (atomic_read(&enode->file.in_flight) == 0) { return; }

    long res = wait_event_timeout(enode->file.in_flight_wq, atomic_read(&enode->file.in_flight) == 0, 10 * HZ);
    if (res > 0) { return; }
    eggsfs_warn("waited for 10 seconds for in flight requests for inode %016lx, either some requests are stuck or this is a bug, will wait for a minute more", enode->inode.i_ino);

    res = wait_event_timeout(enode->file.in_flight_wq, atomic_read(&enode->file.in_flight) == 0, 60 * HZ);
    if (res > 0) { return; }
    eggsfs_warn("waited for 60 seconds for in flight requests for inode %016lx, either some requests are stuck or this is a bug", enode->inode.i_ino);
}
