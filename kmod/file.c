#include "file.h"

#include <linux/uio.h>
#include <linux/sched/mm.h>

#include "bincode.h"
#include "inode.h"
#include "log.h"
#include "metadata.h"
#include "err.h"
#include "rs.h"
#include "block.h"
#include "skb.h"
#include "crc.h"
#include "trace.h"
#include "span.h"
#include "wq.h"
#include "bincode.h"

static struct kmem_cache* eggsfs_transient_span_cachep;

struct eggsfs_transient_span {
    // The transient span holds a reference to this. As long as the
    // transient span lives, the enode must live.
    struct eggsfs_inode* enode;
    // Offset in the file for this span
    u64 offset;
    // Linear list of pages with the body of the span, used when we're still gathering
    // content for it. We use `index` in the page to track where we're writing to.
    struct list_head pages;
    // These are finalized when we start flushing out a span.
    struct list_head blocks[EGGSFS_MAX_BLOCKS]; // the pages for each block
    u64 block_ids[EGGSFS_MAX_BLOCKS]; // the block ids assigned by add span initiate
    spinlock_t blocks_proofs_lock;
    u64 blocks_proofs[EGGSFS_MAX_BLOCKS];
    u32 written; // how much we've written to this span
    u32 block_size;
    atomic_t refcount;
    u8 stripes;
    u8 storage_class;
    u8 parity;
};

// open_mutex held here
// really want atomic open for this
static int eggsfs_file_open(struct inode* inode, struct file* filp) {
    struct eggsfs_inode* enode = EGGSFS_I(inode);

    eggsfs_debug("enode=%p status=%d owner=%p", enode, enode->file.status, current->group_leader);

    if (enode->file.status == EGGSFS_FILE_STATUS_WRITING) {
        // TODO can this ever happen?
        eggsfs_debug("trying to open file we're already writing");
        return -ENOENT;
    }

    if (filp->f_mode & FMODE_WRITE) {
        eggsfs_debug("opening file for writing");
        if (enode->file.status != EGGSFS_FILE_STATUS_CREATED) {
            eggsfs_debug("trying to open for write non-writeable file");
            return -EPERM;
        }
        enode->file.status = EGGSFS_FILE_STATUS_WRITING;
    } else {
        enode->file.status = EGGSFS_FILE_STATUS_READING;
    }

    return 0;
}

static void eggsfs_init_transient_span(void* p) {
    struct eggsfs_transient_span* span = (struct eggsfs_transient_span*)p;
    INIT_LIST_HEAD(&span->pages);
    int i;
    for (i = 0; i < EGGSFS_MAX_BLOCKS; i++) {
        INIT_LIST_HEAD(&span->blocks[i]);
    }
    spin_lock_init(&span->blocks_proofs_lock);
}

// starts out with refcount = 1
static struct eggsfs_transient_span* eggsfs_new_transient_span(struct eggsfs_inode* enode, u64 offset) {
    struct eggsfs_transient_span* span = kmem_cache_alloc(eggsfs_transient_span_cachep, GFP_KERNEL);
    if (span == NULL) { return span; }
    span->enode = enode;
    eggsfs_in_flight_begin(enode);
    atomic_set(&span->refcount, 1);
    span->offset = offset;
    span->written = 0;
    memset(span->blocks_proofs, 0, sizeof(span->blocks_proofs));
    return span;
}

static void eggsfs_hold_transient_span(struct eggsfs_transient_span* span) {
    atomic_inc(&span->refcount);
}

static void eggsfs_put_transient_span(struct eggsfs_transient_span* span) {
    if (atomic_dec_return(&span->refcount) == 0) {
        BUG_ON(spin_is_locked(&span->blocks_proofs_lock));
        // free pages, adjust OOM score
        int num_pages = 0;
#define FREE_PAGES(__pages) \
        while (!list_empty(__pages)) { \
            struct page* victim = lru_to_page(__pages); \
            list_del(&victim->lru); \
            put_page(victim); \
            num_pages++; \
        }
        FREE_PAGES(&span->pages);
        int b;
        for (b = 0; b < eggsfs_blocks(span->parity); b++) {
            FREE_PAGES(&span->blocks[b]);
        }
#undef FREE_PAGES
        add_mm_counter(span->enode->file.mm, MM_FILEPAGES, -num_pages);
        eggsfs_in_flight_end(span->enode);
        // Free the span itself
        kmem_cache_free(eggsfs_transient_span_cachep, span);
    }
}

