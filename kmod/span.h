#ifndef _EGGSFS_SPAN_H
#define _EGGSFS_SPAN_H

#include <linux/kernel.h>

#include "inode.h"
#include "counter.h"
#include "block.h"
#include "rs.h"

EGGSFS_DECLARE_COUNTER(eggsfs_stat_cached_stripes);
EGGSFS_DECLARE_COUNTER(eggsfs_stat_dropped_stripes_refetched);
EGGSFS_DECLARE_COUNTER(eggsfs_stat_stripe_cache_hit);
EGGSFS_DECLARE_COUNTER(eggsfs_stat_stripe_cache_miss);
EGGSFS_DECLARE_COUNTER(eggsfs_stat_stripe_page_cache_hit);
EGGSFS_DECLARE_COUNTER(eggsfs_stat_stripe_page_cache_miss);
extern atomic64_t eggsfs_stat_cached_stripe_pages;

extern unsigned long eggsfs_stripe_cache_max_size_async;
extern unsigned long eggsfs_stripe_cache_min_avail_mem_async;
extern unsigned long eggsfs_stripe_cache_max_size_sync;
extern unsigned long eggsfs_stripe_cache_min_avail_mem_sync;
extern unsigned long eggsfs_stripe_cache_max_size_drop;
extern unsigned long eggsfs_stripe_cache_min_avail_mem_drop;

struct eggsfs_span {
    // All the spans operations are independent from `struct eggsfs_inode`.
    // So we need the inode here.
    u64 ino;
    u64 start;
    u64 end;
    // To be in the inode tree. Note that we might _not_ be in the
    // tree once the inode releases us.
    struct rb_node node;
    // The inode and the stripes hold references to us. So the inode
    // needs to unlink us and all the stripes need to be gone after
    // this can be freed. Inline spans obviously only are referenced
    // by inodes.
    atomic_t refcount;
    // Used to determine which type of span this is enclosed in.
    u8 storage_class;
};

struct eggsfs_inline_span {
    struct eggsfs_span span;
    u8 len;
    u8 body[255];
};

#define EGGSFS_INLINE_SPAN(_span) ({ \
        BUG_ON((_span)->storage_class != EGGSFS_INLINE_STORAGE); \
        container_of(_span, struct eggsfs_inline_span, span); \
    })

struct eggsfs_block {
    struct eggsfs_stored_block_service* bs;
    u64 id;
    u32 crc;
};

struct eggsfs_stripe {
    // Holds reference to it (we can always dereference it from
    // the stripe).
    struct eggsfs_block_span* span;
    // Full list of contents if they have been fetched.
    struct xarray pages;
    // To synchronize fillin in the pages for the first time.
    struct eggsfs_latch latch;
    struct list_head lru;
    // When refcount = 0, this might still be alive, but in an LRU.
    // When refcount < 0, we're currently reclaiming this stripe.
    // Going from 0 to 1, and from 0 to -1 involves taking the respective
    // LRU lock. This field is not atomic_t because _all changes to this
    // value go through the LRU lock_, since they must be paired with
    // manipulating the LRU list.
    atomic_t refcount;
    u8 stripe_ix; // Which stripe it is
    bool touched; // Whether the stripe was touched before putting it back
};

struct eggsfs_block_span {
    struct eggsfs_span span;
    struct eggsfs_block blocks[EGGSFS_MAX_BLOCKS];
    // If NULL, the stripe is currently not initialized.
    // The value has to be accessed with the lock of the containg stripe_lru.
    // It is set without LRU lock in a single place when creating a new stripe.
    // There should be no new code paths that manipulate it without locking
    // the LRU first.
    struct eggsfs_stripe* stripes[EGGSFS_MAX_STRIPES];
    bool stripe_was_cached[EGGSFS_MAX_STRIPES];
    u32 cell_size;
    u32 stripes_crc[EGGSFS_MAX_STRIPES];
    u8 num_stripes;
    u8 parity;
};

static inline u32 eggsfs_stripe_size(struct eggsfs_block_span* span) {
    return span->cell_size * eggsfs_data_blocks(span->parity);
}

