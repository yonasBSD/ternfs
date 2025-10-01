// Copyright 2025 XTX Markets Technologies Limited
// crc32c_4k_fusion Copyright 2022 Peter Cawley <corsix@corsix.org>
//
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
// See <https://www.corsix.org/content/fast-crc32c-4k> for the 4k fusion kernel
// and <https://mazzo.li/posts/crc-tips.html> for the other transformations.

#if CRC32C_USE_PCLMUL

#ifdef __clang__
__attribute__((no_sanitize("integer")))
#endif
#ifdef __KERNEL__
__attribute__((target("crc32,pclmul")))
#endif
static u32 crc32c_4k_fusion(u32 acc_a, const char* buf, size_t n_blocks) {
    size_t stride = n_blocks * 24 + 8;
    // Four chunks:
    //  Chunk A: 0 through stride
    //  Chunk B: stride through stride*2
    //  Chunk C: stride*2 through stride*3-8
    //  Chunk D: stride*3-8 through n_blocks*136+16
    // First block of 64 from D is easy.
    const char* buf2 = buf + n_blocks * 72 + 16;
    __m128i x1 = _mm_loadu_si128((__m128i*)buf2);
    __m128i x2 = _mm_loadu_si128((__m128i*)(buf2 + 16));
    __m128i x3 = _mm_loadu_si128((__m128i*)(buf2 + 32));
    __m128i x4 = _mm_loadu_si128((__m128i*)(buf2 + 48));
    u32 acc_b = 0;
    u32 acc_c = 0;
    // Parallel fold remaining blocks of 64 from D, and 24 from each of A/B/C.
    // k1 == magic(4*128+32-1)
    // k2 == magic(4*128-32-1)
    __m128i k1k2 = _mm_setr_epi32(/*k1*/ 0x740EEF02, 0, /*k2*/ 0x9E4ADDF8, 0);
    const char* end = buf + (n_blocks * 136 + 16) - 64;
    while (buf2 < end) {
        acc_a = _mm_crc32_u64(acc_a, *(uint64_t*)buf);
        __m128i x5 = _mm_clmulepi64_si128(x1, k1k2, 0x00);
        acc_b = _mm_crc32_u64(acc_b, *(uint64_t*)(buf + stride));
        x1 = _mm_clmulepi64_si128(x1, k1k2, 0x11);
        acc_c = _mm_crc32_u64(acc_c, *(uint64_t*)(buf + stride*2));
        __m128i x6 = _mm_clmulepi64_si128(x2, k1k2, 0x00);
        acc_a = _mm_crc32_u64(acc_a, *(uint64_t*)(buf + 8));
        x2 = _mm_clmulepi64_si128(x2, k1k2, 0x11);
        acc_b = _mm_crc32_u64(acc_b, *(uint64_t*)(buf + stride + 8));
        __m128i x7 = _mm_clmulepi64_si128(x3, k1k2, 0x00);
        acc_c = _mm_crc32_u64(acc_c, *(uint64_t*)(buf + stride*2 + 8));
        x3 = _mm_clmulepi64_si128(x3, k1k2, 0x11);
        acc_a = _mm_crc32_u64(acc_a, *(uint64_t*)(buf + 16));
        __m128i x8 = _mm_clmulepi64_si128(x4, k1k2, 0x00);
        acc_b = _mm_crc32_u64(acc_b, *(uint64_t*)(buf + stride + 16));
        x4 = _mm_clmulepi64_si128(x4, k1k2, 0x11);
        acc_c = _mm_crc32_u64(acc_c, *(uint64_t*)(buf + stride*2 + 16));
        x5 = _mm_xor_si128(x5, _mm_loadu_si128((__m128i*)(buf2 + 64)));
        x1 = _mm_xor_si128(x1, x5);
        x6 = _mm_xor_si128(x6, _mm_loadu_si128((__m128i*)(buf2 + 80)));
        x2 = _mm_xor_si128(x2, x6);
        x7 = _mm_xor_si128(x7, _mm_loadu_si128((__m128i*)(buf2 + 96)));
        x3 = _mm_xor_si128(x3, x7);
        x8 = _mm_xor_si128(x8, _mm_loadu_si128((__m128i*)(buf2 + 112)));
        x4 = _mm_xor_si128(x4, x8);
        buf2 += 64;
        buf += 24;
    }
    // Next 24 bytes from A/B/C, and 8 more from A/B, then merge A/B/C.
    // Meanwhile, fold together D's four parallel streams.
    // k3 == magic(128+32-1)
    // k4 == magic(128-32-1)
    __m128i k3k4 = _mm_setr_epi32(/*k3*/ 0xF20C0DFE, 0, /*k4*/ 0x493C7D27, 0);
    acc_a = _mm_crc32_u64(acc_a, *(uint64_t*)buf);
    __m128i x5 = _mm_clmulepi64_si128(x1, k3k4, 0x00);
    acc_b = _mm_crc32_u64(acc_b, *(uint64_t*)(buf + stride));
    x1 = _mm_clmulepi64_si128(x1, k3k4, 0x11);
    acc_c = _mm_crc32_u64(acc_c, *(uint64_t*)(buf + stride*2));
    __m128i x6 = _mm_clmulepi64_si128(x3, k3k4, 0x00);
    acc_a = _mm_crc32_u64(acc_a, *(uint64_t*)(buf + 8));
    x3 = _mm_clmulepi64_si128(x3, k3k4, 0x11);
    acc_b = _mm_crc32_u64(acc_b, *(uint64_t*)(buf + stride + 8));
    acc_c = _mm_crc32_u64(acc_c, *(uint64_t*)(buf + stride*2 + 8));
    acc_a = _mm_crc32_u64(acc_a, *(uint64_t*)(buf + 16));
    acc_b = _mm_crc32_u64(acc_b, *(uint64_t*)(buf + stride + 16));
    x5 = _mm_xor_si128(x5, x2);
    acc_c = _mm_crc32_u64(acc_c, *(uint64_t*)(buf + stride*2 + 16));
    x1 = _mm_xor_si128(x1, x5);
    acc_a = _mm_crc32_u64(acc_a, *(uint64_t*)(buf + 24));
    // k5 == magic(2*128+32-1)
    // k6 == magic(2*128-32-1)
    __m128i k5k6 = _mm_setr_epi32(/*k5*/ 0x3DA6D0CB, 0, /*k6*/ 0xBA4FC28E, 0);
    x6 = _mm_xor_si128(x6, x4);
    x3 = _mm_xor_si128(x3, x6);
    x5 = _mm_clmulepi64_si128(x1, k5k6, 0x00);
    acc_b = _mm_crc32_u64(acc_b, *(uint64_t*)(buf + stride + 24));
    x1 = _mm_clmulepi64_si128(x1, k5k6, 0x11);

    // Compute the magic numbers which depend upon n_blocks
    // (required for merging A/B/C/D)
    uint64_t bits_c = n_blocks*64 - 33;
    uint64_t bits_b = bits_c + stride - 8;
    uint64_t bits_a = bits_b + stride;
    uint64_t stack_a = ~(uint64_t)8;
    uint64_t stack_b = stack_a;
    uint64_t stack_c = stack_a;
    while (bits_a > 191) {
        stack_a = (stack_a << 1) + (bits_a & 1); bits_a = (bits_a >> 1) - 16;
        stack_b = (stack_b << 1) + (bits_b & 1); bits_b = (bits_b >> 1) - 16;
        stack_c = (stack_c << 1) + (bits_c & 1); bits_c = (bits_c >> 1) - 16;
    }
    stack_a = ~stack_a;
    stack_b = ~stack_b;
    stack_c = ~stack_c;
    u32 magic_a = ((u32)0x80000000) >> (bits_a & 31); bits_a >>= 5;
    u32 magic_b = ((u32)0x80000000) >> (bits_b & 31); bits_b >>= 5;
    u32 magic_c = ((u32)0x80000000) >> (bits_c & 31); bits_c >>= 5;
    bits_a -= bits_b;
    bits_b -= bits_c;
    for (; bits_c; --bits_c) magic_a = _mm_crc32_u32(magic_a, 0), magic_b = _mm_crc32_u32(magic_b, 0), magic_c = _mm_crc32_u32(magic_c, 0);
    for (; bits_b; --bits_b) magic_a = _mm_crc32_u32(magic_a, 0), magic_b = _mm_crc32_u32(magic_b, 0);
    for (; bits_a; --bits_a) magic_a = _mm_crc32_u32(magic_a, 0);
    for (;;) {
        u32 low = stack_a & 1;
        if (!(stack_a >>= 1)) break;
        __m128i x = _mm_cvtsi32_si128(magic_a);
        uint64_t y = _mm_cvtsi128_si64(_mm_clmulepi64_si128(x, x, 0));
        magic_a = _mm_crc32_u64(0, y << low);
        x = _mm_cvtsi32_si128(magic_c);
        y = _mm_cvtsi128_si64(_mm_clmulepi64_si128(x, x, 0));
        magic_c = _mm_crc32_u64(0, y << (stack_c & 1));
        stack_c >>= 1;
        x = _mm_cvtsi32_si128(magic_b);
        y = _mm_cvtsi128_si64(_mm_clmulepi64_si128(x, x, 0));
        magic_b = _mm_crc32_u64(0, y << (stack_b & 1));
        stack_b >>= 1;
    }
    __m128i vec_c = _mm_clmulepi64_si128(_mm_cvtsi32_si128(acc_c), _mm_cvtsi32_si128(magic_c), 0x00);
    __m128i vec_a = _mm_clmulepi64_si128(_mm_cvtsi32_si128(acc_a), _mm_cvtsi32_si128(magic_a), 0x00);
    __m128i vec_b = _mm_clmulepi64_si128(_mm_cvtsi32_si128(acc_b), _mm_cvtsi32_si128(magic_b), 0x00);
    x5 = _mm_xor_si128(x5, x3);
    x1 = _mm_xor_si128(x1, x5);
    uint64_t abc = _mm_cvtsi128_si64(_mm_xor_si128(_mm_xor_si128(vec_c, vec_a), vec_b));
    // Apply missing <<32 and fold down to 32-bits.
    u32 crc = _mm_crc32_u64(0, _mm_extract_epi64(x1, 0));
    crc = _mm_crc32_u64(crc, abc ^ _mm_extract_epi64(x1, 1));
    return crc;
}

