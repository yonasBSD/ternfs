#ifndef _EGGSFS_INODE_H
#define _EGGSFS_INODE_H

#include <linux/fs.h>
#include <linux/spinlock.h>
#include <linux/xarray.h>

#include "block.h"
#include "latch.h"
#include "bincode.h"

#define EGGSFS_ROOT_INODE 0x2000000000000000ull

struct eggsfs_file_span;

// When we create a file, it is writeable. When we start writing to it, it's transient.
// In any case, with any of these two flags, we have the `transient` case of the union.
#define EGGSFS_INODE_WRITEABLE 1
#define EGGSFS_INODE_TRANSIENT 2

// These two influence the size of eggsfs_inode, currently
#define EGGSFS_MAX_BLOCK_POLICIES 2 // one for flash, one for hdd
#define EGGSFS_MAX_SPAN_POLICIES 3 // we happen to have three for now

struct eggsfs_block_policies {
    u8 len;
    char body[EGGSFS_MAX_BLOCK_POLICIES*EGGSFS_BLOCK_POLICY_ENTRY_SIZE];
};

static inline int eggsfs_block_policies_len(struct eggsfs_block_policies* policies) {
    BUG_ON(policies->len == 0);
    return (int)policies->len / EGGSFS_BLOCK_POLICY_ENTRY_SIZE;
}

static inline void eggsfs_block_policies_get(struct eggsfs_block_policies* policies, int ix, u8* storage_class, u32* min_size) {
    BUG_ON(ix < 0 || ix >= EGGSFS_MAX_BLOCK_POLICIES);
    char* b = policies->body + ix*EGGSFS_BLOCK_POLICY_ENTRY_SIZE;
    *storage_class = *(u8*)b; b += 1;
    *min_size = get_unaligned_le32(b); b += 4;
}

struct eggsfs_span_policies {
    u8 len;
    char body[EGGSFS_MAX_SPAN_POLICIES*EGGSFS_SPAN_POLICY_ENTRY_SIZE];
};

static inline int eggsfs_span_policies_len(struct eggsfs_span_policies* policies) {
    BUG_ON(policies->len == 0);
    return (int)policies->len / EGGSFS_SPAN_POLICY_ENTRY_SIZE;
}

static inline void eggsfs_span_policies_get(struct eggsfs_span_policies* policies, int ix, u32* max_size, u8* parity) {
    BUG_ON(ix < 0 || ix >= EGGSFS_MAX_SPAN_POLICIES);
    char* b = policies->body + ix*EGGSFS_BLOCK_POLICY_ENTRY_SIZE;
    *max_size = get_unaligned_le32(b); b += 4;
    *parity = *(u8*)b; b += 1;
}

static inline void eggsfs_span_policies_last(struct eggsfs_span_policies* policies, u32* max_size, u8* parity) {
    eggsfs_span_policies_get(policies, eggsfs_span_policies_len(policies)-1, max_size, parity);
}

#define EGGSFS_SPAN_STATUS_WRITING 0       // we're writing to this span
#define EGGSFS_SPAN_STATUS_IDLE 1          // we're waiting to flush this out
#define EGGSFS_SPAN_STATUS_LAST 2          // like idle, but signals that there won't be other spans after this one
#define EGGSFS_SPAN_STATUS_FLUSHING 3      // we're flushing this to the block services
#define EGGSFS_SPAN_STATUS_FLUSHING_LAST 4 // we're flushing this to the block services, and it's the last span

#define EGGSFS_MAX_DATA 10
#define EGGSFS_MAX_PARITY 4
#define EGGSFS_MAX_BLOCKS (EGGSFS_MAX_DATA+EGGSFS_MAX_PARITY)

struct eggsfs_transient_span {
    struct list_head list;
    // Linear list of pages with the body of the span, used when we're still gathering
    // content for it. We use `index` in the page to track where we're writing to.
    // We also add the parity block pages here, just to have all the pages available in
    // one place.
    struct list_head pages;
    // Used by the workers which write out to the block services. They first arrange
    // the pages here and compute the parity blocks. Then they can just write them out.
    //
    // We use `->private` to link these together, so that `pages` list can still contain
    // all of them, which is handy in a couple of places.
    //
    // In this stage we use `index` to track where we're reading from the head page,
    // and keep popping the head page as we write them out.
    struct page* blocks[EGGSFS_MAX_BLOCKS];
    struct eggsfs_block_socket* block_sockets[EGGSFS_MAX_BLOCKS];
    struct eggsfs_write_block_request* block_reqs[EGGSFS_MAX_BLOCKS];
    // These are decided by whoever moves the span from WRITING to IDLE/LAST.
    u32 size; // the logical size of the span, might be smaller than actual if we added leading zeros because of RS
    u32 block_size;
    u8 stripes;
    u8 storage_class;
    u8 parity;
    // See EGGSFS_SPAN_STATUS above.
    u8 status;
};

