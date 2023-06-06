#include "bincode.h"
#include "log.h"
#include "span.h"
#include "metadata.h"
#include "rs.h"
#include "latch.h"
#include "crc.h"
#include "err.h"
#include "counter.h"
#include "wq.h"
#include "trace.h"
#include "intrshims.h"

EGGSFS_DEFINE_COUNTER(eggsfs_stat_cached_spans);

// This currently also includes in-flight pages (i.e. pages that cannot be
// reclaimed), just because the code is a bit simpler this way.
atomic64_t eggsfs_stat_cached_span_pages = ATOMIC64_INIT(0);

// These numbers do not mean anything in particular. We do want to avoid
// flickering sync drops though, therefore we go down to 25GiB from
// 50GiB, and similarly for free memory.
unsigned long eggsfs_span_cache_max_size_async = (50ull << 30); // 50GiB
unsigned long eggsfs_span_cache_min_avail_mem_async = (2ull << 30); // 2GiB
unsigned long eggsfs_span_cache_max_size_drop = (45ull << 30); // 25GiB
unsigned long eggsfs_span_cache_min_avail_mem_drop = (2ull << 30) + (500ull << 20); // 2GiB + 500MiB
unsigned long eggsfs_span_cache_max_size_sync = (100ull << 30); // 100GiB
unsigned long eggsfs_span_cache_min_avail_mem_sync = (1ull << 30); // 1GiB

struct eggsfs_span_lru {
    spinlock_t lock;
    struct list_head lru;
} ____cacheline_aligned;

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
            eggsfs_debug("off=%llu span=%p", offset, span);
            return span;
        }
    }
    eggsfs_debug("off=%llu no_span", offset);
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
            eggsfs_debug("span=%p (%llu-%llu) already present", span, span->start, span->end);
            return false;
        }
    }

    rb_link_node(&span->node, parent, new);
    rb_insert_color(&span->node, spans);
    eggsfs_debug("span=%p (%llu-%llu) inserted", span, span->start, span->end);
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
    eggsfs_debug("offset=%llu size=%u crc=%08x storage_class=%d parity=%d stripes=%d cell_size=%u", offset, size, crc, storage_class, parity, stripes, cell_size);

    struct eggsfs_get_span_ctx* ctx = (struct eggsfs_get_span_ctx*)data;
    if (ctx->err) { return; }

    struct eggsfs_block_span* span = kmalloc(sizeof(struct eggsfs_block_span), GFP_KERNEL);
    if (!span) { ctx->err = -ENOMEM; return; }

    if (eggsfs_data_blocks(parity) > EGGSFS_MAX_DATA || eggsfs_parity_blocks(parity) > EGGSFS_MAX_PARITY) {
        eggsfs_warn("D=%d > %d || P=%d > %d", eggsfs_data_blocks(parity), EGGSFS_MAX_DATA, eggsfs_data_blocks(parity), EGGSFS_MAX_PARITY);
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

    eggsfs_debug("adding normal span");
    list_add_tail(&span->span.lru, &ctx->spans);
}

void eggsfs_file_spans_cb_block(
    void* data, int block_ix,
    u64 bs_id, u32 ip1, u16 port1, u32 ip2, u16 port2, u8 flags,
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
    block->bs.flags = flags;
    block->id = block_id;
    block->crc = crc;
}

void eggsfs_file_spans_cb_inline_span(void* data, u64 offset, u32 size, u8 len, const char* body) {
    eggsfs_debug("offset=%llu size=%u len=%u body=%*pE", offset, size, len, len, body);

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
    eggsfs_debug("adding inline span");
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

    eggsfs_debug("ino=%016lx, pid=%d, off=%llu getting span", enode->inode.i_ino, get_current()->pid, offset);

    trace_eggsfs_get_span_enter(enode->inode.i_ino, offset);

#define GET_SPAN_EXIT(s) do { \
        trace_eggsfs_get_span_exit(enode->inode.i_ino, offset, IS_ERR(s) ? PTR_ERR(s) : 0); \
        return s; \
    } while(0)

    // This helps below: it means that we _must_ have a span. So if we
    // get NULL at any point, we can retry, because it means we're conflicting
    // with a reclaimer.
    if (offset >= enode->inode.i_size) { GET_SPAN_EXIT(NULL); }

    u64 iterations = 0;

