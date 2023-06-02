#ifndef _EGGSFS_RS_H
#define _EGGSFS_RS_H

#include <linux/kernel.h>

#define EGGSFS_MAX_DATA 10
#define EGGSFS_MAX_PARITY 4
#define EGGSFS_MAX_BLOCKS (EGGSFS_MAX_DATA+EGGSFS_MAX_PARITY)

extern int eggsfs_rs_cpu_level;

static inline u8 eggsfs_data_blocks(u8 parity) {
    return parity & 0x0F;
}

static inline u8 eggsfs_parity_blocks(u8 parity) {
    return parity >> 4;
}

static inline u8 eggsfs_blocks(u8 parity) {
    return eggsfs_data_blocks(parity) + eggsfs_parity_blocks(parity);
}

static inline u8 eggsfs_mk_parity(u8 data, u8 parity) {
    return (parity << 4) | data;
}

// len(data) = eggsfs_data_blocks(parity)
// len(out)  = eggsfs_parity_blocks(parity)
//
// You _must_ wrap this with kernel_fpu_begin()/kernel_fpu_end()!
int eggsfs_compute_parity(u8 parity, ssize_t size, const char** data, char** out);

// This function does kernel_fpu_begin()/kernel_fpu_end() itself, since it already
// works with pages.
int eggsfs_recover(
    u8 parity,
    u32 have_blocks,
    u32 want_block,
    u32 num_pages,
    // B long array of pages. We'll get the pages from the blocks we have and
    // put them in the block we want. The lists should all be of the same length.
    struct list_head* pages
);

int __init eggsfs_rs_init(void);
void __cold eggsfs_rs_exit(void);

#endif