#define EGGSFS_FILE_STATUS_NONE 0 // when we create the inode
#define EGGSFS_FILE_STATUS_READING 1
#define EGGSFS_FILE_STATUS_CREATED 2
#define EGGSFS_FILE_STATUS_WRITING 3

struct eggsfs_inode_file {
    int status;

    // Normal file stuff

    struct rb_root spans;
    struct rw_semaphore spans_lock;

    // These two things which are really only concerning the transient files
    // are always kept around since we check that they're empty when
    // tearing down the inode.

    // List of spans, FIFO. There's always a "WRITING/LAST" span at the end,
    // and there's never a WRITING span which could be IDLE.
    struct list_head transient_spans;
    // Used to synchronize between things adding and removing to the list of spans.
    spinlock_t transient_spans_lock;

    // Transient file stuff. Only initialized on file creation, otherwise it's garbage.
    // Could be factored out to separate data structure since it's completely useless
    // when writing.

    u64 cookie;
    // Worker which flushes the spans
    struct work_struct flusher;
    // To know when we're done flushing
    struct semaphore done_flushing;
    // If we've encountered an error such that we want to stop.
    atomic_t transient_err;
    // How many bytes we've flushed so far
    atomic64_t flushed_so_far;
    // Used to bound the number of in-flight spans (otherwise we
    // would eat quite a bit of memory for big files)
    struct semaphore in_flight_spans;
    // We use this to track where we should close the file from.
    struct task_struct* owner;
    // We store these one separatedly from `owner` above because when a process exits
    // it frees the mm before it frees the files. And we need the mm to account the
    // MM_FILEPAGES in the file flushing logic.
    struct mm_struct* mm;
    // Size _apart from current span_.
    u64 size_without_current_span;
};


struct eggsfs_inode_dir {
    // In `struct page`, we use:
    //
    // ->mapping to store the next page
    // ->private to store the number of entries stored in the page (high 32 bits),
    //     and a reference count with how many people need the cache (low 32 bits).
    //     The reference count is only stored in the "head" page.
    // ->rcu_head to synchronize between modifications to the reference count.
    struct page __rcu * pages;
    struct eggsfs_latch pages_latch;
};

struct eggsfs_inode {
    struct inode inode;

    // We cache things based on the eggsfs mtime, but we need to decide when the
    // mtime itself is stale, which we do using `mtime_expiry`.
    u64 mtime;        // in eggsfs time
    u64 mtime_expiry; // in jiffies

    u64 edge_creation_time; // in eggsfs time, used for operations (re)moving the edge

    // These are relevant for directoriese (obviously), but we also use them for transient
    // files when we create them, since we need it in many places.
    // TODO consider refreshing it for directories, right now we never do.
    struct eggsfs_block_policies block_policies;
    struct eggsfs_span_policies span_policies;
    u32 target_stripe_size;

    union {
        struct eggsfs_inode_file file;
        struct eggsfs_inode_dir dir;
    };

    struct eggsfs_latch getattr_update_latch;
    int getattr_err;
};

#define EGGSFS_I(ptr) container_of(ptr, struct eggsfs_inode, inode)
#define EGGSFS_INODE_NEED_GETATTR 1

#define EGGSFS_INODE_DIRECTORY 1
#define EGGSFS_INODE_FILE 2
#define EGGSFS_INODE_SYMLINK 3

static inline u64 eggsfs_inode_type(u64 ino) {
    return (ino >> 61) & 0x03;
}

static inline u32 eggsfs_inode_shard(u64 ino) {
    return ino & 0xff;
}

struct inode* eggsfs_get_inode(struct super_block* sb, u64 ino);


// super ops
struct inode* eggsfs_inode_alloc(struct super_block* sb);
void eggsfs_inode_evict(struct inode* inode);
void eggsfs_inode_free(struct inode* inode);

int __init eggsfs_inode_init(void);
void __cold eggsfs_inode_exit(void);

// inode ops
int eggsfs_do_getattr(struct eggsfs_inode* enode);

#endif
