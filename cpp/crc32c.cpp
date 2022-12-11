#include "crc32c.hpp"

#include <immintrin.h>
#include <stdint.h>
#include <string.h>
#include <bit>

static uint32_t crc32_fusion_kernel(uint32_t acc_a, const char* buf, size_t n_blocks) {
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
    uint32_t acc_b = 0;
    uint32_t acc_c = 0;
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
    uint32_t magic_a = ((uint32_t)0x80000000) >> (bits_a & 31); bits_a >>= 5;
    uint32_t magic_b = ((uint32_t)0x80000000) >> (bits_b & 31); bits_b >>= 5;
    uint32_t magic_c = ((uint32_t)0x80000000) >> (bits_c & 31); bits_c >>= 5;
    bits_a -= bits_b;
    bits_b -= bits_c;
    for (; bits_c; --bits_c) magic_a = _mm_crc32_u32(magic_a, 0), magic_b = _mm_crc32_u32(magic_b, 0), magic_c = _mm_crc32_u32(magic_c, 0);
    for (; bits_b; --bits_b) magic_a = _mm_crc32_u32(magic_a, 0), magic_b = _mm_crc32_u32(magic_b, 0);
    for (; bits_a; --bits_a) magic_a = _mm_crc32_u32(magic_a, 0);
    for (;;) {
        uint32_t low = stack_a & 1;
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
    uint32_t crc = _mm_crc32_u64(0, _mm_extract_epi64(x1, 0));
    crc = _mm_crc32_u64(crc, abc ^ _mm_extract_epi64(x1, 1));
    return crc;
}

// TODO due to the `-((uintptr_t)buf + n_blocks * 8)`, needs investigation
__attribute__((no_sanitize("integer")))
static uint32_t crc32_fusion(uint32_t crc, const char* buf, size_t length) {
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
            crc = _mm_crc32_u8(crc, *(const uint8_t*)buf);
        }
        if (n_blocks) {
            length -= kernel_length;
            crc = crc32_fusion_kernel(crc, buf, n_blocks);
            buf = kernel_end;
        }
    }
    for (; length >= 8; length -= 8, buf += 8) {
        crc = _mm_crc32_u64(crc, *(const uint64_t*)buf);
    }
    for (; length; --length, ++buf) {
        crc = _mm_crc32_u8(crc, *(const uint8_t*)buf);
    }
    return crc;
}

std::array<uint8_t, 4> crc32c(const char* buf, size_t len) {
    static_assert(std::endian::native == std::endian::little);
    uint32_t crc = 0xffffffff;
    crc = crc32_fusion(crc, buf, len);
    crc ^= 0xffffffff;
    std::array<uint8_t, 4> res;
    memcpy(&res[0], &crc, 4);
    return res;
}

constexpr uint32_t ISCSI_POLY = 0x82F63B78u;

static uint32_t mult_mod_p(uint32_t x, uint32_t y) {
    uint32_t m = 1u << 31;
    uint32_t p = 0;
    for (;;) {
        if (x & m) {
            p ^= y;
            if ((x & (m - 1)) == 0) {
                break;
            }
        }
        m >>= 1;
        y = (y & 1) ? ((y >> 1) ^ ISCSI_POLY) : y >> 1;
    }
    return p;
}

static uint32_t CRC_POWER_TABLE[31] = {0x40000000U, 0x20000000U, 0x8000000U, 0x800000U, 0x8000U, 0x82f63b78U, 0x6ea2d55cU, 0x18b8ea18U, 0x510ac59aU, 0xb82be955U, 0xb8fdb1e7U, 0x88e56f72U, 0x74c360a4U, 0xe4172b16U, 0xd65762aU, 0x35d73a62U, 0x28461564U, 0xbf455269U, 0xe2ea32dcU, 0xfe7740e6U, 0xf946610bU, 0x3c204f8fU, 0x538586e3U, 0x59726915U, 0x734d5309U, 0xbc1ac763U, 0x7d0722ccU, 0xd289cabeU, 0xe94ca9bcU, 0x5b74f3fU, 0xa51e1f42U};

static uint32_t x2n_mod_p(uint32_t n, uint32_t k) {
    // POWER_TABLE has all powers of two
    // can multiply combinations of these to achieve any power
    uint32_t p = 1u << 31;
    while (n) {
        if (n & 1) {
            p = mult_mod_p(CRC_POWER_TABLE[k % 31], p);
        }
        n >>= 1;
        k += 1;
    }
    return p;
}

std::array<uint8_t, 4> crc32cCombine(std::array<uint8_t, 4> crcAarr, std::array<uint8_t, 4> crcBarr, size_t lenB) {
    static_assert(std::endian::native == std::endian::little);
    uint32_t crcA; memcpy(&crcA, &crcAarr[0], 4);
    uint32_t crcB; memcpy(&crcB, &crcBarr[0], 4);
    uint32_t crc = mult_mod_p(x2n_mod_p(lenB, 3), crcA) ^ crcB;
    std::array<uint8_t, 4> crcArr;
    memcpy(&crcArr[0], &crc, 4);
    return crcArr;
}