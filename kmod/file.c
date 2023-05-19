#include "file.h"

#include <linux/uio.h>

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

// open_mutex held here
// really want atomic open for this
static int eggsfs_file_open(struct inode* inode, struct file* filp) {
    struct eggsfs_inode* enode = EGGSFS_I(inode);

    eggsfs_debug_print("enode=%p status=%d owner=%p", enode, enode->file.status, current->group_leader);

    if (enode->file.status == EGGSFS_FILE_STATUS_WRITING) {
        // TODO can this ever happen?
        eggsfs_debug_print("trying to open file we're already writing");
        return -ENOENT;
    }

    if (filp->f_mode & FMODE_WRITE) {
        eggsfs_debug_print("opening file for writing");
        if (enode->file.status != EGGSFS_FILE_STATUS_CREATED) {
            eggsfs_debug_print("trying to open for write non-writeable file");
            return -EPERM;
        }
        enode->file.status = EGGSFS_FILE_STATUS_WRITING;
    } else {
        enode->file.status = EGGSFS_FILE_STATUS_READING;
    }

    return 0;
}

struct eggsfs_transient_span* eggsfs_add_new_span(struct list_head* spans) {
    // The zeroing is important: eg we rely on it in `eggsfs_flush_spans` to detect whether we
    // need to compute the parity or not.
    struct eggsfs_transient_span* span = kzalloc(sizeof(struct eggsfs_transient_span), GFP_KERNEL);
    if (!span) {
        return ERR_PTR(-ENOMEM);
    }
    span->status = EGGSFS_SPAN_STATUS_WRITING;
    INIT_LIST_HEAD(&span->pages);
    list_add_tail(&span->list, spans);
    return span;
}

struct initiate_ctx {
    struct eggsfs_inode_file* file;
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
    struct eggsfs_block_socket* sock = eggsfs_get_write_block_socket(&bs);
    if (IS_ERR(sock)) { return PTR_ERR(sock); }
    ctx->span->block_sockets[block] = sock;
    int B = eggsfs_blocks(ctx->span->parity);
    int cell_size = ctx->span->block_size / ctx->span->stripes;
    u32 block_crc = 0;
    int s;
    for (s = 0; s < ctx->span->stripes; s++) {
        block_crc = eggsfs_crc32c_append(block_crc, ctx->cell_crcs[s*B + block], cell_size);
    }
    struct eggsfs_write_block_request* req = eggsfs_write_block(
        sock,
        &ctx->file->flusher,
        block_service_id,
        block_id,
        certificate,
        ctx->span->block_size,
        block_crc,
        ctx->span->blocks[block]
    );
    if (IS_ERR(req)) {
        eggsfs_put_write_block_socket(sock);
        return PTR_ERR(req);
    }
    ctx->span->block_reqs[block] = req;
    return 0;
}

// Checks if the block-writing requests have all finished.
//
// * 0: not finished
// * 1: finished
// * -n: some request has failed with this error.
//
// Also, releases the sockets for requests that are done.
static int eggsfs_block_writing_finished(struct eggsfs_transient_span* span) {
    int status = 1;
    int B = eggsfs_blocks(span->parity);
    int i;
    for (i = 0; i < B; i++) {
        struct eggsfs_write_block_request* req = span->block_reqs[i];
        if (req == NULL) {
            eggsfs_debug_print("%d is NULL", i);
            continue;
        }
        int req_status = atomic_read(&req->status);
        if (req_status < 0) { // error
            eggsfs_warn_print("request for block %d failed: %d", i, req_status);
            status = req_status;
        }
        if (req_status == 0) { // not completed yet
            eggsfs_debug_print("%d not completed yet", i);
            status = status == 1 ? 0 : status; // preserve errors
        }
    }
    return status;
}

