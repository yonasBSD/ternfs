#ifndef _EGGSFS_SPAN_H
#define _EGGSFS_SPAN_H

#include <linux/kernel.h>

#include "inode.h"
#include "counter.h"
#include "block.h"

EGGSFS_DECLARE_COUNTER(eggsfs_stat_cached_spans);
extern atomic64_t eggsfs_stat_cached_span_pages;

extern unsigned long eggsfs_span_cache_max_size_async;
extern unsigned long eggsfs_span_cache_min_avail_mem_async;
extern unsigned long eggsfs_span_cache_max_size_sync;
extern unsigned long eggsfs_span_cache_min_avail_mem_sync;
extern unsigned long eggsfs_span_cache_max_size_drop;
extern unsigned long eggsfs_span_cache_min_avail_mem_drop;

struct eggsfs_span {
    u64 ino; // for logging and various other non-essential things.
    struct rb_node node;
    struct list_head lru;
    u64 start;
    u64 end;
    u8 storage_class; // used to determine which type of span this is enclosed in
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

struct eggsfs_block_span {
    struct eggsfs_span span;
    // When this span was fetched. We use this to expire the
    // span. Remember that the file _contents_
    struct xarray pages;       // indexed by page number
    struct eggsfs_block blocks[EGGSFS_MAX_BLOCKS];
    struct eggsfs_latch stripe_latches[EGGSFS_MAX_STRIPES];
    // This field, like the one below, is protected by the LRU lock.
    // If it is NULL, it means that the span has outlived the inode.
    // Given the `file->in_flight` field this should really never
    // happen, but it's a good sanity check.
    //
    // Note that ihold'ing this would defeat the purpose (the cached
    // spans would prevent inode eviction). Think of it as a weak
    // reference to the inode.
    struct eggsfs_inode* enode;
    // When refcount = 0, this might still be alive, but in an LRU.
    // When refcount < 0, we're currently reclaiming this span.
    // Going from 0 to 1, and from 0 to -1 involves taking the respective
    // LRU lock. This field is not atomic_t because _all changes to this
    // value go through the LRU lock_, since they must be paired with
    // manipulating the LRU list.
    int refcount;
    u32 cell_size;
    u32 stripes_crc[EGGSFS_MAX_STRIPES];
    // wether somebody actually read this span (will determine if it ends
    // up at the front or at the back of the LRU)
    bool touched;
    u8 stripes;
    u8 parity;    
};

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

// Makes the span available for reclamation.
void eggsfs_put_span(struct eggsfs_span* span, bool was_read);

// The page_ix is the page number inside the span. Can't be called with inline
// span.
struct page* eggsfs_get_span_page(struct eggsfs_block_span* span, u32 page_ix);

// Drop all the spans in a specific file. Note that this does not mean that
// the span will me immediately deallocated -- there might be still things
// holding onto them (prefetching requests, mostly).
//
// This function _must_ be called before inode is evicted! Otherwise we'll have
// dangling references to the enode in the spans.
//
// In fact this function can only really be called right before eviction, since
// it sets `span->enode = NULL`, which means that many functions on the spans
// will fail after calling this function.
void eggsfs_drop_file_spans(struct eggsfs_inode* enode);

// Drops a single span from a file. Might not succeed because we can't
// lock the spans tree for writing, and also because the span still has
// readers.
int eggsfs_drop_file_span(struct eggsfs_inode* enode, u64 offset);

// Drops all cached spans not being currently used. Returns number of
// freed pages.
u64 eggsfs_drop_all_spans(void);

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
