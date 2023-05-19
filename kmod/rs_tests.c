#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;

#define die(...) do { fprintf(stderr, __VA_ARGS__); exit(1); } while(false)
#define rs_malloc malloc
#define rs_free free
#define eggsfs_warn_print(fmt, args...) fprintf(stderr, "%s: " fmt "\n", __func__, ##args)
#define eggsfs_info_print(fmt, args...) fprintf(stderr, "%s: " fmt "\n", __func__, ##args)
#define __init
#define __cold

#include "rs.h"
#include "rs.c"

#define DATA_BLOCKS 10
#define PARITY_BLOCKS 4
#define BLOCKS (10+4)
#define PAGE_SIZE 4096

int main(void) {
    eggsfs_rs_init();
    char* blocks = malloc(PAGE_SIZE*BLOCKS);
    if (!blocks) { die("couldn't allocate"); }
    char* ptrs[BLOCKS];
    int i;
    for (i = 0; i < BLOCKS; i++) {
        ptrs[i] = blocks + PAGE_SIZE*i;
    }
    for (i = 1; i < 4; i++) {
        eggsfs_rs_cpu_level = i;
        int res = eggsfs_compute_parity(eggsfs_mk_parity(DATA_BLOCKS, PARITY_BLOCKS), PAGE_SIZE, (const char**)ptrs, ptrs + DATA_BLOCKS);
        if (res < 0) {
            die("couldn't compute: %d", res);
        }
    }

    return 0;
}