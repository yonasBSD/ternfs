#include "inode.h"

#include <linux/sched/mm.h>

#include "log.h"
#include "dir.h"
#include "metadata.h"
#include "dentry.h"
#include "net.h"
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
        bool has_atime = false;
        u64 atime;
        if (S_ISDIR(enode->inode.i_mode)) {
            u64 owner;
            struct eggsfs_policy_body block_policy;
            struct eggsfs_policy_body span_policy;
            struct eggsfs_policy_body stripe_policy;
            err = eggsfs_shard_getattr_dir(
                (struct eggsfs_fs_info*)enode->inode.i_sb->s_fs_info,
                enode->inode.i_ino,
                &mtime,
                &owner,
                &block_policy,
                &span_policy,
                &stripe_policy
            );
            if (err == 0) {
                if (block_policy.len) {
                    enode->block_policy = eggsfs_upsert_policy(enode->inode.i_ino, BLOCK_POLICY_TAG, block_policy.body, block_policy.len);
                }
                if (span_policy.len) {
                    enode->span_policy = eggsfs_upsert_policy(enode->inode.i_ino, SPAN_POLICY_TAG, span_policy.body, span_policy.len);
                }
                if (stripe_policy.len) {
                    enode->stripe_policy = eggsfs_upsert_policy(enode->inode.i_ino, STRIPE_POLICY_TAG, stripe_policy.body, stripe_policy.len);
                }
                expiry = ts + eggsfs_dir_refresh_time_jiffies;
            }
        } else {
            if (enode->file.status == EGGSFS_FILE_STATUS_READING || enode->file.status == EGGSFS_FILE_STATUS_NONE) {
                eggsfs_debug("updating getattr for reading file");
                u64 size;
                err = eggsfs_shard_getattr_file(
                    (struct eggsfs_fs_info*)enode->inode.i_sb->s_fs_info, 
                    enode->inode.i_ino,
                    &mtime,
                    &atime,
                    &size
                );
                has_atime = true;
                if (err == EGGSFS_ERR_FILE_NOT_FOUND && enode->file.status == EGGSFS_FILE_STATUS_NONE) { // probably just created
                    enode->inode.i_size = 0;
                    expiry = 0;
                    mtime = 0;
                } else if (err == 0) {
                    enode->inode.i_size = size;
                    expiry = ts + eggsfs_file_refresh_time_jiffies;
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
            eggsfs_debug("got mtime %llu", mtime);
            enode->inode.i_mtime.tv_sec = mtime / 1000000000;
            enode->inode.i_mtime.tv_nsec = mtime % 1000000000;
            if (has_atime) {
                eggsfs_debug("got atime %llu", mtime);
                enode->inode.i_atime.tv_sec = atime / 1000000000;
                enode->inode.i_atime.tv_nsec = atime % 1000000000;
            }
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

    // internal-repo/blob/main/docs/kmod-file-tracking.md
    // This (should) happen only from NFSd. Right now we only re-export on NFS for
    // this reason, so we should never hit this.
    if (current->group_leader->mm == NULL) {
        eggsfs_warn("current->group_leader->mm = NULL, called from kernel thread?");
        return ERR_PTR(-EIO);
    }

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

    // new_inode
    struct inode* inode = eggsfs_get_inode_transient(parent->i_sb, parent_enode, ino);
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

static int eggsfs_setattr(struct dentry* dentry, struct iattr* attr) {
    // ATTR_TIMES_SET/ATTR_TOUCH is just to distinguish between a touch
    // and an explicit time set
    // Note that `utimes_common` unconditionally sets ATTR_CTIME, but we
    // can't do much with it.
    if (attr->ia_valid & ~(ATTR_ATIME|ATTR_ATIME_SET|ATTR_MTIME|ATTR_MTIME_SET|ATTR_TIMES_SET|ATTR_CTIME|ATTR_TOUCH)) {
        eggsfs_debug("skipping setattr, ia_valid=%08x", attr->ia_valid);
        return -EPERM;
    }

    uint64_t now = ktime_get_real_ns();

    uint64_t atime = 0;
    if (attr->ia_valid & ATTR_ATIME) {
        if (attr->ia_valid & ATTR_ATIME_SET) {
            atime = (attr->ia_atime.tv_sec * 1000000000ll) + attr->ia_atime.tv_nsec;
        } else {
            atime = now;
        }
        eggsfs_debug("setting atime to %llu", atime);
        atime |= 1ull << 63;
    }

    uint64_t mtime = 0;
    if (attr->ia_valid & ATTR_MTIME) {
        if (attr->ia_valid & ATTR_MTIME_SET) {
            mtime = (attr->ia_mtime.tv_sec * 1000000000ll) + attr->ia_mtime.tv_nsec;
        } else {
            mtime = now;
        }
        eggsfs_debug("setting mtime to %llu", mtime);
        mtime |= 1ull << 63;
    }

    int err = eggsfs_shard_set_time((struct eggsfs_fs_info*)dentry->d_inode->i_sb->s_fs_info, dentry->d_inode->i_ino, mtime, atime);
    if (err) { return err; }

    // could copy out attributes instead?
    smp_store_release(&EGGSFS_I(dentry->d_inode)->mtime_expiry, 0);

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
    .setattr = eggsfs_setattr,
};

static const struct inode_operations eggsfs_symlink_inode_ops = {
    .getattr = eggsfs_getattr,
    .setattr = eggsfs_setattr,
    .get_link = eggsfs_get_link,
};

extern struct file_operations eggsfs_dir_operations;

struct inode* eggsfs_get_inode(
    struct super_block* sb,
    bool transient,
    bool allow_no_parent,
    struct eggsfs_inode* parent,
    u64 ino
) {
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

            rcu_assign_pointer(enode->dir.dirents, NULL);
            eggsfs_latch_init(&enode->dir.dirents_latch);
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

        inode->i_uid = make_kuid(&init_user_ns, 1000);
        inode->i_gid = make_kgid(&init_user_ns, 1000);

        // Only == NULL when we're getting the root inode,
        // unless we allow no parent, which is only in the
        // case of NFS exports. In that case we never write
        // files anyway, so not having the right policy does
        // not matter. Ofc we could re-export a mount that
        // is also written to, but this is an edge case that
        // we do not cater to very well for now. The proper
        // solution is to store the correct policy on
        // `d_splice_alias`, but we can only do that if we
        // also remember if the current enode policy is its
        // own or not. In fact, I just realized that `eggsfs_rename`
        // is wrong: it unconditionally sets the policy of the
        // child directory to that of the parent, but that's
        // not right, it should do that _only if the child
        // policy is inherited_. We should store a bit that tells
        // us whether it is inherited, and then use it
        // on `d_splice_alias` and `d_move`.
        if (parent) {
            enode->block_policy = parent->block_policy;
            enode->span_policy = parent->span_policy;
            enode->stripe_policy = parent->stripe_policy;
        } else {
            bool is_root = ino == EGGSFS_ROOT_INODE;
            BUG_ON(!allow_no_parent && !is_root);
            if (is_root) { // we've just created the root inode, policy will be filled in later
                enode->block_policy = NULL;
                enode->span_policy = NULL;
                enode->stripe_policy = NULL;
            } else {
                struct eggsfs_inode* root = EGGSFS_I(sb->s_root->d_inode);
                enode->block_policy = root->block_policy;
                enode->span_policy = root->span_policy;
                enode->stripe_policy = root->stripe_policy;
            }
        }

        unlock_new_inode(inode);

        // Make sure attrs are filled in before doing anything else --
        // e.g. if we do open + lseek we wouldn't have the chance to
        // getattr anywhere.
        if (!transient) {
            int err = eggsfs_do_getattr(enode);
            if (err) {
                iput(inode);
                return ERR_PTR(err);
            }
        }
    }

    trace_eggsfs_get_inode_exit(ino, inode, new, 0);

    return inode;
}

void eggsfs_wait_in_flight(struct eggsfs_inode* enode) {
    if (atomic_read(&enode->file.in_flight) == 0) { return; }

    long res = wait_event_timeout(enode->file.in_flight_wq, atomic_read(&enode->file.in_flight) == 0, 10 * HZ);
    if (res > 0) { return; }

    // Some processes can be stuck for as long as it takes to process metadata
    // requests. So wait for at least that much.
    u64 wait_for = max(eggsfs_overall_shard_timeout_jiffies, eggsfs_overall_cdc_timeout_jiffies)*2;
    eggsfs_warn("waited for 10 seconds for in flight requests for inode %016lx, either some requests are stuck or this is a bug, will wait for %llums", enode->inode.i_ino, jiffies64_to_msecs(wait_for));

    res = wait_event_timeout(enode->file.in_flight_wq, atomic_read(&enode->file.in_flight) == 0, wait_for);
    if (res > 0) { return; }
    eggsfs_warn("waited for %llu seconds for in flight requests for inode %016lx, either some requests are stuck or this is a bug", wait_for, enode->inode.i_ino);
}
