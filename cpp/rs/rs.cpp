#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <signal.h>
#include <immintrin.h>
#include <array>

#include "rs.h"
#include "gf.hpp"

#define die(...) do { fprintf(stderr, __VA_ARGS__); raise(SIGABRT); } while(false)

static void* malloc_or_die(size_t size, const char* what) {
    void* ptr = malloc(size);
    if (ptr == nullptr) {
        die(what);
    }
    return ptr;
}

struct rs {
    uint8_t parity;
    // uint8_t[D*B], in column-major.
    uint8_t* matrix;
    // uint8_t[D*P][32], in column-major. These are the lookup tables
    // to perform multiplication quickly.
    uint8_t* expanded_matrix;
};

static struct rs* rs_cached[256] = {
    nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
    nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
    nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
    nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
    nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
    nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
    nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
    nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
    nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
    nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
    nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
    nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
    nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
    nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
    nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
    nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
};

// Note that we pervasively assume that the first column of the parity columns
// is 1s, which causes the first parity to be the XORs of the data. So you can't
// really change how the matrix is generated.
static void rs_cauchy_matrix(struct rs* r) {
    int D = rs_data_blocks(r->parity);
    int B = rs_blocks(r->parity);
    uint8_t* matrix = r->matrix;
    memset(matrix, 0, D*B);
    // Identity in the d*d upper half
    for (int i = 0; i < D; i++) {
        matrix[D*i + i] = 1;
    }
    // Fill in the rest using cauchy
    for (int col = D; col < B; col++) {
        for (int row = 0; row < D; row++) {
            matrix[col*D + row] = gf_inv(col ^ row);
        }
    }
    // Scale the columns
    for (int col = D; col < B; col++) {
        uint8_t factor = gf_inv(matrix[col*D]);
        for (int row = 0; row < D; row++) {
            matrix[col*D + row] = gf_mul(matrix[col*D + row], factor);
        }
    }
    // Scale the rows
    for (int row = 1; row < D; row++) {
        uint8_t factor = gf_inv(matrix[D*D + row]);
        for (int col = D; col < B; col++) {
            matrix[col*D + row] = gf_mul(matrix[col*D + row], factor);
        }
    }
}

static struct rs* rs_new(uint8_t parity) {
    int B = rs_blocks(parity);
    int D = rs_data_blocks(parity);
    int P = rs_parity_blocks(parity);
    struct rs* r = (struct rs*)malloc_or_die(sizeof(struct rs) + B*D + D*P*32, "rs_new\n");
    r->parity = parity;
    r->matrix = (uint8_t*)(r + 1);
    r->expanded_matrix = r->matrix + B*D;
    rs_cauchy_matrix(r);
    for (int p = 0; p < P; p++) {
        for (int d = 0; d < D; d++) {
            gf_mul_expand_factor(r->matrix[D*D + D*p + d], &r->expanded_matrix[D*32*p + 32*d]);
        }
    }
    return r;
}

static void rs_delete(struct rs* r) {
    free(r);
}

static std::array<uint32_t, 4> rs_cpuidex(uint32_t function_id, uint32_t subfunction_id) {
    uint32_t a, b, c, d;
    __asm("cpuid":"=a"(a),"=b"(b),"=c"(c),"=d"(d):"0"(function_id),"2"(subfunction_id));
    return {a, b, c, d};
}

static uint8_t rs_chosen_cpu_level = RS_CPU_SCALAR;

// See `valgrind.h`
static uint64_t rs_valgrind_client_request(uint64_t defaultResult, uint64_t reqID, uint64_t arg1, uint64_t arg2, uint64_t arg3, uint64_t arg4, uint64_t arg5) {
    uint64_t args[6] = {reqID, arg1, arg2, arg3, arg4, arg5};
    uint64_t result = defaultResult;
    asm volatile ("rol $3, %%rdi\n\t"
                  "rol $13, %%rdi\n\t"
                  "rol $61, %%rdi\n\t"
                  "rol $51, %%rdi\n\t"
                  "xchg %%rbx, %%rbx\n\t"
                  : "+d" (result)
                  : "a" (args)
                  : "cc", "memory"
                  );
    return result;
}

static bool rs_detect_valgrind() {
    return rs_valgrind_client_request(0, 0x1001, 0, 0, 0, 0, 0);
}

bool rs_has_cpu_level(rs_cpu_level level) {
    const auto cpuid7 = (rs_cpuidex(0, 0)[0] >= 7) ? rs_cpuidex(7, 0) : std::array<uint32_t, 4>{0, 0, 0, 0};
    switch (level) {
    case RS_CPU_SCALAR:
        return true;
    case RS_CPU_AVX2:
        return cpuid7[1] & (1<<5);
    case RS_CPU_GFNI:
        return cpuid7[2] & (1<<8) && !rs_detect_valgrind();
    default:
        die("bad CPU level %d\n", level);
    }
}

__attribute__((constructor))
void rs_detect_cpu_level() {
    if (rs_has_cpu_level(RS_CPU_GFNI)) {
        rs_chosen_cpu_level = RS_CPU_GFNI;
        return;
    }
    if (rs_has_cpu_level(RS_CPU_AVX2)) {
        rs_chosen_cpu_level = RS_CPU_AVX2;
        return;
    }
    fprintf(stderr, "Picking scalar execution for RS library -- this will be slow.\n");
    rs_chosen_cpu_level = RS_CPU_SCALAR;
    return;
}

void rs_set_cpu_level(rs_cpu_level level) {
    __atomic_store_n(&rs_chosen_cpu_level, level, __ATOMIC_RELAXED);
}

rs_cpu_level rs_get_cpu_level() {
    return (rs_cpu_level)__atomic_load_n(&rs_chosen_cpu_level, __ATOMIC_RELAXED);
}

struct rs* rs_get(uint8_t parity) {
    if (rs_data_blocks(parity) < 2 || rs_parity_blocks(parity) < 1) {
        die("bad parity (%d,%d), expected at least 2 data blocks and 1 parity block.\n", rs_data_blocks(parity), rs_parity_blocks(parity));
    }
    struct rs* r = __atomic_load_n(&rs_cached[parity], __ATOMIC_RELAXED);
    if (__builtin_expect(r == nullptr, 0)) {
        r = rs_new(parity);
        struct rs* expected = nullptr;
        if (!__atomic_compare_exchange_n(&rs_cached[parity], &expected, r, false, __ATOMIC_RELAXED, __ATOMIC_RELAXED)) {
            // somebody else got to it first
            rs_delete(r);
            r = __atomic_load_n(&rs_cached[parity], __ATOMIC_RELAXED);
        }
    }
    return r;
}

