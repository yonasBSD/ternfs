#include <linux/module.h>
#include <linux/random.h>

#include "bincode.h"
#include "inode.h"
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
#include "sysctl.h"

EGGSFS_DEFINE_COUNTER(eggsfs_stat_cached_spans);

// This currently also includes in-flight pages (i.e. pages that cannot be
// reclaimed), just because the code is a bit simpler this way.
atomic64_t eggsfs_stat_cached_span_pages = ATOMIC64_INIT(0);

// These numbers do not mean anything in particular. We do want to avoid
// flickering sync drops though, therefore we go down to 25GiB from
// 50GiB, and similarly for free memory.
unsigned long eggsfs_span_cache_max_size_async = (50ull << 30); // 50GiB
unsigned long eggsfs_span_cache_min_avail_mem_async = (2ull << 30); // 2GiB
unsigned long eggsfs_span_cache_max_size_drop = (45ull << 30); // 45GiB
unsigned long eggsfs_span_cache_min_avail_mem_drop = (2ull << 30) + (500ull << 20); // 2GiB + 500MiB
unsigned long eggsfs_span_cache_max_size_sync = (100ull << 30); // 100GiB
unsigned long eggsfs_span_cache_min_avail_mem_sync = (1ull << 30); // 1GiB

static struct kmem_cache* eggsfs_block_span_cachep;
static struct kmem_cache* eggsfs_inline_span_cachep;
static struct kmem_cache* eggsfs_fetch_stripe_cachep;
struct eggsfs_span_lru {
    spinlock_t lock;
    struct list_head lru;
} ____cacheline_aligned;

// These are global locks, but the sharding should alleviate contention.
#define EGGSFS_SPAN_BITS 8
#define EGGSFS_SPAN_LRUS (1<<EGGSFS_SPAN_BITS) // 256
static struct eggsfs_span_lru span_lrus[EGGSFS_SPAN_LRUS];

static struct eggsfs_span_lru* get_span_lru(struct eggsfs_span* span) {
    int h;
    h  = hash_64(span->ino, EGGSFS_SPAN_BITS);
    h ^= hash_64(span->start, EGGSFS_SPAN_BITS);
    return &span_lrus[h%EGGSFS_SPAN_LRUS];
}

