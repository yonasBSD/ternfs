#pragma once

#include <stdint.h>
#include <string.h>

extern const uint8_t gf_inv_table[256];
extern const uint8_t gf_log_table[256];
extern const uint8_t gf_exp_table[256];

inline uint8_t gf_inv(uint8_t x) {
    return gf_inv_table[x];
}

inline uint8_t gf_mul(uint8_t x, uint8_t y) {
    if (x == 0 || y == 0) {
        return 0;
    }
    int i = gf_log_table[x] + gf_log_table[y];
    return gf_exp_table[i > 254 ? i - 255 : i];
}

// From <https://github.com/intel/isa-l/blob/33a2d9484595c2d6516c920ce39a694c144ddf69/erasure_code/ec_base.c#L110>,
// just Gaussian elimination.
//
// TODO this is in row-major, it'd be nice to have it in column-major
// to save have the final operation to be more natural in rs_recover.
static bool gf_invert_matrix(uint8_t* in_mat, uint8_t* out_mat, const int n) {
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