static void eggsfs_write_block_done(void* data, u64 block_id, u64 proof, int err) {
    // We need to be careful with locking here: the writer waits on the
    // flushing semaphore while holding the inode lock.

    // we use 0 to mean "no proof yet"
    BUG_ON(proof == 0); // we need to get _very_ unlucky for this to happen!

    struct eggsfs_transient_span* span = (struct eggsfs_transient_span*)data;
    struct eggsfs_inode* enode = span->enode;

    eggsfs_debug("block_id=%016llx proof=%016llx err=%d", block_id, proof, err);

    bool just_finished = true;

    // the block is toast
    if (err) { goto out; }

    // the file is already toast
    err = atomic_read(&enode->file.transient_err);
    if (err) { goto out; }

    // OK, the file & block are good, and block_err = 0.
    int B = eggsfs_blocks(span->parity);

    // find the index that concerns us now
    int b;
    for (b = 0; b < B; b++) {
        if (span->block_ids[b] == block_id) {
            break;
        }
    }
    BUG_ON(b == B); // we found the block
    BUG_ON(span->blocks_proofs[b] != 0); // no proof already

    // Mark the current one as done, and check if we're just completing things
    int i;
    spin_lock_bh(&span->blocks_proofs_lock);
    for (i = 0; i < B; i++) {
        if (span->block_ids[i] == 0) { continue; } // we weren't downloading this
        if (i == b) { span->blocks_proofs[i] = proof; }
        else if (span->blocks_proofs[i] == 0) {
            // we're waiting on some other block proof.
            just_finished = false;
        }
    }
    spin_unlock_bh(&span->blocks_proofs_lock);

    if (just_finished) {
        // TODO just like in the other case, would be nicer to have this async, since we're in a queue here.
        err = eggsfs_error_to_linux(eggsfs_shard_add_span_certify(
            (struct eggsfs_fs_info*)enode->inode.i_sb->s_fs_info,
            enode->inode.i_ino, enode->file.cookie, span->offset, span->parity,
            span->block_ids, span->blocks_proofs
        ));
    }

out:
    if (err) {
        atomic_cmpxchg(&enode->file.transient_err, 0, err);
    }
    if (err || just_finished) {
        up(&span->enode->file.flushing_span_sema); // wake up waiters
    }
    eggsfs_put_transient_span(span);
}

struct initiate_ctx {
    struct eggsfs_transient_span* span;
    u32* cell_crcs;
};

int eggsfs_shard_add_span_initiate_block_cb(
    void* data, int block, u32 ip1, u16 port1, u32 ip2, u16 port2, u64 block_service_id, u64 block_id, u64 certificate
) {
    struct initiate_ctx* ctx = data;
    struct eggsfs_block_service bs = {
        .id = block_service_id,
        .ip1 = ip1,
        .port1 = port1,
        .ip2 = ip2,
        .port2 = port2,
    };
    int B = eggsfs_blocks(ctx->span->parity);
    int cell_size = ctx->span->block_size / ctx->span->stripes;
    u32 block_crc = 0;
    int s;
    for (s = 0; s < ctx->span->stripes; s++) {
        block_crc = eggsfs_crc32c_append(block_crc, ctx->cell_crcs[s*B + block], cell_size);
    }
    ctx->span->block_ids[block] = block_id;
    eggsfs_debug("about to write block from callback, crc %08x", block_crc);
    int err = eggsfs_write_block(
        &eggsfs_write_block_done,
        ctx->span,
        &bs,
        block_id,
        certificate,
        ctx->span->block_size,
        block_crc,
        &ctx->span->blocks[block]
    );
    if (err) {
        return err;
    }
    // for the callback
    eggsfs_hold_transient_span(ctx->span);
    return 0;
}

