#include "bincode.h"
#include "log.h"
#include "span.h"
#include "metadata.h"
#include "rs.h"
#include "latch.h"
#include "crc.h"
#include "err.h"
#include "counter.h"

EGGSFS_DEFINE_COUNTER(eggsfs_stat_cached_spans);
EGGSFS_DEFINE_COUNTER(eggsfs_stat_cached_span_pages);

struct eggsfs_span_lru {
    spinlock_t lock ____cacheline_aligned;
    struct list_head lru ____cacheline_aligned;
};

// These are global locks, but the sharding should alleviate contention.
#define EGGSFS_SPAN_BITS 7
#define EGGSFS_SPAN_LRUS (1<<EGGSFS_SPAN_BITS) // 128

static struct eggsfs_span_lru eggsfs_span_lrus[EGGSFS_SPAN_LRUS];

static struct eggsfs_span_lru* eggsfs_get_span_lru(struct eggsfs_span* span) {
    int h;
    h  = hash_64(span->enode->inode.i_ino, EGGSFS_SPAN_BITS);
    h ^= hash_64(span->start, EGGSFS_SPAN_BITS);
    return &eggsfs_span_lrus[h%EGGSFS_SPAN_LRUS];
}

static struct eggsfs_span* eggsfs_lookup_span(struct rb_root* spans, u64 offset) {
    struct rb_node* node = spans->rb_node;
    while (node) {
        struct eggsfs_span* span = container_of(node, struct eggsfs_span, node);
        if (offset < span->start) { node = node->rb_left; }
        else if (offset >= span->end) { node = node->rb_right; }
        else {
            eggsfs_debug_print("off=%llu span=%p", offset, span);
            return span;
        }
    }
    eggsfs_debug_print("off=%llu no_span", offset);
    return NULL;
}

static bool eggsfs_insert_span(struct rb_root* spans, struct eggsfs_span* span) {
    struct rb_node** new = &(spans->rb_node);
    struct rb_node* parent = NULL;
    while (*new) {
        struct eggsfs_span* this = container_of(*new, struct eggsfs_span, node);
        parent = *new;
        if (span->end <= this->start) { new = &((*new)->rb_left); }
        else if (span->start >= this->end) { new = &((*new)->rb_right); }
        else {
            // TODO: be loud if there are overlaping spans
            eggsfs_debug_print("span=%p (%llu-%llu) already present", span, span->start, span->end);
            return false;
        }
    }

    rb_link_node(&span->node, parent, new);
    rb_insert_color(&span->node, spans);
    eggsfs_debug_print("span=%p (%llu-%llu) inserted", span, span->start, span->end);
    return true;
}

static void eggsfs_free_span(struct eggsfs_span* span) {
    if (span->storage_class == EGGSFS_INLINE_STORAGE) {
        kfree(EGGSFS_INLINE_SPAN(span));
    } else {
        kfree(EGGSFS_BLOCK_SPAN(span));
    }
}

struct eggsfs_get_span_ctx {
    struct eggsfs_inode* enode;
    struct list_head spans;
    int err;
};

void eggsfs_file_spans_cb_span(void* data, u64 offset, u32 size, u32 crc, u8 storage_class, u8 parity, u8 stripes, u32 cell_size, const uint32_t* stripes_crcs) {
    eggsfs_debug_print("offset=%llu size=%u crc=%08x storage_class=%d parity=%d stripes=%d cell_size=%u", offset, size, crc, storage_class, parity, stripes, cell_size);

    struct eggsfs_get_span_ctx* ctx = (struct eggsfs_get_span_ctx*)data;
    if (ctx->err) { return; }

    struct eggsfs_block_span* span = kmalloc(sizeof(struct eggsfs_block_span), GFP_KERNEL);
    if (!span) { ctx->err = -ENOMEM; return; }

    if (eggsfs_data_blocks(parity) > EGGSFS_MAX_DATA || eggsfs_parity_blocks(parity) > EGGSFS_MAX_PARITY) {
        eggsfs_warn_print("D=%d > %d || P=%d > %d", eggsfs_data_blocks(parity), EGGSFS_MAX_DATA, eggsfs_data_blocks(parity), EGGSFS_MAX_PARITY);
        ctx->err = -EIO;
        return;
    }

    span->span.enode = ctx->enode;
    span->span.start = offset;
    span->span.end = offset + size;
    span->span.storage_class = storage_class;

    xa_init(&span->pages);
    int i;
    for (i = 0; i < stripes; i++) {
        eggsfs_latch_init(&span->stripe_latches[i]);
    }
    span->cell_size = cell_size;
    memcpy(span->stripes_crc, stripes_crcs, sizeof(uint32_t)*stripes);
    span->stripes = stripes;
    span->parity = parity;
    // important, this will have readers skip over this until it ends up in the LRU
    span->readers = -1;

    eggsfs_debug_print("adding normal span");
    list_add_tail(&span->span.lru, &ctx->spans);
}