retry:
    iterations++;
    // Sadly while this should be somewhat uncommon it can happen when we're busy
    // looping while another task is between inserting span in the span tree and
    // adding it to the LRU.
    if (unlikely(iterations == 10)) {
        eggsfs_warn("we've been fetching the same span for %llu iterations, we're probably stuck on a yet-to-be enabled span we just fetched", iterations);
    }

    // Try to read the semaphore if it's already there.
    err = down_read_killable(&enode->file.spans_lock);
    if (err) { GET_SPAN_EXIT(ERR_PTR(err)); }
    {
        struct eggsfs_span* span = eggsfs_lookup_span(&file->spans, offset);
        if (likely(span)) {
            if (unlikely(!eggsfs_span_acquire(span))) {
                up_read(&enode->file.spans_lock);
                goto retry;
            }
            up_read(&enode->file.spans_lock);
            GET_SPAN_EXIT(span);
        }
        up_read(&enode->file.spans_lock);
    }

    // We need to fetch the spans.
    err = down_write_killable(&file->spans_lock);
    if (err) { GET_SPAN_EXIT(ERR_PTR(err)); }

    // Check if somebody go to it first.
    {
        struct eggsfs_span* span = eggsfs_lookup_span(&file->spans, offset);
        if (unlikely(span)) {
            if (unlikely(!eggsfs_span_acquire(span))) {
                up_write(&file->spans_lock);
                goto retry;
            }
            up_write(&file->spans_lock);
            GET_SPAN_EXIT(span);
        }
    }

    // We always get the full set of spans, this simplifies prefetching (we just
    // blindly assume the span is there, and if it's been reclaimed in the meantime
    // we just don't care).
    u64 spans_offset;
    for (spans_offset = 0;;) {
        // fetch next batch of spans
        u64 next_offset;
        struct eggsfs_get_span_ctx ctx = { .err = 0, .enode = enode };
        INIT_LIST_HEAD(&ctx.spans);
        err = eggsfs_error_to_linux(eggsfs_shard_file_spans(
            (struct eggsfs_fs_info*)enode->inode.i_sb->s_fs_info, enode->inode.i_ino, spans_offset, &next_offset,&ctx
        ));
        err = err ?: ctx.err;
        if (unlikely(err)) {
            eggsfs_debug("failed to get file spans at %llu err=%d", spans_offset, err);
            for (;;) {
                struct eggsfs_span* span = list_first_entry_or_null(&ctx.spans, struct eggsfs_span, lru);
                list_del(&span->lru);
                eggsfs_free_span(span);
            }
            up_write(&file->spans_lock);
            GET_SPAN_EXIT(ERR_PTR(err));
        }
        // add them to enode spans and LRU
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
        // stop if we're done
        if (next_offset == 0) { break; }
        spans_offset = next_offset;
    }

    up_write(&file->spans_lock);

    // We now restart, we know that the span must be there (unless the shard is broken).
    // It might get reclaimed in the meantime though.
    goto retry;

#undef GET_SPAN_EXIT
}