static struct page* eggsfs_alloc_write_page(struct eggsfs_inode_file* file) {
    // the zeroing is to just write out to blocks assuming we have trailing
    // zeros, we could do it on demand but I can't be bothered.
    struct page* p = alloc_page(GFP_KERNEL | __GFP_ZERO);
    if (p == NULL) { return p; }
    // This contributes to OOM score.
    inc_mm_counter(file->mm, MM_FILEPAGES);
    return p;
}

static void eggsfs_compute_spans_parameters(
    struct eggsfs_block_policies* block_policies,
    struct eggsfs_span_policies* span_policies,
    u32 target_stripe_size,
    struct eggsfs_transient_span* span
) {
    u32 span_size = span->written;
    eggsfs_debug("span_size=%u", span_size);

    // Easy case: inline span (or empty, can happen for empty files)
    if (span_size < 256) {
        span->storage_class = span_size > 0 ? EGGSFS_INLINE_STORAGE : EGGSFS_EMPTY_STORAGE;
        span->block_size = 0;
        span->stripes = 0;
        span->parity = 0;
        return;
    }

    // Pick parity
    int num_span_policies = eggsfs_span_policies_len(span_policies);
    BUG_ON(num_span_policies == 0);
    int i;
    u8 parity = 0;
    for (i = num_span_policies - 2; i >= 0; i--) {
        u32 max_size;
        u8 this_parity;
        eggsfs_span_policies_get(span_policies, i, &max_size, &this_parity);
        if (span_size > max_size) {
            i++;
            eggsfs_span_policies_get(span_policies, i, &max_size, &parity);
            break;
        }
    }
    if (parity == 0) {
        i = 0;
        u32 max_size;
        eggsfs_span_policies_get(span_policies, i, &max_size, &parity);
        BUG_ON(span_size > max_size);
    }

    // For simplicity, we have all cells to be a multiple of PAGE_SIZE, which means that all
    // blocks/stripes/spans are multiples of PAGE_SIZE. We might also have some extra pages
    // at the end to have things to line up correctly. This should only happens for big spans
    // anyway (small spans will just use mirroring).
    int S;
    if (eggsfs_data_blocks(parity) == 1) {
        // If we only have one data block, things are also pretty simple (just mirroring).
        // It doesn't make sense to have stripes in this case.
        S = 1;
    } else {
        // Otherwise compute striping etc.
        // We use target_stripe_size as an upper bound, for now (i.e. the stripe will always be smaller
        // than TargetStripeSize)
        S = min(max((u32)1, span_size / target_stripe_size), (u32)15);
    }
    int D = eggsfs_data_blocks(parity);
    int block_size = (span_size + D - 1) / D;
    int cell_size = (block_size + S - 1) / S;
    // Round up to cell to page size
    cell_size = (cell_size + PAGE_SIZE - 1) & PAGE_MASK;
    block_size = cell_size * S;

    // Pick storage class
    BUG_ON(span_size > EGGSFS_MAX_BLOCK_SIZE);
    int num_block_policies = eggsfs_block_policies_len(block_policies);
    BUG_ON(num_block_policies == 0);
    u8 storage_class;
    for (i = num_block_policies-1; i >= 0; i--) {
        u32 min_size;
        eggsfs_block_policies_get(block_policies, i, &storage_class, &min_size);
        if (block_size > min_size) {
            break;
        }
    }

    span->storage_class = storage_class;
    span->block_size = block_size;
    span->stripes = S;
    span->parity = parity;
}

// To be called with the inode lock. Acquires the flushing sema.
static int eggsfs_wait_flushed(struct eggsfs_inode* enode, bool non_blocking) {
    BUG_ON(!inode_is_locked(&enode->inode));

    // make sure we're free to go
    if (down_trylock(&enode->file.flushing_span_sema) == 1) {
        if (non_blocking) {
            return -EAGAIN;
        }
        int err = down_killable(&enode->file.flushing_span_sema);
        if (err < 0) { return err; }
    }

    return 0;
}

