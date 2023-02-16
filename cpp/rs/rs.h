// Reed-Solomon implementation in the style of
// <https://www.corsix.org/content/reed-solomon-for-software-raid>
//
// Specifically:
//
// * We use the GF(2^8) as our word -- using the GNFI/Rijndael
//     polynomial x^8 + x^4 + x^3 + x + 1.
// * We use a matrix so that the encoding is systematic -- the data
//     blocks are preserved. Or in other words, the first part of
//     the encoding matrix is the identity matrix.
// * We rescale the encoding matrix so that the first parity block
//     is the XOR of the data blocks, in line with RAID5.
//
// In the comments below, we use D to mean the number of Data blocks,
// P to mean the number of Parity blocks, and B for the total number
// of Blocks.
//
// We also store the number of parity/data blocks in the two nibbles
// of a uint8_t, but this is a fairly irrelevant quirk of EggsFS,
// although it does nicely enforce that we do not go beyond what's
// resonable for data storage purposes (rather than for error correction).
//
// TODO allow to pass short data blocks so that the input won't have to
// be copied to compute the parity in the simple case.
#ifndef EGGS_RS
#define EGGS_RS

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct rs;

inline uint8_t rs_data_blocks(uint8_t parity) {
    return parity & 0x0F;
}

inline uint8_t rs_parity_blocks(uint8_t parity) {
    return parity >> 4;
}

inline uint8_t rs_blocks(uint8_t parity) {
    return rs_data_blocks(parity) + rs_parity_blocks(parity);
}

// Will crash (SIGABRT) if:
//
// * If `D < 2` (just use mirroring);
// * If `P < 1` (can't do RS with no parity blocks);
//
// There's no `rs_delete` -- once allocated the objects are kept around
// forever, since they are immutable.
struct rs* rs_get(uint8_t parity);

uint8_t rs_parity(struct rs* rs);

// Tells us how big each block will be given a certain size we want to store.
//
// We'll have that `rs_block_size() * D >= size`, and the user
// is responsible with creating the data blocks by just filling the blocks
// with the data, and padding the remainder with zeros.
uint64_t rs_block_size(struct rs* rs, uint64_t size);

// Computes all parity blocks at once. This is what you use when you store
// something for the first time.
void rs_compute_parity(
    struct rs* rs,
    uint64_t block_size,
    const uint8_t** data, // input, uint8_t[D][block_size]
    uint8_t** parity      // output, uint8_t[P][block_size]
);

// Computes an arbitrary block given at least `D` other blocks.
// This is what you use to recover a lost block.
void rs_recover(
    struct rs* rs,
    uint64_t block_size,
    const uint8_t* have_blocks, // [0, B)[D], must be sorted.
    const uint8_t** blocks,     // uint8_t[D][block_size]
    uint8_t want_block,         // [0, B) and not in `have_blocks`
    uint8_t* block              // uint8_t[block_size]
);

#ifdef __cplusplus
}
#endif

#endif