// If it returns -1, there are no spans to drop. Otherwise, returns the
// next index to pass in to reclaim the next span.
static int eggsfs_drop_one_span(int lru_ix, u64* dropped_pages) {
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
                atomic64_dec(&eggsfs_stat_cached_span_pages);
                (*dropped_pages)++;
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

static inline void eggsfs_drop_spans_enter(const char* type) {
    trace_eggsfs_drop_spans_enter(type, PAGE_SIZE*si_mem_available(), atomic64_read(&eggsfs_stat_cached_span_pages), eggsfs_counter_get(&eggsfs_stat_cached_spans));
}

static inline void eggsfs_drop_spans_exit(const char* type, u64 dropped_pages) {
    trace_eggsfs_drop_spans_exit(type, PAGE_SIZE*si_mem_available(), atomic64_read(&eggsfs_stat_cached_span_pages), eggsfs_counter_get(&eggsfs_stat_cached_spans), dropped_pages);
}

// returns the number of dropped pages
u64 eggsfs_drop_all_spans(void) {
    eggsfs_drop_spans_enter("all");

    u64 dropped_pages = 0;
    s64 spans_begin = eggsfs_counter_get(&eggsfs_stat_cached_spans);
    int lru_ix = 0;
    for (;;) {
        lru_ix = eggsfs_drop_one_span(lru_ix, &dropped_pages);
        if (lru_ix < 0) { break; }
    }
    s64 spans_end = eggsfs_counter_get(&eggsfs_stat_cached_spans);
    eggsfs_info("reclaimed %llu pages, %lld spans (approx)", dropped_pages, spans_begin-spans_end);
    eggsfs_drop_spans_exit("all", dropped_pages);
    return dropped_pages;
}

static DEFINE_MUTEX(eggsfs_drop_spans_mu);

static u64 eggsfs_drop_spans(const char* type) {
    mutex_lock(&eggsfs_drop_spans_mu);

    eggsfs_drop_spans_enter(type);

    u64 dropped_pages = 0;
    int lru_ix = 0;
    for (;;) {
        u64 pages = atomic64_read(&eggsfs_stat_cached_span_pages);
        if (
            pages*PAGE_SIZE < eggsfs_span_cache_max_size_drop &&
            si_mem_available()*PAGE_SIZE > eggsfs_span_cache_min_avail_mem_drop
        ) {
            break;
        }
        lru_ix = eggsfs_drop_one_span(lru_ix, &dropped_pages);
        if (lru_ix < 0) { break; }
    }
    eggsfs_debug("dropped %llu pages", dropped_pages);
    
    eggsfs_drop_spans_exit(type, dropped_pages);

    mutex_unlock(&eggsfs_drop_spans_mu);

    return dropped_pages;
}

static void eggsfs_reclaim_spans_async(struct work_struct* work) {
    if (!mutex_is_locked(&eggsfs_drop_spans_mu)) { // somebody's already taking care of it
        eggsfs_drop_spans("async");
    }
}

static DECLARE_WORK(eggsfs_reclaim_spans_work, eggsfs_reclaim_spans_async);

static unsigned long eggsfs_span_shrinker_count(struct shrinker* shrinker, struct shrink_control* sc) {
    u64 pages = atomic64_read(&eggsfs_stat_cached_span_pages);
    if (pages == 0) { return SHRINK_EMPTY; }
    return pages;
}

static int eggsfs_span_shrinker_lru_ix = 0;
static u64 eggsfs_span_shrinker_pages_round = 25600; // We drop at most 100MiB in one shrinker round

static unsigned long eggsfs_span_shrinker_scan(struct shrinker* shrinker, struct shrink_control* sc) {
    eggsfs_drop_spans_enter("shrinker");
    u64 dropped_pages;
    int lru_ix = eggsfs_span_shrinker_lru_ix;
    for (dropped_pages = 0; dropped_pages < eggsfs_span_shrinker_pages_round;) {
        lru_ix = eggsfs_drop_one_span(lru_ix, &dropped_pages);
        if (lru_ix < 0) { break; }
    }
    eggsfs_span_shrinker_lru_ix = lru_ix >= 0 ? lru_ix : 0;
    eggsfs_drop_spans_exit("shrinker", dropped_pages);
    return dropped_pages;
}

static struct shrinker eggsfs_span_shrinker = {
    .count_objects = eggsfs_span_shrinker_count,
    .scan_objects = eggsfs_span_shrinker_scan,
    .seeks = DEFAULT_SEEKS,
    .flags = SHRINKER_NONSLAB, // what's this?
};

void eggsfs_drop_file_spans(struct eggsfs_inode* enode) {
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
                atomic64_dec(&eggsfs_stat_cached_span_pages);
            }
        }
        eggsfs_free_span(span);
    }

    up_write(&enode->file.spans_lock);
}