#ifdef __clang__
__attribute__((no_sanitize("integer")))
#endif
#ifdef __KERNEL__
__attribute__((target("crc32")))
#endif
u32 CRC32C_NAME(crc32c)(u32 crc, const char* buf, size_t length) {
    crc = ~crc; // preset to -1 (distinguish leading zeros)
    if (length >= 31) {
        size_t n_blocks = (length - 16) / 136;
        size_t kernel_length = n_blocks * 136 + 16;
        if (kernel_length + (-((uintptr_t)buf + n_blocks * 8) & 15) > length) {
            n_blocks -= 1;
            kernel_length -= 136;
        }
        const char* kernel_end = (const char*)((uintptr_t)(buf + kernel_length + 15) & ~(uintptr_t)15);
        const char* kernel_start = kernel_end - kernel_length;
        length -= kernel_start - buf;
        for (; buf != kernel_start; ++buf) {
            crc = _mm_crc32_u8(crc, *(const u8*)buf);
        }
        if (n_blocks) {
            length -= kernel_length;
            crc = crc32c_4k_fusion(crc, buf, n_blocks);
            buf = kernel_end;
        }
    }
    for (; length >= 8; length -= 8, buf += 8) {
        crc = _mm_crc32_u64(crc, *(const uint64_t*)buf);
    }
    for (; length; --length, ++buf) {
        crc = _mm_crc32_u8(crc, *(const u8*)buf);
    }
    return ~crc; // post-invert -- distinguish scaled multiples
}