uint8_t rs_parity(struct rs* r) {
    return r->parity;
}

// This will emit vbroadcastb
__attribute__((no_sanitize("integer")))
inline __m256i broadcast_u8(uint8_t x) {
    return _mm256_set_epi8(
        x, x, x, x, x, x, x, x,
        x, x, x, x, x, x, x, x,
        x, x, x, x, x, x, x, x,
        x, x, x, x, x, x, x, x
    );
}

template<int D, int P>
static void rs_compute_parity_single(struct rs* r, uint64_t i, const uint8_t** data, uint8_t** parity) {
    parity[0][i] = 0;
    for (int d = 0; d < D; d++) {
        parity[0][i] ^= data[d][i];
    }
    for (int p = 1; p < P; p++) {
        const uint8_t* factor = &r->expanded_matrix[D*32*p];
        parity[p][i] = 0;
        for (int d = 0; d < D; d++, factor += 32) {
            parity[p][i] ^= gf_mul_expanded(data[d][i], factor);
        }
    }
}

template<int D, int P>
__attribute__((noinline))
void rs_compute_parity_scalar(struct rs* r, uint64_t size, const uint8_t** data, uint8_t** parity) {
    // parity = r->matrix * data
    for (uint64_t i = 0; i < size; i++) {
        rs_compute_parity_single<D, P>(r, i, data, parity);
    }
}

template<int D, int P>
__attribute__((noinline))
void rs_compute_parity_avx2(struct rs* r, uint64_t size, const uint8_t** data, uint8_t** parity) {
    __m256i low_nibble_mask = broadcast_u8(0x0f);
    size_t avx_leftover = size % 32;
    size_t avx_size = size-avx_leftover;
    for (uint64_t i = 0; i < avx_size; i += 32) {
        {
            __m256i parity_0 = _mm256_setzero_si256();
            for (int d = 0; d < D; d++) {
                parity_0 = _mm256_xor_si256(parity_0, _mm256_loadu_si256((const __m256i*)(data[d] + i)));
            }
            _mm256_storeu_si256((__m256i*)(parity[0] + i), parity_0);
        }
        for (int p = 1; p < P; p++) {
            __m256i parity_p = _mm256_setzero_si256();
            for (int d = 0; d < D; d++) {
                __m256i data_d = _mm256_loadu_si256((const __m256i*)(data[d] + i));
                __m256i factor = _mm256_loadu_si256((const __m256i*)&r->expanded_matrix[D*p*32 + 32*d]);
                parity_p = _mm256_xor_si256(parity_p, gf_mul_expanded_avx2(data_d, factor, low_nibble_mask));
            }
            _mm256_storeu_si256((__m256i*)(parity[p] + i), parity_p);
        }
    }
    for (uint64_t i = avx_size; i < size; i++) {
        rs_compute_parity_single<D, P>(r, i, data, parity);
    }
}

template<int D, int P>
__attribute__((noinline))
void rs_compute_parity_gfni(struct rs* r, uint64_t size, const uint8_t** data, uint8_t** parity) {
    size_t avx_leftover = size % 32;
    size_t avx_size = size-avx_leftover;
    for (uint64_t i = 0; i < avx_size; i += 32) {
        {
            __m256i parity_0 = _mm256_setzero_si256();
            for (int d = 0; d < D; d++) {
                parity_0 = _mm256_xor_si256(parity_0, _mm256_loadu_si256((const __m256i*)(data[d] + i)));
            }
            _mm256_storeu_si256((__m256i*)(parity[0] + i), parity_0);
        }
        for (int p = 1; p < P; p++) {
            __m256i parity_p = _mm256_setzero_si256();
            for (int d = 0; d < D; d++) {
                __m256i data_d = _mm256_loadu_si256((const __m256i*)(data[d] + i));
                __m256i factor = broadcast_u8(r->matrix[D*D + D*p + d]);
                parity_p = _mm256_xor_si256(parity_p, _mm256_gf2p8mul_epi8(data_d, factor));
            }
            _mm256_storeu_si256((__m256i*)(parity[p] + i), parity_p);
        }
    }
    for (uint64_t i = avx_size; i < size; i++) {
        rs_compute_parity_single<D, P>(r, i, data, parity);
    }
}

template<int D, int P>
static void rs_compute_parity_tmpl(struct rs* r, uint64_t size, const uint8_t** data, uint8_t** parity) {
    switch (rs_cpu_level l = rs_get_cpu_level()) {
    case RS_CPU_SCALAR:
        rs_compute_parity_scalar<D, P>(r, size, data, parity);
        break;
    case RS_CPU_AVX2:
        rs_compute_parity_avx2<D, P>(r, size, data, parity);
        break;
    case RS_CPU_GFNI:
        rs_compute_parity_gfni<D, P>(r, size, data, parity);
        break;
    default:
        die("bad cpu_level %d\n", l);
    }
}

static void (*rs_compute_parity_funcs[256])(struct rs* r, uint64_t size, const uint8_t** data, uint8_t** parity);
void rs_compute_parity(struct rs* r, uint64_t size, const uint8_t** data, uint8_t** parity) {
    rs_compute_parity_funcs[r->parity](r, size, data, parity);
}

template<int D>
static void rs_recover_matmul_single(uint64_t i, const uint8_t** have, uint8_t* want, const uint8_t* have_to_want) {
    want[i] = 0;
    for (int j = 0; j < D; j++) {
        want[i] ^= gf_mul(have_to_want[j], have[j][i]);
    }
}

template<int D>
static void rs_recover_matmul_single_expanded(uint64_t i, const uint8_t** have, uint8_t* want, const uint8_t* have_to_want_expanded) {
    want[i] = 0;
    for (int j = 0; j < D; j++) {
        want[i] ^= gf_mul_expanded(have[j][i], &have_to_want_expanded[j*32]);
    }
}

template<int D>
__attribute__((noinline))
static void rs_recover_matmul_scalar(uint64_t size, const uint8_t** have, uint8_t* want, const uint8_t* have_to_want) {
    uint8_t have_to_want_expanded[D*32];
    for (int i = 0; i < D; i++) {
        gf_mul_expand_factor(have_to_want[i], &have_to_want_expanded[i*32]);
    }
    for (size_t i = 0; i < size; i++) {
        rs_recover_matmul_single_expanded<D>(i, have, want, have_to_want_expanded);
    }
}

