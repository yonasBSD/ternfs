#include "rs.h"

#ifdef __KERNEL__

#include <linux/string.h>
#include <linux/slab.h>
#include <asm/fpu/api.h>

#define rs_malloc(sz) kmalloc(sz, GFP_KERNEL)
#define rs_free(p) kfree(p)
#define die(...) BUG()

#include "log.h"

#endif // __KERNEL__

#include "intrshims.h"

enum rs_cpu_level {
    RS_CPU_SCALAR = 1,
    RS_CPU_AVX2 = 2,
    RS_CPU_GFNI = 3,
};

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

#define rs_gen(D, P) \
    static struct rs* rs_##D##_##P = NULL; \
    static int rs_init_##D##_##P(void) { \
        rs_##D##_##P = rs_new_core(eggsfs_mk_parity(D, P)); \
        if (rs_##D##_##P == NULL) { \
            return -ENOMEM; \
        } \
        return 0; \
    } \
    rs_compute_parity_scalar_func(D, P) \
    rs_compute_parity_avx2_func(D, P) \
    rs_compute_parity_gfni_func(D, P) \
    static void rs_compute_parity_##D##_##P(uint64_t size, const uint8_t** data, uint8_t** parity) { \
        switch (eggsfs_rs_cpu_level) { \
        case RS_CPU_SCALAR: \
            rs_compute_parity_scalar_##D##_##P(rs_##D##_##P, size, data, parity); \
            break; \
        case RS_CPU_AVX2: \
            rs_compute_parity_avx2_##D##_##P(rs_##D##_##P, size, data, parity); \
            break; \
        case RS_CPU_GFNI: \
            rs_compute_parity_gfni_##D##_##P(rs_##D##_##P, size, data, parity); \
            break; \
        default: \
            die("bad cpu level %d", eggsfs_rs_cpu_level); \
        } \
    }

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

    int err = 0;
    if (parity == eggsfs_mk_parity(4, 4)) {
        rs_compute_parity_4_4(size, (const u8**)data, (u8**)out);
    } else if (parity == eggsfs_mk_parity(10, 4)) {
        rs_compute_parity_10_4(size, (const u8**)data, (u8**)out);
    } else {
        eggsfs_warn_print("cannot compute with RS(%d,%d)", eggsfs_data_blocks(parity), eggsfs_parity_blocks(parity));
        err = -EINVAL;
    }
    return err;
}

int __init eggsfs_rs_init(void) {
    if (rs_has_cpu_level_core(RS_CPU_GFNI)) {
        eggsfs_info_print("picking GFNI");
        eggsfs_rs_cpu_level = RS_CPU_GFNI;
    } else if (rs_has_cpu_level_core(RS_CPU_AVX2)) {
        eggsfs_info_print("picking AVX2");
        eggsfs_rs_cpu_level = RS_CPU_AVX2;
    } else {
        eggsfs_warn_print("picking scalar execution -- this will be slow.");
        eggsfs_rs_cpu_level = RS_CPU_SCALAR;
    }

    int err;
    err = rs_init_4_4();
    if (err < 0) { return err; }
    err = rs_init_10_4();
    if (err < 0) { rs_free(rs_4_4); return err; }

    return 0;
}

void __cold eggsfs_rs_exit(void) {
    rs_free(rs_4_4);
    rs_free(rs_10_4);
}

#include "gf_tables.c"