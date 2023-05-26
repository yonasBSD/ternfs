#include "dir.h"

#include <linux/mm.h>
#include <linux/slab.h>

#include "log.h"
#include "err.h"
#include "inode.h"
#include "metadata.h"
#include "trace.h"

// sysctls
int eggsfs_dir_refresh_time; // in jiffies
                             //
#define eggsfs_dir_get_page_n(_page) ({ *((((u32*)&(_page)->private))+1); })
#define eggsfs_dir_set_page_n(_page, _n) ({ *((((u32*)&(_page)->private))+1) = _n; })

static inline int eggsfs_dir_entry_size(int name_len) {
    return 8 + 1 + name_len; // inode + len + name
}

static inline void eggsfs_dir_entry_parse(const char* buf, u64* ino, u8* name_len, const char** name) {
    *ino = get_unaligned_le64(buf); buf += 8;
    *name_len = *(u8*)buf; buf++;
    *name = buf; buf += *name_len;
}

struct eggsfs_readdir_ctx {
    struct page* current_page;
    struct page* pages;
    u32 page_offset;
    u32 page_n;
};

// Increments the refcount of the cache in ->private under RCU.
// After this we know the page won't be deallocated until we
// decrease the refcount.
static struct page* eggsfs_dir_get_cache(struct eggsfs_inode* enode) {
    struct page* page;
    rcu_read_lock();
    page = rcu_dereference(enode->dir.pages);
    if (page) { atomic_inc((atomic_t*)&page->private); }
    rcu_read_unlock();
    return page;
}

// Decrements the refcount, and frees up all the pages in the
// cache if we were the last ones. Must be called by
// somebody who called eggsfs_dir_get_cache before.
//
// Note that ->current_page and ->dir_pages are both holding a
// reference, so once you clear those you should call `eggsfs_dir_put_cache`.
static void eggsfs_dir_put_cache(struct page* page) {
    if (atomic_dec_and_test((atomic_t*)&page->private)) {
        LIST_HEAD(pages);
        while (page) {
            struct page* next = (struct page*)page->mapping;
            page->mapping = NULL;
            page->private = 0;
            list_add_tail(&page->lru, &pages);
            page = next;
        }
        put_pages_list(&pages);
    }
}

static void __eggsfs_dir_put_cache_rcu(struct rcu_head* rcu) {
    struct page* page = container_of(rcu, struct page, rcu_head);
    eggsfs_dir_put_cache(page);
}

// Removes the cache from ->dir_pages, and also calls
// `eggsfs_dir_put_cache` to clear the reference.
void eggsfs_dir_drop_cache(struct eggsfs_inode* enode) {
    struct page* page;
    static_assert(sizeof(struct rcu_head) == sizeof(struct list_head));
    if (!atomic64_read((atomic64_t*)&enode->dir.pages)) { return; }
    page = (struct page*)atomic64_xchg((atomic64_t*)&enode->dir.pages, (u64)0);
    if (page) { call_rcu(&page->rcu_head, __eggsfs_dir_put_cache_rcu); }
}

static struct page* eggsfs_dir_read_all(struct dentry* dentry, u64 dir_seqno);