struct eggsfs_fetch_stripe_state {
    u64 ino;
    struct semaphore sema; // used to wait on remaining = 0. could be done faster with a wait_queue, but don't want to worry about smb subtleties.
    s64 stripe_seqno;      // used to release the stripe when we're done with it
    struct eggsfs_block_span* span; // what we're working on (must not be put until the end of the fetch)
    // these will be filled in by the fetching (only D of them well be
    // filled by fetching the blocks, the others might be filled in by the RS
    // recovery)
    struct list_head blocks_pages[EGGSFS_MAX_BLOCKS];
    atomic_t remaining;   // how many block fetch requests are still ongoing
    atomic_t refcount;    // to garbage collect the state
    atomic_t err;         // the result of the stripe fetching
    static_assert(EGGSFS_MAX_BLOCKS <= 16);
    u16 blocks;           // which blocks we're fetching, bitmap
    u8 stripe;            // which stripe we're into
    u8 prefetching;       // wether we're prefetching (if we are we'll have to dispose of the span ourselves)
};

static void eggsfs_put_fetch_stripe(struct eggsfs_fetch_stripe_state* st) {
    struct eggsfs_inode* enode = st->span->span.enode;

    int refs = atomic_dec_return(&st->refcount);
    if (refs == 0) { // we're the last ones here
        trace_eggsfs_fetch_stripe(enode->inode.i_ino, st->span->span.start, st->stripe, st->prefetching, EGGSFS_FETCH_STRIPE_FREE, atomic_read(&st->err));
        struct eggsfs_block_span* span = st->span;
        int B = eggsfs_blocks(span->parity);
        int i;
        for (i = 0; i < B; i++) {
            put_pages_list(&st->blocks_pages[i]);
        }
        if (st->prefetching) {
            smp_mb__before_atomic();
            atomic_dec_return(&enode->file.prefetches);
            wake_up_all(&enode->file.prefetches_wq);
        }
        kfree(st);
    }
}

static void eggsfs_fetch_stripe_store_pages(struct eggsfs_fetch_stripe_state* st) {
    struct eggsfs_block_span* span = st->span;
    int D = eggsfs_data_blocks(span->parity);

    int i, j;

    // we need to recover data using RS
    if (unlikely(st->blocks != (1u<<D)-1)) {
        u32 pages = 0;
        { // get the number of pages
            struct list_head* tmp;
            // eggsfs_info("picking %d", __builtin_ctz(st->blocks));
            list_for_each(tmp, &st->blocks_pages[__builtin_ctz(st->blocks)]) { // just pick the first one
                pages++;
            }
        }
        for (i = 0; i < D; i++) { // fill in missing data blocks
            if ((1u<<i) & st->blocks) { continue; } // we have this one already
            // eggsfs_info("recovering %d, pages=%u", i, pages);
            // allocate the pages
            struct list_head* i_pages = &st->blocks_pages[i];
            for (j = 0; j < pages; j++) {
                struct page* page = alloc_page(GFP_KERNEL);
                if (!page) {
                    atomic_set(&st->err, -ENOMEM);
                    return;
                }
                list_add_tail(&page->lru, i_pages);
            }
            // compute
            int err = eggsfs_recover(span->parity, st->blocks, 1u<<i, pages, st->blocks_pages);
            if (err) {
                atomic_set(&st->err, err);
                return;
            }
        }
    }

    u32 start_page = (span->cell_size/PAGE_SIZE)*D*st->stripe;
    u32 end_page = (span->cell_size/PAGE_SIZE)*D*((int)st->stripe + 1);
    u32 curr_page = start_page;
    u32 crc = 0;
    kernel_fpu_begin(); // crc
    // move the pages to the span
    for (i = 0; i < D; i++) {
        struct page* page;
        struct page* tmp;
        list_for_each_entry_safe(page, tmp, &st->blocks_pages[i], lru) {
            list_del(&page->lru);
            // eggsfs_info("storing page for span %p at %u", span, curr_page);
            char* page_buf = kmap_atomic(page);
            crc = eggsfs_crc32c(crc, page_buf, PAGE_SIZE);
            kunmap_atomic(page_buf);
            struct page* old_page = xa_store(&span->pages, curr_page, page, GFP_KERNEL);
            if (IS_ERR(old_page)) {
                atomic_set(&st->err, PTR_ERR(old_page));
                eggsfs_debug("xa_store failed: %ld", PTR_ERR(old_page));
                put_page(page); // we removed it from the list
                kernel_fpu_end();
                return;
            }
            BUG_ON(old_page != NULL); // the latch protects against this
            atomic64_inc(&eggsfs_stat_cached_span_pages);
            curr_page++;
        }
        // eggsfs_info("i=%d curr_page=%u", i, curr_page);
    }
    kernel_fpu_end();
    // eggsfs_info("curr_page=%u end_page=%u", curr_page, end_page);
    BUG_ON(curr_page != end_page);
    if (crc != span->stripes_crc[st->stripe]) {
        eggsfs_warn("bad crc for file %016lx, stripe %u, expected %08x, got %08x", span->span.enode->inode.i_ino, st->stripe, span->stripes_crc[st->stripe], crc);
        atomic_set(&st->err, -EIO);
    }
}

