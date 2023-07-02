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
    u32 written; // how much we've written to this span
    atomic_t refcount;

    // These are finalized when we start flushing out a span.
    char failure_domains[EGGSFS_MAX_BLOCKS][16]; // used to decide which to avoid in subsequent runs
    struct list_head blocks[EGGSFS_MAX_BLOCKS]; // the pages for each block
    u64 block_ids[EGGSFS_MAX_BLOCKS]; // the block ids assigned by add span initiate
    spinlock_t lock; // we use this for various modifications
    u64 blocks_proofs[EGGSFS_MAX_BLOCKS]; // when completing blocks, one of proof or err is set.
    int blocks_errs[EGGSFS_MAX_BLOCKS];
    u32 span_crc;
    u32 cell_crcs[EGGSFS_MAX_BLOCKS*EGGSFS_MAX_STRIPES];
    u32 block_size;
    u8 attempts;
    u8 stripes;
    u8 storage_class;
    u8 parity;
};
// just a sanity check, this is just two per page rn
static_assert(sizeof(struct eggsfs_transient_span) < (2<<10));

// open_mutex held here
// really want atomic open for this
static int file_open(struct inode* inode, struct file* filp) {
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

static void init_transient_span(void* p) {
    struct eggsfs_transient_span* span = (struct eggsfs_transient_span*)p;
    INIT_LIST_HEAD(&span->pages);
    int i;
    for (i = 0; i < EGGSFS_MAX_BLOCKS; i++) {
        INIT_LIST_HEAD(&span->blocks[i]);
    }
    spin_lock_init(&span->lock);
}

// starts out with refcount = 1
static struct eggsfs_transient_span* new_transient_span(struct eggsfs_inode* enode, u64 offset) {
    struct eggsfs_transient_span* span = kmem_cache_alloc(eggsfs_transient_span_cachep, GFP_KERNEL);
    if (span == NULL) { return span; }
    span->enode = enode;
    eggsfs_in_flight_begin(enode);
    atomic_set(&span->refcount, 1);
    span->offset = offset;
    span->written = 0;
    memset(span->failure_domains, 0, sizeof(span->failure_domains));
    memset(span->blocks_proofs, 0, sizeof(span->blocks_proofs));
    memset(span->blocks_errs, 0, sizeof(span->blocks_errs));
    span->span_crc = 0;
    memset(span->cell_crcs, 0, sizeof(span->cell_crcs));
    span->attempts = 0;
    return span;
}

static void hold_transient_span(struct eggsfs_transient_span* span) {
    atomic_inc(&span->refcount);
}

static void put_transient_span(struct eggsfs_transient_span* span) {
    if (atomic_dec_return(&span->refcount) == 0) {
        BUG_ON(spin_is_locked(&span->lock));
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

static int add_span_initiate(struct eggsfs_transient_span* span);

static int move_span_and_restart(struct eggsfs_transient_span* span) {
    struct eggsfs_inode* enode = span->enode;

    // create new file
    u64 ino;
    u64 cookie;
    const char* name = "move_span";
    int err = eggsfs_error_to_linux(eggsfs_shard_create_file(
        (struct eggsfs_fs_info*)enode->inode.i_sb->s_fs_info,
        eggsfs_inode_shard(enode->inode.i_ino), EGGSFS_INODE_FILE, name, strlen(name), &ino, &cookie
    ));
    if (err) { goto out_err; }

    // move span
    err = eggsfs_error_to_linux(eggsfs_shard_move_span(
        (struct eggsfs_fs_info*)enode->inode.i_sb->s_fs_info,
        enode->inode.i_ino, span->offset, enode->file.cookie, ino, 0, cookie, span->written
    ));
    if (err) { goto out_err; }

    // go for it
    err = add_span_initiate(span);
    if (err) { goto out_err; }

    return 0;

out_err:
    eggsfs_info("moving span failed %d", err);
    return err;
}

static int retry_after_block_error(struct eggsfs_transient_span* span) {
    int i;
    int B = eggsfs_blocks(span->parity);
    int err = 0;

    if (span->attempts < eggsfs_max_write_span_attempts) {
        eggsfs_info("writing span failed after %d attempts, will try again", span->attempts);
        return move_span_and_restart(span);
    } else {
        for (i = 0; i < B; i++) {
            if (span->blocks_errs[i]) {
                if (err) {
                    eggsfs_info("dropping err %d, since we already returned %d", span->blocks_errs[i], err);
                } else {
                    err = span->blocks_errs[i];
                }
            }
        }
        BUG_ON(err == 0);
        eggsfs_warn("writing span failed after %d attempts with err %d, giving up", span->attempts, err);
        return err;
    }
}

static void write_block_finalize(struct eggsfs_transient_span* span, int b, u64 proof, int block_write_err) {
    int i;
    int B = eggsfs_blocks(span->parity);
    struct eggsfs_inode* enode = span->enode;

    eggsfs_debug("block=%d proof=%016llx err=%d", b, proof, block_write_err);

    bool finished_writing = true;
    bool any_block_errs = false;
    // Mark the current one as done/error out, and check if we're done with all block
    // writing requests.
    spin_lock_bh(&span->lock);
    for (i = 0; i < B; i++) {
        BUG_ON(span->block_ids[i] == 0);
        if (i == b) {
            if (block_write_err == 0) {
                span->blocks_proofs[i] = proof;
            } else {
                span->blocks_errs[i] = block_write_err;
            }
        } else if (span->blocks_proofs[i] == 0 && span->blocks_errs[i] == 0) {
            // we're waiting on some other block proof.
            finished_writing = false;
        }
        any_block_errs = any_block_errs || (span->blocks_errs[i] != 0);
    }
    spin_unlock_bh(&span->lock);

    int err = 0;
    if (finished_writing && likely(!any_block_errs)) {
        // TODO just like in the other case, would be nicer to have this async, since we're in a queue here.
        err = eggsfs_error_to_linux(eggsfs_shard_add_span_certify(
            (struct eggsfs_fs_info*)enode->inode.i_sb->s_fs_info,
            enode->inode.i_ino, enode->file.cookie, span->offset, span->parity,
            span->block_ids, span->blocks_proofs
        ));
    }

    if (finished_writing) {
        span->attempts++;
        if (unlikely(err == 0 && any_block_errs)) {
            // if we failed writing blocks, consider retrying
            err = retry_after_block_error(span);
            if (err == 0) {
                // we've successfully retried, so not done yet.
                goto out;
            }
        }
        atomic_cmpxchg(&enode->file.transient_err, 0, err); // store error if we have one
        up(&span->enode->file.flushing_span_sema); // wake up waiters
    }

out:
    put_transient_span(span);
    return;
}

static void write_block_done(void* data, struct list_head* pages, u64 block_id, u64 proof, int block_write_err) {
    // We need to be careful with locking here: the writer waits on the
    // flushing semaphore while holding the inode lock.

    // we use 0 to mean "no proof yet"
    if (unlikely(block_write_err == 0 && proof == 0)) {
        block_write_err = -EIO; // we need to get _very_ unlucky for this to happen!
    }

    struct eggsfs_transient_span* span = (struct eggsfs_transient_span*)data;

    eggsfs_debug("block_id=%016llx proof=%016llx err=%d", block_id, proof, block_write_err);

    int B = eggsfs_blocks(span->parity);

    // find the index that concerns us now
    int b;
    for (b = 0; b < B; b++) {
        if (span->block_ids[b] == block_id) {
            break;
        }
    }
    BUG_ON(b == B); // we found the block
    BUG_ON(span->blocks_proofs[b] != 0); // no proof or err already
    BUG_ON(span->blocks_errs[b] != 0);

    // re-acquire the pages
    list_replace_init(pages, &span->blocks[b]);

    write_block_finalize(span, b, proof, block_write_err);
}

static int add_span_initiate(struct eggsfs_transient_span* span) {
    int i;
    struct eggsfs_inode* enode = span->enode;
    int B = eggsfs_blocks(span->parity);
    
    int cell_size = span->block_size / span->stripes;

    eggsfs_debug("add span initiate");

    // fill in blacklist
    char blacklist[EGGSFS_MAX_BLACKLIST_LENGTH][16];
    u16 blacklist_length = 0;
    for (i = 0; i < B; i++) {
        if (span->blocks_errs[i]) {
            if (blacklist_length >= EGGSFS_MAX_BLACKLIST_LENGTH) {
                eggsfs_info("could not fit blacklist! this almost certainly means we're screwed anyway");
                return -EIO;
            }
            static_assert(sizeof(blacklist[blacklist_length]) == sizeof(span->failure_domains[i]));
            memcpy(blacklist[blacklist_length], span->failure_domains[i], sizeof(span->failure_domains[i]));
            blacklist_length++;
        }
    }

    // reset stuff (including bad failure domains themselves)
    memset(span->block_ids, 0, sizeof(span->block_ids));
    memset(span->failure_domains, 0, sizeof(span->failure_domains));
    memset(span->blocks_proofs, 0, sizeof(span->blocks_proofs));
    memset(span->blocks_errs, 0, sizeof(span->blocks_errs));

    struct eggsfs_add_span_initiate_block blocks[EGGSFS_MAX_BLOCKS];
    int err = eggsfs_error_to_linux(eggsfs_shard_add_span_initiate(
        (struct eggsfs_fs_info*)enode->inode.i_sb->s_fs_info,
        span, enode->inode.i_ino, enode->file.cookie, span->offset, span->written,
        span->span_crc, span->storage_class, span->parity, span->stripes, cell_size, span->cell_crcs,
        blacklist_length, blacklist,
        blocks
    ));
    if (unlikely(err)) {
        // this means that we've failed to do the metadata (not the blocks), we can't recover from this
        return err;
    }

    // send all the requests. at this point we never fail, we instead defer
    // failure to the block requests. we intentionally let all the requests
    // reach completion (rather than erroring on the first one) to try to
    // clear out all bad failure domains in one go.
    for (i = 0; i < B; i++) {
        // first mark the downloading blocks. required to do this upfront
        // so that we'll mark everything as finished only once we've gone
        // through all the requests.
        struct eggsfs_add_span_initiate_block* block = &blocks[i];
        span->block_ids[i] = block->block_id;
        static_assert(sizeof(span->failure_domains[i]) == sizeof(block->failure_domain));
        memcpy(span->failure_domains[i], block->failure_domain, sizeof(block->failure_domain));
    }
    for (i = 0; i < B; i++) { // then start downloading
        struct eggsfs_add_span_initiate_block* block = &blocks[i];
        u32 block_crc = 0;
        int s;
        for (s = 0; s < span->stripes; s++) {
            block_crc = eggsfs_crc32c_append(block_crc, span->cell_crcs[s*B + i], cell_size);
        }
        struct eggsfs_block_service bs = {
            .id = block->block_service_id,
            .ip1 = block->ip1,
            .port1 = block->port1,
            .ip2 = block->ip2,
            .port2 = block->port2,
        };
        int err = eggsfs_write_block(
            &write_block_done,
            span,
            &bs,
            block->block_id,
            block->certificate,
            span->block_size,
            block_crc,
            &span->blocks[i]
        );
        hold_transient_span(span); // for the callback when block is done
        if (err) { // "finish" immediately, we've errored out
            write_block_finalize(span, i, 0, err);
        }
    }

    // Now we wait.
    eggsfs_debug("sent all block writing requests");
    return 0;
}

static struct page* alloc_write_page(struct eggsfs_inode_file* file) {
    // the zeroing is to just write out to blocks assuming we have trailing
    // zeros, we could do it on demand but I can't be bothered.
    struct page* p = alloc_page(GFP_KERNEL | __GFP_ZERO);
    if (p == NULL) { return p; }
    // This contributes to OOM score.
    inc_mm_counter(file->mm, MM_FILEPAGES);
    return p;
}

static void compute_span_parameters(
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
static int wait_flushed(struct eggsfs_inode* enode, bool non_blocking) {
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

static int write_blocks(struct eggsfs_transient_span* span) {
    int i, j;
    int err = 0;

    struct eggsfs_inode* enode = span->enode;

    // transient file is already borked
    err = atomic_read(&enode->file.transient_err);
    if (err) { goto out; }

    int D = eggsfs_data_blocks(span->parity);
    int B = eggsfs_blocks(span->parity);
    int S = span->stripes;

    eggsfs_debug("size=%d, D=%d, B=%d, S=%d", span->written, D, B, S);
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
        struct page* zpage = alloc_write_page(&enode->file);
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
            struct page* ppage = alloc_write_page(&enode->file);
            if (ppage == NULL) { err = -ENOMEM; goto out; }
            list_add_tail(&ppage->lru, &span->blocks[i]);
        }
    }
    eggsfs_debug("computing parity");
    // Then, we compute the parity blocks and the CRCs for each block.
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
                span->cell_crcs[s*B + i] = eggsfs_crc32c(span->cell_crcs[s*B + i], pages_bufs[i], PAGE_SIZE);
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
                span->span_crc = eggsfs_crc32c_append(span->span_crc, span->cell_crcs[s*B + i], cell_size);
            }
        }
        span->span_crc = eggsfs_crc32c_zero_extend(span->span_crc, (int)span->written - (int)(span->block_size*D));
        kernel_fpu_end();
    }
    // Start the first attempt
    // Now we need to init the span (the callback will actually send the requests)
    // TODO would be better to have this async, and free up the queue for other stuff,
    // and also not block in this call which might be in a non-blocking write.
    // TODO Isn't this called on file write, and therefore not taking up time in
    // the queue? I think the comment above is partially outdated.
    err = add_span_initiate(span);

out:
    if (err) { // we've failed before trying to write blocks, terminate immediately
        atomic_cmpxchg(&enode->file.transient_err, 0, err); // store error if we have one
        up(&span->enode->file.flushing_span_sema); // wake up waiters
    }
    return err;

out_err_fpu:
    kernel_fpu_end();
    goto out;
}