void eggsfs_file_spans_cb_block(
    void* data, int block_ix,
    u64 bs_id, u32 ip1, u16 port1, u32 ip2, u16 port2,
    u64 block_id, u32 crc
) {
    struct eggsfs_get_span_ctx* ctx = (struct eggsfs_get_span_ctx*)data;
    if (ctx->err) { return; }

    struct eggsfs_span* span = list_last_entry(&ctx->spans, struct eggsfs_span, lru);
    struct eggsfs_block_span* block_span = EGGSFS_BLOCK_SPAN(span);

    struct eggsfs_block* block = &block_span->blocks[block_ix];
    block->bs.id = bs_id;
    block->bs.ip1 = ip1;
    block->bs.port1 = port1;
    block->bs.ip2 = ip2;
    block->bs.port2 = port2;
    block->id = block_id;
    block->crc = crc;
}

void eggsfs_file_spans_cb_inline_span(void* data, u64 offset, u32 size, u8 len, const char* body) {
    eggsfs_debug_print("offset=%llu size=%u len=%u body=%*pE", offset, size, len, len, body);

    struct eggsfs_get_span_ctx* ctx = (struct eggsfs_get_span_ctx*)data;
    if (ctx->err) { return; }
 
    struct eggsfs_inline_span* span = kmalloc(sizeof(struct eggsfs_inline_span), GFP_KERNEL);
    if (!span) { ctx->err = -ENOMEM; return; }

    span->span.enode = ctx->enode;
    span->span.start = offset;
    span->span.end = offset + size;
    span->span.storage_class = EGGSFS_INLINE_STORAGE;
    span->len = len;
    memcpy(span->body, body, len);
    eggsfs_debug_print("adding inline span");
    list_add_tail(&span->span.lru, &ctx->spans);
}

// returns whether we could acquire it
static bool eggsfs_span_acquire(struct eggsfs_span* span) {
    if (span->storage_class == EGGSFS_INLINE_STORAGE) { return true; }

    struct eggsfs_block_span* block_span = EGGSFS_BLOCK_SPAN(span);
    struct eggsfs_span_lru* lru = eggsfs_get_span_lru(span);

    spin_lock_bh(&lru->lock);
    if (unlikely(block_span->readers < 0)) { // the reclaimer got here before us.
        spin_unlock_bh(&lru->lock);
        return false;
    }
    block_span->readers++;
    if (block_span->readers == 1) {
        // we're the first ones here, take it out of the LRU
        list_del(&span->lru);
    }
    spin_unlock_bh(&lru->lock);

    return true;
}

void eggsfs_span_put(struct eggsfs_span* span, bool was_read) {
    if (span->storage_class == EGGSFS_INLINE_STORAGE) { return; }

    struct eggsfs_block_span* block_span = EGGSFS_BLOCK_SPAN(span);
    struct eggsfs_span_lru* lru = eggsfs_get_span_lru(span);

    spin_lock_bh(&lru->lock);
    block_span->actually_read = block_span->actually_read || was_read;
    BUG_ON(block_span->readers < 1);
    block_span->readers--;
    if (block_span->readers == 0) { // we need to put it back into the LRU
        if (block_span->actually_read) {
            list_add_tail(&span->lru, &lru->lru);
        } else {
            list_add(&span->lru, &lru->lru);
        }
        block_span->actually_read = false;
    }
    spin_unlock_bh(&lru->lock);
}

struct eggsfs_span* eggsfs_get_span(struct eggsfs_inode* enode, u64 offset) {
    struct eggsfs_inode_file* file = &enode->file;
    int err;

    eggsfs_debug_print("ino=%016lx, pid=%d, off=%llu getting span", enode->inode.i_ino, get_current()->pid, offset);

    // This helps below: it means that we _must_ have a span. So if we
    // get NULL at any point, we can retry, because it means we're conflicting
    // with a reclaimer.
    if (offset >= enode->inode.i_size) { return NULL; }

    u64 iterations = 0;

retry:
    iterations++;
    if (unlikely(iterations > 10)) {
        eggsfs_warn_print("we've been stuck on fetching spans for %llu iterations, something is wrong (reclaimer is running too often?)", iterations);
        return ERR_PTR(-EIO);
    }