static void eggsfs_fetch_stripe_block_done(void* data, u64 block_id, struct list_head* pages, int err) {
    eggsfs_debug("block complete %016llx", block_id);
    struct eggsfs_fetch_stripe_state* st = (struct eggsfs_fetch_stripe_state*)data;
    struct eggsfs_block_span* span = st->span;
    int B = eggsfs_blocks(span->parity);

    trace_eggsfs_fetch_stripe(
        st->ino,
        st->span->span.start,
        st->stripe,
        st->prefetching,
        EGGSFS_FETCH_STRIPE_BLOCK_DONE,
        err
    );

    int i;
    for (i = 0; i < B; i++) {
        if (st->span->blocks[i].id == block_id) { break; }
    }
    // eggsfs_info("block ix %d, B=%d", i, B);
    BUG_ON(i == B);

    // It failed, mark it
    if (err) {
        eggsfs_debug("block %016llx ix %d failed with %d", block_id, i, err);
        atomic_cmpxchg(&st->err, 0, err);
        goto out;
    }

    // It succeeded, set crc and grab all the pages
    struct page* page;
    struct page* tmp;
    list_for_each_entry_safe(page, tmp, pages, lru) {
        list_del(&page->lru);
        list_add_tail(&page->lru, &st->blocks_pages[i]);
    }

    int remaining;
out:
    remaining = atomic_dec_return(&st->remaining);
    eggsfs_debug("remaining=%d", remaining);
    if (remaining == 0) {
        // we're the last one to finish, we need to store the pages in the span
        eggsfs_fetch_stripe_store_pages(st);
        // release the stripe latch, we're done writing
        eggsfs_latch_release(&span->stripe_latches[st->stripe], st->stripe_seqno);
        // release the span too, if we were prefetching
        if (st->prefetching) {
            eggsfs_span_put(&st->span->span, false);
        }
        // and wake up waiters, if any
        up(&st->sema);
        trace_eggsfs_fetch_stripe(
            st->ino,
            st->span->span.start,
            st->stripe,
            st->prefetching,
            EGGSFS_FETCH_STRIPE_END,
            atomic_read(&st->err)
        );
    }
    eggsfs_put_fetch_stripe(st); // one refcount per request
    eggsfs_debug("done");
}