#define EGGSFS_BLOCK_SPAN(_span) ({ \
        BUG_ON((_span)->storage_class == EGGSFS_EMPTY_STORAGE || (_span)->storage_class == EGGSFS_INLINE_STORAGE); \
        container_of(_span, struct eggsfs_block_span, span); \
    })

// Fetches and caches the span if necessary. `offset` need not be the actual
// span offset, the span containing the offset will be returned.
//
// If the offset is out of bounds, NULL will be returned. Errors might
// be returned if we fail to fetch the span.
struct eggsfs_span* eggsfs_get_span(struct eggsfs_inode* enode, u64 offset);

// Remove the span from the inode tree, decrementing the refcount.
void eggsfs_unlink_span(struct eggsfs_inode* enode, struct eggsfs_span* span);

// Drop all the spans in a specific file. The span might linger around
// if the stripes are being read (since they have references to it too).
//
// This function must be called before inode eviction, otherwise we'll
// leak spans and stripes.
void eggsfs_unlink_spans(struct eggsfs_inode* enode);

// Gets a stripe within a span. offset is the offset inside the span.
// Once this returns, all the pages will be filled in already. The stripe
// needs to be put back with `eggsfs_put_stripe` when you're done with it.
struct eggsfs_stripe* eggsfs_get_stripe(struct eggsfs_block_span* span, u32 offset);

// Makes the stripe available for reclamation.
void eggsfs_put_stripe(struct eggsfs_stripe* stripe, bool was_read);

// The page_ix is the page number inside the stripe. The first call demanding
// a page will pull in all the pages for the stripe.
struct page* eggsfs_get_stripe_page(struct eggsfs_stripe* stripe, u32 page_ix);

// Fetches pages from block services to fill the supplied list of pages.
// @pages - original list of pages to be filled. The list is expected to
//          contain pages with page->index values increasing by 1.
//          If file offsets in the supplied list fall beyond the end of
//          the supplied block_span, pages for those offsets are dropped
//          and removed from the list.
// @extra_pages - empty list to add fetched pages that fall outside of the
//                requested range.
// Pages are taken out of the list, added to required blocks for fetching, 
// filled with data fetched from block services and then assembled back
// maintaining the original order to match file offsets. Each supplied page
// has page->index prefilled as file_offset/PAGE_SIZE. Fetches are done in
// multiple blocks, but only up to the end of the stripe. If more pages are
// needed, separate fetch for the next stripe is kicked off. Fetch is kicked
// off with the same amount of pages in each block, so if any blocks end up
// having fewer pages, additional padding pages are allocated. If any blocks
// fail, the fetch for the rest of the blocks in the stripe and required parity
// blocks is kicked off. These pages and any padding pages have correct
// page->index set to match the file offset and they are added to the extra_pages
// list. It is up to the caller how to handle them.
// If the requested range is larger than span end, the remaining pages are
// dropped with put_page and not filled. The upstream code only really cares that
// the first page is filled in by the readahead code, so returning fewer pages or
// even returning error aggressively is fine.
int eggsfs_span_get_pages(struct eggsfs_block_span* block_span, struct address_space* mapping, struct list_head *pages, unsigned nr_pages, struct list_head *extra_pages);

// Drops all cached stripes not being currently used. Returns number of
// freed pages.
u64 eggsfs_drop_all_stripes(void);

// Callbacks for metadata
void eggsfs_file_spans_cb_span(
    void* data, u64 offset, u32 size, u32 crc,
    u8 storage_class, u8 parity, u8 stripes, u32 cell_size,
    const uint32_t* stripes_crcs
);
void eggsfs_file_spans_cb_block(
    void* data, int block_ix,
    // block service stuff
    u64 bs_id, u32 ip1, u16 port1, u32 ip2, u16 port2, u8 flags,
    // block stuff
    u64 block_id, u32 crc
);
void eggsfs_file_spans_cb_inline_span(void* data, u64 offset, u32 size, u8 len, const char* body);

int __init eggsfs_span_init(void);
void __cold eggsfs_span_exit(void);

#endif