// To be called with the inode lock.
static int eggsfs_start_flushing(struct eggsfs_inode* enode, bool non_blocking) {
    BUG_ON(!inode_is_locked(&enode->inode));

    // Wait for the existing thing to be flushed
    int err = eggsfs_wait_flushed(enode, non_blocking);
    if (err < 0) { return err; }

    // Now turn the writing span into a flushing span
    struct eggsfs_transient_span* span = enode->file.writing_span;
    if (span == NULL) {
        up(&enode->file.flushing_span_sema);
        return 0;
    }

    eggsfs_compute_spans_parameters(
        &enode->block_policies, &enode->span_policies, enode->target_stripe_size, span
    );

    // Here we might block even if we're non blocking, since we don't have
    // async metadata requests yet, which is a bit unfortunate.

    if (span->storage_class == EGGSFS_EMPTY_STORAGE) {
        up(&enode->file.flushing_span_sema); // nothing to do
    } else if (span->storage_class == EGGSFS_INLINE_STORAGE) {
        // this is an easy one, just add the inline span
        up(&enode->file.flushing_span_sema); // however it goes, we won't be 
        struct page* page = list_first_entry(&span->pages, struct page, lru);
        char* data = kmap(page);
        eggsfs_debug("adding inline span of length %d", span->written);
        err = eggsfs_error_to_linux(eggsfs_shard_add_inline_span(
            (struct eggsfs_fs_info*)enode->inode.i_sb->s_fs_info, enode->inode.i_ino, enode->file.cookie,
            span->offset, span->written, data, span->written
        ));
        kunmap(page);
        if (err) { goto out_err_keep_writing; }
    } else {
        // the real deal, we need to write blocks

        int D = eggsfs_data_blocks(span->parity);
        int B = eggsfs_blocks(span->parity);
        int S = span->stripes;
        uint32_t cell_crcs[EGGSFS_MAX_BLOCKS*EGGSFS_MAX_STRIPES];
        memset(cell_crcs, 0, sizeof(cell_crcs));

        eggsfs_debug("size=%d, D=%d, B=%d, S=%d", span->written, D, B, S);
        int i, j;
        loff_t offset = enode->inode.i_size - span->written;
        trace_eggsfs_span_flush_enter(
            enode->inode.i_ino, offset, span->written, span->parity, span->stripes, span->storage_class
        );
        // Below, remember that cell size is always a multiple of PAGE_SIZE.
        u32 pages_per_block = span->block_size/PAGE_SIZE;
        u32 data_pages = pages_per_block*D;
        u32 current_data_pages = (span->written + PAGE_SIZE - 1) / PAGE_SIZE;
        eggsfs_debug("allocating padding pages_per_block=%u total_pages=%u current_pages=%u", pages_per_block, data_pages, current_data_pages);
        // This can happen if we have trailing zeros because of how we arrange the
        // stripes.
        for (i = current_data_pages; i < data_pages; i++) {
            struct page* zpage = eggsfs_alloc_write_page(&enode->file);
            if (zpage == NULL) {
                err = -ENOMEM;
                // we'd have to back out the pages we allocated so far and i can't be bothered
                goto out;
            }
            list_add_tail(&zpage->lru, &span->pages);
        }
        int cell_size = span->block_size / S;
        eggsfs_debug("arranging pages");
        // First, we arrange the data blocks appropriatedly.
        {
            int cell_offset = 0;
            int block = 0;
            struct page* page;
            struct page* tmp;
            list_for_each_entry_safe(page, tmp, &span->pages, lru) {
                list_del(&page->lru);
                list_add_tail(&page->lru, &span->blocks[block]);
                cell_offset += PAGE_SIZE;
                if (cell_offset >= cell_size) {
                    block++;
                    block = block % D;
                    cell_offset = 0;
                }
            }
        }
        // Then we allocate the parity blocks
        for (i = D; i < B; i++) {
            for (j = 0; j < pages_per_block; j++) {
                struct page* ppage = eggsfs_alloc_write_page(&enode->file);
                if (ppage == NULL) { err = -ENOMEM; goto out; }
                list_add_tail(&ppage->lru, &span->blocks[i]);
            }
        }
        eggsfs_debug("computing parity");
        // Then, we compute the parity blocks and the CRCs for each block.
        u32 span_crc = 0;
        {
            char* pages_bufs[EGGSFS_MAX_BLOCKS];
            kernel_fpu_begin();
            // traverse each block, page by page, rotating as we go along
            for (j = 0; j < pages_per_block; j++) {
                // get buffer pointers
                int s = (j*PAGE_SIZE)/cell_size;
                for (i = 0; i < B; i++) {
                    pages_bufs[i] = kmap_atomic(list_first_entry(&span->blocks[i], struct page, lru));
                }
                // compute parity
                err = eggsfs_compute_parity(span->parity, PAGE_SIZE, (const char**)&pages_bufs[0], &pages_bufs[D]);
                // compute CRCs
                for (i = 0; i < B; i++) {
                    cell_crcs[s*B + i] = eggsfs_crc32c(cell_crcs[s*B + i], pages_bufs[i], PAGE_SIZE);
                }
                for (i = 0; i < B; i++) {
                    kunmap_atomic(pages_bufs[i]);
                    list_rotate_left(&span->blocks[i]);
                }
                // unmap pages before exiting
                if (err) { goto out_err_fpu; }
            }
            // Compute overall span crc
            int s;
            for (s = 0; s < S; s++) {
                for (i = 0; i < D; i++) {
                    span_crc = eggsfs_crc32c_append(span_crc, cell_crcs[s*B + i], cell_size);
                }
            }
            span_crc = eggsfs_crc32c_zero_extend(span_crc, (int)span->written - (int)(span->block_size*D));
            kernel_fpu_end();
        }
        // Now we need to init the span (the callback will actually send the requests)
        // TODO would be better to have this async, and free up the queue for other stuff,
        // and also not block in this call which might be in a non-blocking write.
        eggsfs_debug("add span initiate");
        {
            struct initiate_ctx ctx = {
                .span = span,
                .cell_crcs = cell_crcs,
            };
            err = eggsfs_error_to_linux(eggsfs_shard_add_span_initiate(
                (struct eggsfs_fs_info*)enode->inode.i_sb->s_fs_info,
                &ctx, enode->inode.i_ino, enode->file.cookie, offset, span->written,
                span_crc, span->storage_class, span->parity, span->stripes, cell_size, cell_crcs
            ));
            if (err) { goto out; }
            // Now we wait.
            eggsfs_debug("sent all block writing requests");
        }
    }

out:
    if (err) {
        eggsfs_info("attempting to flush failed permanently, err=%d ino=%016lx", err, enode->inode.i_ino);
        atomic_cmpxchg(&enode->file.transient_err, 0, err);
    }
    // we don't need this anymore (the requests might though)
    eggsfs_put_transient_span(span);
    enode->file.writing_span = NULL;
    return err;

out_err_fpu:
    kernel_fpu_end();
    goto out;

    // When we got an error before even starting to flush
out_err_keep_writing:
    up(&enode->file.flushing_span_sema); // nothing is flushing
    return err;
}

