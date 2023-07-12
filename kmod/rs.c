#include "rs.h"

#ifndef __CHECKER__ // sparse doesn't like this code at all.

#include <linux/string.h>
#include <linux/slab.h>
#include <asm/fpu/api.h>
#include <linux/highmem.h>

#include "log.h"

#define rs_warn(...) eggsfs_error(__VA_ARGS__)

#include "intrshims.h"

enum rs_cpu_level {
    RS_CPU_SCALAR = 1,
    RS_CPU_AVX2 = 2,
    RS_CPU_GFNI = 3,
};

int eggsfs_rs_cpu_level_min = 1;
int eggsfs_rs_cpu_level_max = 3;

static inline bool rs_detect_valgrind(void) {
    return false;
}

#define broadcast_u8(x) \
    ((__m256i)__builtin_ia32_pbroadcastb256((__v16qi){x,0,0,0,0,0,0,0,0,0,0,0,0,0,0}))

int eggsfs_rs_cpu_level = RS_CPU_SCALAR;

#include "rs_core.c"

#define rs_compute_parity_scalar_func(D, P) \
    static void rs_compute_parity_scalar_##D##_##P(struct rs* r, uint64_t size, const uint8_t** data, uint8_t** parity) { \
        rs_compute_parity_scalar(D, P, r, size, data, parity); \
    }

#define rs_compute_parity_avx2_func(D, P) \
    __attribute__((target("avx,avx2"))) \
    static void rs_compute_parity_avx2_##D##_##P(struct rs* r, uint64_t size, const uint8_t** data, uint8_t** parity) { \
        rs_compute_parity_avx2(D, P, r, size, data, parity); \
    }

#define rs_compute_parity_gfni_func(D, P) \
    __attribute__((target("avx,avx2,gfni"))) \
    static void rs_compute_parity_gfni_##D##_##P(struct rs* r, uint64_t size, const uint8_t** data, uint8_t** parity) { \
        rs_compute_parity_gfni(D, P, r, size, data, parity); \
    }

#define rs_recover_matmul_scalar_func(D) \
    static void rs_recover_matmul_scalar_##D(u64 size, const u8** have, u8* want, const u8* mat) { \
        rs_recover_matmul_scalar(D, size, have, want, mat); \
    }

#define rs_recover_matmul_avx2_func(D) \
    __attribute__((target("avx,avx2"))) \
    static void rs_recover_matmul_avx2_##D(u64 size, const u8** have, u8* want, const u8* mat) { \
        rs_recover_matmul_avx2(D, size, have, want, mat); \
    }

#define rs_recover_matmul_gfni_func(D) \
    __attribute__((target("avx,avx2,gfni"))) \
    static void rs_recover_matmul_gfni_##D(u64 size, const u8** have, u8* want, const u8* mat) { \
        rs_recover_matmul_gfni(D, size, have, want, mat); \
    }

#define rs_gen(D, P) \
    static char rs_##D##_##P_data[RS_SIZE(D, P)]; \
    static struct rs* rs_##D##_##P = (struct rs*)rs_##D##_##P_data; \
    static void rs_init_##D##_##P(void) { \
        rs_new_core(eggsfs_mk_parity(D, P), rs_##D##_##P); \
    } \
    rs_compute_parity_scalar_func(D, P) \
    rs_compute_parity_avx2_func(D, P) \
    rs_compute_parity_gfni_func(D, P) \
    static int rs_compute_parity_##D##_##P(uint64_t size, const uint8_t** data, uint8_t** parity) { \
        switch (eggsfs_rs_cpu_level) { \
        case RS_CPU_SCALAR: \
            rs_compute_parity_scalar_##D##_##P(rs_##D##_##P, size, data, parity); \
            return 0; \
        case RS_CPU_AVX2: \
            rs_compute_parity_avx2_##D##_##P(rs_##D##_##P, size, data, parity); \
            return 0; \
        case RS_CPU_GFNI: \
            rs_compute_parity_gfni_##D##_##P(rs_##D##_##P, size, data, parity); \
            return 0; \
        default: \
            rs_warn("bad cpu level %d", eggsfs_rs_cpu_level); \
            return -EIO; \
        } \
    } \
    rs_recover_matmul_scalar_func(D) \
    rs_recover_matmul_avx2_func(D) \
    rs_recover_matmul_gfni_func(D)

#define rs_recover_matmul(D) ({ \
        void (*fun)(u64 size, const u8** have, u8* want, const u8* mat) = NULL; \
        switch (eggsfs_rs_cpu_level) { \
        case RS_CPU_SCALAR: \
            fun = rs_recover_matmul_scalar_##D; \
            break; \
        case RS_CPU_AVX2: \
            fun = rs_recover_matmul_avx2_##D; \
            break; \
        case RS_CPU_GFNI: \
            fun = rs_recover_matmul_gfni_##D; \
            break; \
        default: \
            rs_warn("bad cpu level %d", eggsfs_rs_cpu_level); \
            break; \
        } \
        fun; \
    })

