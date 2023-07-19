// wyhash is chosen because it has 64-bit state (easy to bind to go),
// and it only has one addition in the dependency chain.
//
// Actual code from <https://github.com/lemire/testingRNG/blob/450eea89646c056415686e3b8b58c662a3cdc2d2/source/wyhash.h>.
#ifndef EGGS_WYHASH
#define EGGS_WYHASH

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

__attribute__((unused))
#ifdef __clang__
__attribute__((no_sanitize("integer")))
#endif
inline uint64_t wyhash64(uint64_t* state) {
    *state += UINT64_C(0x60bee2bee120fc15);
    __uint128_t tmp;
    tmp = (__uint128_t)*state * UINT64_C(0xa3b195354a39b70d);
    uint64_t m1 = (tmp >> 64) ^ tmp;
    tmp = (__uint128_t)m1 * UINT64_C(0x1b03738712fad5c9);
    uint64_t m2 = (tmp >> 64) ^ tmp;
    return m2;
}

__attribute__((unused))
static void wyhash64_bytes(uint64_t* state, uint8_t* bytes, size_t len) {
    uint8_t* end = bytes+len;
    uint8_t* unaligned_end = (uint8_t*)(((uintptr_t)bytes - 1 + 8) & ~7u);
    for (; bytes < unaligned_end; bytes++) {
        *bytes = wyhash64(state) & 0xFF;
    }
    uint64_t* words = (uint64_t*)bytes;
    for (; (uint8_t*)(words + 1) <= end; words++) {
        *words = wyhash64(state);
    }
    for (bytes = (uint8_t*)words; bytes < end; bytes++) {
        *bytes = wyhash64(state) & 0xFF;
    }
}

#ifdef __cplusplus
}
#endif

#ifdef __cplusplus
struct wyhash64_gen {
private:
    uint64_t _s;
public:
    wyhash64_gen(uint64_t s): _s(s) {}

    using result_type = uint64_t;
    static constexpr uint64_t min() { return 0; }
    static constexpr uint64_t max() { return ~(uint64_t)0; }

    uint64_t operator()() { return wyhash64(&_s); }
};
#endif

#endif