static int eggsfs_dir_open(struct inode* inode, struct file* filp) {
    struct eggsfs_inode* enode = EGGSFS_I(inode);
    struct eggsfs_readdir_ctx* ctx;
    struct page* page;
    int err;

    trace_eggsfs_vfs_opendir_enter(inode);

    if (get_jiffies_64() >= enode->mtime_expiry) {
        err = eggsfs_dir_revalidate(enode);
        if (err) { return err; }
    }

    ctx = kmalloc(sizeof(*ctx), GFP_KERNEL);
    if (!ctx) { return -ENOMEM; }

again: // progress: whoever wins the lock either get pages or fails
    page = eggsfs_dir_get_cache(enode);
    if (page && page->index != READ_ONCE(enode->mtime)) {
        eggsfs_dir_put_cache(page);
        eggsfs_dir_drop_cache(enode);
        page = NULL;
    }
    if (!page) {
        int seqno;
        if (!eggsfs_latch_try_acquire(&enode->dir.pages_latch, seqno)) {
            err = eggsfs_latch_wait_killable(&enode->dir.pages_latch, seqno);
            if (err) { goto out_ctx; }
            goto again;
        }

        // If somebody else got to creating the page already, drop it,
        // otherwise we'll leak.
        page = eggsfs_dir_get_cache(enode);
        if (page && page->index != READ_ONCE(enode->mtime)) {
            eggsfs_dir_put_cache(page);
            eggsfs_dir_drop_cache(enode);
            page = NULL;
        }

        if (!page) {
            u64 dir_mtime = READ_ONCE(enode->mtime);
            page = eggsfs_dir_read_all(filp->f_path.dentry, dir_mtime);
            if (!IS_ERR(page)) {
                // If the mtime matches (which will be the case unless somebody
                // else revalidated between the READ_ONCE above), then the refcount
                // starts at two: one for the ->dir_pages, and one for the
                // ->current_page below. Otherwise only for ->current_page (basically
                // we have these pages to be private to this read).
                if (likely(READ_ONCE(enode->mtime) == dir_mtime)) {
                    atomic_set((atomic_t*)&page->private, 2);
                    rcu_assign_pointer(enode->dir.pages, page);
                } else {
                    atomic_set((atomic_t*)&page->private, 1);
                }
            }
        }
        eggsfs_latch_release(&enode->dir.pages_latch, seqno);
    }
    if (IS_ERR(page)) { err = PTR_ERR(page); goto out_ctx; }

    ctx->current_page = page;
    ctx->pages = page;
    ctx->page_offset = 0;
    ctx->page_n = 0;

    filp->private_data = ctx;

    trace_eggsfs_vfs_opendir_exit(inode, 0);
    return 0;

out_ctx:
    kfree(ctx);
    trace_eggsfs_vfs_opendir_exit(inode, err);
    eggsfs_debug_print("err=%d", err);
    return err;
}

static void update_dcache(struct dentry* parent, u64 dir_seqno, const char* name, int name_len, u64 ino) {
    struct qstr filename = QSTR_INIT(name, name_len);
    struct dentry* dentry;
    struct dentry* alias;
    DECLARE_WAIT_QUEUE_HEAD_ONSTACK(wq);
    struct inode* parent_inode = parent->d_inode;
    if (unlikely(parent_inode == NULL)) { // TODO can this ever happen?
        eggsfs_warn_print("got NULL in dentry parent (ino %016llx)", ino);
        return;
    }

    eggsfs_debug_print("parent=%pd name=%*pE ino=%llx", parent, name_len, name, ino);

    filename.hash = full_name_hash(parent, filename.name, filename.len);

    dentry = d_lookup(parent, &filename);
    if (!dentry) {
again:
        dentry = d_alloc_parallel(parent, &filename, &wq);
        if (IS_ERR(dentry)) { return; }
    }
    if (!d_in_lookup(dentry)) {
        // pre-existing entry
        struct inode* old_inode = d_inode(dentry);
        if (old_inode && old_inode->i_ino == ino) {
            WRITE_ONCE(dentry->d_time, dir_seqno);
            goto out;
        }
        d_invalidate(dentry);
        dput(dentry);
        goto again;
    } else {
        // new entry
        struct inode* inode = eggsfs_get_inode(parent->d_sb, EGGSFS_I(parent_inode), ino);
        // d_splice_alias propagates error in inode
        WRITE_ONCE(dentry->d_time, dir_seqno);
        alias = d_splice_alias(inode, dentry); 
        d_lookup_done(dentry);
        if (alias) {
            dput(dentry);
            dentry = alias;
        }
        if (IS_ERR(dentry)) { return; }
    }

out:
    dput(dentry);
};

struct eggsfs_dir_fill_ctx {
    struct dentry* parent;
    u64 dir_mtime;

    // NULL or kmap'ed at page_addr
    struct page* current_page;
    // kmap'd address of current_page
    char* page_addr;
    // Current offset inside the page we're writing
    u32 page_off;
    // Number of eggsfs dir entries in the current page
    u32 page_n;
};