static struct eggsfs_span* lookup_span(struct rb_root* spans, u64 offset) {
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

static bool insert_span(struct rb_root* spans, struct eggsfs_span* span) {
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

static void init_block_span(void* p) {
    struct eggsfs_block_span* span = (struct eggsfs_block_span*)p;

    xa_init(&span->pages);
    int i;
    for (i = 0; i < EGGSFS_MAX_STRIPES; i++) {
        eggsfs_latch_init(&span->stripe_latches[i]);
    }
}

static void free_span(struct eggsfs_span* span, u64* dropped_pages) {
    u64 dropped = 0;
    if (span->storage_class == EGGSFS_INLINE_STORAGE) {
        kmem_cache_free(eggsfs_inline_span_cachep, EGGSFS_INLINE_SPAN(span));
    } else {
        struct eggsfs_block_span* block_span = EGGSFS_BLOCK_SPAN(span);
        eggsfs_counter_dec(eggsfs_stat_cached_spans);
        struct page* page;
        unsigned long page_ix;
        xa_for_each(&block_span->pages, page_ix, page) {
            put_page(page);
            dropped++;
            BUG_ON(xa_erase(&block_span->pages, page_ix) == NULL);
        }
        int i;
        for (i = 0; i < EGGSFS_MAX_STRIPES; i++) {
            BUG_ON(atomic64_read(&block_span->stripe_latches[i].counter) & 1); // nobody still hanging onto this
        }
        kmem_cache_free(eggsfs_block_span_cachep, block_span);
    }
    if (dropped_pages != NULL) {
        (*dropped_pages) += dropped;
    }
    atomic64_sub(dropped, &eggsfs_stat_cached_span_pages);
}

struct get_span_ctx {
    struct eggsfs_inode* enode;
    struct list_head spans;
    int err;
};

void eggsfs_file_spans_cb_span(void* data, u64 offset, u32 size, u32 crc, u8 storage_class, u8 parity, u8 stripes, u32 cell_size, const uint32_t* stripes_crcs) {
    eggsfs_debug("offset=%llu size=%u crc=%08x storage_class=%d parity=%d stripes=%d cell_size=%u", offset, size, crc, storage_class, parity, stripes, cell_size);

    struct get_span_ctx* ctx = (struct get_span_ctx*)data;
    if (ctx->err) { return; }

    struct eggsfs_block_span* span = kmem_cache_alloc(eggsfs_block_span_cachep, GFP_KERNEL);
    if (!span) { ctx->err = -ENOMEM; return; }

    if (eggsfs_data_blocks(parity) > EGGSFS_MAX_DATA || eggsfs_parity_blocks(parity) > EGGSFS_MAX_PARITY) {
        eggsfs_warn("D=%d > %d || P=%d > %d", eggsfs_data_blocks(parity), EGGSFS_MAX_DATA, eggsfs_data_blocks(parity), EGGSFS_MAX_PARITY);
        ctx->err = -EIO;
        return;
    }

    span->span.ino = ctx->enode->inode.i_ino;
    span->span.start = offset;
    span->span.end = offset + size;
    span->span.storage_class = storage_class;
    span->enode = ctx->enode; // no ihold: `span->enode` is cleared before eviction
    

    span->cell_size = cell_size;
    memcpy(span->stripes_crc, stripes_crcs, sizeof(uint32_t)*stripes);
    span->stripes = stripes;
    span->parity = parity;
    // important, this will have readers skip over this until it ends up in the LRU
    span->refcount = -1;

    eggsfs_debug("adding normal span");
    list_add_tail(&span->span.lru, &ctx->spans);
}

void eggsfs_file_spans_cb_block(
    void* data, int block_ix,
    u64 bs_id, u32 ip1, u16 port1, u32 ip2, u16 port2, u8 flags,
    u64 block_id, u32 crc
) {
    struct get_span_ctx* ctx = (struct get_span_ctx*)data;
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

    struct get_span_ctx* ctx = (struct get_span_ctx*)data;
    if (ctx->err) { return; }
 
    struct eggsfs_inline_span* span = kmem_cache_alloc(eggsfs_inline_span_cachep, GFP_KERNEL);
    if (!span) { ctx->err = -ENOMEM; return; }

    span->span.ino = ctx->enode->inode.i_ino;

    span->span.start = offset;
    span->span.end = offset + size;
    span->span.storage_class = EGGSFS_INLINE_STORAGE;
    span->len = len;
    memcpy(span->body, body, len);

    eggsfs_debug("adding inline span");

    list_add_tail(&span->span.lru, &ctx->spans);
}

// returns whether we could acquire it
static bool span_acquire(struct eggsfs_span* span) {
    if (span->storage_class == EGGSFS_INLINE_STORAGE) { return true; }

    struct eggsfs_block_span* block_span = EGGSFS_BLOCK_SPAN(span);
    struct eggsfs_span_lru* lru = get_span_lru(span);

    spin_lock_bh(&lru->lock);
    if (unlikely(block_span->refcount < 0)) { // the reclaimer got here before us.
        spin_unlock_bh(&lru->lock);
        return false;
    }
    block_span->refcount++;
    if (block_span->refcount == 1) {
        // we're the first ones here, take it out of the LRU
        list_del(&span->lru);
    }
    spin_unlock_bh(&lru->lock);

    return true;
}

// Looks up and acquires a span. Returns -EBUSY if
// the span is there, but it could not be acquired (contention
// with LRU reclaimer). Returns NULL if the span is not there.
static struct eggsfs_span* lookup_and_acquire_span(struct eggsfs_inode* enode, u64 offset) {
    struct eggsfs_inode_file* file = &enode->file;

    down_read(&enode->file.spans_lock);

    struct eggsfs_span* span = lookup_span(&file->spans, offset);
    if (likely(span)) {
        if (unlikely(!span_acquire(span))) {
            up_read(&enode->file.spans_lock);
            return ERR_PTR(-EBUSY);
        }
    }
    up_read(&enode->file.spans_lock);

    return span;
}

void eggsfs_put_span(struct eggsfs_span* span, bool was_read) {
    if (span->storage_class == EGGSFS_INLINE_STORAGE) { return; }

    struct eggsfs_block_span* block_span = EGGSFS_BLOCK_SPAN(span);
    struct eggsfs_span_lru* lru = get_span_lru(span);

    spin_lock_bh(&lru->lock);
    BUG_ON(block_span->refcount < 1);
    block_span->refcount--;
    block_span->touched = block_span->touched || was_read;
    if (block_span->refcount == 0) { // we need to put it back into the LRU
        if (block_span->touched) {
            list_add_tail(&span->lru, &lru->lru);
        } else {
            list_add(&span->lru, &lru->lru);
        }
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

    // Check if we already have the span
    {
        struct eggsfs_span* span = lookup_and_acquire_span(enode, offset);
        if (unlikely(IS_ERR(span))) {
            int err = PTR_ERR(span);
            if (err == -EBUSY) { goto retry; } // we couldn't acquire the span
            GET_SPAN_EXIT(span); // some other error
        } else if (likely(span != NULL)) {
            GET_SPAN_EXIT(span);
        }
    }

    // We need to fetch the spans. Note that if multiple readers are competing
    // for this span one will win, but both will fetch the span. We prefer
    // this wasted work to taking the write lock for long, which could cause
    // ugly situations if the shrinker is called while this is working, which
    // is possible since `eggsfs_shard_file_spans` allocates in `eggsfs_file_spans_cb_span`.
    // The `alloc_pages*` functions can invoke the shrinker to get as they try to get
    // a page, which in turn might invoke the span shrinker, resulting in a deadlock.

    // We always get the full set of spans, this simplifies prefetching (we just
    // blindly assume the span is there, and if it's been reclaimed in the meantime
    // we just don't care).
    u64 spans_offset;
    for (spans_offset = 0;;) {
        // fetch next batch of spans
        u64 next_offset;
        struct get_span_ctx ctx = { .err = 0, .enode = enode };
        INIT_LIST_HEAD(&ctx.spans);
        err = eggsfs_error_to_linux(eggsfs_shard_file_spans(
            (struct eggsfs_fs_info*)enode->inode.i_sb->s_fs_info, enode->inode.i_ino, spans_offset, &next_offset,&ctx
        ));
        err = err ?: ctx.err;
        if (unlikely(err)) {
            eggsfs_debug("failed to get file spans at %llu err=%d", spans_offset, err);
            for (;;) {
                struct eggsfs_span* span = list_first_entry_or_null(&ctx.spans, struct eggsfs_span, lru);
                if (span == NULL) { break; }
                list_del(&span->lru);
                free_span(span, NULL);
            }
            GET_SPAN_EXIT(ERR_PTR(err));
        }
        // add them to enode spans and LRU
        down_write(&file->spans_lock);
        for (;;) {
            struct eggsfs_span* span = list_first_entry_or_null(&ctx.spans, struct eggsfs_span, lru);
            if (span == NULL) { break; }
            list_del(&span->lru);
            if (!insert_span(&file->spans, span)) {
                // Span is already cached
                free_span(span, NULL);
            } else {
                if (span->storage_class != EGGSFS_INLINE_STORAGE) {
                    // Not already cached, must add it to the LRU
                    eggsfs_counter_inc(eggsfs_stat_cached_spans);
                    struct eggsfs_block_span* block_span = EGGSFS_BLOCK_SPAN(span);
                    struct eggsfs_span_lru* lru = get_span_lru(span);
                    spin_lock_bh(&lru->lock);
                    block_span->refcount = 0;
                    list_add(&span->lru, &lru->lru);
                    spin_unlock_bh(&lru->lock);
                }
            }
        }
        up_write(&file->spans_lock);
        // stop if we're done
        if (next_offset == 0) { break; }
        spans_offset = next_offset;
    }

    // We now restart, we know that the span must be there (unless the shard is broken).
    // It might get reclaimed in the meantime though.
    goto retry;

#undef GET_SPAN_EXIT
}

// If it returns -1, there are no spans to drop. Otherwise, returns the
// next index to pass in to reclaim the next span.
static int drop_one_span(int lru_ix, u64* examined_spans, u64* dropped_pages) {
    BUG_ON(lru_ix < 0 || lru_ix >= EGGSFS_SPAN_LRUS);

    int i;
    for (i = lru_ix; i < lru_ix + EGGSFS_SPAN_LRUS; i++) {
        struct eggsfs_span_lru* lru = &span_lrus[i%EGGSFS_SPAN_LRUS];

        struct eggsfs_span* candidate = NULL;
        struct eggsfs_inode* enode = NULL;
        bool inode_held = false;

        // Pick a candidate from the LRU, mark it as reclaiming, take it out of
        // the LRU.
        spin_lock_bh(&lru->lock);
        candidate = list_first_entry_or_null(&lru->lru, struct eggsfs_span, lru);
        if (unlikely(candidate == NULL)) {
            goto unlock;
        } else {
            (*examined_spans)++;
            BUG_ON(candidate->storage_class == EGGSFS_INLINE_STORAGE); // no inline spans in LRU
            struct eggsfs_block_span* candidate_blocks = EGGSFS_BLOCK_SPAN(candidate);
            if (likely(candidate_blocks->enode)) {
                // There's a span of time between the last inode reference being dropped
                // and the eviction function being called where we do have this enode
                // here but we also don't have references to it anymore. Note that this might
                // be a long span of time, since the inode might sit in the cache for a
                // while.
                //
                // Since we're marking this as being reclaimed this can be safely proceed
                // in this case anyway, since the eviction function will keep retrying
                // until the currently being reclaimed span is gone.
                inode_held = atomic_inc_not_zero(&candidate_blocks->enode->inode.i_count); // conditional ihold
                enode = candidate_blocks->enode;
            }
            // we just got this from LRU, it must be readers == 0
            BUG_ON(candidate_blocks->refcount != 0);
            candidate_blocks->refcount = -1;
            list_del(&candidate->lru);
        }
unlock:
        spin_unlock_bh(&lru->lock);

        if (candidate) {
            // Take it out of the spans for the file
            if (likely(enode)) {
                down_write(&enode->file.spans_lock);
                rb_erase(&candidate->node, &enode->file.spans);
                up_write(&enode->file.spans_lock);
                if (inode_held) { iput(&enode->inode); }
            }

            // At this point we're free to do what we please with the span
            // (it's fully private to us).
            free_span(candidate, dropped_pages);

            i++;
            break;
        }
    }

    // we've fully gone around without finding anything
    if (i == lru_ix + EGGSFS_SPAN_LRUS) { return -1; }

    // We've found something
    return i%EGGSFS_SPAN_LRUS;
}

static inline void drop_spans_start(const char* type) {
    trace_eggsfs_drop_spans(
        type, PAGE_SIZE*si_mem_available(), atomic64_read(&eggsfs_stat_cached_span_pages), eggsfs_counter_get(&eggsfs_stat_cached_spans), EGGSFS_DROP_SPANS_START, 0, 0, 0
    );
}

static inline void drop_spans_end(const char* type, u64 examined_spans, u64 dropped_pages, int err) {
    trace_eggsfs_drop_spans(
        type, PAGE_SIZE*si_mem_available(), atomic64_read(&eggsfs_stat_cached_span_pages), eggsfs_counter_get(&eggsfs_stat_cached_spans), EGGSFS_DROP_SPANS_END, examined_spans, dropped_pages, err
    );
}

// returns the number of dropped pages
u64 eggsfs_drop_all_spans(void) {
    drop_spans_start("all");
    u64 dropped_pages = 0;
    u64 examined_spans = 0;
    s64 spans_begin = eggsfs_counter_get(&eggsfs_stat_cached_spans);
    int lru_ix = 0;
    for (;;) {
        lru_ix = drop_one_span(lru_ix, &examined_spans, &dropped_pages);
        if (lru_ix < 0) { break; }
    }
    s64 spans_end = eggsfs_counter_get(&eggsfs_stat_cached_spans);
    eggsfs_info("reclaimed %llu pages, %lld spans (approx)", dropped_pages, spans_begin-spans_end);
    drop_spans_end("all", examined_spans, dropped_pages, 0);
    return dropped_pages;
}

static DEFINE_MUTEX(drop_spans_mu);

// Return negative if lock could not be taken, otherwise the number of dropped
// pages.
static s64 drop_spans(const char* type) {
    drop_spans_start(type);

    mutex_lock(&drop_spans_mu);

    u64 dropped_pages = 0;
    u64 examined_spans = 0;
    int lru_ix = 0;
    for (;;) {
        u64 pages = atomic64_read(&eggsfs_stat_cached_span_pages);
        if (
            pages*PAGE_SIZE < eggsfs_span_cache_max_size_drop &&
            si_mem_available()*PAGE_SIZE > eggsfs_span_cache_min_avail_mem_drop
        ) {
            break;
        }
        lru_ix = drop_one_span(lru_ix, &examined_spans, &dropped_pages);
        if (lru_ix < 0) { break; }
    }
    eggsfs_debug("dropped %llu pages, %llu spans examined", dropped_pages, examined_spans);

    mutex_unlock(&drop_spans_mu);

    drop_spans_end(type, examined_spans, dropped_pages, 0);

    return dropped_pages;
}

static void reclaim_spans_async(struct work_struct* work) {
    if (!mutex_is_locked(&drop_spans_mu)) { // somebody's already taking care of it
        drop_spans("async");
    }
}

static DECLARE_WORK(reclaim_spans_work, reclaim_spans_async);

static unsigned long eggsfs_span_shrinker_count(struct shrinker* shrinker, struct shrink_control* sc) {
    u64 pages = atomic64_read(&eggsfs_stat_cached_span_pages);
    if (pages == 0) { return SHRINK_EMPTY; }
    return pages;
}

static int eggsfs_span_shrinker_lru_ix = 0;

static unsigned long eggsfs_span_shrinker_scan(struct shrinker* shrinker, struct shrink_control* sc) {
    drop_spans_start("shrinker");

    u64 dropped_pages = 0;
    u64 examined_spans = 0;
    int lru_ix = eggsfs_span_shrinker_lru_ix;
    for (dropped_pages = 0; dropped_pages < sc->nr_to_scan;) {
        lru_ix = drop_one_span(lru_ix, &examined_spans, &dropped_pages);
        if (lru_ix < 0) { break; }
    }
    eggsfs_span_shrinker_lru_ix = lru_ix >= 0 ? lru_ix : 0;

    drop_spans_end("shrinker", examined_spans, dropped_pages, 0);

    return dropped_pages;
}

static struct shrinker eggsfs_span_shrinker = {
    .count_objects = eggsfs_span_shrinker_count,
    .scan_objects = eggsfs_span_shrinker_scan,
    .seeks = DEFAULT_SEEKS,
    .flags = SHRINKER_NONSLAB, // what's this?
};

#define DROP_FILE_SPAN_SUCCESS 0
#define DROP_FILE_SPAN_BEING_RECLAIMED 1
#define DROP_FILE_SPAN_HAS_READERS 2
static int drop_file_span(struct eggsfs_inode* enode, struct eggsfs_span* span, bool allow_readers) {
    struct eggsfs_block_span* block_span = NULL;
    bool can_free_span = true;
    if (span->storage_class != EGGSFS_INLINE_STORAGE) {
        block_span = EGGSFS_BLOCK_SPAN(span);
        struct eggsfs_span_lru* lru = get_span_lru(span);
        spin_lock_bh(&lru->lock);
        if (unlikely(block_span->refcount < 0)) {
            // This is being reclaimed, we wait until the reclaimer cleans
            // it up for us. This only happens if the reclaimer started
            // before linux decides to evict.
            spin_unlock_bh(&lru->lock);
            return DROP_FILE_SPAN_BEING_RECLAIMED;
        } else if (unlikely(block_span->refcount > 0)) {
            if (!allow_readers) {
                spin_unlock_bh(&lru->lock);
                return DROP_FILE_SPAN_HAS_READERS;
            }
            // Note that the assumption here is that the reader will correctly put
            // the span back in the LRU once done, so this should not happen.
            eggsfs_warn("span at %llu for inode %016lx still has %d readers, will linger on until next reclaim", span->start, enode->inode.i_ino, block_span->refcount);
            can_free_span = false;
        } else {
            // Make ourselves the reclaimer. Note that we could just let the
            // reclaimer take care of it eventually, but this is the very
            // common case and it'll be faster.
            list_del(&span->lru);
            block_span->refcount = -1;
        }
        block_span->enode = NULL;
        spin_unlock_bh(&lru->lock);
    }

    rb_erase(&span->node, &enode->file.spans);

    if (can_free_span) { free_span(span, NULL); }

    return DROP_FILE_SPAN_SUCCESS;
}

int eggsfs_drop_file_span(struct eggsfs_inode* enode, u64 offset) {
    int err = 0;

again:
    down_write(&enode->file.spans_lock);

    // Did somebody already clear this? In that case we're done.
    struct eggsfs_span* span = lookup_span(&enode->file.spans, offset);
    if (span == NULL) {
        goto out;
    }

    // Don't want to clear the enode since the span might very well
    // be in use, and things that use it might need the enode.
    //
    // Here we don't allow readers since otherwise there isn't much to
    // do: we can't remove the span from the RB tree and not set the
    // ->enode to NULL, since otherwise we'll have a dangling reference
    // to the enode which might go stale, since it won't be cleared
    // anymore on inode eviction.
    //
    // Since the use-case for this function is pretty exotic anyway,
    // this is hopefully fine.
    int res = drop_file_span(enode, span, false);
    if (unlikely(res == DROP_FILE_SPAN_HAS_READERS)) {
        eggsfs_warn("gave up on dropping span for %016lx because it has readers", enode->inode.i_ino);
        err = -EBUSY;
        goto out;
    }
    if (unlikely(res == DROP_FILE_SPAN_BEING_RECLAIMED)) {
        eggsfs_info("span for %016lx is already being reclaimed, will retry", enode->inode.i_ino);
        up_write(&enode->file.spans_lock);
        goto again;
    }
    BUG_ON(res != DROP_FILE_SPAN_SUCCESS);

    eggsfs_debug("span for %016lx has been reclaimed", enode->inode.i_ino);

out:
    up_write(&enode->file.spans_lock);
    return err;
}

void eggsfs_drop_file_spans(struct eggsfs_inode* enode) {
again:
    down_write(&enode->file.spans_lock);

    for (;;) {
        struct rb_node* node = rb_first(&enode->file.spans);
        if (node == NULL) { break; }
    
        struct eggsfs_span* span = rb_entry(node, struct eggsfs_span, node);
        int res = drop_file_span(enode, span, true);
        if (unlikely(res != DROP_FILE_SPAN_SUCCESS)) {
            BUG_ON(res != DROP_FILE_SPAN_BEING_RECLAIMED);
            up_write(&enode->file.spans_lock);
            goto again;
        }    
    }

    up_write(&enode->file.spans_lock);
}

struct fetch_stripe_state {
    // ihold'd
    struct eggsfs_inode *enode;
    struct semaphore sema; // used to wait on every block being finished. could be done faster with a wait_queue, but don't want to worry about smb subtleties.
    s64 stripe_seqno;      // used to release the stripe when we're done with it
    struct eggsfs_block_span* span; // we hold this (via span acquire) until we're done.
    // these will be filled in by the fetching (only D of them well be
    // filled by fetching the blocks, the others might be filled in by the RS
    // recovery)
    struct list_head blocks_pages[EGGSFS_MAX_BLOCKS];
    atomic_t refcount;       // to garbage collect
    atomic_t err;            // the result of the stripe fetching
    atomic_t last_block_err; // to return something vaguely connected to what happened
    static_assert(EGGSFS_MAX_BLOCKS <= 16);
    // 00-16: blocks which are downloading.
    // 16-32: blocks which have succeeded.
    // 32-48: blocks which have failed.
    atomic64_t blocks;
    u8 stripe;              // which stripe we're into
    u8 prefetching;         // used for logging
};

#define DOWNLOADING_SHIFT 0
#define DOWNLOADING_MASK (0xFFFFull<<DOWNLOADING_SHIFT)
#define DOWNLOADING_GET(__b) ((__b>>DOWNLOADING_SHIFT) & 0xFFFFull)
#define DOWNLOADING_SET(__b, __i) (__b | 1ull<<(__i+DOWNLOADING_SHIFT))
#define DOWNLOADING_UNSET(__b, __i) (__b & ~(1ull<<(__i+DOWNLOADING_SHIFT)))

#define SUCCEEDED_SHIFT 16
#define SUCCEEDED_MASK (0xFFFFull<<SUCCEEDED_SHIFT)
#define SUCCEEDED_GET(__b) ((__b>>SUCCEEDED_SHIFT) & 0xFFFFull)
#define SUCCEEDED_SET(__b, __i) (__b | 1ull<<(__i+SUCCEEDED_SHIFT))
#define SUCCEEDED_UNSET(__b, __i) (__b & ~(1ull<<(__i+SUCCEEDED_SHIFT)))

#define FAILED_SHIFT 32
#define FAILED_MASK (0xFFFFull<<FAILED_SHIFT)
#define FAILED_GET(__b) ((__b>>FAILED_SHIFT) & 0xFFFFull)
#define FAILED_SET(__b, __i) (__b | 1ull<<(__i+FAILED_SHIFT))
#define FAILED_UNSET(__b, __i) (__b & ~(1ull<<(__i+FAILED_SHIFT)))

static void init_fetch_stripe(void* p) {
    struct fetch_stripe_state* st = (struct fetch_stripe_state*)p;

    sema_init(&st->sema, 0);
    int i;
    for (i = 0; i < EGGSFS_MAX_BLOCKS; i++) {
        INIT_LIST_HEAD(&st->blocks_pages[i]);
    }
}

// This must be called while already holding the span, which means
// that span_acquire will always succeed.
static struct fetch_stripe_state* new_fetch_stripe(
    struct eggsfs_inode* enode,
    struct eggsfs_block_span* span,
    u8 stripe,
    u64 stripe_seqno,
    bool prefetching
) {
    struct fetch_stripe_state* st = (struct fetch_stripe_state*)kmem_cache_alloc(eggsfs_fetch_stripe_cachep, GFP_KERNEL);
    if (!st) { return st; }

    BUG_ON(!span_acquire(&span->span));

    st->enode = enode;
    st->stripe_seqno = stripe_seqno;
    st->span = span;
    atomic_set(&st->refcount, 1); // the caller
    atomic_set(&st->err, 0);
    atomic_set(&st->last_block_err, 0);
    st->stripe = stripe;
    st->prefetching = prefetching;
    atomic64_set(&st->blocks, 0);

    eggsfs_in_flight_begin(enode);

    return st;
}

static void fetch_stripe_trace(struct fetch_stripe_state* st, u8 event, s8 block, int err) {
    trace_eggsfs_fetch_stripe(
        st->enode->inode.i_ino,
        st->span->span.start,
        st->stripe,
        st->span->parity,
        st->prefetching,
        event,
        block,
        err
    );
}

static void free_fetch_stripe(struct fetch_stripe_state* st) {
    // we're the last ones here
    fetch_stripe_trace(st, EGGSFS_FETCH_STRIPE_FREE, -1, atomic_read(&st->err));

    // Note that we release this here, rather than when `remaining == 0`, since
    // there's no guarantee that we reach `remaining == 0` (we might fail to start
    // enough requests)
    eggsfs_latch_release(&st->span->stripe_latches[st->stripe], st->stripe_seqno);

    while (down_trylock(&st->sema) == 0) {} // reset sema to zero for next usage

    // Free leftover blocks (also ensures the lists are all nice and empty for the
    // next user)
    int i;
    for (i = 0; i < EGGSFS_MAX_BLOCKS; i++) {
        put_pages_list(&st->blocks_pages[i]);
    }

    eggsfs_in_flight_end(st->enode);

    // We don't need the span anymore
    eggsfs_put_span(&st->span->span, !st->prefetching);

    kmem_cache_free(eggsfs_fetch_stripe_cachep, st);
}

static void put_fetch_stripe(struct fetch_stripe_state* st) {
    int remaining = atomic_dec_return(&st->refcount);
    eggsfs_debug("st=%p remaining=%d", st, remaining);
    BUG_ON(remaining < 0);
    if (remaining > 0) { return; }
    free_fetch_stripe(st);
}

static void hold_fetch_stripe(struct fetch_stripe_state* st) {
    int holding = atomic_inc_return(&st->refcount);
    eggsfs_debug("st=%p holding=%d", st, holding)
    WARN_ON(holding < 2);
}

static void store_pages(struct fetch_stripe_state* st) {
    struct eggsfs_block_span* span = st->span;
    int D = eggsfs_data_blocks(span->parity);

    int i, j;

    u32 pages_per_block = span->cell_size/PAGE_SIZE;

    u16 blocks = SUCCEEDED_GET(atomic64_read(&st->blocks));
    BUG_ON(__builtin_popcountll(blocks) != D);
    if (unlikely(blocks != (1ull<<D)-1)) { // we need to recover data using RS
        for (i = 0; i < D; i++) { // fill in missing data blocks
            if ((1ull<<i) & blocks) { continue; } // we have this one already
            // eggsfs_info("recovering %d, pages=%u", i, pages);
            // allocate the pages
            struct list_head* i_pages = &st->blocks_pages[i];
            for (j = 0; j < pages_per_block; j++) {
                struct page* page = alloc_page(GFP_KERNEL);
                if (!page) {
                    atomic_set(&st->err, -ENOMEM);
                    return;
                }
                list_add_tail(&page->lru, i_pages);
            }
            // compute
            int err = eggsfs_recover(span->parity, blocks, 1ull<<i, pages_per_block, st->blocks_pages);
            if (err) {
                atomic_set(&st->err, err);
                return;
            }
        }
    }

    // compute crc
    kernel_fpu_begin();
    u32 crc = 0;
    u32 num_pages = 0;
    for (i = 0; i < D; i++) {
        struct page* page;
        list_for_each_entry(page, &st->blocks_pages[i], lru) {
            num_pages++;
            char* page_buf = kmap_atomic(page);
            crc = eggsfs_crc32c(crc, page_buf, PAGE_SIZE);
            kunmap_atomic(page_buf);
        }
    }
    kernel_fpu_end();

    // if crc is wrong, bail immediately
    if (crc != span->stripes_crc[st->stripe]) {
        eggsfs_warn("bad crc for file %016lx, stripe %u, expected %08x, got %08x, expected %u pages, got %u pages", st->enode->inode.i_ino, st->stripe, span->stripes_crc[st->stripe], crc, pages_per_block*D, num_pages);
        atomic_set(&st->err, -EIO);
        return;
    }

    u32 curr_page = pages_per_block*D*st->stripe;

    // Now move pages to the span. Note that we can't add them
    // as we compute the CRC because `xa_store` might allocate.
    for (i = 0; i < D; i++) {
        struct page* page;
        struct page* tmp;
        list_for_each_entry_safe(page, tmp, &st->blocks_pages[i], lru) {
            list_del(&page->lru);
            struct page* old_page = xa_store(&span->pages, curr_page, page, GFP_KERNEL);
            if (IS_ERR(old_page)) {
                atomic_set(&st->err, PTR_ERR(old_page));
                eggsfs_info("xa_store failed: %ld, error stored", PTR_ERR(old_page));
                // We need to remove all the pages stored so far, and
                // this errored one.
                put_page(page);
                u32 page_ix;
                for (page_ix = 0; page_ix < curr_page; page_ix++) {
                    page = xa_erase(&span->pages, page_ix);
                    BUG_ON(page == NULL);
                    put_page(page);
                }
                return;
            }
            BUG_ON(old_page != NULL); // the latch protects against this
            curr_page++;
        }
    }

    // We know that we added all of them given the BUG_ON(old_page != NULL)
    // above.
    atomic64_add(pages_per_block*D, &eggsfs_stat_cached_span_pages);
    if (crc != span->stripes_crc[st->stripe]) {
        eggsfs_warn("bad crc for file %016lx, stripe %u, expected %08x, got %08x", st->enode->inode.i_ino, st->stripe, span->stripes_crc[st->stripe], crc);
        atomic_set(&st->err, -EIO);
    }
}

static void block_done(void* data, u64 block_id, struct list_head* pages, int err);

// Starts loading up D blocks as necessary.
static int fetch_blocks(struct fetch_stripe_state* st) {
    int i;

    struct eggsfs_block_span* span = st->span;
    int D = eggsfs_data_blocks(span->parity);
    int P = eggsfs_parity_blocks(span->parity);
    int B = eggsfs_blocks(span->parity);

#define LOG_STR "file=%016llx span=%llu stripe=%u D=%d P=%d downloading=%04x(%llu) succeeded=%04x(%llu) failed=%04x(%llu) last_block_err=%d"
#define LOG_ARGS span->span.ino, span->span.start, st->stripe, D, P, downloading, __builtin_popcountll(downloading), succeeded, __builtin_popcountll(succeeded), failed, __builtin_popcountll(failed), atomic_read(&st->last_block_err)

    for (;;) {
        s64 blocks = atomic64_read(&st->blocks);
        u16 downloading = DOWNLOADING_GET(blocks);
        u16 succeeded = SUCCEEDED_GET(blocks);
        u16 failed = FAILED_GET(blocks);
        if (__builtin_popcountll(downloading)+__builtin_popcountll(succeeded) >= D) { // we managed to get out of the woods
            eggsfs_debug("we have enough blocks, terminating " LOG_STR, LOG_ARGS);
            return 0;
        }
        if (__builtin_popcountll(failed) > P) { // nowhere to go from here but tears
            eggsfs_info("we're out of blocks, giving up " LOG_STR, LOG_ARGS);
            int err = atomic_read(&st->last_block_err);
            BUG_ON(err == 0);
            return err;
        }

        // Find the next block to download. If we're mirrored, download
        // blocks at random, so that we can use mirrored files to avoid
        // hotspot for super busy files.
        if (D == 1) {
            int start = prandom_u32()%B;
            int j;
            for (j = start; j < start + B; j++) {
                i = j%B;
                if (!((1ull<<i) & (downloading|failed|succeeded))) { break; }
            }
        } else {
            for (i = 0; i < B; i++) {
                if (!((1ull<<i) & (downloading|failed|succeeded))) { break; }
            }
        }
        BUG_ON(i == B); // something _must_ be there given the checks above

        eggsfs_debug("next block to be fetched is %d " LOG_STR, i, LOG_ARGS);

        // try to set ourselves as selected
        if (atomic64_cmpxchg(&st->blocks, blocks, DOWNLOADING_SET(blocks, i)) != blocks) {
            eggsfs_debug("skipping %d " LOG_STR, i, LOG_ARGS);
            // somebody else got here first (can happen if some blocks complete before this loop finishes) first, keep going
            continue;
        }

        // ok, we're responsible for this now, start
        struct eggsfs_block* block = &st->span->blocks[i];
        u32 block_offset = st->stripe * span->cell_size;
        fetch_stripe_trace(st, EGGSFS_FETCH_STRIPE_BLOCK_START, i, 0);
        hold_fetch_stripe(st);
        eggsfs_debug("block start st=%p block_service=%016llx block_id=%016llx", st, block->bs.id, block->id);
        int block_err = eggsfs_fetch_block(
            &block_done,
            (void*)st,
            &block->bs, block->id, block_offset, span->cell_size
        );
        if (block_err) {
            atomic_set(&st->last_block_err, block_err);
            put_fetch_stripe(st);
            eggsfs_debug("loading block failed %d " LOG_STR, i, LOG_ARGS);
            fetch_stripe_trace(st, EGGSFS_FETCH_STRIPE_BLOCK_DONE, i, block_err);
            // mark it as failed and not downloading
            blocks = atomic64_read(&st->blocks);
            while (unlikely(!atomic64_try_cmpxchg(&st->blocks, &blocks, FAILED_SET(DOWNLOADING_UNSET(blocks, i), i)))) {}
        } else {
            eggsfs_debug("loading block %d " LOG_STR, i, LOG_ARGS);
        }
    }

#undef LOG_STR
#undef LOG_ARGS
}

static void block_done(void* data, u64 block_id, struct list_head* pages, int err) {
    int i;

    struct fetch_stripe_state* st = (struct fetch_stripe_state*)data;
    struct eggsfs_block_span* span = st->span;

    int D = eggsfs_data_blocks(span->parity);
    int P = eggsfs_parity_blocks(span->parity);
    int B = eggsfs_blocks(span->parity);
    u32 pages_per_block = span->cell_size/PAGE_SIZE;

    for (i = 0; i < B; i++) {
        if (st->span->blocks[i].id == block_id) { break; }
    }
    // eggsfs_info("block ix %d, B=%d", i, B);
    BUG_ON(i == B);

    eggsfs_debug(
        "st=%p block=%d block_id=%016llx file=%016llx span=%llu stripe=%u D=%d P=%d err=%d",
        st, i, block_id, span->span.ino, span->span.start, st->stripe, D, P, err
    );

    fetch_stripe_trace(st, EGGSFS_FETCH_STRIPE_BLOCK_DONE, i, err);

    bool finished = false;
    if (err) {
        eggsfs_info("fetching block %016llx in file %016llx failed: %d", block_id, span->span.ino, err);
        atomic_set(&st->last_block_err, err);
        // it failed, try to restore order
        eggsfs_debug("block %016llx ix %d failed with %d", block_id, i, err);
        // mark it as failed and not downloading
        s64 blocks = atomic64_read(&st->blocks);
        while (unlikely(!atomic64_try_cmpxchg(&st->blocks, &blocks, FAILED_SET(DOWNLOADING_UNSET(blocks, i), i)))) {}
        err = fetch_blocks(st);
        // If we failed to restore order, mark the whole thing as failed,
        // and if we were the first ones to notice the error, finish
        // the overall thing. Other failing requests will just drop the
        // reference.
        if (err) {
            int old_err = 0;
            finished = atomic_try_cmpxchg(&st->err, &old_err, err);
            eggsfs_debug("failed finished=%d", finished);
        }
    } else {
        // it succeeded, grab all the pages
        BUG_ON(!list_empty(&st->blocks_pages[i]));
        struct page* page;
        struct page* tmp;
        u32 fetched_pages = 0;
        list_for_each_entry_safe(page, tmp, pages, lru) {
            list_del(&page->lru);
            list_add_tail(&page->lru, &st->blocks_pages[i]);
            fetched_pages++;
        }
        BUG_ON(fetched_pages != pages_per_block);
        // mark as fetched, see if we're the last ones
        s64 blocks = atomic64_read(&st->blocks);
        while (unlikely(!atomic64_try_cmpxchg(&st->blocks, &blocks, SUCCEEDED_SET(DOWNLOADING_UNSET(blocks, i), i)))) {}
        finished = __builtin_popcountll(SUCCEEDED_GET(SUCCEEDED_SET(blocks, i))) == D; // blocks = last old one, we need to reapply
    }

    if (finished) {
        eggsfs_debug("finished");
        // we're the last one to finish, we need to store the pages in the span,
        // if we have no errors
        err = atomic_read(&st->err);
        if (err == 0) { store_pages(st); }
        // and wake up waiters, if any
        up(&st->sema);
        fetch_stripe_trace(st, EGGSFS_FETCH_STRIPE_END, -1, err);
    }

    // drop our reference
    put_fetch_stripe(st);
}

static struct fetch_stripe_state* start_fetch_stripe(
    struct eggsfs_block_span* span, u8 stripe, s64 stripe_seqno, bool prefetching
) {
    int err;

    BUG_ON(!span->enode); // must still be alive here
    struct eggsfs_inode* enode = span->enode;

    trace_eggsfs_fetch_stripe(
        enode->inode.i_ino,
        span->span.start,
        stripe,
        span->parity,
        prefetching,
        EGGSFS_FETCH_STRIPE_START,
        -1,
        0
    );

    struct fetch_stripe_state* st = NULL;

    int D = eggsfs_data_blocks(span->parity);
    int B = eggsfs_blocks(span->parity);
    if (D > EGGSFS_MAX_DATA || B > EGGSFS_MAX_BLOCKS) {
        eggsfs_error("got out of bounds parity of RS(%d,%d) for span %llu in file %016lx", D, B-D, span->span.start, enode->inode.i_ino);
        err = -EIO;
        goto out_err;
    }

    st = new_fetch_stripe(enode, span, stripe, stripe_seqno, prefetching);
    
    // start fetching blocks
    eggsfs_debug("fetching blocks");
    err = fetch_blocks(st);
    if (err) { goto out_err; }

    eggsfs_debug("fetch stripe started");
    return st;

out_err:
    fetch_stripe_trace(st, EGGSFS_FETCH_STRIPE_END, -1, err);
    if (st) { put_fetch_stripe(st); }
    return ERR_PTR(err);
}

static void do_prefetch(struct eggsfs_block_span* block_span, u64 off) {
    BUG_ON(!block_span->enode); // must still be alive at this point
    struct eggsfs_inode* enode = block_span->enode;

    // we first acquire the span for our purposes.
    if (off < block_span->span.start || off >= block_span->span.end) {
        // Another span, we need to load it. We give up if we can't at any occurrence.
        struct eggsfs_span* span = lookup_and_acquire_span(enode, off);
        // this is probably with err = -EBUSY, just give up
        if (unlikely(span == NULL || IS_ERR(span))) { return; }
        if (span->storage_class == EGGSFS_INLINE_STORAGE) {
            goto out_span; // nothing to do, it's an inline span
        }
        block_span = EGGSFS_BLOCK_SPAN(span);
    } else if (unlikely(!span_acquire(&block_span->span))) {
        return; // it's being reclaimed, don't bother
    }

    // At this point we've acquired the span. We now need to check if there's something to get

    int D = eggsfs_data_blocks(block_span->parity);
    u32 page_ix = (off - block_span->span.start)/PAGE_SIZE;
    u32 span_offset = page_ix * PAGE_SIZE;
    u32 stripe = span_offset / (block_span->cell_size*D);
    if (stripe > block_span->stripes) {
        eggsfs_warn("span_offset=%u, stripe=%u, stripes=%u", span_offset, stripe, block_span->stripes);
        goto out_span;
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
    struct fetch_stripe_state* st = start_fetch_stripe(block_span, stripe, seqno, true);
    if (IS_ERR(st)) {
        eggsfs_latch_release(&block_span->stripe_latches[stripe], seqno);
        eggsfs_debug("prefetch failed err=%ld", PTR_ERR(st));
        goto out_span;
    }

    // We want the state to go once all the block fetches are done.
    put_fetch_stripe(st);

out_span:
    // The fetch state has ownership itself
    eggsfs_put_span(&block_span->span, false);
    return;
}

// Returns whether we're low on memory
static bool reclaim_after_fetch_stripe(void) {
    // Reclaim pages if we went over the limit
    u64 pages = atomic64_read(&eggsfs_stat_cached_span_pages);
    u64 free_pages = si_mem_available();
    bool low_on_memory = false;
    if (pages*PAGE_SIZE > eggsfs_span_cache_max_size_sync || free_pages*PAGE_SIZE < eggsfs_span_cache_min_avail_mem_sync) {
        low_on_memory = true;
        // sync dropping, apply backpressure
        drop_spans("sync");
    } else if (pages*PAGE_SIZE > eggsfs_span_cache_max_size_async || free_pages*PAGE_SIZE < eggsfs_span_cache_min_avail_mem_async) {
        low_on_memory = true;
        // TODO Is it a good idea to do it on system_long_wq rather than eggsfs_wq? The freeing
        // job might face heavy contention, so maybe yes?
        // On the other end, this might make it harder to know when it's safe to unload
        // the module by making sure that there's no work left on the queue.
        queue_work(system_long_wq, &reclaim_spans_work);
    }
    return low_on_memory;
}

static void maybe_prefetch(
    struct eggsfs_block_span* span, bool low_on_memory, u64 stripe_start, u64 stripe_end
) {
    BUG_ON(!span->enode); // must still be alive here
    struct eggsfs_inode* enode = span->enode;

    eggsfs_debug("file=%016llx eggsfs_prefetch=%d low_on_memory=%d stripe_start=%llu stripe_end=%llu", span->span.ino, (int)eggsfs_prefetch, (int)low_on_memory, stripe_start, stripe_end);

    if (!eggsfs_prefetch || low_on_memory) { return; }

    struct eggsfs_inode_file* file = &enode->file;
    u64 prefetch_section = atomic64_read(&file->prefetch_section);
    u32 prefetch_section_begin = prefetch_section & (uint32_t)(-1);
    u32 prefetch_section_end = (prefetch_section>>32) & (uint32_t)(-1);
    if (stripe_start > prefetch_section_begin && stripe_end < prefetch_section_end) {
        eggsfs_debug("within bounds, not prefetching");
        // we're comfortably inside, nothing to do
    } if (stripe_end < prefetch_section_begin || stripe_start > prefetch_section_end) {
        eggsfs_debug("out of bounds, resetting prefetch");
        // we're totally out of bounds, reset
        prefetch_section_begin = stripe_start;
        prefetch_section_end = stripe_end;
    } else {
        u64 off;
        // there is some overlap, but we're pushing the boundary, prefetch the next or previous thing
        if (stripe_end > prefetch_section_end) { // going rightwards
            eggsfs_debug("prefetching forwards (stripe_end=%llu, prefetch_section_end=%u)", stripe_end, prefetch_section_end);
            prefetch_section_end = stripe_end;
            off = stripe_end;
        } else { // going leftwards
            eggsfs_debug("prefetching backwards (stripe_end=%llu, prefetch_section_end=%u)", stripe_end, prefetch_section_end);
            prefetch_section_begin = stripe_start;
            off = stripe_start-1;
        }
        u64 new_prefetch_section = prefetch_section_begin | ((u64)prefetch_section_end << 32);
        if (likely(atomic64_cmpxchg(&file->prefetch_section, prefetch_section, new_prefetch_section) == prefetch_section)) {
            do_prefetch(span, off);
        }
    }
}

struct page* eggsfs_get_span_page(struct eggsfs_block_span* block_span, u32 page_ix) {
    struct eggsfs_span* span = &block_span->span;

    trace_eggsfs_get_span_page_enter(span->ino, span->start, page_ix*PAGE_SIZE);

    // We need to load the stripe
    int D = eggsfs_data_blocks(block_span->parity);
    u32 span_offset = page_ix * PAGE_SIZE;
    u32 stripe = span_offset / (block_span->cell_size*D);
    bool low_on_memory = false;
    int err = 0;

#define GET_PAGE_EXIT(p) do { \
        int __err = IS_ERR(p) ? PTR_ERR(p) : 0; \
        if (likely(__err == 0)) { \
            maybe_prefetch(block_span, low_on_memory, span->start + stripe*(block_span->cell_size*D), span->start + (stripe+1)*(block_span->cell_size*D)); \
        } \
        trace_eggsfs_get_span_page_exit(span->ino, span->start, page_ix*PAGE_SIZE, __err); \
        return p; \
    } while(0) 

    // TODO better error?
    if (stripe > block_span->stripes) {
        eggsfs_warn("span_offset=%u, stripe=%u, stripes=%u", span_offset, stripe, block_span->stripes);
        GET_PAGE_EXIT(ERR_PTR(-EIO));
    }

    // this should be guaranteed by the caller but we rely on it below, so let's check
    if (block_span->cell_size%PAGE_SIZE != 0) {
        eggsfs_warn("cell_size=%u, PAGE_SIZE=%lu, block_span->cell_size%%PAGE_SIZE=%lu", block_span->cell_size, PAGE_SIZE, block_span->cell_size%PAGE_SIZE);
        GET_PAGE_EXIT(ERR_PTR(-EIO));
    }

    struct page* page;

again:
    page = xa_load(&block_span->pages, page_ix);
    if (page != NULL) { GET_PAGE_EXIT(page); }

    // There is no matching release here because the latch is released
    // when the fetchers finish (even if this call is interrupted)
    s64 seqno;
    if (!eggsfs_latch_try_acquire(&block_span->stripe_latches[stripe], seqno)) {
        eggsfs_latch_wait(&block_span->stripe_latches[stripe], seqno);
        goto again;
    }

    page = xa_load(&block_span->pages, page_ix);
    if (page != NULL) {
        // somebody got to it first
        eggsfs_latch_release(&block_span->stripe_latches[stripe], seqno);
        GET_PAGE_EXIT(page);
    }

    eggsfs_debug("about to start fetch stripe");
    struct fetch_stripe_state* st = NULL;
    st = start_fetch_stripe(block_span, stripe, seqno, false);
    if (IS_ERR(st)) {
        err = PTR_ERR(st);
        st = NULL;
        goto out_err;
    }
    eggsfs_debug("started fetch stripe, waiting");

    // Wait for all the block requests
    down(&st->sema);

    eggsfs_debug("done");

    err = atomic_read(&st->err);
    if (err) { goto out_err; }

    // We're finally done
    page = xa_load(&block_span->pages, page_ix);
    // eggsfs_info("loaded page %p for span %p at %u", page, span, page_ix);
    BUG_ON(page == NULL);

out:
    if (st != NULL) { put_fetch_stripe(st); }

    // Reclaim if needed
    low_on_memory = reclaim_after_fetch_stripe();

    GET_PAGE_EXIT(err == 0 ? page : ERR_PTR(err));

out_err:
    eggsfs_info("getting span page failed, err=%d", err);
    goto out;

#undef GET_PAGE_EXIT

}

int eggsfs_span_init(void) {
    int i;
    for (i = 0; i < EGGSFS_SPAN_LRUS; i++) {
        spin_lock_init(&span_lrus[i].lock);
        INIT_LIST_HEAD(&span_lrus[i].lru);
    }

    int err = register_shrinker(&eggsfs_span_shrinker);
    if (err) { return err; }

    eggsfs_block_span_cachep = kmem_cache_create(
        "eggsfs_block_span_cache",
        sizeof(struct eggsfs_block_span),
        0,
        SLAB_RECLAIM_ACCOUNT | SLAB_MEM_SPREAD,
        &init_block_span
    );
    if (!eggsfs_block_span_cachep) {
        err = -ENOMEM;
        goto out_shrinker;
    }

    eggsfs_inline_span_cachep = kmem_cache_create_usercopy(
        "eggsfs_inline_span_cache",
        sizeof(struct eggsfs_inline_span),
        0,
        SLAB_RECLAIM_ACCOUNT | SLAB_MEM_SPREAD,
        offsetof(struct eggsfs_inline_span, body),
        sizeof(((struct eggsfs_inline_span*)NULL)->body),
        NULL
    );
    if (!eggsfs_block_span_cachep) {
        err = -ENOMEM;
        goto out_block;
    }

    eggsfs_fetch_stripe_cachep = kmem_cache_create(
        "eggsfs_fetch_stripe_cache",
        sizeof(struct fetch_stripe_state),
        0,
        SLAB_RECLAIM_ACCOUNT | SLAB_MEM_SPREAD,
        &init_fetch_stripe
    );
    if (!eggsfs_fetch_stripe_cachep) {
        err = -ENOMEM;
        goto out_inline;
    }

    return 0;

out_inline:
    kmem_cache_destroy(eggsfs_fetch_stripe_cachep);
out_block:
    kmem_cache_destroy(eggsfs_block_span_cachep);
out_shrinker:
    unregister_shrinker(&eggsfs_span_shrinker);
    return err;
}

void eggsfs_span_exit(void) {
    eggsfs_debug("span exit");
    unregister_shrinker(&eggsfs_span_shrinker);
    kmem_cache_destroy(eggsfs_block_span_cachep);
    kmem_cache_destroy(eggsfs_inline_span_cachep);
    kmem_cache_destroy(eggsfs_fetch_stripe_cachep);
}