static void eggsfs_block_writing_put_reqs(struct eggsfs_transient_span* span) {
    int B = eggsfs_blocks(span->parity);
    int i;
    for (i = 0; i < B; i++) {
        struct eggsfs_write_block_request* req = span->block_reqs[i];
        if (req != NULL) {
            eggsfs_put_write_block_request(req);
            span->block_reqs[i] = NULL;
        }
        struct eggsfs_block_socket* sock = span->block_sockets[i];
        if (sock != NULL) {
            eggsfs_put_write_block_socket(sock);
            span->block_sockets[i] = NULL;
        }
    }
}

static struct page* eggsfs_alloc_write_page(struct eggsfs_inode_file* file) {
    // the zeroing is to just write out to blocks assuming we have trailing
    // zeros, we could do it on demand but I can't be bothered.
    struct page* p = alloc_page(GFP_KERNEL | __GFP_ZERO);
    if (p == NULL) { return p; }
    // this contributes to OOM score
    inc_mm_counter(file->owner->mm, MM_FILEPAGES);
    return p;
}

static void eggsfs_free_transient_span(struct eggsfs_inode_file* file, struct eggsfs_transient_span* span) {
    // free pages
    int num_pages = 0;
    while (!list_empty(&span->pages)) {
        struct page *victim = lru_to_page(&span->pages);
        list_del(&victim->lru);
        put_page(victim);
        num_pages++;
    }
    add_mm_counter(file->owner->mm, MM_FILEPAGES, -num_pages);
    // Free reqs
    eggsfs_block_writing_put_reqs(span);
    // Free the span itself
    kfree(span);
}

// Frees up everything after an error. Takes the inode lock.
static void eggsfs_transient_error_cleanup(struct eggsfs_inode* enode) {
    struct eggsfs_inode_file* file = &enode->file;

    if (atomic_read(&enode->file.transient_err) == 0) {
        eggsfs_warn_print("Error cleanup function is running, but the transient file is not errored. This should not be happening.");
        return;
    }

    // We need to take the exclusive lock to ensure nobody else is writing to the last span.
    // We could even release it immediately afterwards, since afterwards everything else
    // will exit early singe `enode->file.transient_err` is non-zero.
    inode_lock(&enode->inode);
    
    bool finished = false;
    struct eggsfs_transient_span* span;
    for (;;) {
        // Pick out first span (which is also the oldest, but doesn't matter anyway)
        spin_lock(&file->transient_spans_lock);
        span = list_first_entry_or_null(&file->transient_spans, struct eggsfs_transient_span, list);
        if (span == NULL) {
            // we've cleared all the spans
            finished = true;
            break;
        }
        if (eggsfs_block_writing_finished(span) == 0) {
            // The requests need to complete first, they rely on the pages still being there
            // (to read from them). We currently do not interrupt the requests mid-writing.
            // Note that we return here even if we aren't done cleaning up, but `eggsfs_flush_transient_span`
            // will be called back into, which will in turn call this function, continuing the cleanup.
            eggsfs_debug_print("requests not done yet, cleanup not finished");
            break;
        }
        eggsfs_block_writing_put_reqs(span);
        // We can destroy this span.
        list_del(&span->list);
        spin_unlock(&file->transient_spans_lock);
        eggsfs_free_transient_span(file, span);
    }
    spin_unlock(&file->transient_spans_lock);

    inode_unlock(&enode->inode);

    if (finished) {
        eggsfs_debug_print("finished cleaning up, we're done flushing");
        up(&file->done_flushing);
    } else {
        eggsfs_debug_print("not finished cleaning up");
    }
}