// To be called with the inode lock.
ssize_t eggsfs_file_write(struct eggsfs_inode* enode, int flags, loff_t* ppos, struct iov_iter* from) {
    BUG_ON(!inode_is_locked(&enode->inode));

    int err;
    if (flags & IOCB_DIRECT) { return -ENOSYS; }

    loff_t ppos_before = *ppos;

    eggsfs_debug("enode=%p, ino=%lu, count=%lu, size=%lld, *ppos=%lld", enode, enode->inode.i_ino, iov_iter_count(from), enode->inode.i_size, *ppos);

    if (enode->file.status != EGGSFS_FILE_STATUS_WRITING) { err = -EPERM; goto out_err; }

    // permanent error
    err = atomic_read(&enode->file.transient_err);
    if (err) { goto out_err; }

    // we're writing at the right offset
    if (*ppos != enode->inode.i_size) {
        err = -EINVAL;
        goto out_err;
    }

    // Precompute the bound for spans
    u32 max_span_size;
    {
        u8 p;
        eggsfs_span_policies_last(&enode->span_policies, &max_span_size, &p);
    }
    BUG_ON(max_span_size%PAGE_SIZE != 0); // needed for "new span" logic paired with page copying below, we could avoid it

    // Get the span we're currently writing at
    if (enode->file.writing_span == NULL) {
        enode->file.writing_span = eggsfs_new_transient_span(enode, enode->inode.i_size);
        if (enode->file.writing_span == NULL) {
            err = -ENOMEM;
            goto out_err;
        }
    }
    struct eggsfs_transient_span* span = enode->file.writing_span;

    // We now start writing into the span, as much as we can anyway
    while (span->written < max_span_size && iov_iter_count(from)) {
        // grab the page to write to
        struct page* page = list_empty(&span->pages) ? NULL : list_last_entry(&span->pages, struct page, lru);
        if (page == NULL || page->index == 0) { // we're the first ones to get here, or we need to switch to the next one
            BUG_ON(page != NULL && page->index > PAGE_SIZE);
            page = eggsfs_alloc_write_page(&enode->file);
            if (!page) {
                err = -ENOMEM;
                goto out_err; // we haven't corrupted anything, so no need to make it permanent
            }
            page->index = 0;
            list_add_tail(&page->lru, &span->pages);
        }

        // copy stuff into page
        int ret = copy_page_from_iter(page, page->index, PAGE_SIZE - page->index, from);
        if (ret < 0) { err = ret; goto out_err_permanent; }
        eggsfs_debug("written %d to page %p", ret, page);
        enode->inode.i_size += ret;
        span->written += ret;
        page->index = (page->index + ret) % PAGE_SIZE;
        *ppos += ret;
    }
    
    if (span->written >= max_span_size) { // we need to start flushing the span
        // this might block even if it says nonblock, which is unfortunate.
        err = eggsfs_start_flushing(enode, !!(flags&IOCB_NOWAIT));
    }

    int written;
out:
    written = *ppos - ppos_before;
    eggsfs_debug("written=%d", written);
    return written;

out_err_permanent:
    atomic_set(&enode->file.transient_err, err);
out_err:
    eggsfs_debug("err=%d", err);
    if (err == -EAGAIN && *ppos > ppos_before) {
        goto out; // we can just return what we've already written
    }
    return err;
}

