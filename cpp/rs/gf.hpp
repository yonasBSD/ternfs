#pragma once

#include <immintrin.h>
#include <stdint.h>
#include <string.h>

extern const uint8_t rs_gf_inv_table[256];
extern const uint8_t rs_gf_log_table[256];
extern const uint8_t rs_gf_exp_table[256];

inline uint8_t gf_inv(uint8_t x) {
    return rs_gf_inv_table[x];
}

inline uint8_t gf_mul(uint8_t x, uint8_t y) {
    if (x == 0 || y == 0) {
        return 0;
    }
    int i = rs_gf_log_table[x] + rs_gf_log_table[y];
    return rs_gf_exp_table[i > 254 ? i - 255 : i];
}

inline void gf_mul_expand_factor(uint8_t x, uint8_t* expanded_x) {
    for (int i = 0; i < 16; i++) {
        expanded_x[i] = gf_mul(i, x);
    }
    for (int i = 0; i < 16; i++) {
        expanded_x[16 + i] = gf_mul(i << 4, x);
    }
}

inline uint8_t gf_mul_expanded(uint8_t x, const uint8_t* expanded_y) {
    return expanded_y[x & 0x0f] ^ expanded_y[16 + ((x & 0xf0) >> 4)];
}

inline __m256i gf_mul_expanded_avx2(__m256i x, __m256i expanded_y, __m256i low_nibble_mask) {
    __m256i expanded_y_lo = _mm256_permute2x128_si256(expanded_y, expanded_y, 0x00);
    __m256i expanded_y_hi = _mm256_permute2x128_si256(expanded_y, expanded_y, 0x11);

    __m256i x_lo = _mm256_and_si256(x, low_nibble_mask);
    __m256i x_hi = _mm256_and_si256(_mm256_srli_epi16(x, 4), low_nibble_mask);

    return _mm256_xor_si256(
        _mm256_shuffle_epi8(expanded_y_lo, x_lo),
        _mm256_shuffle_epi8(expanded_y_hi, x_hi)
    );
}

// From <https://github.com/intel/isa-l/blob/33a2d9484595c2d6516c920ce39a694c144ddf69/erasure_code/ec_base.c#L110>,
// just Gaussian elimination.
//
// TODO this is in row-major, it'd be nice to have it in column-major
// to save have the final operation to be more natural in rs_recover.
__attribute__((noinline))
static bool rs_gf_invert_matrix(uint8_t* in_mat, uint8_t* out_mat, const int n) {
    int i, j, k;
    uint8_t temp;

    // Set out_mat[] to the identity matrix
    memset(out_mat, 0, n*n);
    for (i = 0; i < n; i++) {
        out_mat[i * n + i] = 1;
    }

    // Inverse
    for (i = 0; i < n; i++) {
        // Check for 0 in pivot element
        if (in_mat[i * n + i] == 0) {
            // Find a row with non-zero in current column and swap
            for (j = i + 1; j < n; j++) {
                if (in_mat[j * n + i]) {
                    break;
                }
            }

            if (j == n) {    // Couldn't find means it's singular
                return false;
            }

            for (k = 0; k < n; k++) {    // Swap rows i,j
                temp = in_mat[i * n + k];
                in_mat[i * n + k] = in_mat[j * n + k];
                in_mat[j * n + k] = temp;

                temp = out_mat[i * n + k];
                out_mat[i * n + k] = out_mat[j * n + k];
                out_mat[j * n + k] = temp;
            }
        }

        temp = gf_inv(in_mat[i * n + i]);    // 1/pivot
        for (j = 0; j < n; j++) {    // Scale row i by 1/pivot
            in_mat[i * n + j] = gf_mul(in_mat[i * n + j], temp);
            out_mat[i * n + j] = gf_mul(out_mat[i * n + j], temp);
        }

        for (j = 0; j < n; j++) {
            if (j == i) {
                continue;
            }

            temp = in_mat[j * n + i];
            for (k = 0; k < n; k++) {
                out_mat[j * n + k] ^= gf_mul(temp, out_mat[i * n + k]);
                in_mat[j * n + k] ^= gf_mul(temp, in_mat[i * n + k]);
            }
        }
    }
    return true;
}
