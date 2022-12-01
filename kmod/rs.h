#ifndef _EGGSFS_RS_H
#define _EGGSFS_RS_H

#include <linux/kernel.h>

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

int __init eggsfs_rs_init(void);
void __cold eggsfs_rs_exit(void);

#endif