static struct eggsfs_fetch_stripe_state* eggsfs_start_fetch_stripe(
    struct eggsfs_block_span* span, u8 stripe, s64 stripe_seqno, bool prefetching
) {
    int err;
    struct eggsfs_inode* enode = span->span.enode;
    struct eggsfs_fetch_stripe_state* st = NULL;
    int i;

    trace_eggsfs_fetch_stripe(enode->inode.i_ino, span->span.start, stripe, prefetching, EGGSFS_FETCH_STRIPE_START, 0);

    int D = eggsfs_data_blocks(span->parity);
    int B = eggsfs_blocks(span->parity);
    if (D > EGGSFS_MAX_DATA || B > EGGSFS_MAX_BLOCKS) {
        eggsfs_error("got out of bounds parity of RS(%d,%d) for span %llu in file %016lx", D, B-D, span->span.start, enode->inode.i_ino);
        err = -EIO;
        goto out_err;
    }

    // If the span does not have enough available blocks, we're in trouble
    u32 blocks = 0;
#if 1
    {
        int found_blocks = 0;
        for (i = 0; i < B && found_blocks < D; i++) {
            if (likely(!(span->blocks[i].bs.flags & EGGSFS_BLOCK_SERVICE_DONT_READ))) {
                blocks |= 1u<<i;
                found_blocks++;
            }
        }
        if (found_blocks < D) {
            eggsfs_warn("could only find %d blocks for span %llu in file %016lx", found_blocks, span->span.start, enode->inode.i_ino);
            err = -EIO;
            goto out_err;
        }
    }
#else
    // Randomly drop some blocks, useful for testing
    while (__builtin_popcount(blocks) < D) {
        uint32_t r;
        get_random_bytes(&r, sizeof(r));
        blocks |= 1u << (r%B);
    }
#endif

    st = kmalloc(sizeof(struct eggsfs_fetch_stripe_state), GFP_KERNEL);
    if (!st) { err = -ENOMEM; goto out_err; }

    st->ino = enode->inode.i_ino;
    sema_init(&st->sema, 0);
    st->stripe_seqno = stripe_seqno;
    st->span = span;
    for (i = 0; i < B; i++) {
        INIT_LIST_HEAD(&st->blocks_pages[i]);
    }
    atomic_set(&st->remaining, D);
    atomic_set(&st->refcount, 1); // the caller
    atomic_set(&st->err, 0);
    st->stripe = stripe;
    st->prefetching = prefetching;
    smp_mb__before_atomic();
    if (st->prefetching) {
        atomic_inc_return(&enode->file.prefetches);
    }
    st->blocks = blocks;
    
    // start fetching blocks
    eggsfs_debug("fetching blocks");
    u32 block;
    u32 block_offset = stripe * span->cell_size;
    for (i = 0, block = blocks; block; block >>= 1, i++) {
        if ((block&1u) == 0) { continue; }
        struct eggsfs_block* block = &span->blocks[i];
        atomic_inc(&st->refcount);
        err = eggsfs_fetch_block(
            &eggsfs_fetch_stripe_block_done,
            (void*)st,
            &block->bs, block->id, block_offset, span->cell_size
        );
        if (err) {
            atomic_dec(&st->refcount);
            goto out_err;
        }
    }

    eggsfs_debug("waiting");
    return st;

out_err:
    trace_eggsfs_fetch_stripe(enode->inode.i_ino, span->span.start, stripe, prefetching, EGGSFS_FETCH_STRIPE_END, err);
    if (st) { eggsfs_put_fetch_stripe(st); }
    return ERR_PTR(err);
}

static void eggsfs_do_prefetch(struct eggsfs_block_span* block_span, u64 off) {
    struct eggsfs_inode* enode = block_span->span.enode;

    if (off < block_span->span.start || off >= block_span->span.end) {
        // Another span, we need to load it. We give up if we can't at any occurrence.
        int err = down_read_killable(&enode->file.spans_lock);
        if (err) { return; }
        struct eggsfs_span* span = eggsfs_lookup_span(&enode->file.spans, off);
        if (likely(span)) {
            if (unlikely(!eggsfs_span_acquire(span))) {
                up_read(&enode->file.spans_lock);
                return;
            }
        } else {
            up_read(&enode->file.spans_lock);
            return;
        }
        up_read(&enode->file.spans_lock);
        if (span->storage_class == EGGSFS_INLINE_STORAGE) {
            return; // nothing to do, it's an inline span
        }
        block_span = EGGSFS_BLOCK_SPAN(span);
    } else if (unlikely(!eggsfs_span_acquire(&block_span->span))) {
        return; // it's being reclaimed, don't bother
    }

    // At this point we've acquired the span. We now need to check if there's something to get

    int D = eggsfs_data_blocks(block_span->parity);
    u32 page_ix = (off - block_span->span.start)/PAGE_SIZE;
    u32 span_offset = page_ix * PAGE_SIZE;
    u32 stripe = span_offset / (block_span->cell_size*D);
    if (stripe > block_span->stripes) {
        eggsfs_warn("span_offset=%u, stripe=%u, stripes=%u", span_offset, stripe, block_span->stripes);
        return;
    }

    struct page* page = xa_load(&block_span->pages, page_ix);
    if (page != NULL) { // already there
        goto out_span;
    }

    s64 seqno;
    if (!eggsfs_latch_try_acquire(&block_span->stripe_latches[stripe], seqno)) { // somebody else is working on it
        goto out_span;
    }

    page = xa_load(&block_span->pages, page_ix);
    if (page != NULL) { // somebody got to it first
        eggsfs_latch_release(&block_span->stripe_latches[stripe], seqno);
        goto out_span;
    }

    // Finally start the work.
    struct eggsfs_fetch_stripe_state* st = eggsfs_start_fetch_stripe(block_span, stripe, seqno, true);
    if (IS_ERR(st)) {
        eggsfs_latch_release(&block_span->stripe_latches[stripe], seqno);
        eggsfs_debug("prefetch failed err=%ld", PTR_ERR(st));
        return;
    }

    // We want the state to go once all the block fetches are done.
    eggsfs_put_fetch_stripe(st);
    return;

out_span:
    eggsfs_span_put(&block_span->span, false);
}