static int eggsfs_dir_add_entry(struct eggsfs_dir_fill_ctx* ctx, const char* name, int name_len, u64 ino) {
    if (name_len > EGGSFS_MAX_FILENAME) { return -ENAMETOOLONG; }
    // Flush current page, and start a new one, linking it in ->mapping
    if (ctx->page_off + eggsfs_dir_entry_size(name_len) > PAGE_SIZE) {
        struct page* page = alloc_page(GFP_KERNEL);
        if (!page) { return -ENOMEM; }

        kunmap(ctx->current_page);
        ctx->current_page->mapping = (void*)page;
        eggsfs_dir_set_page_n(ctx->current_page, ctx->page_n);

        ctx->current_page = page;
        ctx->page_addr = kmap(page);
        ctx->page_off = 0;
        ctx->page_n = 0;
    }
    if (ctx->page_off + eggsfs_dir_entry_size(name_len) > PAGE_SIZE) {
        WARN_ON(ctx->page_off + eggsfs_dir_entry_size(name_len) > PAGE_SIZE);
        return -EIO;
    }

    char* entry = ctx->page_addr + ctx->page_off;
    put_unaligned_le64(ino, entry); entry += 8;
    *(u8*)entry = (u8)name_len; entry++;
    memcpy(entry, name, name_len);
    ctx->page_off += eggsfs_dir_entry_size(name_len);
    ++ctx->page_n;
    return 0;
}

int eggsfs_dir_readdir_entry_cb(void* ptr, const char* name, int name_len, u64 hash, u64 ino) {
    struct eggsfs_dir_fill_ctx* ctx = ptr;
    eggsfs_debug_print("hash=%llx ino=%llx, name_len=%d, name=%*pE", hash, ino, name_len, name_len, name);
    update_dcache(ctx->parent, ctx->dir_mtime, name, name_len, ino);
    return eggsfs_dir_add_entry(ctx, name, name_len, ino);
}

// mut hold
static struct page* eggsfs_dir_read_all(struct dentry* dentry, u64 dir_mtime) {
    bool eof = false;
    int err;
    u64 continuation_key = 0;
    struct inode* inode = d_inode(dentry);
    struct eggsfs_inode* enode = EGGSFS_I(inode);
    struct eggsfs_dir_fill_ctx ctx = {
        .parent = dentry,
        .dir_mtime = dir_mtime,
    };
    struct page* first_page;

    first_page = alloc_page(GFP_KERNEL);
    if (!first_page) { return ERR_PTR(-ENOMEM); }
    ctx.current_page = first_page;
    ctx.page_addr = kmap(first_page);
    ctx.page_off = 0;
    ctx.page_n = 0;
    first_page->index = dir_mtime;

    while (!eof) {
        eggsfs_debug_print("cont_key=%llx", continuation_key);
        err = eggsfs_shard_readdir((struct eggsfs_fs_info*)enode->inode.i_sb->s_fs_info, enode->inode.i_ino, continuation_key, &ctx, &continuation_key);
        if (err) { err = eggsfs_error_to_linux(err); goto out_err; }
        eof = continuation_key == 0;
    }

    kunmap(ctx.current_page);
    eggsfs_dir_set_page_n(ctx.current_page, ctx.page_n);

    return first_page;

out_err:
    kunmap(ctx.current_page);
    while (first_page) {
        struct page* next = (struct page*)first_page->mapping;
        first_page->mapping = NULL;
        put_page(first_page);
        first_page = next;
    }
    eggsfs_info_print("err=%d", err);
    return ERR_PTR(err);
};