// Right now we don't need anything else, let's not needlessly generate tons of code
rs_gen( 4, 4)
rs_gen(10, 4)

int eggsfs_compute_parity(u8 parity, ssize_t size, const char** data, char** out) {
    if (eggsfs_parity_blocks(parity) == 0) { // nothing to do
        return 0;
    }
    if (eggsfs_data_blocks(parity) == 1) { // mirroring
        int i;
        for (i = 0; i < eggsfs_parity_blocks(parity); i++) {
            memcpy(out[i], data[0], size);
        }
        return 0;
    }

    int err;
    if (parity == eggsfs_mk_parity(4, 4)) {
        err = rs_compute_parity_4_4(size, (const u8**)data, (u8**)out);
    } else if (parity == eggsfs_mk_parity(10, 4)) {
        err = rs_compute_parity_10_4(size, (const u8**)data, (u8**)out);
    } else {
        rs_warn("cannot compute with RS(%d,%d)", eggsfs_data_blocks(parity), eggsfs_parity_blocks(parity));
        err = -EINVAL;
    }
    return err;
}

int eggsfs_recover(
    u8 parity,
    u32 have_blocks,
    u32 want_block,
    u32 num_pages,
    struct list_head* pages
) {
    int D = eggsfs_data_blocks(parity);
    int P = eggsfs_parity_blocks(parity);
    int B = eggsfs_blocks(parity);

    BUG_ON(D < 1);
    BUG_ON(P == 0);
    BUG_ON(__builtin_popcountll(have_blocks) != D || __builtin_popcountll(want_block) != 1);

    if (D == 1) { // mirroring, just copy over
        u32 i;
        struct list_head* have = &pages[__builtin_ctz(have_blocks)];
        struct list_head* want = &pages[__builtin_ctz(want_block)];
        for (i = 0; i < num_pages; i++, list_rotate_left(have), list_rotate_left(want)) {
            char* want_buf = kmap_atomic(list_first_entry(want, struct page, lru));
            char* have_buf = kmap_atomic(list_first_entry(have, struct page, lru));
            memcpy(want_buf,have_buf,PAGE_SIZE);
            kunmap_atomic(want_buf);
            kunmap_atomic(have_buf);
        }
        return 0;
    }

    // decide which one to do
    struct rs* rs;
    void (*recover_matmul)(u64 size, const u8** have, u8* want, const u8* mat);
    if (parity == eggsfs_mk_parity(4, 4)) {
        rs = rs_4_4;
        recover_matmul = rs_recover_matmul(4);
    } else if (parity == eggsfs_mk_parity(10, 4)) {
        rs = rs_10_4;
        recover_matmul = rs_recover_matmul(10);
    } else {
        eggsfs_error("cannot compute with RS(%d,%d)", D, P);
        return -EIO;
    }
    if (recover_matmul == NULL) {
        return -EIO;
    }

    kernel_fpu_begin();

    // compute matrix
    u8 mat[RS_RECOVER_MAT_SIZE(EGGSFS_MAX_DATA)];
    if (!rs_recover_mat(rs, have_blocks, want_block, mat)) {
        kernel_fpu_end();
        return -EIO;
    }

    // compute data
    char* have_bufs[EGGSFS_MAX_DATA];
    char* want_buf;
    int i, j, b;
    for (i = 0; i < num_pages; i++) {
        for (b = 0, j = 0; b < B; b++) { // map pages
            if ((1u<<b) & have_blocks) {
                have_bufs[j] = kmap_atomic(list_first_entry(&pages[b], struct page, lru));
                j++;
            }
            if ((1u<<b) & want_block) {
                want_buf = kmap_atomic(list_first_entry(&pages[b], struct page, lru));
            }
        }

        recover_matmul(PAGE_SIZE, (const u8**)have_bufs, want_buf, mat);
        for (b = 0; b < B; b++) { // unmap pages, rotate list
            if ((1u<<b) & have_blocks) {
                kunmap_atomic(have_bufs[b]);
                list_rotate_left(&pages[b]);
            }
            if ((1u<<b) & want_block) {
                kunmap_atomic(want_buf);
                list_rotate_left(&pages[b]);
            }
        }
    }

    kernel_fpu_end();
    return 0;
}

int __init eggsfs_rs_init(void) {
    if (rs_has_cpu_level_core(RS_CPU_GFNI)) {
        eggsfs_info("picking GFNI");
        eggsfs_rs_cpu_level = RS_CPU_GFNI;
    } else if (rs_has_cpu_level_core(RS_CPU_AVX2)) {
        eggsfs_info("picking AVX2");
        eggsfs_rs_cpu_level = RS_CPU_AVX2;
    } else {
        eggsfs_warn("picking scalar execution -- this will be slow.");
        eggsfs_rs_cpu_level = RS_CPU_SCALAR;
    }

    rs_init_4_4();
    rs_init_10_4();

    return 0;
}

void __cold eggsfs_rs_exit(void) {
    eggsfs_debug("rs exit");
}

#include "gf_tables.c"

#endif
