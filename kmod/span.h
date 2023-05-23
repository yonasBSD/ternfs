#ifndef _EGGSFS_SPANSIMPLE_H
#define _EGGSFS_SPANSIMPLE_H

#include <linux/kernel.h>

#include "inode.h"
#include "counter.h"

EGGSFS_DECLARE_COUNTER(eggsfs_stat_cached_spans);
EGGSFS_DECLARE_COUNTER(eggsfs_stat_cached_span_pages);

struct eggsfs_span {
    struct eggsfs_inode* enode;
    struct rb_node node;  // to look them up
    struct list_head lru; // to decide what to evict
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
        BUG_ON(_span->storage_class != EGGSFS_INLINE_STORAGE); \
        container_of(_span, struct eggsfs_inline_span, span); \
    })

struct eggsfs_block {
    u64 id;
    struct eggsfs_block_service bs;
    u32 crc;
};

struct eggsfs_block_span {
    struct eggsfs_span span;
    struct xarray pages;       // indexed by page number
    struct eggsfs_block blocks[EGGSFS_MAX_BLOCKS];
    struct eggsfs_latch stripe_latches[15];
    u32 cell_size;
    u32 stripes_crc[15];
    // * 0: there are no readers, the span is in the LRU.
    // * n > 0: there are readers, the span is _not_ in the LRU.
    // * n < 0: the span is being reclaimed, it is _not_ in the LRU.
    s64 readers;
    // If anybody's actually read some of this span 
    bool actually_read;
    u8 stripes;
    u8 parity;    
};

#define EGGSFS_BLOCK_SPAN(_span) ({ \
        BUG_ON(_span->storage_class == EGGSFS_EMPTY_STORAGE || _span->storage_class == EGGSFS_INLINE_STORAGE); \
        container_of(_span, struct eggsfs_block_span, span); \
    })

// Fetches and caches the span if necessary. `offset` need not be the actual
// span offset, the span containing the offset will be returned.
//
// If the offset is out of bounds, NULL will be returned. Errors might
// be returned if we fail to fetch the span.
struct eggsfs_span* eggsfs_get_span(struct eggsfs_inode* enode, u64 offset);

// Makes the span available for reclamation.
void eggsfs_span_put(struct eggsfs_span* span, bool was_read);

// The page_ix is the page number inside the span.
struct page* eggsfs_get_span_page(struct eggsfs_block_span* span, u32 page_ix);

void eggsfs_drop_all_spans(void);

void eggsfs_file_spans_cb_span(void* data, u64 offset, u32 size, u32 crc, u8 storage_class, u8 parity, u8 stripes, u32 cell_size, const uint32_t* stripes_crcs);
void eggsfs_file_spans_cb_block(
    void* data, int block_ix,
    // block service stuff
    u64 bs_id, u32 ip1, u16 port1, u32 ip2, u16 port2,
    // block stuff
    u64 block_id, u32 crc
);
void eggsfs_file_spans_cb_inline_span(void* data, u64 offset, u32 size, u8 len, const char* body);

// To be used on eviction, i.e. when we expect no contention.
void eggsfs_drop_spans(struct eggsfs_inode* enode);

#endif
