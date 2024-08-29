#include <linux/module.h>
#include <linux/random.h>
#include <linux/err.h>
#include <linux/slab.h>

#include "bincode.h"
#include "block.h"
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
#include "block_services.h"

EGGSFS_DEFINE_COUNTER(eggsfs_stat_cached_stripes);
EGGSFS_DEFINE_COUNTER(eggsfs_stat_dropped_stripes_refetched);
EGGSFS_DEFINE_COUNTER(eggsfs_stat_stripe_cache_hit);
EGGSFS_DEFINE_COUNTER(eggsfs_stat_stripe_cache_miss);
EGGSFS_DEFINE_COUNTER(eggsfs_stat_stripe_page_cache_hit);
EGGSFS_DEFINE_COUNTER(eggsfs_stat_stripe_page_cache_miss);

// This currently also includes in-flight pages (i.e. pages that cannot be
// reclaimed), just because the code is a bit simpler this way.
atomic64_t eggsfs_stat_cached_stripe_pages = ATOMIC64_INIT(0);

// These numbers do not mean anything in particular. We do want to avoid
// flickering sync drops though, therefore we go down to 25GiB from
// 50GiB, and similarly for free memory.
unsigned long eggsfs_stripe_cache_max_size_async = (50ull << 30); // 50GiB
unsigned long eggsfs_stripe_cache_min_avail_mem_async = (2ull << 30); // 2GiB
unsigned long eggsfs_stripe_cache_max_size_drop = (45ull << 30); // 45GiB
unsigned long eggsfs_stripe_cache_min_avail_mem_drop = (2ull << 30) + (500ull << 20); // 2GiB + 500MiB
unsigned long eggsfs_stripe_cache_max_size_sync = (100ull << 30); // 100GiB
unsigned long eggsfs_stripe_cache_min_avail_mem_sync = (1ull << 30); // 1GiB

static struct kmem_cache* eggsfs_block_span_cachep;
static struct kmem_cache* eggsfs_inline_span_cachep;
static struct kmem_cache* eggsfs_stripe_cachep;
static struct kmem_cache* eggsfs_fetch_stripe_cachep;
static struct kmem_cache* eggsfs_fetch_span_pages_cachep;

//
// STRIPE PART
//
struct eggsfs_stripe_lru {
    spinlock_t lock;
    struct list_head lru;
} ____cacheline_aligned;

// These are global locks, but the sharding should alleviate contention.
#define EGGSFS_STRIPE_BITS 8
#define EGGSFS_STRIPE_LRUS (1<<EGGSFS_STRIPE_BITS) // 256
static struct eggsfs_stripe_lru stripe_lrus[EGGSFS_STRIPE_LRUS];

// We use only inode for hashing because all the stripes from the file need
// to be in the same LRU.
//  In this case having more
// stripes in the same LRU, allows us to return them into right places and have
// the older stripes be reclaimed before the more recently used ones.
// Evicting may be unfair for smaller files, but we consider it less important.
static struct eggsfs_stripe_lru* get_stripe_lru(struct eggsfs_span* span) {
    int h;
    h  = hash_64(span->ino, EGGSFS_STRIPE_BITS);
    return &stripe_lrus[h%EGGSFS_STRIPE_LRUS];
}

static struct eggsfs_stripe* read_and_acquire_stripe(struct eggsfs_block_span* span, u8 stripe_ix) {
    struct eggsfs_stripe_lru* lru = get_stripe_lru(&span->span);
    bool lru_del = false; // for debug output
    spin_lock(&lru->lock);
    struct eggsfs_stripe *stripe = (struct eggsfs_stripe*)atomic64_read_acquire((atomic64_t*)&span->stripes[stripe_ix]);
    if (stripe == NULL) { goto unlock; }

    if (unlikely(atomic_read(&stripe->refcount) < 0)) {
        // if the reclaimer locked first, we could only get stripe == NULL.
        BUG();
    }
    if (atomic_inc_return(&stripe->refcount) == 1) {
        // we're the first ones here, take it out of the LRU
        list_del(&stripe->lru);
        lru_del = true;
        // We only register a cache hit when the stripe is taken from lru.
        // All other cases would be concurrent access and such stripe would not
        // be considered to be dropped.
        eggsfs_counter_inc(eggsfs_stat_stripe_cache_hit);
    }
unlock:
    spin_unlock(&lru->lock);
    if (stripe) {
        eggsfs_debug("acquired stripe %d of inode %llu, lru_del:%d, addr %p", stripe->stripe_ix, stripe->span->span.ino, lru_del, stripe);
    }
    return stripe;
}

void eggsfs_put_stripe(struct eggsfs_stripe* stripe, bool was_read) {
    struct eggsfs_stripe_lru* lru = get_stripe_lru(&stripe->span->span);
    bool lru_add = false; // for debug output
    spin_lock(&lru->lock);
    BUG_ON(atomic_read(&stripe->refcount) < 1);
    stripe->touched = stripe->touched || was_read;
    if (atomic_dec_return(&stripe->refcount) == 0) { // we need to put it back into the LRU
        lru_add = true;
        if (stripe->touched) {
            list_add_tail(&stripe->lru, &lru->lru);
        } else {
            list_add(&stripe->lru, &lru->lru);
        }
    }
    spin_unlock(&lru->lock);
    eggsfs_debug("done for stripe %d of inode %llu, lru_add=%d, addr %p", stripe->stripe_ix, stripe->span->span.ino, lru_add, stripe);
}

struct eggsfs_stripe* eggsfs_get_stripe(struct eggsfs_block_span* span, u32 offset) {
    u32 stripe_size = eggsfs_stripe_size(span);
    u32 stripe_ix = offset / stripe_size;
    if (unlikely(stripe_ix >= span->num_stripes)) { return NULL; }

    struct eggsfs_stripe* stripe;
    u64 iterations = 0;
    eggsfs_debug("ino:%llu, stripe_size:%u, offset: %u, stripe_ix:%u", span->span.ino, stripe_size, offset, stripe_ix);
retry:
    iterations++;
    // This would indicate some serious contention where the stripe is being
    // added/reclaimed by many different tasks at the same time.
    if (unlikely(iterations == 10)) {
        eggsfs_warn("we've been fetching the same stripe for %llu iterations, we're probably stuck", iterations);
    }

    stripe = read_and_acquire_stripe(span, stripe_ix);
    if (stripe != NULL) { return stripe; }

    // Not found or was errored, first allocate it and bootstrap it
    {
        struct eggsfs_stripe* new_stripe = kmem_cache_alloc(eggsfs_stripe_cachep, GFP_KERNEL);
        if (new_stripe == NULL) {
            eggsfs_warn("failed allocating stripe: %ld", PTR_ERR(stripe));
            return ERR_PTR(-ENOMEM);
        }
        xa_init(&new_stripe->pages);
        eggsfs_latch_init(&new_stripe->latch);
        atomic_set(&new_stripe->refcount, 1); // the caller
        new_stripe->stripe_ix = stripe_ix;
        new_stripe->touched = false;
        new_stripe->span = span;
        eggsfs_debug("created stripe at index %u for %llu, addr %p", new_stripe->stripe_ix, span->span.ino, new_stripe);
        atomic64_inc((atomic64_t*)&span->span.refcount);
        // This is a single place where stripe is set without holding an LRU lock,
        // but it is not in any LRU at this point and the race can only happen with
        // this same code path, which is guarded by `atomic64_cmpxchg_release`.
        if ((struct eggsfs_stripe*)atomic64_cmpxchg_release((atomic64_t*)&span->stripes[stripe_ix], (s64)0, (s64)new_stripe) != NULL) {
            // Somebody got there first, free up and retry
            eggsfs_debug("failed adding stripe %p", new_stripe);
            kmem_cache_free(eggsfs_stripe_cachep, new_stripe);
            atomic64_dec((atomic64_t*)&span->span.refcount);
            goto retry;
        }
        if (span->stripe_was_cached[stripe_ix]) {
            eggsfs_counter_inc(eggsfs_stat_dropped_stripes_refetched);
        } else {
            // We know it will eventually come back to lru.
            span->stripe_was_cached[stripe_ix] = 1;
        }
        eggsfs_counter_inc(eggsfs_stat_cached_stripes);
        eggsfs_counter_inc(eggsfs_stat_stripe_cache_miss);
        return new_stripe;
    }
}

//
// STRIPE RECLAIM PART
//

// Implementation below with the rest of the span code.
static void put_span(struct eggsfs_span* span);

