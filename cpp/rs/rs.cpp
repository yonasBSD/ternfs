#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <signal.h>

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

#include "gf.hpp"

struct rs {
    uint8_t parity;
    // uint8_t[rs_d*rs_blocks], in column-major.
    uint8_t* matrix;
};

static struct rs* cached[256] = {
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

static uint8_t* rs_cauchy_matrix(uint8_t parity) {
    int D = rs_data_blocks(parity);
    int B = rs_blocks(parity);
    uint8_t* matrix = (uint8_t*)malloc_or_die(D*B, "cannot allocate cauchy matrix\n");
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
    return matrix;
}

static struct rs* rs_new(uint8_t parity) {
    struct rs* r = (struct rs*)malloc_or_die(sizeof(struct rs), "cannot allocate 'struct rs'\n");
    r->parity = parity;
    r->matrix = rs_cauchy_matrix(parity);
    return r;
}

static void rs_delete(struct rs* r) {
    free(r->matrix);
    free(r);
}

struct rs* rs_get(uint8_t parity) {
    if (rs_data_blocks(parity) < 2 || rs_parity_blocks(parity) < 1) {
        die("bad parity (%d,%d), expected at least 2 data blocks and 1 parity block.\n", rs_data_blocks(parity), rs_parity_blocks(parity));
    }
    struct rs* r = __atomic_load_n(&cached[parity], __ATOMIC_RELAXED);
    if (__builtin_expect(r == nullptr, 0)) {
        r = rs_new(parity);
        struct rs* expected = nullptr;
        if (!__atomic_compare_exchange_n(&cached[parity], &expected, r, false, __ATOMIC_RELAXED, __ATOMIC_RELAXED)) {
            // somebody else got to it first
            rs_delete(r);
            r = __atomic_load_n(&cached[parity], __ATOMIC_RELAXED);
        }
    }
    return r;
}

uint8_t rs_parity(struct rs* r) {
    return r->parity;
}

// Round up to number of data blocks.
uint64_t rs_block_size(struct rs* r, uint64_t size) {
    size_t d = rs_data_blocks(r->parity);
    return (size + (d - 1)) / d;
}

void rs_compute_parity(struct rs* r, uint64_t size, const uint8_t** data, uint8_t** parity) {
    int D = rs_data_blocks(r->parity);
    int P = rs_parity_blocks(r->parity);
    // parity = r->matrix * data
    for (size_t i = 0; i < size; i++) {
        for (int j = 0; j < P; j++) {
            const uint8_t* col = &r->matrix[D*D + j*D];
            parity[j][i] = 0;
            for (int k = 0; k < D; k++) {
                parity[j][i] ^= gf_mul(col[k], data[k][i]);
            }
        }
    }
}

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
        if (i == 0) {
            continue;
        }
        if (have_blocks[i] <= have_blocks[i-1]) {
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
    if (!gf_invert_matrix(data_to_have, have_to_data, D)) {
        die("unexpected singular matrix");
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
    for (size_t i = 0; i < size; i++) {
        want[i] = 0;
        for (int j = 0; j < D; j++) {
            want[i] ^= gf_mul(have_to_want[j], have[j][i]);
        }
    }
    // We're done.
    free(scratch);
}