template<int D>
__attribute__((noinline))
static void rs_recover_matmul_avx2(uint64_t size, const uint8_t** have, uint8_t* want, const uint8_t* have_to_want) {
    __m256i have_to_want_expanded[D];
    for (int i = 0; i < D; i++) {
        gf_mul_expand_factor(have_to_want[i], (uint8_t*)&have_to_want_expanded[i]);
    }
    __m256i low_nibble_mask = broadcast_u8(0x0f);
    size_t avx_leftover = size % 32;
    size_t avx_size = size-avx_leftover;
    for (uint64_t i = 0; i < avx_size; i += 32) {
        __m256i want_i = _mm256_setzero_si256();
        for (int d = 0; d < D; d++) {
            want_i = _mm256_xor_si256(
                want_i,
                gf_mul_expanded_avx2(
                    _mm256_loadu_si256((const __m256i*)(have[d] + i)),
                    have_to_want_expanded[d],
                    low_nibble_mask
                )
            );
        }
        _mm256_storeu_si256((__m256i*)(want + i), want_i);
    }
    for (uint64_t i = avx_size; i < size; i++) {
        rs_recover_matmul_single_expanded<D>(i, have, want, (const uint8_t*)have_to_want_expanded);
    }
}

template<int D>
__attribute__((noinline))
static void rs_recover_matmul_gfni(uint64_t size, const uint8_t** have, uint8_t* want, const uint8_t* have_to_want) {
    __m256i have_to_want_avx[D];
    for (int i = 0; i < D; i++) {
        have_to_want_avx[i] = broadcast_u8(have_to_want[i]);
    }
    size_t avx_leftover = size % 32;
    size_t avx_size = size-avx_leftover;
    for (uint64_t i = 0; i < avx_size; i += 32) {
        __m256i want_i = _mm256_setzero_si256();
        for (int d = 0; d < D; d++) {
            want_i = _mm256_xor_si256(
                want_i,
                _mm256_gf2p8mul_epi8(
                    _mm256_loadu_si256((const __m256i*)(have[d] + i)),
                    have_to_want_avx[d]
                )
            );
        }
        _mm256_storeu_si256((__m256i*)(want + i), want_i);
    }
    for (uint64_t i = avx_size; i < size; i++) {
        rs_recover_matmul_single<D>(i, have, want, have_to_want);
    }
}

template<int D>
static void rs_recover_matmul_tmpl(uint64_t size, const uint8_t** have, uint8_t* want, const uint8_t* mat) {
    switch (rs_cpu_level l = rs_get_cpu_level()) {
    case RS_CPU_SCALAR:
        rs_recover_matmul_scalar<D>(size, have, want, mat);
        break;
    case RS_CPU_AVX2:
        rs_recover_matmul_avx2<D>(size, have, want, mat);
        break;
    case RS_CPU_GFNI:
        rs_recover_matmul_gfni<D>(size, have, want, mat);
        break;
    default:
        die("bad cpu_level %d\n", l);
    }
}

static void (*rs_recover_matmul_funcs[16])(uint64_t size, const uint8_t** have, uint8_t* want, const uint8_t* mat);

void rs_recover(
    struct rs* r,
    uint64_t size,
    const uint8_t* have_blocks,
    const uint8_t** have,
    uint8_t want_block,
    uint8_t* want
) {
    int D = rs_data_blocks(r->parity);
    int B = rs_blocks(r->parity);
    // Create some space
    uint8_t* scratch = (uint8_t*)malloc(D*D + D*D);
    uint8_t* mat_1 = scratch;
    uint8_t* mat_2 = scratch + D*D;
    // Preliminary checks
    for (int i = 0; i < D; i++) {
        if (have_blocks[i] >= B) {
            die("have_blocks[%d]=%d >= %d\n", i, have_blocks[i], B);
        }
        if (have_blocks[i] == want_block) {
            die("have_blocks[%d]=%d == want_block=%d\n", i, have_blocks[i], want_block);
        }
        if (i > 0 && have_blocks[i] <= have_blocks[i-1]) {
            die("have_blocks[%d]=%d <= have_blocks[%d-1]=%d\n", i, have_blocks[i], i, have_blocks[i-1]);
        }
    }
    // below in the dimensionality annotation we paper over transposes
    // [DxD] matrix going from the data blocks to the blocks we currently have
    uint8_t* data_to_have = mat_1;
    for (int i = 0, have_cursor = 0; i < B; i++) {
        if (have_cursor >= D || have_blocks[have_cursor] != i) {
            continue;
        }
        memcpy(data_to_have + have_cursor*D, r->matrix + i*D, D);
        have_cursor++;
    }
    // [DxD] matrix going from what we have to the original data blocks
    uint8_t* have_to_data = mat_2;
    if (!rs_gf_invert_matrix(data_to_have, have_to_data, D)) {
        die("unexpected singular matrix\n");
    }
    data_to_have = nullptr;
    // [Dx1] matrix going from the data blocks to the block we want
    uint8_t* data_to_want = &r->matrix[want_block*D];
    // have_to_want = data_to_want * have_to_data
    // [Dx1] matrix going from `blocks` to the block we're into
    uint8_t* have_to_want = mat_1;
    for (int i = 0; i < D; i++) {
        have_to_want[i] = 0;
        for (int j = 0; j < D; j++) {
            have_to_want[i] ^= gf_mul(data_to_want[j], have_to_data[j*D + i]);
        }
    }
    // want = have_to_want * have
    rs_recover_matmul_funcs[D](size, have, want, have_to_want);
    // We're done.
    free(scratch);
}