static void free_stripe(struct eggsfs_stripe* stripe, u64* dropped_pages) {
    u64 dropped = 0;
    eggsfs_counter_dec(eggsfs_stat_cached_stripes);
    struct page* page;
    unsigned long page_ix;
    xa_for_each(&stripe->pages, page_ix, page) {
        put_page(page);
        dropped++;
        BUG_ON(xa_erase(&stripe->pages, page_ix) == NULL);
    }
    BUG_ON(atomic64_read(&stripe->latch.counter) & 1); // somebody still hanging onto this

    // decresing the refcount may happen much later after setting the stripe
    // to NULL, but it is ok because if, in the mean time new stripe was added,
    // the refcount would be incremented and this will balance it.
    put_span(&stripe->span->span);
    eggsfs_debug("freeing stripe %p", stripe);
    kmem_cache_free(eggsfs_stripe_cachep, stripe);
    if (dropped_pages != NULL) {
        (*dropped_pages) += dropped;
    }
    atomic64_sub(dropped, &eggsfs_stat_cached_stripe_pages);
}

// Needs to be called with the lru lock that is holding the stripe.
static void stripe_validate_and_remove_from_lru(struct eggsfs_stripe *candidate, struct eggsfs_stripe_lru *lru) {
    BUG_ON(!spin_is_locked(&lru->lock));
    struct eggsfs_stripe *old_stripe = (struct eggsfs_stripe*)atomic64_cmpxchg_release((atomic64_t*)&candidate->span->stripes[candidate->stripe_ix], (s64)candidate, (s64)0);
    // If we could read the item from the lru, nothing should be able to replace it.
    BUG_ON(candidate != old_stripe);
    // stripe obtained while holding the LRU lock, it must be readers == 0 and goes to -1 after our dec
    if (atomic_dec_return(&candidate->refcount) == -1) {
        list_del(&candidate->lru);
    } else {
        eggsfs_warn("refcount %d for ino %llu span %llu:%llu stripe %d", atomic_read(&candidate->refcount), candidate->span->span.ino, candidate->span->span.start, candidate->span->span.end, candidate->stripe_ix);
        BUG();
    }
}

// If it returns -1, there are no stripes to drop. Otherwise, returns the
// next index to pass in to reclaim the next stripe.
static int drop_one_stripe(int lru_ix, u64* examined_stripes, u64* dropped_pages) {
    BUG_ON(lru_ix < 0 || lru_ix >= EGGSFS_STRIPE_LRUS);

    int i;
    for (i = lru_ix; i < lru_ix + EGGSFS_STRIPE_LRUS; i++) {
        struct eggsfs_stripe_lru* lru = &stripe_lrus[i%EGGSFS_STRIPE_LRUS];
        struct eggsfs_stripe* candidate = NULL;

        // Pick a candidate from the LRU, mark it as reclaiming, take it out of
        // the LRU.
        spin_lock(&lru->lock);
        candidate = list_first_entry_or_null(&lru->lru, struct eggsfs_stripe, lru);
        if (likely(candidate != NULL)) {
            (*examined_stripes)++;
            stripe_validate_and_remove_from_lru(candidate, lru);
        }
        spin_unlock(&lru->lock);
        if (candidate) {
            // At this point we're free to do what we please with the stripe
            // (it's fully private to us).
            free_stripe(candidate, dropped_pages);

            i++;
            break;
        }
    }

    // we've fully gone around without finding anything
    if (i == lru_ix + EGGSFS_STRIPE_LRUS) { return -1; }

    // We've found something
    return i%EGGSFS_STRIPE_LRUS;
}

static inline void drop_stripes_start(const char* type) {
    trace_eggsfs_drop_stripes(
        type, PAGE_SIZE*si_mem_available(), atomic64_read(&eggsfs_stat_cached_stripe_pages), eggsfs_counter_get(&eggsfs_stat_cached_stripes), EGGSFS_DROP_STRIPES_START, 0, 0, 0
    );
}

static inline void drop_stripes_end(const char* type, u64 examined_stripes, u64 dropped_pages, int err) {
    trace_eggsfs_drop_stripes(
        type, PAGE_SIZE*si_mem_available(), atomic64_read(&eggsfs_stat_cached_stripe_pages), eggsfs_counter_get(&eggsfs_stat_cached_stripes), EGGSFS_DROP_STRIPES_END, examined_stripes, dropped_pages, err
    );
}

// returns the number of dropped pages
u64 eggsfs_drop_all_stripes(void) {
    drop_stripes_start("all");
    u64 dropped_pages = 0;
    u64 dropped_pages_last = 0;
    u64 examined_stripes = 0;
    s64 stripes_begin = eggsfs_counter_get(&eggsfs_stat_cached_stripes);
    int lru_ix = 0;
    int last_ix = 0;
    int total_iterations = 0;
    for (;;) {
        dropped_pages_last = 0;
        last_ix = drop_one_stripe(lru_ix, &examined_stripes, &dropped_pages_last);
        if (last_ix < 0) {
            // We have iterated through all lrus without freeing anything
            if (dropped_pages_last == 0) { break; }
            // We will continue again in the same lru.
        } else {
            lru_ix = last_ix;
        }
        dropped_pages += dropped_pages_last;
        total_iterations++;
    }
    s64 stripes_end = eggsfs_counter_get(&eggsfs_stat_cached_stripes);
    eggsfs_info("reclaimed %llu pages, %lld stripes (approx), iterations:%d", dropped_pages, stripes_begin-stripes_end, total_iterations);
    drop_stripes_end("all", examined_stripes, dropped_pages, 0);
    return dropped_pages;
}

static DEFINE_MUTEX(drop_stripes_mu);

// Return the number of dropped pages.
static s64 drop_stripes(const char* type) {
    drop_stripes_start(type);

    mutex_lock(&drop_stripes_mu);

    u64 dropped_pages = 0;
    u64 examined_stripes = 0;
    int lru_ix = 0;
    for (;;) {
        u64 pages = atomic64_read(&eggsfs_stat_cached_stripe_pages);
        if (
            pages*PAGE_SIZE < eggsfs_stripe_cache_max_size_drop &&
            si_mem_available()*PAGE_SIZE > eggsfs_stripe_cache_min_avail_mem_drop
        ) {
            break;
        }
        lru_ix = drop_one_stripe(lru_ix, &examined_stripes, &dropped_pages);
        if (lru_ix < 0) { break; }
    }
    eggsfs_debug("dropped %llu pages, %llu stripes examined", dropped_pages, examined_stripes);

    mutex_unlock(&drop_stripes_mu);
    drop_stripes_end(type, examined_stripes, dropped_pages, 0);

    return dropped_pages;
}

static void reclaim_stripes_async(struct work_struct* work) {
    if (!mutex_is_locked(&drop_stripes_mu)) { // somebody's already taking care of it
        drop_stripes("async");
    }
}

static DECLARE_WORK(reclaim_stripes_work, reclaim_stripes_async);

static unsigned long eggsfs_stripe_shrinker_count(struct shrinker* shrinker, struct shrink_control* sc) {
    u64 pages = atomic64_read(&eggsfs_stat_cached_stripe_pages);
    if (pages == 0) { return SHRINK_EMPTY; }
    return pages;
}

static int eggsfs_stripe_shrinker_lru_ix = 0;

static unsigned long eggsfs_stripe_shrinker_scan(struct shrinker* shrinker, struct shrink_control* sc) {
    drop_stripes_start("shrinker");

    u64 dropped_pages = 0;
    u64 examined_stripes = 0;
    int lru_ix = eggsfs_stripe_shrinker_lru_ix;
    for (dropped_pages = 0; dropped_pages < sc->nr_to_scan;) {
        lru_ix = drop_one_stripe(lru_ix, &examined_stripes, &dropped_pages);
        if (lru_ix < 0) { break; }
    }
    eggsfs_stripe_shrinker_lru_ix = lru_ix >= 0 ? lru_ix : 0;
    drop_stripes_end("shrinker", examined_stripes, dropped_pages, 0);

    return dropped_pages;
}

static struct shrinker eggsfs_stripe_shrinker = {
    .count_objects = eggsfs_stripe_shrinker_count,
    .scan_objects = eggsfs_stripe_shrinker_scan,
    .seeks = DEFAULT_SEEKS,
    .flags = SHRINKER_NONSLAB, // what's this?
};

// Returns whether we're low on memory
static bool reclaim_after_fetch_stripe(void) {
    // Reclaim pages if we went over the limit
    u64 pages = atomic64_read(&eggsfs_stat_cached_stripe_pages);
    u64 free_pages = si_mem_available();
    bool low_on_memory = false;
    if (pages*PAGE_SIZE > eggsfs_stripe_cache_max_size_sync || free_pages*PAGE_SIZE < eggsfs_stripe_cache_min_avail_mem_sync) {
        low_on_memory = true;
        // sync dropping, apply backpressure
        drop_stripes("sync");
    } else if (pages*PAGE_SIZE > eggsfs_stripe_cache_max_size_async || free_pages*PAGE_SIZE < eggsfs_stripe_cache_min_avail_mem_async) {
        low_on_memory = true;
        // TODO Is it a good idea to do it on system_long_wq rather than eggsfs_wq? The freeing
        // job might face heavy contention, so maybe yes?
        // On the other end, this might make it harder to know when it's safe to unload
        // the module by making sure that there's no work left on the queue.
        queue_work(system_long_wq, &reclaim_stripes_work);
    }
    return low_on_memory;
}