// vfs: exclusive inode.i_rwsem
// vfs: mutex file.f_pos_lock 
static int eggsfs_dir_read(struct file* filp, struct dir_context* dctx) {
    struct eggsfs_readdir_ctx* ctx = filp->private_data;
    char* page_addr;

    WARN_ON(!ctx);
    if (!ctx) { return -EINVAL; }

    eggsfs_debug_print("ctx=%p ctx.current_page=%p ctx.pages=%p ctx.page_offset=%u ctx.page_n=%u", ctx,
            ctx->current_page, ctx->pages, ctx->page_offset, ctx->page_n);

    if (!dir_emit_dots(filp, dctx)) { return 0; }
    if (!ctx->current_page || !eggsfs_dir_get_page_n(ctx->current_page)) { return 0; }

    page_addr = kmap(ctx->current_page);

    while (ctx->current_page) {
        int dtype;
        const char* entry = page_addr + ctx->page_offset;
        eggsfs_debug_print("next page_addr=%p ctx.page_offset=%u entry=%p", page_addr, ctx->page_offset, entry);

        u64 ino;
        u8 name_len;
        const char* name;
        eggsfs_dir_entry_parse(entry, &ino, &name_len, &name);
        eggsfs_debug_print("name=%*pE ino=0x%016llx", name_len, name, ino);

        switch (eggsfs_inode_type(ino)) {
        case EGGSFS_INODE_DIRECTORY: dtype = DT_DIR; break;
        case EGGSFS_INODE_FILE: dtype = DT_REG; break;
        case EGGSFS_INODE_SYMLINK: dtype = DT_LNK; break;
        default: dtype = DT_UNKNOWN;
        }

        if (!dir_emit(dctx, name, name_len, ino, dtype)) {
            eggsfs_debug_print("break");
            break;
        }

        ctx->page_offset += eggsfs_dir_entry_size(name_len);
        ++ctx->page_n;
        ++dctx->pos;

        eggsfs_debug_print(
            "next ctx=%p ctx.current_page=%p ctx.pages=%p ctx.page_offset=%u ctx.page_n=%u page_n=%u", ctx,
            ctx->current_page, ctx->pages, ctx->page_offset, ctx->page_n, eggsfs_dir_get_page_n(ctx->current_page)
        );

        if (ctx->page_n >= eggsfs_dir_get_page_n(ctx->current_page)) {
            kunmap(ctx->current_page);
            ctx->current_page = (struct page*)ctx->current_page->mapping;
            eggsfs_debug_print("next page current_page = %p", ctx->current_page);
            ctx->page_offset = 0;
            ctx->page_n = 0;
            page_addr = kmap(ctx->current_page);
        }
    }

    if (ctx->current_page) {
        eggsfs_debug_print("unmap current_page = %p", ctx->current_page);
        kunmap(ctx->current_page);
    }

    eggsfs_debug_print("done");
    return 0;
}

#if 0
// multiple seeks at the same time?
static loff_t eggsfs_llseek_dir(struct file* filp, loff_t offset, int whence) {
    int err;
    struct eggsfs_readdir_ctx* ctx = filp->private_data;
    loff_t dir_offset;

    if (whence != SEEK_CUR) { return -EINVAL; }
    // concurrency with readdir?
    // TODO: if offset == 0 reacquire pages

    ctx->current_page = ctx->pages;
    ctx->page_offset = 0;
    ctx->page_n = 0;
    if (offset > 2) {
        dir_offset = offset - 2;
        while (ctx->current_page && eggsfs_dir_get_page_n(ctx->current_page) <= dir_offset) {
                dir_offset -= eggsfs_dir_get_page_n(ctx->current_page);
                ctx->current_page = (struct page*)ctx->current_page->mapping;
        }
        if (ctx->current_page) {
            char* page_addr = (char*)kmap(ctx->current_page);
            while (ctx->page_n < dir_offset) {
                BUG_ON(ctx->page_offset >= PAGE_SIZE);
                u64 ino;
                u8 name_len;
                const char* name;
                eggsfs_dir_entry_parse(page_addr + ctx->page_offset, &ino, &name_len, &name);
                ctx->page_offset += eggsfs_dir_entry_size(name_len);
                ++ctx->page_n;
            }
            kunmap(ctx->current_page);
        }
    }
    
    filp->f_pos = offset;

    return offset;
}
#endif

static int eggsfs_dir_close(struct inode* inode, struct file* filp) {
    struct eggsfs_readdir_ctx* ctx = (struct eggsfs_readdir_ctx*)filp->private_data;

    WARN_ON(!ctx);
    if (!ctx) { return -EINVAL; }

    trace_eggsfs_vfs_closedir_enter(inode);
    eggsfs_dir_put_cache(ctx->pages);
    kfree(ctx);
    trace_eggsfs_vfs_closedir_exit(inode, 0);
	return 0;
}

struct file_operations eggsfs_dir_operations = {
    .open = eggsfs_dir_open,
    .iterate = eggsfs_dir_read,
    .iterate_shared = eggsfs_dir_read,
    .release = eggsfs_dir_close,
};