__attribute__((constructor))
static void rs_initialize_compute_parity_funcs() {
    rs_compute_parity_funcs[rs_mk_parity(0, 0)] = nullptr;
    rs_compute_parity_funcs[rs_mk_parity(0, 1)] = nullptr;
    rs_compute_parity_funcs[rs_mk_parity(0, 2)] = nullptr;
    rs_compute_parity_funcs[rs_mk_parity(0, 3)] = nullptr;
    rs_compute_parity_funcs[rs_mk_parity(0, 4)] = nullptr;
    rs_compute_parity_funcs[rs_mk_parity(0, 5)] = nullptr;
    rs_compute_parity_funcs[rs_mk_parity(0, 6)] = nullptr;
    rs_compute_parity_funcs[rs_mk_parity(0, 7)] = nullptr;
    rs_compute_parity_funcs[rs_mk_parity(0, 8)] = nullptr;
    rs_compute_parity_funcs[rs_mk_parity(0, 9)] = nullptr;
    rs_compute_parity_funcs[rs_mk_parity(0, 10)] = nullptr;
    rs_compute_parity_funcs[rs_mk_parity(0, 11)] = nullptr;
    rs_compute_parity_funcs[rs_mk_parity(0, 12)] = nullptr;
    rs_compute_parity_funcs[rs_mk_parity(0, 13)] = nullptr;
    rs_compute_parity_funcs[rs_mk_parity(0, 14)] = nullptr;
    rs_compute_parity_funcs[rs_mk_parity(0, 15)] = nullptr;
    rs_compute_parity_funcs[rs_mk_parity(1, 0)] = nullptr;
    rs_compute_parity_funcs[rs_mk_parity(1, 1)] = nullptr;
    rs_compute_parity_funcs[rs_mk_parity(1, 2)] = nullptr;
    rs_compute_parity_funcs[rs_mk_parity(1, 3)] = nullptr;
    rs_compute_parity_funcs[rs_mk_parity(1, 4)] = nullptr;
    rs_compute_parity_funcs[rs_mk_parity(1, 5)] = nullptr;
    rs_compute_parity_funcs[rs_mk_parity(1, 6)] = nullptr;
    rs_compute_parity_funcs[rs_mk_parity(1, 7)] = nullptr;
    rs_compute_parity_funcs[rs_mk_parity(1, 8)] = nullptr;
    rs_compute_parity_funcs[rs_mk_parity(1, 9)] = nullptr;
    rs_compute_parity_funcs[rs_mk_parity(1, 10)] = nullptr;
    rs_compute_parity_funcs[rs_mk_parity(1, 11)] = nullptr;
    rs_compute_parity_funcs[rs_mk_parity(1, 12)] = nullptr;
    rs_compute_parity_funcs[rs_mk_parity(1, 13)] = nullptr;
    rs_compute_parity_funcs[rs_mk_parity(1, 14)] = nullptr;
    rs_compute_parity_funcs[rs_mk_parity(1, 15)] = nullptr;
    rs_compute_parity_funcs[rs_mk_parity(2, 0)] = nullptr;
    rs_compute_parity_funcs[rs_mk_parity(2, 1)] = &rs_compute_parity_tmpl<2, 1>;
    rs_compute_parity_funcs[rs_mk_parity(2, 2)] = &rs_compute_parity_tmpl<2, 2>;
    rs_compute_parity_funcs[rs_mk_parity(2, 3)] = &rs_compute_parity_tmpl<2, 3>;
    rs_compute_parity_funcs[rs_mk_parity(2, 4)] = &rs_compute_parity_tmpl<2, 4>;
    rs_compute_parity_funcs[rs_mk_parity(2, 5)] = &rs_compute_parity_tmpl<2, 5>;
    rs_compute_parity_funcs[rs_mk_parity(2, 6)] = &rs_compute_parity_tmpl<2, 6>;
    rs_compute_parity_funcs[rs_mk_parity(2, 7)] = &rs_compute_parity_tmpl<2, 7>;
    rs_compute_parity_funcs[rs_mk_parity(2, 8)] = &rs_compute_parity_tmpl<2, 8>;
    rs_compute_parity_funcs[rs_mk_parity(2, 9)] = &rs_compute_parity_tmpl<2, 9>;
    rs_compute_parity_funcs[rs_mk_parity(2, 10)] = &rs_compute_parity_tmpl<2, 10>;
    rs_compute_parity_funcs[rs_mk_parity(2, 11)] = &rs_compute_parity_tmpl<2, 11>;
    rs_compute_parity_funcs[rs_mk_parity(2, 12)] = &rs_compute_parity_tmpl<2, 12>;
    rs_compute_parity_funcs[rs_mk_parity(2, 13)] = &rs_compute_parity_tmpl<2, 13>;
    rs_compute_parity_funcs[rs_mk_parity(2, 14)] = &rs_compute_parity_tmpl<2, 14>;
    rs_compute_parity_funcs[rs_mk_parity(2, 15)] = &rs_compute_parity_tmpl<2, 15>;
    rs_compute_parity_funcs[rs_mk_parity(3, 0)] = nullptr;
    rs_compute_parity_funcs[rs_mk_parity(3, 1)] = &rs_compute_parity_tmpl<3, 1>;
    rs_compute_parity_funcs[rs_mk_parity(3, 2)] = &rs_compute_parity_tmpl<3, 2>;
    rs_compute_parity_funcs[rs_mk_parity(3, 3)] = &rs_compute_parity_tmpl<3, 3>;
    rs_compute_parity_funcs[rs_mk_parity(3, 4)] = &rs_compute_parity_tmpl<3, 4>;
    rs_compute_parity_funcs[rs_mk_parity(3, 5)] = &rs_compute_parity_tmpl<3, 5>;
    rs_compute_parity_funcs[rs_mk_parity(3, 6)] = &rs_compute_parity_tmpl<3, 6>;
    rs_compute_parity_funcs[rs_mk_parity(3, 7)] = &rs_compute_parity_tmpl<3, 7>;
    rs_compute_parity_funcs[rs_mk_parity(3, 8)] = &rs_compute_parity_tmpl<3, 8>;
    rs_compute_parity_funcs[rs_mk_parity(3, 9)] = &rs_compute_parity_tmpl<3, 9>;
    rs_compute_parity_funcs[rs_mk_parity(3, 10)] = &rs_compute_parity_tmpl<3, 10>;
    rs_compute_parity_funcs[rs_mk_parity(3, 11)] = &rs_compute_parity_tmpl<3, 11>;
    rs_compute_parity_funcs[rs_mk_parity(3, 12)] = &rs_compute_parity_tmpl<3, 12>;
    rs_compute_parity_funcs[rs_mk_parity(3, 13)] = &rs_compute_parity_tmpl<3, 13>;
    rs_compute_parity_funcs[rs_mk_parity(3, 14)] = &rs_compute_parity_tmpl<3, 14>;
    rs_compute_parity_funcs[rs_mk_parity(3, 15)] = &rs_compute_parity_tmpl<3, 15>;
    rs_compute_parity_funcs[rs_mk_parity(4, 0)] = nullptr;
    rs_compute_parity_funcs[rs_mk_parity(4, 1)] = &rs_compute_parity_tmpl<4, 1>;
    rs_compute_parity_funcs[rs_mk_parity(4, 2)] = &rs_compute_parity_tmpl<4, 2>;
    rs_compute_parity_funcs[rs_mk_parity(4, 3)] = &rs_compute_parity_tmpl<4, 3>;
    rs_compute_parity_funcs[rs_mk_parity(4, 4)] = &rs_compute_parity_tmpl<4, 4>;
    rs_compute_parity_funcs[rs_mk_parity(4, 5)] = &rs_compute_parity_tmpl<4, 5>;
    rs_compute_parity_funcs[rs_mk_parity(4, 6)] = &rs_compute_parity_tmpl<4, 6>;
    rs_compute_parity_funcs[rs_mk_parity(4, 7)] = &rs_compute_parity_tmpl<4, 7>;
    rs_compute_parity_funcs[rs_mk_parity(4, 8)] = &rs_compute_parity_tmpl<4, 8>;
    rs_compute_parity_funcs[rs_mk_parity(4, 9)] = &rs_compute_parity_tmpl<4, 9>;
    rs_compute_parity_funcs[rs_mk_parity(4, 10)] = &rs_compute_parity_tmpl<4, 10>;
    rs_compute_parity_funcs[rs_mk_parity(4, 11)] = &rs_compute_parity_tmpl<4, 11>;
    rs_compute_parity_funcs[rs_mk_parity(4, 12)] = &rs_compute_parity_tmpl<4, 12>;
    rs_compute_parity_funcs[rs_mk_parity(4, 13)] = &rs_compute_parity_tmpl<4, 13>;
    rs_compute_parity_funcs[rs_mk_parity(4, 14)] = &rs_compute_parity_tmpl<4, 14>;
    rs_compute_parity_funcs[rs_mk_parity(4, 15)] = &rs_compute_parity_tmpl<4, 15>;
    rs_compute_parity_funcs[rs_mk_parity(5, 0)] = nullptr;
    rs_compute_parity_funcs[rs_mk_parity(5, 1)] = &rs_compute_parity_tmpl<5, 1>;
    rs_compute_parity_funcs[rs_mk_parity(5, 2)] = &rs_compute_parity_tmpl<5, 2>;
    rs_compute_parity_funcs[rs_mk_parity(5, 3)] = &rs_compute_parity_tmpl<5, 3>;
    rs_compute_parity_funcs[rs_mk_parity(5, 4)] = &rs_compute_parity_tmpl<5, 4>;
    rs_compute_parity_funcs[rs_mk_parity(5, 5)] = &rs_compute_parity_tmpl<5, 5>;
    rs_compute_parity_funcs[rs_mk_parity(5, 6)] = &rs_compute_parity_tmpl<5, 6>;
    rs_compute_parity_funcs[rs_mk_parity(5, 7)] = &rs_compute_parity_tmpl<5, 7>;
    rs_compute_parity_funcs[rs_mk_parity(5, 8)] = &rs_compute_parity_tmpl<5, 8>;
    rs_compute_parity_funcs[rs_mk_parity(5, 9)] = &rs_compute_parity_tmpl<5, 9>;
    rs_compute_parity_funcs[rs_mk_parity(5, 10)] = &rs_compute_parity_tmpl<5, 10>;
    rs_compute_parity_funcs[rs_mk_parity(5, 11)] = &rs_compute_parity_tmpl<5, 11>;
    rs_compute_parity_funcs[rs_mk_parity(5, 12)] = &rs_compute_parity_tmpl<5, 12>;
    rs_compute_parity_funcs[rs_mk_parity(5, 13)] = &rs_compute_parity_tmpl<5, 13>;
    rs_compute_parity_funcs[rs_mk_parity(5, 14)] = &rs_compute_parity_tmpl<5, 14>;
    rs_compute_parity_funcs[rs_mk_parity(5, 15)] = &rs_compute_parity_tmpl<5, 15>;
    rs_compute_parity_funcs[rs_mk_parity(6, 0)] = nullptr;
    rs_compute_parity_funcs[rs_mk_parity(6, 1)] = &rs_compute_parity_tmpl<6, 1>;
    rs_compute_parity_funcs[rs_mk_parity(6, 2)] = &rs_compute_parity_tmpl<6, 2>;
    rs_compute_parity_funcs[rs_mk_parity(6, 3)] = &rs_compute_parity_tmpl<6, 3>;
    rs_compute_parity_funcs[rs_mk_parity(6, 4)] = &rs_compute_parity_tmpl<6, 4>;
    rs_compute_parity_funcs[rs_mk_parity(6, 5)] = &rs_compute_parity_tmpl<6, 5>;
    rs_compute_parity_funcs[rs_mk_parity(6, 6)] = &rs_compute_parity_tmpl<6, 6>;
    rs_compute_parity_funcs[rs_mk_parity(6, 7)] = &rs_compute_parity_tmpl<6, 7>;
    rs_compute_parity_funcs[rs_mk_parity(6, 8)] = &rs_compute_parity_tmpl<6, 8>;
    rs_compute_parity_funcs[rs_mk_parity(6, 9)] = &rs_compute_parity_tmpl<6, 9>;
    rs_compute_parity_funcs[rs_mk_parity(6, 10)] = &rs_compute_parity_tmpl<6, 10>;
    rs_compute_parity_funcs[rs_mk_parity(6, 11)] = &rs_compute_parity_tmpl<6, 11>;
    rs_compute_parity_funcs[rs_mk_parity(6, 12)] = &rs_compute_parity_tmpl<6, 12>;
    rs_compute_parity_funcs[rs_mk_parity(6, 13)] = &rs_compute_parity_tmpl<6, 13>;
    rs_compute_parity_funcs[rs_mk_parity(6, 14)] = &rs_compute_parity_tmpl<6, 14>;
    rs_compute_parity_funcs[rs_mk_parity(6, 15)] = &rs_compute_parity_tmpl<6, 15>;
    rs_compute_parity_funcs[rs_mk_parity(7, 0)] = nullptr;
    rs_compute_parity_funcs[rs_mk_parity(7, 1)] = &rs_compute_parity_tmpl<7, 1>;
    rs_compute_parity_funcs[rs_mk_parity(7, 2)] = &rs_compute_parity_tmpl<7, 2>;
    rs_compute_parity_funcs[rs_mk_parity(7, 3)] = &rs_compute_parity_tmpl<7, 3>;
    rs_compute_parity_funcs[rs_mk_parity(7, 4)] = &rs_compute_parity_tmpl<7, 4>;
    rs_compute_parity_funcs[rs_mk_parity(7, 5)] = &rs_compute_parity_tmpl<7, 5>;
    rs_compute_parity_funcs[rs_mk_parity(7, 6)] = &rs_compute_parity_tmpl<7, 6>;
    rs_compute_parity_funcs[rs_mk_parity(7, 7)] = &rs_compute_parity_tmpl<7, 7>;
    rs_compute_parity_funcs[rs_mk_parity(7, 8)] = &rs_compute_parity_tmpl<7, 8>;
    rs_compute_parity_funcs[rs_mk_parity(7, 9)] = &rs_compute_parity_tmpl<7, 9>;
    rs_compute_parity_funcs[rs_mk_parity(7, 10)] = &rs_compute_parity_tmpl<7, 10>;
    rs_compute_parity_funcs[rs_mk_parity(7, 11)] = &rs_compute_parity_tmpl<7, 11>;
    rs_compute_parity_funcs[rs_mk_parity(7, 12)] = &rs_compute_parity_tmpl<7, 12>;
    rs_compute_parity_funcs[rs_mk_parity(7, 13)] = &rs_compute_parity_tmpl<7, 13>;
    rs_compute_parity_funcs[rs_mk_parity(7, 14)] = &rs_compute_parity_tmpl<7, 14>;
    rs_compute_parity_funcs[rs_mk_parity(7, 15)] = &rs_compute_parity_tmpl<7, 15>;
    rs_compute_parity_funcs[rs_mk_parity(8, 0)] = nullptr;
    rs_compute_parity_funcs[rs_mk_parity(8, 1)] = &rs_compute_parity_tmpl<8, 1>;
    rs_compute_parity_funcs[rs_mk_parity(8, 2)] = &rs_compute_parity_tmpl<8, 2>;
    rs_compute_parity_funcs[rs_mk_parity(8, 3)] = &rs_compute_parity_tmpl<8, 3>;
    rs_compute_parity_funcs[rs_mk_parity(8, 4)] = &rs_compute_parity_tmpl<8, 4>;
    rs_compute_parity_funcs[rs_mk_parity(8, 5)] = &rs_compute_parity_tmpl<8, 5>;
    rs_compute_parity_funcs[rs_mk_parity(8, 6)] = &rs_compute_parity_tmpl<8, 6>;
    rs_compute_parity_funcs[rs_mk_parity(8, 7)] = &rs_compute_parity_tmpl<8, 7>;
    rs_compute_parity_funcs[rs_mk_parity(8, 8)] = &rs_compute_parity_tmpl<8, 8>;
    rs_compute_parity_funcs[rs_mk_parity(8, 9)] = &rs_compute_parity_tmpl<8, 9>;
    rs_compute_parity_funcs[rs_mk_parity(8, 10)] = &rs_compute_parity_tmpl<8, 10>;
    rs_compute_parity_funcs[rs_mk_parity(8, 11)] = &rs_compute_parity_tmpl<8, 11>;
    rs_compute_parity_funcs[rs_mk_parity(8, 12)] = &rs_compute_parity_tmpl<8, 12>;
    rs_compute_parity_funcs[rs_mk_parity(8, 13)] = &rs_compute_parity_tmpl<8, 13>;
    rs_compute_parity_funcs[rs_mk_parity(8, 14)] = &rs_compute_parity_tmpl<8, 14>;
    rs_compute_parity_funcs[rs_mk_parity(8, 15)] = &rs_compute_parity_tmpl<8, 15>;
    rs_compute_parity_funcs[rs_mk_parity(9, 0)] = nullptr;
    rs_compute_parity_funcs[rs_mk_parity(9, 1)] = &rs_compute_parity_tmpl<9, 1>;
    rs_compute_parity_funcs[rs_mk_parity(9, 2)] = &rs_compute_parity_tmpl<9, 2>;
    rs_compute_parity_funcs[rs_mk_parity(9, 3)] = &rs_compute_parity_tmpl<9, 3>;
    rs_compute_parity_funcs[rs_mk_parity(9, 4)] = &rs_compute_parity_tmpl<9, 4>;
    rs_compute_parity_funcs[rs_mk_parity(9, 5)] = &rs_compute_parity_tmpl<9, 5>;
    rs_compute_parity_funcs[rs_mk_parity(9, 6)] = &rs_compute_parity_tmpl<9, 6>;
    rs_compute_parity_funcs[rs_mk_parity(9, 7)] = &rs_compute_parity_tmpl<9, 7>;
    rs_compute_parity_funcs[rs_mk_parity(9, 8)] = &rs_compute_parity_tmpl<9, 8>;
    rs_compute_parity_funcs[rs_mk_parity(9, 9)] = &rs_compute_parity_tmpl<9, 9>;
    rs_compute_parity_funcs[rs_mk_parity(9, 10)] = &rs_compute_parity_tmpl<9, 10>;
    rs_compute_parity_funcs[rs_mk_parity(9, 11)] = &rs_compute_parity_tmpl<9, 11>;
    rs_compute_parity_funcs[rs_mk_parity(9, 12)] = &rs_compute_parity_tmpl<9, 12>;
    rs_compute_parity_funcs[rs_mk_parity(9, 13)] = &rs_compute_parity_tmpl<9, 13>;
    rs_compute_parity_funcs[rs_mk_parity(9, 14)] = &rs_compute_parity_tmpl<9, 14>;
    rs_compute_parity_funcs[rs_mk_parity(9, 15)] = &rs_compute_parity_tmpl<9, 15>;
    rs_compute_parity_funcs[rs_mk_parity(10, 0)] = nullptr;
    rs_compute_parity_funcs[rs_mk_parity(10, 1)] = &rs_compute_parity_tmpl<10, 1>;
    rs_compute_parity_funcs[rs_mk_parity(10, 2)] = &rs_compute_parity_tmpl<10, 2>;
    rs_compute_parity_funcs[rs_mk_parity(10, 3)] = &rs_compute_parity_tmpl<10, 3>;
    rs_compute_parity_funcs[rs_mk_parity(10, 4)] = &rs_compute_parity_tmpl<10, 4>;
    rs_compute_parity_funcs[rs_mk_parity(10, 5)] = &rs_compute_parity_tmpl<10, 5>;
    rs_compute_parity_funcs[rs_mk_parity(10, 6)] = &rs_compute_parity_tmpl<10, 6>;
    rs_compute_parity_funcs[rs_mk_parity(10, 7)] = &rs_compute_parity_tmpl<10, 7>;
    rs_compute_parity_funcs[rs_mk_parity(10, 8)] = &rs_compute_parity_tmpl<10, 8>;
    rs_compute_parity_funcs[rs_mk_parity(10, 9)] = &rs_compute_parity_tmpl<10, 9>;
    rs_compute_parity_funcs[rs_mk_parity(10, 10)] = &rs_compute_parity_tmpl<10, 10>;
    rs_compute_parity_funcs[rs_mk_parity(10, 11)] = &rs_compute_parity_tmpl<10, 11>;
    rs_compute_parity_funcs[rs_mk_parity(10, 12)] = &rs_compute_parity_tmpl<10, 12>;
    rs_compute_parity_funcs[rs_mk_parity(10, 13)] = &rs_compute_parity_tmpl<10, 13>;
    rs_compute_parity_funcs[rs_mk_parity(10, 14)] = &rs_compute_parity_tmpl<10, 14>;
    rs_compute_parity_funcs[rs_mk_parity(10, 15)] = &rs_compute_parity_tmpl<10, 15>;
    rs_compute_parity_funcs[rs_mk_parity(11, 0)] = nullptr;
    rs_compute_parity_funcs[rs_mk_parity(11, 1)] = &rs_compute_parity_tmpl<11, 1>;
    rs_compute_parity_funcs[rs_mk_parity(11, 2)] = &rs_compute_parity_tmpl<11, 2>;
    rs_compute_parity_funcs[rs_mk_parity(11, 3)] = &rs_compute_parity_tmpl<11, 3>;
    rs_compute_parity_funcs[rs_mk_parity(11, 4)] = &rs_compute_parity_tmpl<11, 4>;
    rs_compute_parity_funcs[rs_mk_parity(11, 5)] = &rs_compute_parity_tmpl<11, 5>;
    rs_compute_parity_funcs[rs_mk_parity(11, 6)] = &rs_compute_parity_tmpl<11, 6>;
    rs_compute_parity_funcs[rs_mk_parity(11, 7)] = &rs_compute_parity_tmpl<11, 7>;
    rs_compute_parity_funcs[rs_mk_parity(11, 8)] = &rs_compute_parity_tmpl<11, 8>;
    rs_compute_parity_funcs[rs_mk_parity(11, 9)] = &rs_compute_parity_tmpl<11, 9>;
    rs_compute_parity_funcs[rs_mk_parity(11, 10)] = &rs_compute_parity_tmpl<11, 10>;
    rs_compute_parity_funcs[rs_mk_parity(11, 11)] = &rs_compute_parity_tmpl<11, 11>;
    rs_compute_parity_funcs[rs_mk_parity(11, 12)] = &rs_compute_parity_tmpl<11, 12>;
    rs_compute_parity_funcs[rs_mk_parity(11, 13)] = &rs_compute_parity_tmpl<11, 13>;
    rs_compute_parity_funcs[rs_mk_parity(11, 14)] = &rs_compute_parity_tmpl<11, 14>;
    rs_compute_parity_funcs[rs_mk_parity(11, 15)] = &rs_compute_parity_tmpl<11, 15>;
    rs_compute_parity_funcs[rs_mk_parity(12, 0)] = nullptr;
    rs_compute_parity_funcs[rs_mk_parity(12, 1)] = &rs_compute_parity_tmpl<12, 1>;
    rs_compute_parity_funcs[rs_mk_parity(12, 2)] = &rs_compute_parity_tmpl<12, 2>;
    rs_compute_parity_funcs[rs_mk_parity(12, 3)] = &rs_compute_parity_tmpl<12, 3>;
    rs_compute_parity_funcs[rs_mk_parity(12, 4)] = &rs_compute_parity_tmpl<12, 4>;
    rs_compute_parity_funcs[rs_mk_parity(12, 5)] = &rs_compute_parity_tmpl<12, 5>;
    rs_compute_parity_funcs[rs_mk_parity(12, 6)] = &rs_compute_parity_tmpl<12, 6>;
    rs_compute_parity_funcs[rs_mk_parity(12, 7)] = &rs_compute_parity_tmpl<12, 7>;
    rs_compute_parity_funcs[rs_mk_parity(12, 8)] = &rs_compute_parity_tmpl<12, 8>;
    rs_compute_parity_funcs[rs_mk_parity(12, 9)] = &rs_compute_parity_tmpl<12, 9>;
    rs_compute_parity_funcs[rs_mk_parity(12, 10)] = &rs_compute_parity_tmpl<12, 10>;
    rs_compute_parity_funcs[rs_mk_parity(12, 11)] = &rs_compute_parity_tmpl<12, 11>;
    rs_compute_parity_funcs[rs_mk_parity(12, 12)] = &rs_compute_parity_tmpl<12, 12>;
    rs_compute_parity_funcs[rs_mk_parity(12, 13)] = &rs_compute_parity_tmpl<12, 13>;
    rs_compute_parity_funcs[rs_mk_parity(12, 14)] = &rs_compute_parity_tmpl<12, 14>;
    rs_compute_parity_funcs[rs_mk_parity(12, 15)] = &rs_compute_parity_tmpl<12, 15>;
    rs_compute_parity_funcs[rs_mk_parity(13, 0)] = nullptr;
    rs_compute_parity_funcs[rs_mk_parity(13, 1)] = &rs_compute_parity_tmpl<13, 1>;
    rs_compute_parity_funcs[rs_mk_parity(13, 2)] = &rs_compute_parity_tmpl<13, 2>;
    rs_compute_parity_funcs[rs_mk_parity(13, 3)] = &rs_compute_parity_tmpl<13, 3>;
    rs_compute_parity_funcs[rs_mk_parity(13, 4)] = &rs_compute_parity_tmpl<13, 4>;
    rs_compute_parity_funcs[rs_mk_parity(13, 5)] = &rs_compute_parity_tmpl<13, 5>;
    rs_compute_parity_funcs[rs_mk_parity(13, 6)] = &rs_compute_parity_tmpl<13, 6>;
    rs_compute_parity_funcs[rs_mk_parity(13, 7)] = &rs_compute_parity_tmpl<13, 7>;
    rs_compute_parity_funcs[rs_mk_parity(13, 8)] = &rs_compute_parity_tmpl<13, 8>;
    rs_compute_parity_funcs[rs_mk_parity(13, 9)] = &rs_compute_parity_tmpl<13, 9>;
    rs_compute_parity_funcs[rs_mk_parity(13, 10)] = &rs_compute_parity_tmpl<13, 10>;
    rs_compute_parity_funcs[rs_mk_parity(13, 11)] = &rs_compute_parity_tmpl<13, 11>;
    rs_compute_parity_funcs[rs_mk_parity(13, 12)] = &rs_compute_parity_tmpl<13, 12>;
    rs_compute_parity_funcs[rs_mk_parity(13, 13)] = &rs_compute_parity_tmpl<13, 13>;
    rs_compute_parity_funcs[rs_mk_parity(13, 14)] = &rs_compute_parity_tmpl<13, 14>;
    rs_compute_parity_funcs[rs_mk_parity(13, 15)] = &rs_compute_parity_tmpl<13, 15>;
    rs_compute_parity_funcs[rs_mk_parity(14, 0)] = nullptr;
    rs_compute_parity_funcs[rs_mk_parity(14, 1)] = &rs_compute_parity_tmpl<14, 1>;
    rs_compute_parity_funcs[rs_mk_parity(14, 2)] = &rs_compute_parity_tmpl<14, 2>;
    rs_compute_parity_funcs[rs_mk_parity(14, 3)] = &rs_compute_parity_tmpl<14, 3>;
    rs_compute_parity_funcs[rs_mk_parity(14, 4)] = &rs_compute_parity_tmpl<14, 4>;
    rs_compute_parity_funcs[rs_mk_parity(14, 5)] = &rs_compute_parity_tmpl<14, 5>;
    rs_compute_parity_funcs[rs_mk_parity(14, 6)] = &rs_compute_parity_tmpl<14, 6>;
    rs_compute_parity_funcs[rs_mk_parity(14, 7)] = &rs_compute_parity_tmpl<14, 7>;
    rs_compute_parity_funcs[rs_mk_parity(14, 8)] = &rs_compute_parity_tmpl<14, 8>;
    rs_compute_parity_funcs[rs_mk_parity(14, 9)] = &rs_compute_parity_tmpl<14, 9>;
    rs_compute_parity_funcs[rs_mk_parity(14, 10)] = &rs_compute_parity_tmpl<14, 10>;
    rs_compute_parity_funcs[rs_mk_parity(14, 11)] = &rs_compute_parity_tmpl<14, 11>;
    rs_compute_parity_funcs[rs_mk_parity(14, 12)] = &rs_compute_parity_tmpl<14, 12>;
    rs_compute_parity_funcs[rs_mk_parity(14, 13)] = &rs_compute_parity_tmpl<14, 13>;
    rs_compute_parity_funcs[rs_mk_parity(14, 14)] = &rs_compute_parity_tmpl<14, 14>;
    rs_compute_parity_funcs[rs_mk_parity(14, 15)] = &rs_compute_parity_tmpl<14, 15>;
    rs_compute_parity_funcs[rs_mk_parity(15, 0)] = nullptr;
    rs_compute_parity_funcs[rs_mk_parity(15, 1)] = &rs_compute_parity_tmpl<15, 1>;
    rs_compute_parity_funcs[rs_mk_parity(15, 2)] = &rs_compute_parity_tmpl<15, 2>;
    rs_compute_parity_funcs[rs_mk_parity(15, 3)] = &rs_compute_parity_tmpl<15, 3>;
    rs_compute_parity_funcs[rs_mk_parity(15, 4)] = &rs_compute_parity_tmpl<15, 4>;
    rs_compute_parity_funcs[rs_mk_parity(15, 5)] = &rs_compute_parity_tmpl<15, 5>;
    rs_compute_parity_funcs[rs_mk_parity(15, 6)] = &rs_compute_parity_tmpl<15, 6>;
    rs_compute_parity_funcs[rs_mk_parity(15, 7)] = &rs_compute_parity_tmpl<15, 7>;
    rs_compute_parity_funcs[rs_mk_parity(15, 8)] = &rs_compute_parity_tmpl<15, 8>;
    rs_compute_parity_funcs[rs_mk_parity(15, 9)] = &rs_compute_parity_tmpl<15, 9>;
    rs_compute_parity_funcs[rs_mk_parity(15, 10)] = &rs_compute_parity_tmpl<15, 10>;
    rs_compute_parity_funcs[rs_mk_parity(15, 11)] = &rs_compute_parity_tmpl<15, 11>;
    rs_compute_parity_funcs[rs_mk_parity(15, 12)] = &rs_compute_parity_tmpl<15, 12>;
    rs_compute_parity_funcs[rs_mk_parity(15, 13)] = &rs_compute_parity_tmpl<15, 13>;
    rs_compute_parity_funcs[rs_mk_parity(15, 14)] = &rs_compute_parity_tmpl<15, 14>;
    rs_compute_parity_funcs[rs_mk_parity(15, 15)] = &rs_compute_parity_tmpl<15, 15>;
}