//
// FETCH STRIPE PART
//

struct fetch_stripe_state {
    struct eggsfs_stripe* stripe;
    // Used to wait on every block being finished. Could be done faster with a wait_queue, but don't want to
    // worry about smb subtleties.
    struct semaphore sema;
    // Used to release the stripe when we're done with it
    s64 stripe_seqno;
    // These will be filled in by the fetching (only D of them well be
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
    struct eggsfs_stripe* stripe,
    u64 stripe_seqno,
    bool prefetching
) {
    struct fetch_stripe_state* st = (struct fetch_stripe_state*)kmem_cache_alloc(eggsfs_fetch_stripe_cachep, GFP_KERNEL);
    if (!st) { return st; }

    st->stripe = stripe;
    atomic_set(&st->refcount, 1); // the caller
    atomic_set(&st->err, 0);
    atomic_set(&st->last_block_err, 0);
    st->stripe = stripe;
    st->prefetching = prefetching;
    atomic64_set(&st->blocks, 0);
    st->stripe_seqno = stripe_seqno;

    return st;
}

static void fetch_stripe_trace(struct fetch_stripe_state* st, u8 event, s8 block, int err) {
    trace_eggsfs_fetch_stripe(
        st->stripe->span->span.ino,
        st->stripe->span->span.start,
        st->stripe->stripe_ix,
        st->stripe->span->parity,
        st->prefetching,
        event,
        block,
        err
    );
}