static ssize_t eggsfs_file_write_iter(struct kiocb* iocb, struct iov_iter* from) {
    struct file* file = iocb->ki_filp;
    struct inode* inode = file->f_inode;
    struct eggsfs_inode* enode = EGGSFS_I(inode);

    if (!inode_trylock(inode)) {
        if (iocb->ki_flags & IOCB_NOWAIT) {
            return -EAGAIN;
        }
        inode_lock(inode);
    }
    ssize_t res = eggsfs_file_write(enode, iocb->ki_flags, &iocb->ki_pos, from);
    inode_unlock(inode);
    
    return res;
}

int eggsfs_file_flush(struct eggsfs_inode* enode, struct dentry* dentry) {
    inode_lock(&enode->inode);

    int err = 0;

    // Not writing, there's nothing to do, there's nothing to do, files are immutable
    if (enode->file.status != EGGSFS_FILE_STATUS_WRITING) {
        eggsfs_debug("status=%d, won't flush", enode->file.status);
        goto out_early;
    }

    // We are in another process, skip
    if (enode->file.owner != current->group_leader) {
        eggsfs_debug("owner=%p != group_leader=%p, won't flush", enode->file.owner, current->group_leader);
        goto out_early;
    }

    // If the owner doesn't have an mm anymore, it means that the file
    // is being torn down after the process has been terminated. In that case
    // we shouldn't even link the file.
    if (enode->file.owner->mm != enode->file.mm) {
        // abort everything
        atomic_cmpxchg(&enode->file.transient_err, 0, -EINTR);
        goto out;
    }

    // OK, in this case we are indeed linking it, we need to do two things: first wait
    // for the span which is being flushed to be flushed, and then flush the current span,
    // if we have one.
    err = eggsfs_start_flushing(enode, false);
    if (err < 0) { goto out; }

    err = down_killable(&enode->file.flushing_span_sema);
    if (err < 0) { goto out; }

    // the requests might have failed
    err = atomic_read(&enode->file.transient_err);
    if (err < 0) { goto out; }

    // now we actually need to link
    eggsfs_debug("linking file");
    err = eggsfs_error_to_linux(eggsfs_shard_link_file(
        (struct eggsfs_fs_info*)enode->inode.i_sb->s_fs_info, enode->inode.i_ino,
        enode->file.cookie, dentry->d_parent->d_inode->i_ino, dentry->d_name.name, dentry->d_name.len,
        &enode->edge_creation_time
    ));
    if (err < 0) { goto out; }

    // Switch the file to a normal file
    enode->file.status = EGGSFS_FILE_STATUS_READING;

out:
    if (err && err != -EAGAIN) {
        atomic_cmpxchg(&enode->file.transient_err, 0, err);
    }
    // Put spans, if any
    if (err != -EAGAIN) {
        if (enode->file.writing_span != NULL) {
            eggsfs_put_transient_span(enode->file.writing_span);
        }
        enode->file.writing_span = NULL;
    }
out_early:
    inode_unlock(&enode->inode);
    eggsfs_wait_in_flight(enode);
    return err;
}