    // Try to read the semaphore if it's already there.
    err = down_read_killable(&enode->file.spans_lock);
    if (err) { return ERR_PTR(err); }
    {
        struct eggsfs_span* span = eggsfs_lookup_span(&file->spans, offset);
        if (likely(span)) {
            if (unlikely(!eggsfs_span_acquire(span))) {
                up_read(&enode->file.spans_lock);
                goto retry;
            }
            up_read(&enode->file.spans_lock);
            return span;
        }
        up_read(&enode->file.spans_lock);
    }

    // We need to fetch the spans.
    err = down_write_killable(&file->spans_lock);
    if (err) { return ERR_PTR(err); }

    // Check if somebody go to it first.
    {
        struct eggsfs_span* span = eggsfs_lookup_span(&file->spans, offset);
        if (unlikely(span)) {
            if (unlikely(!eggsfs_span_acquire(span))) {
                up_write(&file->spans_lock);
                goto retry;
            }
            up_write(&file->spans_lock);
            return span;
        }
    }

    // We always get the full set of spans, this simplifies prefetching (we just
    // blindly assume the span is there, and if it's been reclaimed in the meantime
    // we just don't care).
    struct eggsfs_get_span_ctx ctx = { .err = 0, .enode = enode };
    INIT_LIST_HEAD(&ctx.spans);
    u64 spans_offset;
    for (spans_offset = 0;;) {
        u64 next_offset;
        err = eggsfs_error_to_linux(eggsfs_shard_file_spans(
            (struct eggsfs_fs_info*)enode->inode.i_sb->s_fs_info, enode->inode.i_ino, spans_offset, &next_offset,&ctx
        ));
        err = err ?: ctx.err;
        if (unlikely(err)) {
            eggsfs_debug_print("failed to get file spans at %llu err=%d", spans_offset, err);
            for (;;) {
                struct eggsfs_span* span = list_first_entry_or_null(&ctx.spans, struct eggsfs_span, lru);
                list_del(&span->lru);
                eggsfs_free_span(span);
            }
            up_write(&file->spans_lock);
            return ERR_PTR(err);
        }
        if (next_offset == 0) { break; }
        spans_offset = next_offset;
    }

    for (;;) {
        struct eggsfs_span* span = list_first_entry_or_null(&ctx.spans, struct eggsfs_span, lru);
        if (span == NULL) { break; }
        list_del(&span->lru);
        if (!eggsfs_insert_span(&file->spans, span)) {
            // Span is already cached
            eggsfs_free_span(span);
        } else {
            if (span->storage_class != EGGSFS_INLINE_STORAGE) {
                // Not already cached, must add it to the LRU
                eggsfs_counter_inc(eggsfs_stat_cached_spans);
                struct eggsfs_block_span* block_span = EGGSFS_BLOCK_SPAN(span);
                struct eggsfs_span_lru* lru = eggsfs_get_span_lru(span);
                spin_lock_bh(&lru->lock);
                block_span->readers = 0;
                list_add(&span->lru, &lru->lru);
                spin_unlock_bh(&lru->lock);
            }
        }
    }

    up_write(&file->spans_lock);

    // We now restart, we know that the span must be there (unless the shard is broken).
    // It might get reclaimed in the meantime though.
    goto retry;
}

// If it returns -1, there are no spans to drop. Otherwise, returns the
// next index to pass in to reclaim the next span.
static int eggsfs_drop_one_span(int lru_ix) {
    BUG_ON(lru_ix < 0 || lru_ix >= EGGSFS_SPAN_LRUS);

    int i;
    for (i = lru_ix; i < lru_ix + EGGSFS_SPAN_LRUS; i++) {
        struct eggsfs_span_lru* lru = &eggsfs_span_lrus[i%EGGSFS_SPAN_LRUS];

        struct eggsfs_span* candidate = NULL;
        struct eggsfs_block_span* candidate_blocks = NULL;

        // Pick a candidate from the LRU, mark it as reclaiming, take it out of
        // the LRU.
        spin_lock_bh(&lru->lock);
        candidate = list_first_entry_or_null(&lru->lru, struct eggsfs_span, lru);
        if (unlikely(candidate == NULL)) {
            spin_unlock_bh(&lru->lock);
        } else {
            BUG_ON(candidate->storage_class == EGGSFS_INLINE_STORAGE); // no inline spans in LRU
            candidate_blocks = EGGSFS_BLOCK_SPAN(candidate);
            // we just got this from LRU, it must be readers == 0
            BUG_ON(candidate_blocks->readers != 0);
            candidate_blocks->readers = -1;
            list_del(&candidate->lru);
            spin_unlock_bh(&lru->lock);
        }

        if (candidate) {
            // Take it out of the spans for the file
            down_write(&candidate->enode->file.spans_lock);
            rb_erase(&candidate->node, &candidate->enode->file.spans);
            up_write(&candidate->enode->file.spans_lock);

            // At this point we're free to do what we please with the span
            // (it's fully private to us).
            eggsfs_counter_dec(eggsfs_stat_cached_spans);
            struct page* page;
            unsigned long page_ix;
            xa_for_each(&candidate_blocks->pages, page_ix, page) {
                put_page(page);
                xa_erase(&candidate_blocks->pages, page_ix);
                eggsfs_counter_dec(eggsfs_stat_cached_span_pages);
            }
            eggsfs_free_span(candidate);

            i++;
            break;
        }
    }

    // we've fully gone around without finding anything
    if (i == lru_ix + EGGSFS_SPAN_LRUS) {
        return -1;
    }

    // We've found something
    return i%EGGSFS_SPAN_LRUS;
}