__attribute__((constructor))
static void rs_initialize_recover_matmul_funcs() {
    rs_recover_matmul_funcs[0] = nullptr;
    rs_recover_matmul_funcs[1] = nullptr;
    rs_recover_matmul_funcs[2] = &rs_recover_matmul_tmpl<2>;
    rs_recover_matmul_funcs[3] = &rs_recover_matmul_tmpl<3>;
    rs_recover_matmul_funcs[4] = &rs_recover_matmul_tmpl<4>;
    rs_recover_matmul_funcs[5] = &rs_recover_matmul_tmpl<5>;
    rs_recover_matmul_funcs[6] = &rs_recover_matmul_tmpl<6>;
    rs_recover_matmul_funcs[7] = &rs_recover_matmul_tmpl<7>;
    rs_recover_matmul_funcs[8] = &rs_recover_matmul_tmpl<8>;
    rs_recover_matmul_funcs[9] = &rs_recover_matmul_tmpl<9>;
    rs_recover_matmul_funcs[10] = &rs_recover_matmul_tmpl<10>;
    rs_recover_matmul_funcs[11] = &rs_recover_matmul_tmpl<11>;
    rs_recover_matmul_funcs[12] = &rs_recover_matmul_tmpl<12>;
    rs_recover_matmul_funcs[13] = &rs_recover_matmul_tmpl<13>;
    rs_recover_matmul_funcs[14] = &rs_recover_matmul_tmpl<14>;
    rs_recover_matmul_funcs[15] = &rs_recover_matmul_tmpl<15>;
}