static int eggsfs_file_flush_internal(struct file* filp, fl_owner_t id) { // can we get write while this is in progress?
    struct eggsfs_inode* enode = EGGSFS_I(filp->f_inode);
    struct dentry* dentry = filp->f_path.dentry;
    return eggsfs_file_flush(enode, dentry);
}

static ssize_t eggsfs_file_read_iter(struct kiocb* iocb, struct iov_iter* to) {
    struct file* file = iocb->ki_filp;
    struct inode* inode = file->f_inode;
    struct eggsfs_inode* enode = EGGSFS_I(inode);
    loff_t *ppos = &iocb->ki_pos;
    ssize_t written = 0;

    if (unlikely(iocb->ki_flags & IOCB_DIRECT)) { return -ENOSYS; }

    eggsfs_debug("start of read loop, *ppos=%llu", *ppos);
    struct eggsfs_span* span = NULL;
    while (*ppos < inode->i_size && iov_iter_count(to)) {
        if (span) { eggsfs_put_span(span, true); }
        span = eggsfs_get_span(enode, *ppos);
        if (IS_ERR(span)) {
            written = PTR_ERR(span);
            span = NULL; // important to not trip the eggsfs_span_put below
            goto out;
        }
        if (span == NULL) { goto out; }
        if (span->start%PAGE_SIZE != 0) {
            eggsfs_warn("span start is not a multiple of page size %llu", span->start);
            written = -EIO;
            goto out;
        }

        eggsfs_debug("span start=%llu end=%llu", span->start, span->end);

        u64 span_offset = *ppos - span->start;
        u64 span_size = span->end - span->start;

        if (span->storage_class == EGGSFS_INLINE_STORAGE) {
            struct eggsfs_inline_span* inline_span = EGGSFS_INLINE_SPAN(span);
            size_t to_copy = span_size - span_offset;
            eggsfs_debug("(inline) copying %lu, have %lu remaining", to_copy, iov_iter_count(to));
            size_t copied =
                copy_to_iter(inline_span->body + span_offset, inline_span->len - span_offset, to) +
                iov_iter_zero(span_size - inline_span->len, to); // trailing zeros
            if (copied < to_copy && iov_iter_count(to)) { written = -EFAULT; goto out; }
            written += copied;
            *ppos += copied;
        } else {
            struct eggsfs_block_span* block_span = EGGSFS_BLOCK_SPAN(span);
            if (block_span->cell_size%PAGE_SIZE != 0) {
                eggsfs_warn("cell size not multiple of page size %u", block_span->cell_size);
                written = -EIO;
                goto out;
            }

            u32 page_ix = span_offset/PAGE_SIZE;
            // how much we should read (e.g. exclude trailing zeros)
            u64 span_data_end = min(
                span->end,
                span->start + (u64)block_span->cell_size*eggsfs_data_blocks(block_span->parity)*block_span->stripes
            );
            eggsfs_debug("span_data_end=%llu", span_data_end);
            while (*ppos < span_data_end && iov_iter_count(to)) {
                struct page* page = eggsfs_get_span_page(block_span, page_ix);
                if (IS_ERR(page)) { written = PTR_ERR(page); goto out; }
                size_t to_copy = min((u64)PAGE_SIZE - (*ppos % PAGE_SIZE), span_data_end - *ppos);
                eggsfs_debug("(block) copying %lu, have %lu remaining", to_copy, iov_iter_count(to));
                size_t copied = copy_page_to_iter(page, *ppos % PAGE_SIZE, to_copy, to);
                eggsfs_debug("copied=%lu, remaining %lu", copied, iov_iter_count(to));
                if (copied < to_copy && iov_iter_count(to)) { written = -EFAULT; goto out; }
                written += copied;
                *ppos += copied;
                page_ix++;
            }
            // trailing zeros
            if (iov_iter_count(to)) {
                size_t to_copy = span->end - span_data_end;
                size_t copied = iov_iter_zero(to_copy, to);
                eggsfs_debug("(block zeroes) copying %lu, have %lu remaining", to_copy, iov_iter_count(to));
                if (copied < to_copy && iov_iter_count(to)) { written = -EFAULT; goto out; }
                written += copied;
                *ppos += copied;
            }
        }
        eggsfs_debug("before loop end, remaining %lu", iov_iter_count(to));
    }
out:
    if (span) { eggsfs_put_span(span, true); }
    eggsfs_debug("out of the loop, written=%ld", written);
    if (unlikely(written < 0)) {
        eggsfs_debug("reading failed, err=%ld", written);
    }
    return written;
}