void eggsfs_drop_all_spans(void) {
    s64 pages_begin = eggsfs_counter_get(&eggsfs_stat_cached_span_pages);
    s64 spans_begin = eggsfs_counter_get(&eggsfs_stat_cached_spans);
    int lru_ix = 0;
    for (;;) {
        lru_ix = eggsfs_drop_one_span(lru_ix);
        if (lru_ix < 0) { break; }
    }
    s64 pages_end = eggsfs_counter_get(&eggsfs_stat_cached_span_pages);
    s64 spans_end = eggsfs_counter_get(&eggsfs_stat_cached_spans);
    eggsfs_info_print("reclaimed %llu pages, %llu spans (approx)", pages_begin-pages_end, spans_begin-spans_end);
}

void eggsfs_drop_spans(struct eggsfs_inode* enode) {
again:
    down_write(&enode->file.spans_lock);

    for (;;) {
        struct rb_node* node = rb_first(&enode->file.spans);
        if (node == NULL) { break; }
    
        struct eggsfs_span* span = rb_entry(node, struct eggsfs_span, node);
        struct eggsfs_block_span* block_span = NULL;
        if (span->storage_class != EGGSFS_INLINE_STORAGE) {
            block_span = EGGSFS_BLOCK_SPAN(span);
            struct eggsfs_span_lru* lru = eggsfs_get_span_lru(span);
            // This function is only called on eviction, so either this is being
            // reclaimed, or it's in the LRU.
            spin_lock_bh(&lru->lock);
            BUG_ON(block_span->readers > 0);
            if (unlikely(block_span->readers < 0)) {
                // This is being reclaimed, we have to wait until the reclaimer
                // cleans it up for us.
                spin_unlock_bh(&lru->lock);
                up_write(&enode->file.spans_lock);
                goto again;
            }
            block_span->readers = -1;
            list_del(&span->lru);
            spin_unlock_bh(&lru->lock);
        }
    
        rb_erase(node, &enode->file.spans);

        if (block_span) {
            eggsfs_counter_dec(eggsfs_stat_cached_spans);
            struct page* page;
            unsigned long page_ix;
            xa_for_each(&block_span->pages, page_ix, page) {
                put_page(page);
                xa_erase(&block_span->pages, page_ix);
                eggsfs_counter_dec(eggsfs_stat_cached_span_pages);
            }
        }
        eggsfs_free_span(span);
    }

    up_write(&enode->file.spans_lock);
}

struct page* eggsfs_get_span_page(struct eggsfs_block_span* span, u32 page_ix) {
    // this should be guaranteed by the caller but we rely on it below, so let's check
    if (span->cell_size%PAGE_SIZE != 0) {
        eggsfs_warn_print("cell_size=%u, PAGE_SIZE=%lu, span->cell_size%%PAGE_SIZE=%lu", span->cell_size, PAGE_SIZE, span->cell_size%PAGE_SIZE);
        return ERR_PTR(-EIO);
    }

    struct page* page;
    u32 start_page, end_page, curr_page;
    int err = 0;

again:
    page = xa_load(&span->pages, page_ix);
    if (page != NULL) { return page; }

    // We need to load the stripe
    int D = eggsfs_data_blocks(span->parity);
    u32 span_offset = page_ix * PAGE_SIZE;
    u32 stripe = span_offset / (span->cell_size*D);
    // TODO better error?
    if (stripe > span->stripes) {
        eggsfs_warn_print("span_offset=%u, stripe=%u, stripes=%u", span_offset, stripe, span->stripes);
        return ERR_PTR(-EIO);
    }