#else

#ifdef __clang__
__attribute__((no_sanitize("integer")))
#endif
#ifdef __KERNEL__
__attribute__((target("crc32")))
#endif
u32 CRC32C_NAME(crc32c)(u32 crc, const char* buf, size_t length) {
    crc = ~crc; // preset to -1 (distinguish leading zeros)
    for (; length >= 8; length -= 8, buf += 8) {
        crc = _mm_crc32_u64(crc, *(const uint64_t*)buf);
    }
    for (; length; --length, ++buf) {
        crc = _mm_crc32_u8(crc, *(const u8*)buf);
    }
    return ~crc; // post-invert -- distinguish scaled multiples
}

#endif

#define CASTAGNOLI_POLY 0x82F63B78u

#if CRC32C_USE_PCLMUL

#ifdef __clang__
__attribute__((no_sanitize("integer")))
#endif
#ifdef __KERNEL__
__attribute__((target("pclmul")))
#endif
static u64 crc32c_mul(u32 a, u32 b) {
  uint64_t c = _mm_cvtsi128_si64(
    _mm_clmulepi64_si128(_mm_set_epi32(0, 0, 0, a), _mm_set_epi32(0, 0, 0, b), 0)
  );
  return c << 1; // unused bit
}

#else

#ifdef __clang__
__attribute__((no_sanitize("integer")))
#endif
u64 crc32c_mul(u32 a, u32 b32) {
    u64 b = (u64)b32 << 32;
    u64 c = 0;
    int i;
    for (i = 0; i < 32; i++, a <<= 1, b >>= 1) {
        c ^= (a & (1u<<31)) ? b : 0;
    }
    return c;
}

#endif

// `a mod CASTAGNOLI_POLY`
#ifdef __clang__
__attribute__((no_sanitize("integer")))
#endif
#ifdef __KERNEL__
__attribute__((target("crc32")))
#endif
static u32 crc32c_mod_p(u64 a) {
  return _mm_crc32_u32(0, a) ^ (a >> 32);
}

static u32 crc32c_mul_mod_p(u32 a, u32 b) {
  return crc32c_mod_p(crc32c_mul(a, b));
}