// To be called with the inode lock.
static int start_flushing(struct eggsfs_inode* enode, bool non_blocking) {
    BUG_ON(!inode_is_locked(&enode->inode));

    // Wait for the existing thing to be flushed
    int err = wait_flushed(enode, non_blocking);
    if (err < 0) { return err; }

    // Now turn the writing span into a flushing span
    struct eggsfs_transient_span* span = enode->file.writing_span;
    if (span == NULL) {
        up(&enode->file.flushing_span_sema);
        return 0;
    }

    compute_span_parameters(
        &enode->block_policies, &enode->span_policies, enode->target_stripe_size, span
    );

    // Here (and in `write_blocks`) we might block even if we're non
    // blocking, since we don't have async metadata requests yet, which is
    // a bit unfortunate.

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
        err = write_blocks(span);
        if (err) { goto out; }
    }

out:
    if (err) {
        eggsfs_info("attempting to flush failed permanently, err=%d ino=%016lx", err, enode->inode.i_ino);
        atomic_cmpxchg(&enode->file.transient_err, 0, err);
    }
    // we don't need this anymore (the requests might though)
    put_transient_span(span);
    enode->file.writing_span = NULL;
    return err;

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
        enode->file.writing_span = new_transient_span(enode, enode->inode.i_size);
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
            page = alloc_write_page(&enode->file);
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
        err = start_flushing(enode, !!(flags&IOCB_NOWAIT));
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