// Returns whether we're low on memory
static bool eggsfs_reclaim_after_fetch_stripe(void) {
    // Reclaim pages if we went over the limit
    u64 pages = atomic64_read(&eggsfs_stat_cached_span_pages);
    u64 free_pages = si_mem_available();
    bool low_on_memory = false;
    if (pages*PAGE_SIZE > eggsfs_span_cache_max_size_sync || free_pages*PAGE_SIZE < eggsfs_span_cache_min_avail_mem_sync) {
        low_on_memory = true;
        // sync dropping, apply backpressure
        if (!mutex_lock_killable(&eggsfs_drop_spans_mu)) {
            eggsfs_drop_spans("sync");
            mutex_unlock(&eggsfs_drop_spans_mu);
        }
    } else if (pages*PAGE_SIZE > eggsfs_span_cache_max_size_async || free_pages*PAGE_SIZE < eggsfs_span_cache_min_avail_mem_async) {
        low_on_memory = true;
        // don't bother submitting if another span dropper is running already
        if (!mutex_is_locked(&eggsfs_drop_spans_mu)) {
            // TODO Is it a good idea to do it on system_long_wq rather than eggsfs_wq? The freeing
            // job might face heavy contention, so maybe yes?
            // On the other end, this might make it harder to know when it's safe to unload
            // the module by making sure that there's no work left on the queue.
            queue_work(system_long_wq, &eggsfs_reclaim_spans_work);
        }
    }
    return low_on_memory;
}

extern int eggsfs_prefetch;

static void eggsfs_maybe_prefetch(struct eggsfs_block_span* span, bool low_on_memory, u64 stripe_start, u64 stripe_end) {
    if (!eggsfs_prefetch) { return; }

    struct eggsfs_inode_file* file = &span->span.enode->file;
    u64 prefetch_section = atomic64_read(&file->prefetch_section);
    u32 prefetch_section_begin = prefetch_section & (uint32_t)(-1);
    u32 prefetch_section_end = (prefetch_section>>32) & (uint32_t)(-1);
    if (stripe_start > prefetch_section_begin && stripe_end < prefetch_section_end) {
        // we're comfortably inside, nothing to do
    } if (stripe_end < prefetch_section_begin || stripe_start > prefetch_section_end) {
        // we're totally out of bounds, reset
        prefetch_section_begin = stripe_start;
        prefetch_section_end = stripe_end;
    } else {
        u64 off;
        // there is some overlap, but we're pushing the boundary, prefetch the next or previous thing
        if (stripe_end > prefetch_section_end) { // going rightwards
            prefetch_section_end = stripe_end;
            off = stripe_end;
        } else { // going leftwards
            prefetch_section_begin = stripe_start;
            off = stripe_start-1;
        }
        u64 new_prefetch_section = prefetch_section_begin | ((u64)prefetch_section_end << 32);
        if (likely(atomic64_cmpxchg(&file->prefetch_section, prefetch_section, new_prefetch_section) == prefetch_section)) {
            eggsfs_do_prefetch(span, off);
        }
    }
}