    start_page = (span->cell_size/PAGE_SIZE)*D*stripe;
    curr_page = start_page;
    end_page = (span->cell_size/PAGE_SIZE)*D*((int)stripe + 1);

    int seqno;
    if (!eggsfs_latch_try_acquire(&span->stripe_latches[stripe], seqno)) {
        int err = eggsfs_latch_wait_killable(&span->stripe_latches[stripe], seqno);
        if (err) { return ERR_PTR(err); }
        goto again;
    }

    struct eggsfs_block_socket* socks[EGGSFS_MAX_DATA];
    memset(socks, 0, sizeof(struct eggsfs_block_socket*)*EGGSFS_MAX_DATA);
    struct eggsfs_fetch_block_request* reqs[EGGSFS_MAX_DATA];
    memset(reqs, 0, sizeof(struct eggsfs_fetch_block_request*)*EGGSFS_MAX_DATA);

    // get block services sockets
    int i;
    for (i = 0; i < D; i++) {
        socks[i] = eggsfs_get_fetch_block_socket(&span->blocks[i].bs);
        if (IS_ERR(socks[i])) {
            err = PTR_ERR(socks[i]);
            goto out_err;
        }
    }

    // fetch them all at the right offset
    u32 block_offset = stripe * span->cell_size;
    for (i = 0; i < D; i++) {
        struct eggsfs_block* block = &span->blocks[i];
        reqs[i] = eggsfs_fetch_block(socks[i], block->bs.id, block->id, block_offset, span->cell_size);
        if (IS_ERR(reqs[i])) {
            err = PTR_ERR(reqs[i]);
            goto out_err;
        }
    }

    // Wait for all the block requests
    for (i = 0; i < D; i++) {
        struct eggsfs_fetch_block_request* req = reqs[i];
        err = wait_for_completion_killable(&req->comp);
        if (err) { goto out_err; }
        err = req->err;
        if (err) {
            eggsfs_debug_print("request failed: %d", err);
            goto out_err;
        }
    }

    // Store all the pages into the xarray
    u32 crc = 0;
    for (i = 0; i < D; i++) {
        struct eggsfs_fetch_block_request* req = reqs[i];
        crc = eggsfs_crc32c_append(crc, req->crc, span->cell_size);
        struct page* page;
        list_for_each_entry(page, &req->pages, lru) {
            struct page* old_page = xa_store(&span->pages, curr_page, page, GFP_KERNEL);
            if (IS_ERR(old_page)) {
                err = PTR_ERR(old_page);
                eggsfs_debug_print("xa_store failed: %d", err);
                goto out_err;
            }
            eggsfs_counter_inc(eggsfs_stat_cached_span_pages);
            BUG_ON(old_page != NULL); // the latch protects against this
            curr_page++;
        }
    }

    // Check the crc
    if (crc != span->stripes_crc[stripe]) {
        err = -EIO;
        eggsfs_debug_print("crc check failed failed, expected %08x, got %08x", span->stripes_crc[stripe], crc);
        goto out_err;
    }

    // OK, we're good, now take ownership of the pages by emptying the pages in the requests
    // so that they won't be freed by `eggsfs_put_fetch_block_request`
    for (i = 0; i < D; i++) {
        struct list_head* pages = &reqs[i]->pages;
        while (!list_empty(pages)) {
            list_del(&lru_to_page(pages)->lru);
        }
    }

    page = xa_load(&span->pages, page_ix);
    BUG_ON(page == NULL);

out:
    // Unlock and free everything
    eggsfs_latch_release(&span->stripe_latches[stripe], seqno);
    for (i = 0; i < D; i++) {
        if (reqs[i] == NULL) { continue; }
        eggsfs_put_fetch_block_request(reqs[i]);
    }
    for (i = 0; i < D; i++) {
        if (socks[i] == NULL) { continue; }
        eggsfs_put_fetch_block_socket(socks[i]);
    }
    return err == 0 ? page : ERR_PTR(err);

out_err:
    eggsfs_debug_print("getting span page failed, err=%d", err);
    // we might have partially written the stripe, forget about them
    end_page = curr_page;
    for (curr_page = start_page; curr_page <= end_page; curr_page++) {
        eggsfs_counter_dec(eggsfs_stat_cached_span_pages);
        xa_erase(&span->pages, curr_page);
    }
    goto out;
}

void eggsfs_span_init(void) {
    int i;
    for (i = 0; i < EGGSFS_SPAN_LRUS; i++) {
        spin_lock_init(&eggsfs_span_lrus[i].lock);
        INIT_LIST_HEAD(&eggsfs_span_lrus[i].lru);
    }
}
