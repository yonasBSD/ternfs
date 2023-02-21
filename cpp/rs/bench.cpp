#include <vector>
#include <string>
#include <chrono>

#include "rs.h"
#include "wyhash.h"

int main(int argc, const char** argv) {
    const auto usageAndDie = [argv]() {
        fprintf(stderr, "Usage: %s SCALAR|AVX2|GFNI D P block_size\n", argv[0]);
        exit(2);
    };
    if (argc != 5) {
        usageAndDie();
    }
    rs_cpu_level level = RS_CPU_SCALAR;
    if (argv[1] == std::string("SCALAR")) {
        level = RS_CPU_SCALAR;
    } else if (argv[1] == std::string("AVX2")) {
        level = RS_CPU_AVX2;
    } else if (argv[1] == std::string("GFNI")) {
        level = RS_CPU_GFNI;
    } else {
        usageAndDie();
    }
    rs_set_cpu_level(level);
    int D = atoi(argv[2]);
    int P = atoi(argv[3]);
    struct rs* r = rs_get(rs_mk_parity(D, P));
    uint64_t blockSize = std::stoull(argv[4]);
    uint64_t iterations = 100;
    fprintf(stderr, "Running with RS(%d,%d), block size %ld, %ld iterations.\n", D, P, blockSize, iterations);
    uint64_t rand = 0;
    std::vector<uint8_t> buf(blockSize*(D+P+1));
    wyhash64_bytes(&rand, buf.data(), blockSize*D);
    std::vector<const uint8_t*> dataBlocks(D);
    for (int i = 0; i < D; i++) {
        dataBlocks[i] = &buf[i*blockSize];
    }
    std::vector<uint8_t*> parityBlocks(D);
    for (int i = 0; i < P; i++) {
        parityBlocks[i] = &buf[D*blockSize + i*blockSize];
    }

    rs_compute_parity(r, blockSize, &dataBlocks[0], &parityBlocks[0]);
    auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < iterations; i ++) {
        rs_compute_parity(r, blockSize, &dataBlocks[0], &parityBlocks[0]);
    }
    double deltaSeconds = ((double)std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - t0).count())/1e6;
    double gbPerSecond = ((double)((D+P)*blockSize*iterations)/1e9) / deltaSeconds;
    printf("Compute (total memory touched): %0.2fGB/s\n", gbPerSecond);

    // just recover the last one
    std::vector<uint8_t> haveBlocks(D);
    for (int i = 0; i < D-1; i++) {
        haveBlocks[i] = i;
    }
    haveBlocks[D-1] = D;
    dataBlocks[D-1] = &buf[D*blockSize];
    rs_recover(r, blockSize, &haveBlocks[0], &dataBlocks[0], D-1, &buf[(D+1)*blockSize]);
    t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < iterations; i++) {
        rs_recover(r, blockSize, &haveBlocks[0], &dataBlocks[0], D-1, &buf[(D+1)*blockSize]);
    }
    deltaSeconds = ((double)std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - t0).count())/1e6;
    gbPerSecond = ((double)((D+1)*blockSize*iterations)/1e9) / deltaSeconds;
    printf("Recover (total memory touched): %0.2fGB/s\n", gbPerSecond);

    return 0;
}