struct page* eggsfs_get_span_page(struct eggsfs_block_span* span, u32 page_ix) {
    trace_eggsfs_get_span_page_enter(span->span.enode->inode.i_ino, span->span.start, page_ix*PAGE_SIZE);

    // We need to load the stripe
    int D = eggsfs_data_blocks(span->parity);
    u32 span_offset = page_ix * PAGE_SIZE;
    u32 stripe = span_offset / (span->cell_size*D);
    bool low_on_memory = false;
    int err = 0;

#define GET_PAGE_EXIT(p) do { \
        int __err = IS_ERR(p) ? PTR_ERR(p) : 0; \
        if (likely(__err == 0)) { \
            eggsfs_maybe_prefetch(span, low_on_memory, span->span.start + stripe*(span->cell_size*D), span->span.start + (stripe+1)*(span->cell_size*D)); \
        } \
        trace_eggsfs_get_span_page_exit(span->span.enode->inode.i_ino, span->span.start, page_ix*PAGE_SIZE, __err); \
        return p; \
    } while(0) 

    // TODO better error?
    if (stripe > span->stripes) {
        eggsfs_warn("span_offset=%u, stripe=%u, stripes=%u", span_offset, stripe, span->stripes);
        GET_PAGE_EXIT(ERR_PTR(-EIO));
    }

    // this should be guaranteed by the caller but we rely on it below, so let's check
    if (span->cell_size%PAGE_SIZE != 0) {
        eggsfs_warn("cell_size=%u, PAGE_SIZE=%lu, span->cell_size%%PAGE_SIZE=%lu", span->cell_size, PAGE_SIZE, span->cell_size%PAGE_SIZE);
        GET_PAGE_EXIT(ERR_PTR(-EIO));
    }

    struct page* page;

again:
    page = xa_load(&span->pages, page_ix);
    if (page != NULL) { GET_PAGE_EXIT(page); }

    s64 seqno;
    if (!eggsfs_latch_try_acquire(&span->stripe_latches[stripe], seqno)) {
        int err = eggsfs_latch_wait_killable(&span->stripe_latches[stripe], seqno);
        if (err) { GET_PAGE_EXIT(ERR_PTR(err)); }
        goto again;
    }

    page = xa_load(&span->pages, page_ix);
    if (page != NULL) {
        // somebody got to it first
        eggsfs_latch_release(&span->stripe_latches[stripe], seqno);
        GET_PAGE_EXIT(page);
    }

    eggsfs_debug("about to start fetch stripe");
    struct eggsfs_fetch_stripe_state* st = NULL;
    st = eggsfs_start_fetch_stripe(span, stripe, seqno, false);
    if (IS_ERR(st)) {
        err = PTR_ERR(st);
        st = NULL;
        goto out_err;
    }
    eggsfs_debug("started fetch stripe, waiting");

    // Wait for all the block requests
    err = down_killable(&st->sema);
    if (err) { goto out_err; }

    eggsfs_debug("done");

    err = atomic_read(&st->err);
    if (err) { goto out_err; }

    // We're finally done
    page = xa_load(&span->pages, page_ix);
    // eggsfs_info("loaded page %p for span %p at %u", page, span, page_ix);
    BUG_ON(page == NULL);

out:
    // Note that the latch was already released by the fetchers.
    if (st != NULL) { eggsfs_put_fetch_stripe(st); }

    // Reclaim if needed
    low_on_memory = eggsfs_reclaim_after_fetch_stripe();

    GET_PAGE_EXIT(err == 0 ? page : ERR_PTR(err));

out_err:
    eggsfs_debug("getting span page failed, err=%d", err);
    goto out;

#undef GET_PAGE_EXIT

}

int eggsfs_span_init(void) {
    int i;
    for (i = 0; i < EGGSFS_SPAN_LRUS; i++) {
        spin_lock_init(&eggsfs_span_lrus[i].lock);
        INIT_LIST_HEAD(&eggsfs_span_lrus[i].lru);
    }

    int err = register_shrinker(&eggsfs_span_shrinker);
    if (err) { return err; }

    return 0;
}

void eggsfs_span_exit(void) {
    unregister_shrinker(&eggsfs_span_shrinker);
}
