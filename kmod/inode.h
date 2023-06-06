#ifndef _EGGSFS_INODE_H
#define _EGGSFS_INODE_H

#include <linux/fs.h>
#include <linux/spinlock.h>
#include <linux/xarray.h>

#include "latch.h"
#include "bincode.h"
#include "rs.h"

#define EGGSFS_ROOT_INODE 0x2000000000000000ull

struct eggsfs_file_span;

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

struct eggsfs_transient_span;

#define EGGSFS_FILE_STATUS_NONE 0 // when we create the inode
#define EGGSFS_FILE_STATUS_READING 1
#define EGGSFS_FILE_STATUS_CREATED 2
#define EGGSFS_FILE_STATUS_WRITING 3

struct eggsfs_inode_file {
    int status;

    // Normal file stuff

    struct rb_root spans;
    struct rw_semaphore spans_lock;
    atomic_t prefetches; // how many prefetches are ongoing
    wait_queue_head_t prefetches_wq; // waited on by evictor
    // low 32 bits: section start. high 32 bits: section end. Rouded up to
    // PAGE_SIZE, since we know each stripe is at PAGE_SIZE boundary anyway.
    // This means that prefetch with be somewhat broken for files > 16TiB.
    // This is what we use in our prefetch heuristic.
    atomic64_t prefetch_section;

    // Transient file stuff. Only initialized on file creation, otherwise it's garbage.
    // Could be factored out to separate data structure since it's completely useless
    // when writing.

    u64 cookie;
    // If we've encountered an error such that we want to stop writing to this file
    // forever.
    atomic_t transient_err;
    // Span we're currently writing to. If NULL we're yet to write anything.
    struct eggsfs_transient_span* writing_span;
    // Whether we're currently flushing a span (block write + add span certify)
    struct semaphore flushing_span_sema;
    // We use this to track where we should close the file from.
    struct task_struct* owner;
    // We store these one separatedly from `owner` above because when a process exits
    // it frees the mm before it frees the files. And we need the mm to account the
    // MM_FILEPAGES in the file flushing logic.
    struct mm_struct* mm;
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

struct inode* eggsfs_get_inode(struct super_block* sb, struct eggsfs_inode* parent, u64 ino);


// super ops
struct inode* eggsfs_inode_alloc(struct super_block* sb);
void eggsfs_inode_evict(struct inode* inode);
void eggsfs_inode_free(struct inode* inode);

int __init eggsfs_inode_init(void);
void __cold eggsfs_inode_exit(void);

// inode ops
int eggsfs_do_getattr(struct eggsfs_inode* enode);

#endif