// x^2^3, x^2^4, ..., x^2^63
static u32 CRC_POWER_TABLE[64] = {
  0x00800000, 0x00008000, 0x82f63b78, 0x6ea2d55c, 0x18b8ea18, 0x510ac59a, 0xb82be955, 0xb8fdb1e7, 
  0x88e56f72, 0x74c360a4, 0xe4172b16, 0x0d65762a, 0x35d73a62, 0x28461564, 0xbf455269, 0xe2ea32dc, 
  0xfe7740e6, 0xf946610b, 0x3c204f8f, 0x538586e3, 0x59726915, 0x734d5309, 0xbc1ac763, 0x7d0722cc, 
  0xd289cabe, 0xe94ca9bc, 0x05b74f3f, 0xa51e1f42, 0x40000000, 0x20000000, 0x08000000, 0x00800000, 
  0x00008000, 0x82f63b78, 0x6ea2d55c, 0x18b8ea18, 0x510ac59a, 0xb82be955, 0xb8fdb1e7, 0x88e56f72, 
  0x74c360a4, 0xe4172b16, 0x0d65762a, 0x35d73a62, 0x28461564, 0xbf455269, 0xe2ea32dc, 0xfe7740e6, 
  0xf946610b, 0x3c204f8f, 0x538586e3, 0x59726915, 0x734d5309, 0xbc1ac763, 0x7d0722cc, 0xd289cabe, 
  0xe94ca9bc, 0x05b74f3f, 0xa51e1f42, 0x40000000, 0x20000000, 0x08000000, 0x00800000, 0x00008000, 
};

static u32 crc32c_x_pow_n(size_t n) {
  u32 x_pow_n = 1u << 31;
  int k;
  for (k = 0; n; k++, n >>= 1) {
    if (n&1) {
      x_pow_n = crc32c_mul_mod_p(x_pow_n, CRC_POWER_TABLE[k]);
    }
  }
  return x_pow_n;
}

// x^-(2^3), x^-(2^4), ..., x^-(2^63)
static u32 CRC_INVERSE_POWER_TABLE[64] = {
  0xfde39562, 0xbef0965e, 0xd610d67e, 0xe67cce65, 0xa268b79e, 0x134fb088, 0x32998d96, 0xcedac2cc, 
  0x70118575, 0x0e004a40, 0xa7864c8b, 0xbc7be916, 0x10ba2894, 0x6077197b, 0x98448e4e, 0x8baf845d, 
  0xe93e07fc, 0xf58027d7, 0x5e2b422d, 0x9db2851c, 0x9270ed25, 0x5984e7b3, 0x7af026f1, 0xe0f4116b, 
  0xace8a6b0, 0x9e09f006, 0x6a60ea71, 0x4fd04875, 0x05ec76f1, 0x0bd8ede2, 0x2f63b788, 0xfde39562, 
  0xbef0965e, 0xd610d67e, 0xe67cce65, 0xa268b79e, 0x134fb088, 0x32998d96, 0xcedac2cc, 0x70118575, 
  0x0e004a40, 0xa7864c8b, 0xbc7be916, 0x10ba2894, 0x6077197b, 0x98448e4e, 0x8baf845d, 0xe93e07fc, 
  0xf58027d7, 0x5e2b422d, 0x9db2851c, 0x9270ed25, 0x5984e7b3, 0x7af026f1, 0xe0f4116b, 0xace8a6b0, 
  0x9e09f006, 0x6a60ea71, 0x4fd04875, 0x05ec76f1, 0x0bd8ede2, 0x2f63b788, 0xfde39562, 0xbef0965e, 
};

static u32 crc32c_x_pow_neg_n(size_t n) {
  u32 x_pow_n = 1u << 31;
  int k;
  for (k = 0; n; k++, n >>= 1) {
    if (n&1) {
      x_pow_n = crc32c_mul_mod_p(x_pow_n, CRC_INVERSE_POWER_TABLE[k]);
    }
  }
  return x_pow_n;
}

u32 CRC32C_NAME(crc32c_zero_extend)(u32 crc, ssize_t zeros) {
    if (zeros > 0) {
        return ~crc32c_mul_mod_p(~crc, crc32c_x_pow_n(zeros));
    } else {
        return ~crc32c_mul_mod_p(~crc, crc32c_x_pow_neg_n(-zeros));
    }
}

u32 CRC32C_NAME(crc32c_append)(u32 crc_a, u32 crc_b, size_t len_b) {
    return crc32c_mul_mod_p(crc_a, crc32c_x_pow_n(len_b)) ^ crc_b;
}

u32 CRC32C_NAME(crc32c_xor)(u32 crc_a, u32 crc_b, size_t len) {
    return crc_a ^ crc_b ^ ~crc32c_mul_mod_p(~(u32)0, crc32c_x_pow_n(len));
}