void eggsfs_flush_transient_spans(struct work_struct* work) {
    struct eggsfs_inode_file* file = container_of(work, struct eggsfs_inode_file, flusher);
    struct eggsfs_inode* enode = container_of(file, struct eggsfs_inode, file);

    eggsfs_debug_print("ino=%lu", enode->inode.i_ino);

    if (atomic_read(&enode->file.transient_err) != 0) {
        eggsfs_transient_error_cleanup(enode);
        return;
    }

again:
    spin_lock(&file->transient_spans_lock);
    struct eggsfs_transient_span* span = list_first_entry_or_null(&file->transient_spans, struct eggsfs_transient_span, list);
    bool good_to_go = false;
    if (span) {
        if (span->status == EGGSFS_SPAN_STATUS_IDLE) {
            good_to_go = true;
            span->status = EGGSFS_SPAN_STATUS_FLUSHING;
        }
        if (span->status == EGGSFS_SPAN_STATUS_LAST) {
            good_to_go = true;
            span->status = EGGSFS_SPAN_STATUS_FLUSHING_LAST;
        }
    }
    spin_unlock(&file->transient_spans_lock);

    if (!good_to_go) {
        eggsfs_debug_print("oldest span is not idle, terminating");
        return;
    }

    int err;
    u32* cell_crcs = NULL;
    bool done = false;
    if (span->storage_class == EGGSFS_EMPTY_STORAGE) {
        done = true;
    } else if (span->storage_class == EGGSFS_INLINE_STORAGE) {
        // this is an easy one, just add the inline span
        struct page* page = list_first_entry_or_null(&span->pages, struct page, lru);
        if (page == NULL) {
            err = -ENOMEM;
            goto error;
        }
        char* data = kmap(page);
        eggsfs_debug_print("adding inline span of length %d", span->size);
        err = eggsfs_error_to_linux(eggsfs_shard_add_inline_span(
            (struct eggsfs_fs_info*)enode->inode.i_sb->s_fs_info, enode->inode.i_ino, enode->file.cookie,
            atomic64_read(&enode->file.flushed_so_far), span->size, data, span->size
        ));
        kunmap(page);
        if (err) { goto error; }
        done = true;
    } else {
        // This means that it's the first time we see this span to flush
        int D = eggsfs_data_blocks(span->parity);
        int B = eggsfs_blocks(span->parity);
        int S = span->stripes;
        eggsfs_debug_print("size=%d, D=%d, B=%d, S=%d, status=%d", span->size, D, B, S, span->status);
        int i;
        if (span->block_reqs[0] != NULL) {
            // If we have already the sockets/requests, it being callback'd here means
            // that one of the request has finished. Let's check whether they all have.
            int reqs_status = eggsfs_block_writing_finished(span);
            if (reqs_status == 1) { // finished
                u64 block_ids[EGGSFS_MAX_BLOCKS];
                u64 block_proofs[EGGSFS_MAX_BLOCKS];
                for (i = 0; i < B; i++) {
                    struct eggsfs_write_block_request* req = span->block_reqs[i];
                    block_ids[i] = req->block_id;
                    block_proofs[i] = be64_to_cpu(req->written_resp.data.proof);
                }
                eggsfs_block_writing_put_reqs(span); // we got our proof
                err = eggsfs_error_to_linux(eggsfs_shard_add_span_certify(
                    (struct eggsfs_fs_info*)enode->inode.i_sb->s_fs_info,
                    enode->inode.i_ino, file->cookie, atomic64_read(&file->flushed_so_far), span->parity,
                    block_ids, block_proofs
                ));
                if (err) { goto error; }
                done = true;
            } else if (reqs_status < 0) { // errored
                eggsfs_block_writing_put_reqs(span);
                err = reqs_status;
                goto error;
            }
        } else {
            trace_eggsfs_span_flush_enter(
                enode->inode.i_ino, atomic64_read(&file->flushed_so_far), span->size, span->parity, span->stripes, span->storage_class
            );
            // Below, remember that cell size is always a multiple of PAGE_SIZE.
            {
                int data_size;
                for (data_size = span->block_size * D; data_size > span->size; data_size -= PAGE_SIZE) {
                    // This can happen if we have trailing zeros because of how we arrange the
                    // stripes.
                    struct page* zpage = eggsfs_alloc_write_page(file);
                    if (zpage == NULL) {
                        err = -ENOMEM;
                        goto error;
                    }
                    list_add_tail(&zpage->lru, &span->pages);
                }
            }
            struct page* curr_pages[EGGSFS_MAX_DATA+EGGSFS_MAX_PARITY]; // buffer we use at various points
            memset(curr_pages, 0, sizeof(curr_pages));
            int cell_size = span->block_size / S;
            // First, we arrange the data blocks in singly linked lists in `span->blocks`.
            {
                int cell_offset = 0;
                int block = 0;
                struct page* page;
                list_for_each_entry(page, &span->pages, lru) {
                    page->private = 0;
                    if (curr_pages[block] == NULL) { // first
                        span->blocks[block] = page;
                    } else {
                        curr_pages[block]->private = (unsigned long)page;
                    }
                    curr_pages[block] = page;
                    cell_offset += PAGE_SIZE;
                    if (cell_offset >= cell_size) {
                        block++;
                        block = block % D;
                        cell_offset = 0;
                    }
                }
            }
            // Then, we compute the parity blocks.
            // We also compute the cell CRCs at this stage.
            cell_crcs = kzalloc(sizeof(uint32_t)*B*span->stripes, GFP_KERNEL);
            if (cell_crcs == NULL) { err = -ENOMEM; goto error; }
            u32 span_crc = 0;
            {
                char* pages_bufs[EGGSFS_MAX_DATA+EGGSFS_MAX_PARITY];
                memset(curr_pages, 0, sizeof(curr_pages));
                kernel_fpu_begin();
                // traverse each block, page by page, keeping track of which cell we're in.
                u32 block_offset;
                for (block_offset = 0; block_offset < span->block_size; block_offset += PAGE_SIZE) {
                    for (i = 0; i < D; i++) { // grab next data page for each block
                        if (block_offset == 0) { // first page
                            curr_pages[i] = span->blocks[i];
                        } else {
                            curr_pages[i] = (struct page*)curr_pages[i]->private;
                        }
                    }
                    for (i = D; i < B; i++) { // allocate parity pages
                        struct page* ppage = eggsfs_alloc_write_page(file);
                        if (ppage == NULL) { err = -ENOMEM; goto kernel_fpu_error; }
                        list_add_tail(&ppage->lru, &span->pages);
                        if (curr_pages[i] == NULL) {
                            span->blocks[i] = ppage;
                        } else {
                            curr_pages[i]->private = (unsigned long)ppage;
                        }
                        curr_pages[i] = ppage;
                    }
                    // get buffer pointers
                    int s = block_offset / cell_size;
                    for (i = 0; i < B; i++) {
                        pages_bufs[i] = kmap(curr_pages[i]);
                    }
                    // compute parity
                    err = eggsfs_compute_parity(span->parity, PAGE_SIZE, (const char**)&pages_bufs[0], &pages_bufs[D]);
                    if (err) { goto kernel_fpu_error; }
                    // compute CRCs
                    for (i = 0; i < B; i++) {
                        cell_crcs[s*B + i] = eggsfs_crc32c(cell_crcs[s*B + i], pages_bufs[i], PAGE_SIZE);
                    }
                    for (i = 0; i < B; i++) {
                        kunmap(curr_pages[i]);
                    }
                }
                // Compute overall span crc
                int s;
                for (s = 0; s < S; s++) {
                    for (i = 0; i < D; i++) {
                        span_crc = eggsfs_crc32c_append(span_crc, cell_crcs[s*B + i], cell_size);
                    }
                }
                span_crc = eggsfs_crc32c_zero_extend(span_crc, (int)span->size - (int)(span->block_size*D));
                kernel_fpu_end();
            }
            // Now we need to init the span (the callback will actually send the requests)
            // TODO would be better to have this async, and free up the queue for other stuff.
            {
                struct initiate_ctx ctx = {
                    .file = file,
                    .span = span,
                    .cell_crcs = cell_crcs,
                };
                err = eggsfs_error_to_linux(eggsfs_shard_add_span_initiate(
                    (struct eggsfs_fs_info*)enode->inode.i_sb->s_fs_info,
                    &ctx, enode->inode.i_ino, file->cookie, atomic64_read(&file->flushed_so_far), span->size,
                    span_crc, span->storage_class, span->parity, span->stripes, cell_size, cell_crcs
                ));
                if (err) { goto error; }
            }
            kfree(cell_crcs);
            // Now we wait.
            eggsfs_debug_print("sent all block writing requests");
        }
    }

    if (done) {
        // Free everything
        spin_lock(&file->transient_spans_lock);
        list_del(&span->list);
        spin_unlock(&file->transient_spans_lock);
        // safe to do this without locks since there's only ever one
        // work queue working on any given span
        trace_eggsfs_span_flush_exit(enode->inode.i_ino, atomic64_read(&file->flushed_so_far), span->size, 0);
        eggsfs_debug_print("finished span at offset %llu", atomic64_read(&file->flushed_so_far));
        atomic64_add(span->size, &enode->file.flushed_so_far);
        bool all_done = span->status == EGGSFS_SPAN_STATUS_FLUSHING_LAST;
        eggsfs_free_transient_span(file, span);
        up(&file->in_flight_spans);
        if (all_done) {
            eggsfs_debug_print("finished");
            up(&file->done_flushing);
        } else {
            eggsfs_debug_print("going to next span");
            goto again;
        }
    } else {
        eggsfs_debug_print("span not done");
        // restore status so that we can keep working on this
        spin_lock(&file->transient_spans_lock);
        if (span->status == EGGSFS_SPAN_STATUS_FLUSHING) {
            span->status = EGGSFS_SPAN_STATUS_IDLE;
        } else if (span->status == EGGSFS_SPAN_STATUS_FLUSHING_LAST) {
            span->status = EGGSFS_SPAN_STATUS_LAST;
        } else {
            BUG();
        }
        spin_unlock(&file->transient_spans_lock);
    }

    return;

kernel_fpu_error:
    kernel_fpu_end();
error:
    eggsfs_info_print("transient span flushing failed with err=%d", err);
    trace_eggsfs_span_flush_exit(enode->inode.i_ino, atomic64_read(&file->flushed_so_far), span->size, err);
    atomic_set(&enode->file.transient_err, err);
    kfree(cell_crcs);
    eggsfs_transient_error_cleanup(enode);
}

