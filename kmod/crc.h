#ifndef _EGGSFS_CRC_H
#define _EGGSFS_CRC_H

#include <linux/kernel.h>

// You _must_ wrap this with kernel_fpu_begin/kernel_fpu_end!
// It's not required for the other functions.
u32 eggsfs_crc32c(u32 crc, const char* buf, size_t len);

// The only difference with `eggsfs_crc32c` is that kernel_fpu_begin/kernel_fpu_end
// are not required for this one.
u32 eggsfs_crc32c_simple(u32 crc, const char* buf, size_t len);

u32 eggsfs_crc32c_xor(u32 crc1, u32 crc2, size_t len);

u32 eggsfs_crc32c_append(u32 crc1, u32 crc2, size_t len2);

u32 eggsfs_crc32c_zero_extend(u32 crc, ssize_t zeros);

#endif