static ssize_t file_write_iter(struct kiocb* iocb, struct iov_iter* from) {
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
    err = start_flushing(enode, false);
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
            put_transient_span(enode->file.writing_span);
        }
        enode->file.writing_span = NULL;
    }
out_early:
    inode_unlock(&enode->inode);
    eggsfs_wait_in_flight(enode);
    return err;
}

static int file_flush_internal(struct file* filp, fl_owner_t id) { // can we get write while this is in progress?
    struct eggsfs_inode* enode = EGGSFS_I(filp->f_inode);
    struct dentry* dentry = filp->f_path.dentry;
    return eggsfs_file_flush(enode, dentry);
}

static ssize_t file_read_iter(struct kiocb* iocb, struct iov_iter* to) {
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
    .open = file_open,
    .read_iter = file_read_iter,
    .write_iter = file_write_iter,
    .flush = file_flush_internal,
    .llseek = generic_file_llseek,
};

int __init eggsfs_file_init(void) {
    eggsfs_transient_span_cachep = kmem_cache_create(
        "eggsfs_transient_span_cache",
        sizeof(struct eggsfs_transient_span),
        0,
        SLAB_RECLAIM_ACCOUNT | SLAB_MEM_SPREAD,
        &init_transient_span
    );
    if (!eggsfs_transient_span_cachep) { return -ENOMEM; }
    return 0;
}

void __cold eggsfs_file_exit(void) {
    // TODO: handle case where there still are requests in flight.
    kmem_cache_destroy(eggsfs_transient_span_cachep);
}