static void free_fetch_stripe(struct fetch_stripe_state* st) {
    // we're the last ones here
    fetch_stripe_trace(st, EGGSFS_FETCH_STRIPE_FREE, -1, atomic_read(&st->err));

    // managing the stripe latch happens in eggsfs_get_stripe_page()

    while (down_trylock(&st->sema) == 0) {} // reset sema to zero for next usage

    // Free leftover blocks (also ensures the lists are all nice and empty for the
    // next user)
    int i;
    for (i = 0; i < EGGSFS_MAX_BLOCKS; i++) {
        put_pages_list(&st->blocks_pages[i]);
    }
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
    struct eggsfs_block_span* span = st->stripe->span;
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
    if (crc != span->stripes_crc[st->stripe->stripe_ix]) {
        eggsfs_warn("bad crc for file %016llx, stripe %u, expected %08x, got %08x, expected %u pages, got %u pages", span->span.ino, st->stripe->stripe_ix, span->stripes_crc[st->stripe->stripe_ix], crc, pages_per_block*D, num_pages);
        atomic_set(&st->err, -EIO);
        return;
    }

    //u32 curr_page = pages_per_block*D*st->stripe->stripe_ix; <-- seems wrong and meant for span
    // We start from 0 on every stripe. We could also switch to the above and track all pages with indices relative to the whole span (would need to change readers)
    u32 curr_page = 0;

    // Now move pages to the span. Note that we can't add them
    // as we compute the CRC because `xa_store` might allocate.
    for (i = 0; i < D; i++) {
        struct page* page;
        struct page* tmp;
        list_for_each_entry_safe(page, tmp, &st->blocks_pages[i], lru) {
            // We don't want the page to be managed by the page cache
            list_del(&page->lru);
            struct page* old_page = xa_store(&st->stripe->pages, curr_page, page, GFP_KERNEL);
            if (IS_ERR(old_page)) {
                atomic_set(&st->err, PTR_ERR(old_page));
                eggsfs_info("xa_store failed: %ld, error stored", PTR_ERR(old_page));
                // We need to remove all the pages stored so far, and
                // this errored one.
                put_page(page);
                u32 page_ix;
                for (page_ix = 0; page_ix < curr_page; page_ix++) {
                    page = xa_erase(&st->stripe->pages, page_ix);
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
    atomic64_add(pages_per_block*D, &eggsfs_stat_cached_stripe_pages);
    if (crc != span->stripes_crc[st->stripe->stripe_ix]) {
        eggsfs_warn("bad crc for file %016llx, stripe %u, expected %08x, got %08x", span->span.ino, st->stripe->stripe_ix, span->stripes_crc[st->stripe->stripe_ix], crc);
        atomic_set(&st->err, -EIO);
    }
}

static void block_done(void* data, u64 block_id, struct list_head* pages, int err);

// Starts loading up D blocks as necessary.
static int fetch_blocks(struct fetch_stripe_state* st) {
    int i;

    struct eggsfs_block_span* span = st->stripe->span;
    int D = eggsfs_data_blocks(span->parity);
    int P = eggsfs_parity_blocks(span->parity);
    int B = eggsfs_blocks(span->parity);

#define LOG_STR "file=%016llx span=%llu stripe=%u D=%d P=%d downloading=%04x(%llu) succeeded=%04x(%llu) failed=%04x(%llu) last_block_err=%d"
#define LOG_ARGS span->span.ino, span->span.start, st->stripe->stripe_ix, D, P, downloading, __builtin_popcountll(downloading), succeeded, __builtin_popcountll(succeeded), failed, __builtin_popcountll(failed), atomic_read(&st->last_block_err)

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
        struct eggsfs_block* block = &span->blocks[i];

        //      <stripe_ix * cell_size> lands at correct cell in a block
        //       +------ stripe is cells at certain index across all data blocks.
        //+----+-v--+----+----+----+
        //|cell|cell|cell|....|cell| <--- Block
        //+----+----+----+----+----+
        u32 block_offset = st->stripe->stripe_ix * span->cell_size;

        fetch_stripe_trace(st, EGGSFS_FETCH_STRIPE_BLOCK_START, i, 0);
        hold_fetch_stripe(st);
        eggsfs_debug("block start st=%p block_service=%016llx block_id=%016llx", st, block->id, block->id);
        struct eggsfs_block_service bs;
        eggsfs_get_block_service(block->bs, &bs);
        // Fetches a single cell from the block
        int block_err = eggsfs_fetch_block_pages(
            &block_done,
            (void*)st,
            &bs, block->id, block_offset, span->cell_size
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
    struct eggsfs_block_span* span = st->stripe->span;

    int D = eggsfs_data_blocks(span->parity);
    int P = eggsfs_parity_blocks(span->parity);
    int B = eggsfs_blocks(span->parity);
    u32 pages_per_block = span->cell_size/PAGE_SIZE;

    for (i = 0; i < B; i++) {
        if (st->stripe->span->blocks[i].id == block_id) { break; }
    }
    // eggsfs_info("block ix %d, B=%d", i, B);
    BUG_ON(i == B);

    eggsfs_debug(
        "st=%p block=%d block_id=%016llx file=%016llx span=%llu stripe=%u D=%d P=%d err=%d",
        st, i, block_id, span->span.ino, span->span.start, st->stripe->stripe_ix, D, P, err
    );

    fetch_stripe_trace(st, EGGSFS_FETCH_STRIPE_BLOCK_DONE, i, err);

    bool finished = false;
    if (err) {
        s64 blocks = atomic64_read(&st->blocks);
        eggsfs_info("fetching block %016llx (index:%d, D=%d, P=%d, blocks:%lld) in file %016llx failed: %d", block_id, i, D, P, blocks, span->span.ino, err);
        atomic_set(&st->last_block_err, err);
        // it failed, try to restore order
        eggsfs_debug("block %016llx ix %d failed with %d", block_id, i, err);
        // mark it as failed and not downloading
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
        fetch_stripe_trace(st, EGGSFS_FETCH_STRIPE_END, -1, err);
        // Wake up waiters. The trace above needs to happen before because it
        // references the stripe. If the caller is allowed to continue,
        // the stripe may get reclaimed and it results in null pointer deref.
        up(&st->sema);
    }

    // drop our reference
    put_fetch_stripe(st);
}

static struct fetch_stripe_state* start_fetch_stripe(
    struct eggsfs_stripe* stripe, s64 stripe_seqno, bool prefetching
) {
    int err;

    trace_eggsfs_fetch_stripe(
        stripe->span->span.ino,
        stripe->span->span.start,
        stripe->stripe_ix,
        stripe->span->parity,
        prefetching,
        EGGSFS_FETCH_STRIPE_START,
        -1,
        0
    );

    struct fetch_stripe_state* st = NULL;

    int D = eggsfs_data_blocks(stripe->span->parity);
    int P = eggsfs_parity_blocks(stripe->span->parity);
    if (D > EGGSFS_MAX_DATA || P > EGGSFS_MAX_PARITY) {
        eggsfs_error("got out of bounds parity of RS(%d,%d) for span %llu in file %016llx", D, P, stripe->span->span.start, stripe->span->span.ino);
        err = -EIO;
        goto out_err;
    }

    st = new_fetch_stripe(stripe, stripe_seqno, prefetching);

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

struct page* eggsfs_get_stripe_page(struct eggsfs_stripe* stripe, u32 page_ix) {
    u64 seqno;
    struct page* page;
    bool low_on_memory = false;
    int err = 0;
    int D = eggsfs_data_blocks(stripe->span->parity);

    eggsfs_debug("reading page from stripe %u at index %u of %u", stripe->stripe_ix, page_ix, D);
    trace_eggsfs_get_stripe_page_enter(stripe->span->span.ino, stripe->span->span.start, stripe->stripe_ix*D*page_ix*PAGE_SIZE);

#define GET_PAGE_EXIT(p) do { \
        int __err = IS_ERR(p) ? PTR_ERR(p) : 0; \
        trace_eggsfs_get_stripe_page_exit(stripe->span->span.ino, stripe->span->span.start, stripe->stripe_ix*D*page_ix*PAGE_SIZE, __err); \
        return p; \
    } while(0)

retry:
    page = xa_load(&stripe->pages, page_ix);
    if (page != NULL) {
        eggsfs_counter_inc(eggsfs_stat_stripe_page_cache_hit);
        GET_PAGE_EXIT(page);
    }

    if (!eggsfs_latch_try_acquire(&stripe->latch, seqno)) {
        eggsfs_latch_wait(&stripe->latch, seqno);
        goto retry;
    }

    // We're the first ones here
    page = xa_load(&stripe->pages, page_ix);
    if (page != NULL) {
        // somebody got to it first
        eggsfs_latch_release(&stripe->latch, seqno);
        GET_PAGE_EXIT(page);
    }

    eggsfs_debug("about to start fetch stripe");
    struct fetch_stripe_state* st = NULL;
    st = start_fetch_stripe(stripe, seqno, false);
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
    page = xa_load(&stripe->pages, page_ix);
    eggsfs_debug("loaded page %p for stripe %u at %u", page, stripe->stripe_ix, page_ix);
    BUG_ON(page == NULL);
    // Only track the miss when we go and fetch the stripe.
    eggsfs_counter_inc(eggsfs_stat_stripe_page_cache_miss);

out:
    eggsfs_latch_release(&stripe->latch, seqno);
    if (st != NULL) { put_fetch_stripe(st); }

    // Reclaim if needed
    low_on_memory = reclaim_after_fetch_stripe();

    GET_PAGE_EXIT(err == 0 ? page : ERR_PTR(err));

out_err:
    eggsfs_info("getting stripe page failed, err=%d", err);
    goto out;

#undef GET_PAGE_EXIT
}

//
// New page fetch
//

struct fetch_span_pages_state {
     struct eggsfs_block_span* block_span;
    // Used to wait on every block being finished. Could be done faster with a wait_queue, but don't want to
    // worry about smb subtleties.
    struct semaphore sema;
    struct address_space *mapping;
    u32 start_offset;
    u32 size;
    // will be set to 0 and D respectively when initiating fetch for RS recovery.
    atomic_t start_block;
    atomic_t end_block;
    // These will be filled in by the fetching (only D of them well be
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
};

static void init_fetch_span_pages(void* p) {
    struct fetch_span_pages_state* st = (struct fetch_span_pages_state*)p;

    sema_init(&st->sema, 0);
    int i;
    for (i = 0; i < EGGSFS_MAX_BLOCKS; i++) {
        INIT_LIST_HEAD(&st->blocks_pages[i]);
    }
}

struct fetch_span_pages_state *new_fetch_span_pages_state(struct eggsfs_block_span *block_span) {
    struct fetch_span_pages_state* st = (struct fetch_span_pages_state*)kmem_cache_alloc(eggsfs_fetch_span_pages_cachep, GFP_KERNEL);
    if (!st) { return st; }

    st->block_span = block_span;
    atomic_set(&st->refcount, 1); // the caller
    atomic_set(&st->err, 0);
    atomic_set(&st->last_block_err, 0);
    atomic64_set(&st->blocks, 0);
    atomic_set(&st->start_block, 0);
    atomic_set(&st->end_block, 0);
    st->start_offset = 0;
    st->size = 0;

    return st;
}

inline u8 fetch_span_pages_state_stripe_ix(struct fetch_span_pages_state* st) {
    return st->start_offset/st->block_span->cell_size;
}

inline int fetch_span_pages_state_need_blocks(struct fetch_span_pages_state* st) {
    int D = eggsfs_data_blocks(st->block_span->parity);
    return min(atomic_read(&st->end_block) - atomic_read(&st->start_block), D);
}

inline void fetch_span_pages_move_list(struct fetch_span_pages_state* st, int src, int dst) {
    BUG_ON(list_empty(&st->blocks_pages[src]));

    if (src == dst) return;

    BUG_ON(!list_empty(&st->blocks_pages[dst]));
    struct page* page;
    struct page* tmp;
    int num_pages = 0;
    list_for_each_entry_safe(page, tmp, &st->blocks_pages[src], lru) {
        list_del(&page->lru);
        list_add_tail(&page->lru, &st->blocks_pages[dst]);
        num_pages++;
    }
    BUG_ON(num_pages != st->size/PAGE_SIZE);
}

inline void fetch_span_pages_state_request_full_fetch(struct fetch_span_pages_state* st, int failed_block) {
        int D = eggsfs_data_blocks(st->block_span->parity);
        // Keep the original pages with block 0 when D == 1.
        // They were transferred to the selected block for fetching and now
        // they need to be handed back to be passed to the next selected block.
        if (D == 1) fetch_span_pages_move_list(st, failed_block, 0);

        int B = eggsfs_blocks(st->block_span->parity);
        atomic_set(&st->start_block, 0);
        atomic_set(&st->end_block, B);
}

static void free_fetch_span_pages(struct fetch_span_pages_state* st) {
    // we're the last ones here
    //fetch_stripe_trace(st, EGGSFS_FETCH_STRIPE_FREE, -1, atomic_read(&st->err));

    while (down_trylock(&st->sema) == 0) {} // reset sema to zero for next usage

    // Free leftover blocks (also ensures the lists are all nice and empty for the
    // next user)
    int i;
    for (i = 0; i < EGGSFS_MAX_BLOCKS; i++) {
        put_pages_list(&st->blocks_pages[i]);
    }
    kmem_cache_free(eggsfs_fetch_span_pages_cachep, st);
}

static void put_fetch_span_pages(struct fetch_span_pages_state* st) {
    int remaining = atomic_dec_return(&st->refcount);
    eggsfs_debug("st=%p remaining=%d", st, remaining);
    BUG_ON(remaining < 0);
    if (remaining > 0) { return; }
    free_fetch_span_pages(st);
}

static void hold_fetch_span_pages(struct fetch_span_pages_state* st) {
    int holding = atomic_inc_return(&st->refcount);
    eggsfs_debug("st=%p holding=%d", st, holding)
    WARN_ON(holding < 2);
}

static void store_block_pages(struct fetch_span_pages_state* st) {
    struct eggsfs_block_span* span = st->block_span;
    int D = eggsfs_data_blocks(span->parity);
    int B = eggsfs_blocks(span->parity);
    // During failure we kick off fetches for all D+P blocks,
    // but we return when we get at least D.
    int want_blocks = min(D, atomic_read(&st->end_block) - atomic_read(&st->start_block));
    int i;

    u32 pages_per_block = st->size/PAGE_SIZE;
    u16 blocks = SUCCEEDED_GET(atomic64_read(&st->blocks));
    u16 failed_blocks = FAILED_GET(atomic64_read(&st->blocks));
    u32 start = atomic_read(&st->start_block);
    u32 end = atomic_read(&st->end_block);

    if (failed_blocks > 0) {
        start = 0;
        end = B;
    }

    if (__builtin_popcountll(blocks) != want_blocks) {
        eggsfs_warn("have %lld blocks, want %d, succeeded: %d, failed:%d, start:%d end:%d", __builtin_popcountll(blocks), want_blocks, blocks, failed_blocks, atomic_read(&st->start_block), atomic_read(&st->end_block));
        BUG();
    }
    if (unlikely(failed_blocks > 0) && D != 1) { // we need to recover data using RS
        for (i = 0; i < D; i++) { // fill in missing data blocks
            if ((1ull<<i) & blocks) { continue; } // we have this one already
            if (list_empty(&st->blocks_pages[i])) {
                eggsfs_warn("list empty for recovery, block %d, start %d, end %d, blocks %d, failed %d", i, start, end, blocks, failed_blocks);
                BUG();
            }

            // compute
            eggsfs_info("recovering block %d, stripe %d, D=%d, want_blocks=%d, blocks=%d, failed_blocks=%d", i, fetch_span_pages_state_stripe_ix(st), D, want_blocks, blocks, failed_blocks);
            int err = eggsfs_recover(span->parity, blocks, 1ull<<i, pages_per_block, st->blocks_pages);
            if (err) {
                eggsfs_warn("recovery for block %d, stripe %d failed. err=%d", i, fetch_span_pages_state_stripe_ix(st), err);
                atomic_set(&st->err, err);
            }
        }
    }
    if (D == 1) {
        for (i = 0; i < B; i++) {
            if ((1ull<<i) & blocks) break;
        }
        if (failed_blocks) eggsfs_info("recovering block %d, stripe %d, D=%d, want_blocks=%d, blocks=%d, failed_blocks=%d", i, fetch_span_pages_state_stripe_ix(st), D, want_blocks, blocks, failed_blocks);
        // Copy pages over from any non 0 block regardless if it was selected
        // to spread the load or because any other block failed.
        fetch_span_pages_move_list(st, i, 0);
    }

    for(i = D; i < B; i++) {
        put_pages_list(&st->blocks_pages[i]);
    }
}

static void span_block_done(void* data, u64 block_id, struct list_head* pages, int err);

static int try_fill_from_page_cache(struct address_space *mapping, struct list_head* pages, unsigned long start_page_ix, unsigned int nr_pages) {
    int have_pages = 0;
    struct page* page;
    struct page* cached_page;
    list_for_each_entry(page, pages, lru) {
        cached_page = find_get_page(mapping, page->index);
        if (cached_page != NULL) {
            char* to_ptr = kmap_atomic(page);
            char* from_ptr = kmap_atomic(cached_page);
            memcpy(to_ptr, from_ptr, PAGE_SIZE);
            kunmap_atomic(to_ptr);
            kunmap_atomic(from_ptr);

            // page cache page is returned with an increased refcount
            put_page(cached_page);
        } else {
            break;
        }
        have_pages++;
    }
    if (have_pages == nr_pages) { return 1; }
    return 0;
}

// Starts loading up D blocks as necessary.
static int fetch_span_blocks(struct fetch_span_pages_state* st) {
    int i;

    struct eggsfs_block_span* span = st->block_span;
    int D = eggsfs_data_blocks(span->parity);
    int P = eggsfs_parity_blocks(span->parity);
    int B = eggsfs_blocks(span->parity);

#define LOG_STR "file=%016llx span=%llu start=%d size=%d D=%d P=%d downloading=%04x(%llu) succeeded=%04x(%llu) failed=%04x(%llu) last_block_err=%d"
#define LOG_ARGS span->span.ino, span->span.start, st->start_offset, st->size, D, P, downloading, __builtin_popcountll(downloading), succeeded, __builtin_popcountll(succeeded), failed, __builtin_popcountll(failed), atomic_read(&st->last_block_err)

    for (;;) {
        s64 blocks = atomic64_read(&st->blocks);
        u16 downloading = DOWNLOADING_GET(blocks);
        u16 succeeded = SUCCEEDED_GET(blocks);
        u16 failed = FAILED_GET(blocks);
        if (__builtin_popcountll(downloading)+__builtin_popcountll(succeeded) >= fetch_span_pages_state_need_blocks(st)) { // we managed to get out of the woods
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
            for (i = atomic_read(&st->start_block); i < atomic_read(&st->end_block); i++) {
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
        struct eggsfs_block* block = &span->blocks[i];
        loff_t curr_off = span->span.start + fetch_span_pages_state_stripe_ix(st)*eggsfs_stripe_size(span) + i*span->cell_size + st->start_offset%span->cell_size;
        u16 failed_blocks = FAILED_GET(atomic64_read(&st->blocks));

        // We start with pages at index 0 when D=1.
        // If another block is selected above to spread the load, we need to
        // transfer pages to that block.
        if (D == 1) fetch_span_pages_move_list(st, 0, i);

        if(list_empty(&st->blocks_pages[i]) && failed_blocks == 0) {
            eggsfs_warn("empty list at file=%016llx, block %d, stripe %d, offset=%d, size=%d, span_start=%lld, span_end=%lld. blocks=%lld, failed:%d", span->span.ino, i, fetch_span_pages_state_stripe_ix(st), st->start_offset, st->size, span->span.start, span->span.end, blocks, failed_blocks);
        }
        BUG_ON(list_empty(&st->blocks_pages[i]) && failed_blocks == 0);

        // we are likely kicking additional blocks due to failed block downloads
        if (list_empty(&st->blocks_pages[i])) {
            int j;
            for (j = 0; j < st->size/PAGE_SIZE; j++) {
                u32 page_ix = curr_off/PAGE_SIZE;
                struct page* page = __page_cache_alloc(readahead_gfp_mask(st->mapping));
                if (!page) {
                    eggsfs_warn("couldn't alocate page at index %d", page_ix);
                    atomic_set(&st->last_block_err, -ENOMEM);
                    put_pages_list(&st->blocks_pages[i]);
                    return -ENOMEM;
                }
                if(curr_off < span->span.end) {
                    page->index = page_ix;
                    curr_off += PAGE_SIZE;
                } else {
                    // These pages will be dropped after RS recovery.
                    page->index = (unsigned long)-1;
                }
                page->private = 0;
                list_add_tail(&page->lru, &st->blocks_pages[i]);
                BUG_ON(atomic_read(&page->_refcount) < 1);
            }
            if (i < D && curr_off < span->span.end) {
                int have_pages = try_fill_from_page_cache(st->mapping, &st->blocks_pages[i], curr_off/PAGE_SIZE, st->size/PAGE_SIZE);
                if (have_pages == st->size/PAGE_SIZE && curr_off + st->size <= st->block_span->span.end) {
                    eggsfs_info("found all pages in cache block %d, stripe %d", i, fetch_span_pages_state_stripe_ix(st));
                    while (unlikely(!atomic64_try_cmpxchg(&st->blocks, &blocks, SUCCEEDED_SET(DOWNLOADING_UNSET(blocks, i), i)))) {}
                    continue;
                }
            }
        }

        //fetch_stripe_trace(st, EGGSFS_FETCH_STRIPE_BLOCK_START, i, 0);
        hold_fetch_span_pages(st);
        eggsfs_debug("block start st=%p block_service=%016llx block_id=%016llx", st, block->id, block->id);
        struct eggsfs_block_service bs;
        eggsfs_get_block_service(block->bs, &bs);
        // Fetches a single cell from the block
        int block_err = eggsfs_fetch_block_pages_with_crc(
            &span_block_done,
            (void*)st,
            &bs, &st->blocks_pages[i], span->span.ino, block->id, block->crc, st->start_offset, st->size
        );
        if (block_err) {
            BUG_ON(list_empty(&st->blocks_pages[i]));
            atomic_set(&st->last_block_err, block_err);
            put_fetch_span_pages(st);
            eggsfs_info("loading block %d failed " LOG_STR, i, LOG_ARGS);

            // Downloading some block failed, all blocks + parity will be needed for RS recovery.
            fetch_span_pages_state_request_full_fetch(st, i);

            //fetch_stripe_trace(st, EGGSFS_FETCH_STRIPE_BLOCK_DONE, i, block_err);
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

static void span_block_done(void* data, u64 block_id, struct list_head* pages, int err) {
    int i;

    struct fetch_span_pages_state* st = (struct fetch_span_pages_state*)data;
    struct eggsfs_block_span* span = st->block_span;

    int D = eggsfs_data_blocks(span->parity);
    int P = eggsfs_parity_blocks(span->parity);
    int B = eggsfs_blocks(span->parity);
    u32 pages_per_block = st->size/PAGE_SIZE;

    for (i = 0; i < B; i++) {
        if (span->blocks[i].id == block_id) { break; }
    }

    BUG_ON(D != 1 && i == atomic_read(&st->end_block));

    eggsfs_debug(
        "st=%p block=%d block_id=%016llx file=%016llx span=%llu offset=%d size=%d D=%d P=%d err=%d",
        st, i, block_id, span->span.ino, span->span.start, st->start_offset, st->size, D, P, err
    );

    //fetch_stripe_trace(st, EGGSFS_FETCH_STRIPE_BLOCK_DONE, i, err);

    bool finished = false;
    // we reuse the supplied pages, move them back to reuse for fetching and
    // RS recovery if needed.
    BUG_ON(!list_empty(&st->blocks_pages[i]));
    BUG_ON(list_empty(pages));
    struct page* page;
    struct page* tmp;
    u32 fetched_pages = 0;
    list_for_each_entry_safe(page, tmp, pages, lru) {
        list_del(&page->lru);
        list_add_tail(&page->lru, &st->blocks_pages[i]);
        fetched_pages++;
    }
    BUG_ON(fetched_pages != pages_per_block);

retry:
    if (err) {
        s64 blocks = atomic64_read(&st->blocks);
        eggsfs_info("fetching block %016llx (index:%d, D=%d, P=%d, blocks:%lld) in file %016llx failed: %d", block_id, i, D, P, blocks, span->span.ino, err);
        atomic_set(&st->last_block_err, err);
        // it failed, try to restore order
        // mark it as failed and not downloading
        while (unlikely(!atomic64_try_cmpxchg(&st->blocks, &blocks, FAILED_SET(DOWNLOADING_UNSET(blocks, i), i)))) {}

        // Kick off downloading all blocks + parity for RS recovery.
        fetch_span_pages_state_request_full_fetch(st, i);
        err = fetch_span_blocks(st);
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
        // mark as fetched, see if we're the last ones
        s64 blocks = atomic64_read(&st->blocks);

        u32 block_offset = 0;
        list_for_each_entry(page, &st->blocks_pages[i], lru) {
            char* page_buf = kmap_atomic(page);
            kernel_fpu_begin();
            u32 crc = eggsfs_crc32c(0, page_buf, PAGE_SIZE);
            kernel_fpu_end();
            kunmap_atomic(page_buf);
            if (crc != (u32)page->private) {
                err = EGGSFS_ERR_BAD_BLOCK_CRC;
                eggsfs_warn("incorrect crc %d (expected %d) for %lld, block %d, offset %d, will try different block", crc, (u32)page->private, span->span.ino, i, block_offset);
                // mark this one as failed and kick off parity blocks for recovery.
                goto retry;
            }
            block_offset += PAGE_SIZE;
        }
        while (unlikely(!atomic64_try_cmpxchg(&st->blocks, &blocks, SUCCEEDED_SET(DOWNLOADING_UNSET(blocks, i), i)))) {}
        finished = __builtin_popcountll(SUCCEEDED_GET(SUCCEEDED_SET(blocks, i))) == fetch_span_pages_state_need_blocks(st); // blocks = last old one, we need to reapply
    }

    if (finished) {
        eggsfs_debug("finished");
        // we're the last one to finish, we need to store the pages in the span,
        // if we have no errors
        err = atomic_read(&st->err);
        if (err == 0) { store_block_pages(st); }
        //fetch_stripe_trace(st, EGGSFS_FETCH_STRIPE_END, -1, err);
        // Wake up waiters. The trace above needs to happen before because it
        // references the stripe. If the caller is allowed to continue,
        // the stripe may get reclaimed and it results in null pointer deref.
        up(&st->sema);
    }

    // drop our reference
    put_fetch_span_pages(st);
}

int eggsfs_span_get_pages(struct eggsfs_block_span* block_span, struct address_space * mapping, struct list_head *pages, unsigned nr_pages, struct list_head *extra_pages) {
    int err;

    loff_t off_start = page_offset(lru_to_page(pages));
    unsigned long page_ix;
    unsigned long first_page_index = page_index(lru_to_page(pages));
    // Work out start and end index for each block we need to fetch.
    loff_t curr_off = off_start;
    u64 span_offset = off_start - block_span->span.start;
    u32 stripe_size = eggsfs_stripe_size(block_span);

    int D = eggsfs_data_blocks(block_span->parity);
    u64 span_physical_end = block_span->span.start + (u64)stripe_size*block_span->num_stripes;
    // span physical end and off_start are always at PAGE_SIZE boundary
    unsigned span_physical_pages = (span_physical_end - off_start)/PAGE_SIZE;
    unsigned span_logical_pages = (block_span->span.end - off_start + PAGE_SIZE -1)/PAGE_SIZE;
    unsigned available_span_pages = min(span_physical_pages, span_logical_pages);
    unsigned span_zero_pages = span_logical_pages - available_span_pages;
    unsigned span_pages = min(nr_pages, available_span_pages);

    // Pages that are used to store zeroes at the end of the file.
    // They are not stored in the block service and are filled by the client.
    LIST_HEAD(zero_pages);
    unsigned num_zero_pages = min(nr_pages - span_pages, span_zero_pages);
    if (num_zero_pages > 0)
        eggsfs_info("zero_pages:%d, num_pages:%d, span_pages:%d, span->start=%lld, span->end=%lld, stripe_size=%d, num_stripes=%d, diff=%lld", num_zero_pages, nr_pages, span_pages, block_span->span.start, block_span->span.end, stripe_size, block_span->num_stripes, block_span->span.end - span_physical_end);

    unsigned remaining_pages = span_pages;
    struct fetch_span_pages_state *fetches[EGGSFS_MAX_STRIPES];
    memset(fetches, 0, EGGSFS_MAX_STRIPES*sizeof(struct fetch_span_pages_state *));

    u32 curr_block_ix;
    int i;

    u32 stripe_ix = (curr_off - block_span->span.start) / stripe_size;
    u64 stripe_offset = (span_offset - stripe_ix*eggsfs_stripe_size(block_span));
    u32 start_block_offset = stripe_offset%block_span->cell_size + stripe_ix * block_span->cell_size;

    u32 next_block_offset = (u32)-1;


    for(;;) {
        BUG_ON(curr_off > block_span->span.end);

        struct fetch_span_pages_state *st = new_fetch_span_pages_state(block_span);
        if (IS_ERR(st)) {
            err = PTR_ERR(st);
            st = NULL;
            goto out;
        }
        stripe_ix = (curr_off - block_span->span.start) / stripe_size;
        BUG_ON(stripe_ix >= block_span->num_stripes);
        st->mapping = mapping;

        u32 start_block_ix = stripe_offset/block_span->cell_size;
        u32 last_block_ix = start_block_ix;
        u32 page_count;

        eggsfs_debug("stripe_ix=%d, st=%p, off_start=%lld, curr_off=%lld, span_start=%lld, stripe_size=%d", stripe_ix, st, off_start, curr_off, block_span->span.start, stripe_size);
        for (curr_block_ix = start_block_ix; curr_block_ix < D && remaining_pages > 0; curr_block_ix++) {
            // Only the first iteration needs to use the block offset as calculated from the request.
            // The rest of the blocks in the stripe will start from 0.
            if (next_block_offset == (u32)-1) {
                next_block_offset = start_block_offset;
            } else {
                next_block_offset = block_span->cell_size*stripe_ix;
            }
            st->start_offset = next_block_offset;

            u32 remaining_cell = min(block_span->span.end - block_span->span.start - stripe_ix*eggsfs_stripe_size(block_span) - curr_block_ix*block_span->cell_size, block_span->cell_size);
            page_count = min(remaining_pages, (remaining_cell - next_block_offset%block_span->cell_size + PAGE_SIZE - 1)/PAGE_SIZE);
            BUG_ON(page_count == 0);
            remaining_pages -= page_count;

            for(i = 0; i < page_count; i++) {
                page_ix = curr_off/PAGE_SIZE;
                struct page *page = lru_to_page(pages);
                if (page != NULL) {
                    if(page->index != page_ix) {
                        eggsfs_warn("adding stripe_ix %d, block_ix=%d page_ix: have %ld, want %ld, curr_off=%lld, block_off=%d", stripe_ix, curr_block_ix, page->index, page_ix, curr_off, next_block_offset);
                        BUG();
                    }
                    list_del(&page->lru);
                } else {
                    page = __page_cache_alloc(readahead_gfp_mask(mapping));
                    if (!page) {
                        err = -ENOMEM;
                        goto out_pages;
                    }
                    page->index = page_ix;
                }
                list_add_tail(&page->lru, &st->blocks_pages[curr_block_ix]);
                BUG_ON(atomic_read(&page->_refcount) < 1);
                curr_off += PAGE_SIZE;
            }

            if(remaining_pages == 0) {
                // The blockservice itself will be padded to the end,
                // we need to fetch (some of) those pages to match other blocks.
                u32 curr_size = page_count * PAGE_SIZE;
                while(curr_size < st->size) {
                    struct page* page = __page_cache_alloc(readahead_gfp_mask(mapping));
                    if (!page) {
                        err = -ENOMEM;
                        goto out_pages;
                    }
                    if(curr_off < block_span->span.end) {
                        // This is a valid page that we want to add in page cache later
                        page->index = curr_off/PAGE_SIZE;
                        curr_off += PAGE_SIZE;
                    } else {
                        // padding page that we will want to discard.
                        page->index = (unsigned long)-1;
                    }
                    curr_size += PAGE_SIZE;
                    list_add_tail(&page->lru, &st->blocks_pages[curr_block_ix]);
                    page_count++;
                    BUG_ON(atomic_read(&page->_refcount) < 1);
                }
            }
            // add extra pages in the very first block to match other blocks.
            if (curr_block_ix == start_block_ix && stripe_ix == ((off_start - block_span->span.start) / stripe_size) && remaining_pages > 0) {
                u64 tmp_off_start = off_start;
                for(i = stripe_offset%block_span->cell_size/PAGE_SIZE; i > 0; i--) {
                    tmp_off_start -= PAGE_SIZE;
                    st->start_offset -= PAGE_SIZE;
                    page_ix = tmp_off_start/PAGE_SIZE;
                    struct page* page = __page_cache_alloc(readahead_gfp_mask(mapping));
                    if (!page) {
                        err = -ENOMEM;
                        goto out_pages;
                    }
                    page->index = page_ix;
                    eggsfs_debug("inserting page %ld before first. pos=%lld, span->start=%lld sbo:%d, stripe_ix=%d", page->index, tmp_off_start, block_span->span.start, start_block_offset, stripe_ix);
                    list_add(&page->lru, &st->blocks_pages[curr_block_ix]);
                    page_count++;
                    BUG_ON(atomic_read(&page->_refcount) < 1);
                }
            }
            BUG_ON(remaining_pages < 0);
            if (st->size == 0) {
                st->size = page_count * PAGE_SIZE;
            } else {
                if (st->size != page_count * PAGE_SIZE) {
                    eggsfs_warn("read_size: %d, new: %ld", st->size, page_count * PAGE_SIZE);
                    BUG();
                }
            }
            BUG_ON(st->size > block_span->cell_size);
            if (curr_block_ix > last_block_ix) last_block_ix = curr_block_ix;
            BUG_ON(list_empty(&st->blocks_pages[curr_block_ix]));
        }
        stripe_offset = 0;

        BUG_ON(st->size == 0);
        atomic_set(&st->start_block, start_block_ix);
        atomic_set(&st->end_block, last_block_ix + 1);

        // fetch blocks from start_block_ix to last_block_ix - these will be our blocks we need when assembling the pages back, the rest will go to extra_pages.
        err = fetch_span_blocks(st);
        if (err) { goto out; }
        fetches[stripe_ix] = st;
        // wait for the fetch to complete before kicking off next stripe
        down(&st->sema);
        if(remaining_pages == 0) {
            break;
        }

    }

    for (i = 0; i < num_zero_pages; i++) {
        struct page* page = lru_to_page(pages);
        BUG_ON(page == NULL);
        list_del(&page->lru);
        char* dst = kmap_atomic(page);
        memset(dst, 0, PAGE_SIZE);
        kunmap_atomic(dst);
        list_add_tail(&page->lru, &zero_pages);
    }
    // if span_pages < nr_pages, some pages at the end may be left not filled.
    // It is ok to just drop them as they will be requested in the next readahead request.
    put_pages_list(pages);

    // collect pages

    remaining_pages = span_pages;

    stripe_ix = (off_start - block_span->span.start) / stripe_size;
    stripe_offset = (span_offset - stripe_ix*eggsfs_stripe_size(block_span));

    // starting offset is the start of the first block we ended up fetching.
    curr_off = block_span->span.start + stripe_ix*eggsfs_stripe_size(block_span) + atomic_read(&fetches[stripe_ix]->start_block)*block_span->cell_size + fetches[stripe_ix]->start_offset%block_span->cell_size;

    for(;;) {
        stripe_ix = (curr_off - block_span->span.start) / stripe_size;
        BUG_ON(stripe_ix >= block_span->num_stripes);
        struct fetch_span_pages_state *st = fetches[stripe_ix];
        err = atomic_read(&st->err);
        if (err) goto out;

        BUG_ON(st == NULL);
        BUG_ON(stripe_ix != fetch_span_pages_state_stripe_ix(st));
        BUG_ON(curr_off > block_span->span.end);

        for(curr_block_ix = atomic_read(&st->start_block); curr_block_ix < atomic_read(&st->end_block) && remaining_pages > 0 && curr_block_ix < D; curr_block_ix++) {
            curr_off = block_span->span.start + stripe_ix*eggsfs_stripe_size(block_span) + curr_block_ix*block_span->cell_size + st->start_offset%block_span->cell_size;
            eggsfs_debug("collecting: block_ix=%d, stripe_ix=%d, read_size=%d, remaining_pages=%d, start_offset=%d, span_end:%lld", curr_block_ix, stripe_ix, st->size, remaining_pages, st->start_offset, block_span->span.end);

            u32 curr_size = 0;
            while(curr_size < st->size) {
                // work out if it needs to be added back to the main list
                if(curr_off < block_span->span.end) {
                    // This is a valid page that we want to add in page cache later
                    page_ix = curr_off/PAGE_SIZE;
                    curr_off += PAGE_SIZE;
                } else {
                    // padding page that we will want to discard.
                    page_ix = (unsigned long)-1;
                }


                struct page *page = list_first_entry_or_null(&st->blocks_pages[curr_block_ix], struct page, lru);
                BUG_ON(page == NULL);
                if (page->index != page_ix) {
                    eggsfs_debug("first_page_index:%ld start_block=%d, end_block=%d, D=%d, stripe_offset=%lld, span_offset=%lld", first_page_index, atomic_read(&st->start_block), atomic_read(&st->end_block), D, stripe_offset, span_offset);
                    eggsfs_info("ino:%016llx, curr_block_ix=%d, read_size=%d, curr_size=%d", block_span->span.ino, curr_block_ix, st->size, curr_size);
                    eggsfs_warn("block_ix:%d curr_off:%lld want:%ld have:%ld remaining:%d", curr_block_ix, curr_off, page->index, page_ix, remaining_pages);
                    BUG();
                }
                list_del(&page->lru);
                page->private = 0;
                if(page->index == (unsigned long) -1) {
                    put_page(page);
                } else if(page->index >= first_page_index && remaining_pages > 0) {
                    list_add_tail(&page->lru, pages);
                    // Only count the pages we were asked to fetch.
                    remaining_pages--;
                } else {
                    list_add_tail(&page->lru, extra_pages);
                }
                curr_size += PAGE_SIZE;
            }
            // We go through all the requested pages, but there may be some
            // added to align fetch ranges for different blocks. At the end
            // all blocks falling in and out of requested range need to
            // balance. This is one of the checks to make sure they do.
            BUG_ON(remaining_pages < 0);
            if(!list_empty(&st->blocks_pages[curr_block_ix])) {
                eggsfs_warn("list not empty at block:%d, stripe_ix:%d, remaining=%d", curr_block_ix, stripe_ix, remaining_pages);
                BUG();
            }
        }
        stripe_offset = 0;
        start_block_offset = 0;

        if (remaining_pages == 0) {
            break;
        }
    }

    for (i = 0; i < num_zero_pages; i++) {
        struct page *page = lru_to_page(&zero_pages);
        BUG_ON(page == NULL);
        list_del(&page->lru);
        list_add_tail(&page->lru, pages);
    }

out:
    for (i = 0; i < block_span->num_stripes; i++) {
        if (fetches[i] != NULL) put_fetch_span_pages(fetches[i]);
    }
    return err;
out_pages:
    put_pages_list(pages);
    goto out;
}

//
// SPANS PART
//

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

static void put_span(struct eggsfs_span* span) {
    if (atomic_dec_return(&span->refcount) > 0) { return; }
    if (span->storage_class == EGGSFS_INLINE_STORAGE) {
        kmem_cache_free(eggsfs_inline_span_cachep, EGGSFS_INLINE_SPAN(span));
    } else {
        kmem_cache_free(eggsfs_block_span_cachep, EGGSFS_BLOCK_SPAN(span));
    }
}

struct get_span_ctx {
    struct eggsfs_inode* enode;
    struct rb_root spans;
    int err;
};

void eggsfs_file_spans_cb_span(void* data, u64 offset, u32 size, u32 crc, u8 storage_class, u8 parity, u8 stripes, u32 cell_size, const uint32_t* stripes_crcs) {
    eggsfs_debug("offset=%llu size=%u crc=%08x storage_class=%d parity=%d stripes=%d cell_size=%u", offset, size, crc, storage_class, parity, stripes, cell_size);

    struct get_span_ctx* ctx = (struct get_span_ctx*)data;
    if (ctx->err) { return; }

    struct eggsfs_block_span* span = kmem_cache_alloc(eggsfs_block_span_cachep, GFP_KERNEL);
    if (!span) { ctx->err = -ENOMEM; return; }
    atomic_set(&span->span.refcount, 1);

    if (eggsfs_data_blocks(parity) > EGGSFS_MAX_DATA || eggsfs_parity_blocks(parity) > EGGSFS_MAX_PARITY) {
        eggsfs_warn("D=%d > %d || P=%d > %d", eggsfs_data_blocks(parity), EGGSFS_MAX_DATA, eggsfs_data_blocks(parity), EGGSFS_MAX_PARITY);
        ctx->err = -EIO;
        put_span(&span->span);
        return;
    }

    span->span.ino = ctx->enode->inode.i_ino;
    span->span.start = offset;
    span->span.end = offset + size;
    span->span.storage_class = storage_class;

    span->cell_size = cell_size;
    memcpy(span->stripes_crc, stripes_crcs, sizeof(uint32_t)*stripes);
    span->num_stripes = stripes;
    span->parity = parity;
    int i;
    for (i = 0; i < stripes; i++) {
        span->stripes[i] = NULL;
    }

    eggsfs_debug("adding normal span");
    insert_span(&ctx->spans, &span->span);
}

void eggsfs_file_spans_cb_block(
    void* data, int block_ix,
    u64 bs_id, u32 ip1, u16 port1, u32 ip2, u16 port2, u8 flags,
    u64 block_id, u32 crc
) {
    struct get_span_ctx* ctx = (struct get_span_ctx*)data;
    if (ctx->err) { return; }

    struct eggsfs_span* span = rb_entry(rb_last(&ctx->spans), struct eggsfs_span, node);
    struct eggsfs_block_span* block_span = EGGSFS_BLOCK_SPAN(span);

    struct eggsfs_block* block = &block_span->blocks[block_ix];
    block->id = block_id;
    block->crc = crc;

    // Populate bs cache
    struct eggsfs_block_service bs;
    bs.id = bs_id;
    bs.ip1 = ip1;
    bs.port1 = port1;
    bs.ip2 = ip2;
    bs.port2 = port2;
    bs.flags = flags;
    block->bs = eggsfs_upsert_block_service(&bs);
    if (IS_ERR(block->bs)) {
        ctx->err = PTR_ERR(block->bs);
        return;
    }
}

void eggsfs_file_spans_cb_inline_span(void* data, u64 offset, u32 size, u8 len, const char* body) {
    eggsfs_debug("offset=%llu size=%u len=%u body=%*pE", offset, size, len, len, body);

    struct get_span_ctx* ctx = (struct get_span_ctx*)data;
    if (ctx->err) { return; }

    struct eggsfs_inline_span* span = kmem_cache_alloc(eggsfs_inline_span_cachep, GFP_KERNEL);
    if (!span) { ctx->err = -ENOMEM; return; }
    atomic_set(&span->span.refcount, 1);

    span->span.ino = ctx->enode->inode.i_ino;

    span->span.start = offset;
    span->span.end = offset + size;
    span->span.storage_class = EGGSFS_INLINE_STORAGE;
    span->len = len;
    memcpy(span->body, body, len);

    eggsfs_debug("adding inline span");

    insert_span(&ctx->spans, &span->span);
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

    // Exit early if we know we're out of bound.
    if (offset >= enode->inode.i_size) { GET_SPAN_EXIT(NULL); }

    bool fetched = false;

retry:
    // Check if we already have the span
    {
        down_read(&enode->file.spans_lock);
        struct eggsfs_span* span = lookup_span(&enode->file.spans, offset);
        up_read(&enode->file.spans_lock);
        if (likely(span != NULL)) {
            return span;
        }
        BUG_ON(fetched); // If we've just fetched it must be here.
    }

    // We need to fetch the spans.
    down_write(&enode->file.spans_lock);

    // Fetch one batch of spans. We could fetch more, which would aid prefetching
    // (right now prefetching just does nothing if it does not have the span ready)
    // but:
    //
    // 1. Currently prefetching is off
    // 2. We have some directories  with max span size 3MB, which
    //      means that files have tons of spans, which stresses out the metadata servers
    //      massively. These are files where the quants are doing 1MB random accesses,
    //      so one batch of spans will suffice.
    u64 next_offset;
    struct get_span_ctx ctx = { .err = 0, .enode = enode };
    ctx.spans = RB_ROOT;
    err = eggsfs_error_to_linux(eggsfs_shard_file_spans(
        (struct eggsfs_fs_info*)enode->inode.i_sb->s_fs_info, enode->inode.i_ino, offset, &next_offset,&ctx
    ));
    err = err ?: ctx.err;
    if (unlikely(err)) {
        eggsfs_debug("failed to get file spans at %llu err=%d", offset, err);
        for (;;) { // free all the spans we just got
            struct rb_node* node = rb_first(&ctx.spans);
            if (node == NULL) { break; }
            rb_erase(node, &ctx.spans);
            put_span(rb_entry(node, struct eggsfs_span, node));
        }
        up_write(&enode->file.spans_lock);
        GET_SPAN_EXIT(ERR_PTR(err));
    }
    // add them to enode spans
    for (;;) {
        struct rb_node* node = rb_first(&ctx.spans);
        if (node == NULL) { break; }
        rb_erase(node, &ctx.spans);
        struct eggsfs_span* span = rb_entry(node, struct eggsfs_span, node);
        if (!insert_span(&file->spans, span)) {
            // Span is already cached
            put_span(span);
        }
    }
    up_write(&file->spans_lock);

    fetched = true;
    goto retry;

#undef GET_SPAN_EXIT
}

static void free_span_stripes(struct eggsfs_block_span* block_span) {

    u8 i;
    struct eggsfs_stripe_lru *lru;
    struct eggsfs_stripe *stripe;

    for (i = 0; i < EGGSFS_MAX_STRIPES; i++) {
        lru = get_stripe_lru(&block_span->span);
        spin_lock(&lru->lock);
        stripe = block_span->stripes[i];
        spin_unlock(&lru->lock);
        if (stripe != NULL) {
            stripe_validate_and_remove_from_lru(stripe, lru);
            free_stripe(stripe, NULL);
        }
    }
}

void eggsfs_unlink_span(struct eggsfs_inode* enode, struct eggsfs_span* span) {
    down_write(&enode->file.spans_lock);
    rb_erase(&span->node, &enode->file.spans);
    if (span->storage_class != EGGSFS_INLINE_STORAGE) {
        free_span_stripes(EGGSFS_BLOCK_SPAN(span));
    }
    put_span(span);
    up_write(&enode->file.spans_lock);
}

void eggsfs_unlink_spans(struct eggsfs_inode* enode) {
    down_write(&enode->file.spans_lock);

    for (;;) {
        struct rb_node* node = rb_first(&enode->file.spans);
        if (node == NULL) { break; }

        struct eggsfs_span* span = rb_entry(node, struct eggsfs_span, node);
        rb_erase(&span->node, &enode->file.spans);
        put_span(span);
    }

    up_write(&enode->file.spans_lock);
}

//
// INIT PART
//

int eggsfs_span_init(void) {
    int i;
    for (i = 0; i < EGGSFS_STRIPE_LRUS; i++) {
        spin_lock_init(&stripe_lrus[i].lock);
        INIT_LIST_HEAD(&stripe_lrus[i].lru);
    }

    int err = register_shrinker(&eggsfs_stripe_shrinker);
    if (err) { return err; }

    eggsfs_block_span_cachep = kmem_cache_create(
        "eggsfs_block_span_cache",
        sizeof(struct eggsfs_block_span),
        0,
        SLAB_RECLAIM_ACCOUNT | SLAB_MEM_SPREAD,
        NULL
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

    eggsfs_stripe_cachep = kmem_cache_create(
        "eggsfs_stripe_cache",
        sizeof(struct eggsfs_stripe),
        0,
        SLAB_RECLAIM_ACCOUNT | SLAB_MEM_SPREAD,
        NULL
    );
    if (!eggsfs_stripe_cachep) {
        err = -ENOMEM;
        goto out_inline;
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
        goto out_stripe;
    }

    eggsfs_fetch_span_pages_cachep = kmem_cache_create(
        "eggsfs_fetch_span_pages_cache",
        sizeof(struct fetch_span_pages_state),
        0,
        SLAB_RECLAIM_ACCOUNT | SLAB_MEM_SPREAD,
        &init_fetch_span_pages
    );
    if (!eggsfs_fetch_span_pages_cachep) {
        err = -ENOMEM;
        goto out_fetch_stripe;
    }

    return 0;
out_fetch_stripe:
    kmem_cache_destroy(eggsfs_fetch_stripe_cachep);
out_stripe:
    kmem_cache_destroy(eggsfs_stripe_cachep);
out_inline:
    kmem_cache_destroy(eggsfs_inline_span_cachep);
out_block:
    kmem_cache_destroy(eggsfs_block_span_cachep);
out_shrinker:
    unregister_shrinker(&eggsfs_stripe_shrinker);
    return err;
}

void eggsfs_span_exit(void) {
    eggsfs_debug("span exit");
    unregister_shrinker(&eggsfs_stripe_shrinker);
    // All the stripes should be freed up when removing spans from inodes,
    // but it seems like the inode cache is not always purged before calling
    // the exit function.

    u64 pages = eggsfs_drop_all_stripes();
    if (pages > 0) eggsfs_info("expected to drop 0 pages, but dropped %llu", pages);
    kmem_cache_destroy(eggsfs_block_span_cachep);
    kmem_cache_destroy(eggsfs_inline_span_cachep);
    kmem_cache_destroy(eggsfs_stripe_cachep);
    kmem_cache_destroy(eggsfs_fetch_stripe_cachep);
    kmem_cache_destroy(eggsfs_fetch_span_pages_cachep);
}