void eggsfs_link_destructor(void* buf) {
    kfree(buf);
}

char* eggsfs_write_link(struct eggsfs_inode* enode) {
    eggsfs_debug("ino=%016lx", enode->inode.i_ino);

    BUG_ON(eggsfs_inode_type(enode->inode.i_ino) != EGGSFS_INODE_SYMLINK);

    // size might not be filled in
    int err = eggsfs_do_getattr(enode);
    if (err) { return ERR_PTR(err); }

    BUG_ON(enode->inode.i_size > PAGE_SIZE); // for simplicity...
    size_t size = enode->inode.i_size;

    eggsfs_debug("size=%lu", size);

    struct eggsfs_span* span = eggsfs_get_span(enode, 0);
    if (span == NULL) {
        eggsfs_debug("got no span, empty file?");
        return "";
    }
    if (IS_ERR(span)) { return ERR_CAST(span); }

    BUG_ON(span->end-span->start != size); // again, for simplicity

    char* buf = kmalloc(size+1, GFP_KERNEL);
    if (buf == NULL) { err = -ENOMEM; goto out_err; }

    if (span->storage_class == EGGSFS_INLINE_STORAGE) {
        memcpy(buf, EGGSFS_INLINE_SPAN(span)->body, size);
    } else {
        struct eggsfs_block_span* block_span = EGGSFS_BLOCK_SPAN(span);
        struct page* page = eggsfs_get_span_page(block_span, 0);
        BUG_ON(page == NULL);
        if (IS_ERR(page)) { err = PTR_ERR(page); goto out_err; }
        char* page_buf = kmap(page);
        memcpy(buf, page_buf, size);
        kunmap(page);
    }
    buf[size] = '\0';
    eggsfs_put_span(span, true);

    eggsfs_debug("link %*pE", (int)size, buf);

    return buf;

out_err:
    eggsfs_debug("get_link err=%d", err);
    eggsfs_put_span(span, false);
    return ERR_PTR(err);
}

const struct file_operations eggsfs_file_operations = {
    .open = eggsfs_file_open,
    .read_iter = eggsfs_file_read_iter,
    .write_iter = eggsfs_file_write_iter,
    .flush = eggsfs_file_flush_internal,
    .llseek = generic_file_llseek,
};

int __init eggsfs_file_init(void) {
    eggsfs_transient_span_cachep = kmem_cache_create(
        "eggsfs_transient_span_cache",
        sizeof(struct eggsfs_transient_span),
        0,
        SLAB_RECLAIM_ACCOUNT | SLAB_MEM_SPREAD,
        &eggsfs_init_transient_span
    );
    if (!eggsfs_transient_span_cachep) { return -ENOMEM; }
    return 0;
}

void __cold eggsfs_file_exit(void) {
    // TODO: handle case where there still are requests in flight.
    kmem_cache_destroy(eggsfs_transient_span_cachep);
}