// Runs in the transient_spans_lock spinlock.
static void eggsfs_compute_spans_parameters(
    struct eggsfs_block_policies* block_policies,
    struct eggsfs_span_policies* span_policies,
    u32 target_stripe_size,
    struct eggsfs_transient_span* span,
    u32 span_size
) {
    eggsfs_debug_print("span_size=%u", span_size);

    span->size = span_size;

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

static ssize_t eggsfs_file_write_iter(struct kiocb* iocb, struct iov_iter* from) {
    int err;
    if (iocb->ki_flags & IOCB_DIRECT) { return -ENOSYS; }

    struct file* file = iocb->ki_filp;
    struct inode* inode = file->f_inode;
    struct eggsfs_inode* enode = EGGSFS_I(inode);
    loff_t* ppos = &iocb->ki_pos;
    loff_t ppos_before = *ppos;

    eggsfs_debug_print("enode=%p, ino=%lu, count=%lu, size=%lld, *ppos=%lld", enode, inode->i_ino, iov_iter_count(from), enode->inode.i_size, *ppos);

    if (!inode_trylock(inode)) {
        if (iocb->ki_flags & IOCB_NOWAIT) {
            return -EAGAIN;
        }
        inode_lock(inode);
    }

    if (enode->file.status != EGGSFS_FILE_STATUS_WRITING) { err = -EPERM; goto out_err; }

    // permanent error
    err = atomic_read(&enode->file.transient_err);
    if (err) { goto out_err; }

    // we're writing at the right offset
    if (*ppos != enode->inode.i_size) {
        err = -EINVAL;
        goto out_err;
    }

    // if it's a zero write, then we exit early
    if (unlikely(!iov_iter_count(from))) { goto out; }

    // Precompute the bound for spans
    u32 max_span_size;
    {
        u8 p;
        eggsfs_span_policies_last(&enode->span_policies, &max_span_size, &p);
    }
    BUG_ON(max_span_size%PAGE_SIZE != 0); // needed for "new span" logic paired with page copying below, we could avoid it

    // get the span we're currently writing at
    spin_lock(&enode->file.transient_spans_lock);
    BUG_ON(list_empty(&enode->file.transient_spans));
    struct eggsfs_transient_span* span = list_last_entry(&enode->file.transient_spans, struct eggsfs_transient_span, list);
    int status = span->status;
    spin_unlock(&enode->file.transient_spans_lock);

    if (status != EGGSFS_SPAN_STATUS_WRITING) { err = -EPERM; goto out_err; } // we've already declared this file done, we're just flushing it

    // We now start writing into the span, as much as we can anyway
    BUG_ON(!iov_iter_count(from)); // we rely on making progress for the switch to next spage below (page->index == 0)
    while (true) {
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

        int ret = copy_page_from_iter(page, page->index, PAGE_SIZE - page->index, from);
        if (ret < 0) { err = ret; goto out_err_permanent; }
        eggsfs_debug_print("written %d to page %p", ret, page);
        enode->inode.i_size += ret;
        page->index = (page->index + ret) % PAGE_SIZE;
        *ppos += ret;

        u32 current_span_size = enode->inode.i_size - enode->file.size_without_current_span;
        if (current_span_size >= max_span_size) { // we need to switch to the next span
            BUG_ON(current_span_size > max_span_size);
            if (down_trylock(&enode->file.in_flight_spans) == 1) {
                if (iocb->ki_flags & IOCB_NOWAIT) {
                    err = -EAGAIN;
                    goto out_err;
                }
                int err = down_killable(&enode->file.in_flight_spans);
                if (err < 0) { goto out_err; }
            }
            spin_lock(&enode->file.transient_spans_lock);
            span->status = EGGSFS_SPAN_STATUS_IDLE;
            eggsfs_compute_spans_parameters(
                &enode->block_policies, &enode->span_policies, enode->target_stripe_size,
                span, current_span_size
            );
            struct eggsfs_transient_span* new_span = eggsfs_add_new_span(&enode->file.transient_spans);
            spin_unlock(&enode->file.transient_spans_lock);
            enode->file.size_without_current_span += current_span_size;
            if (IS_ERR(new_span)) {
                err = PTR_ERR(new_span);
                // This is permanent otherwise we'd break the invariant of always having a WRITING span at
                // the end.
                goto out_err_permanent;
            }
            trace_eggsfs_span_add(enode->inode.i_ino, *ppos);
            queue_work(eggsfs_wq, &enode->file.flusher);
            span = new_span;
        }
    
        if (!iov_iter_count(from)) { break; }
    }

    int written;
out:
    written = *ppos - ppos_before;
    inode_unlock(inode);

    eggsfs_debug_print("written=%d", written);

    return written;

out_err_permanent:
    atomic_set(&enode->file.transient_err, err);
    // we need to free the yet-to-be-written spans
    queue_work(eggsfs_wq, &enode->file.flusher);

out_err:
    inode_unlock(inode);
    eggsfs_info_print("err=%d", err);
    if (err == -EAGAIN && *ppos > ppos_before) {
        goto out; // we can just return what we've already written
    }
    return err;
}

// * 0: skip (not the right owner)
// * 1: proceed
// * -n: error
__must_check static int eggsfs_file_flush_init(struct eggsfs_inode* enode) {
    int res;

    inode_lock(&enode->inode);

    // Not writing, there's nothing to do, there's nothing to do, files are immutable
    if (enode->file.status != EGGSFS_FILE_STATUS_WRITING) {
        eggsfs_debug_print("status=%d, won't flush", enode->file.status);
        res = 0;
        goto out;
    }
    // We are in another process, skip
    if (enode->file.owner != current->group_leader) {
        eggsfs_debug_print("owner=%p != group_leader=%p, won't flush", enode->file.owner, current->group_leader);
        res = 0;
        goto out;
    }

    // Mark the last span as the last one, and queue work
    spin_lock(&enode->file.transient_spans_lock);
    BUG_ON(list_empty(&enode->file.transient_spans));
    struct eggsfs_transient_span* span = list_last_entry(&enode->file.transient_spans, struct eggsfs_transient_span, list);
    // we might not be the first ones here -- every other operation is fine to do twice anyway.
    bool needs_queueing = false;
    if (span->status == EGGSFS_SPAN_STATUS_WRITING) {
        needs_queueing = true;
        u32 current_span_size = enode->inode.i_size - enode->file.size_without_current_span;
        span->status = EGGSFS_SPAN_STATUS_LAST;
        eggsfs_debug_print("finishing with %u", current_span_size);
        eggsfs_compute_spans_parameters(
            &enode->block_policies, &enode->span_policies, enode->target_stripe_size,
            span, current_span_size
        );
        enode->file.size_without_current_span += current_span_size;
    }
    spin_unlock(&enode->file.transient_spans_lock);

    inode_unlock(&enode->inode);

    if (needs_queueing) {
        queue_work(eggsfs_wq, &enode->file.flusher);
    }
    return 1;

out:
    inode_unlock(&enode->inode);
    return res;
}

static int eggsfs_file_flush(struct file* filp, fl_owner_t id) { // can we get write while this is in progress?
    struct eggsfs_inode* enode = EGGSFS_I(filp->f_inode);
    struct dentry* dentry = filp->f_path.dentry;

    int res = eggsfs_file_flush_init(enode);
    if (res == 0) { return 0; }
    if (res < 0) { return res; }

    // Now we need to wait for things to be done
    eggsfs_debug_print("waiting for flush");
    res = down_killable(&enode->file.done_flushing);
    if (res < 0) { return res; }
    up(&enode->file.done_flushing); // allow others to wait on it again
    eggsfs_debug_print("flush done");

    res = atomic_read(&enode->file.transient_err);
    if (res < 0) { return res; }

    inode_lock(&enode->inode);
    // somebody might have gotten here before us
    if (enode->file.status == EGGSFS_FILE_STATUS_WRITING) {
        eggsfs_debug_print("linking file");
        res = eggsfs_error_to_linux(eggsfs_shard_link_file(
            (struct eggsfs_fs_info*)enode->inode.i_sb->s_fs_info, enode->inode.i_ino,
            enode->file.cookie, dentry->d_parent->d_inode->i_ino, dentry->d_name.name, dentry->d_name.len,
            &enode->edge_creation_time
        )); 
        if (res == 0) {
            enode->file.status = EGGSFS_FILE_STATUS_READING;
        }
    } else {
        eggsfs_debug_print("not linking file, status=%d", enode->file.status);
    }
    inode_unlock(&enode->inode);

    if (unlikely(res < 0)) {
        eggsfs_debug_print("flushing failed, err=%d", res);
    }
    return res;
}

static ssize_t eggsfs_file_read_iter(struct kiocb* iocb, struct iov_iter* to) {
    struct file* file = iocb->ki_filp;
    struct inode* inode = file->f_inode;
    struct eggsfs_inode* enode = EGGSFS_I(inode);
    loff_t *ppos = &iocb->ki_pos;
    ssize_t written = 0;

    if (unlikely(iocb->ki_flags & IOCB_DIRECT)) { return -ENOSYS; }

    eggsfs_debug_print("start of read loop, *ppos=%llu", *ppos);
    while (*ppos < inode->i_size && iov_iter_count(to)) {
        struct eggsfs_span* span = eggsfs_get_span(enode, *ppos);
        if (IS_ERR(span)) { written = PTR_ERR(span); goto out; }
        if (span == NULL) { goto out; }
        if (span->start%PAGE_SIZE != 0) {
            eggsfs_warn_print("span start is not a multiple of page size %llu", span->start);
            written = -EIO;
            goto out;
        }

        eggsfs_debug_print("span start=%llu end=%llu", span->start, span->end);

        u64 span_offset = *ppos - span->start;
        u64 span_size = span->end - span->start;

        if (span->storage_class == EGGSFS_INLINE_STORAGE) {
            struct eggsfs_inline_span* inline_span = EGGSFS_INLINE_SPAN(span);
            size_t to_copy = span_size - span_offset;
            eggsfs_debug_print("(inline) copying %lu, have %lu remaining", to_copy, iov_iter_count(to));
            size_t copied =
                copy_to_iter(inline_span->body + span_offset, inline_span->len - span_offset, to) +
                iov_iter_zero(span_size - inline_span->len, to); // trailing zeros
            if (copied < to_copy && iov_iter_count(to)) { written = -EFAULT; goto out; }
            written += copied;
            *ppos += copied;
        } else {
            struct eggsfs_block_span* block_span = EGGSFS_BLOCK_SPAN(span);
            if (block_span->cell_size%PAGE_SIZE != 0) {
                eggsfs_warn_print("cell size not multiple of page size %u", block_span->cell_size);
                return -EIO;
            }

            u32 page_ix = span_offset/PAGE_SIZE;
            // how much we should read (e.g. exclude trailing zeros)
            u64 span_data_end = min(
                span->end,
                span->start + (u64)block_span->cell_size*eggsfs_data_blocks(block_span->parity)*block_span->stripes
            );
            eggsfs_debug_print("span_data_end=%llu", span_data_end);
            while (*ppos < span_data_end && iov_iter_count(to)) {
                struct page* page = eggsfs_get_span_page(block_span, page_ix);
                if (IS_ERR(page)) { written = PTR_ERR(page); goto out; }
                size_t to_copy = min((u64)PAGE_SIZE - (*ppos % PAGE_SIZE), span_data_end - *ppos);
                eggsfs_debug_print("(block) copying %lu, have %lu remaining", to_copy, iov_iter_count(to));
                size_t copied = copy_page_to_iter(page, *ppos % PAGE_SIZE, to_copy, to);
                eggsfs_debug_print("copied=%lu, remaining %lu", copied, iov_iter_count(to));
                if (copied < to_copy && iov_iter_count(to)) { written = -EFAULT; goto out; }
                written += copied;
                *ppos += copied;
                page_ix++;
            }
            // trailing zeros
            if (iov_iter_count(to)) {
                size_t to_copy = span->end - span_data_end;
                size_t copied = iov_iter_zero(to_copy, to);
                eggsfs_debug_print("(block zeroes) copying %lu, have %lu remaining", to_copy, iov_iter_count(to));
                if (copied < to_copy && iov_iter_count(to)) { written = -EFAULT; goto out; }
                written += copied;
                *ppos += copied;
            }
        }
        eggsfs_debug_print("before loop end, remaining %lu", iov_iter_count(to));
    }
out:
    eggsfs_debug_print("out of the loop, written=%ld", written);
    if (written < 0) {
        eggsfs_debug_print("reading failed, err=%ld", written);
    }
    return written;
}

const struct file_operations eggsfs_filesimple_operations = {
    .open = eggsfs_file_open,
    .read_iter = eggsfs_file_read_iter,
    .write_iter = eggsfs_file_write_iter,
    .flush = eggsfs_file_flush,
    .llseek = generic_file_llseek,
};
