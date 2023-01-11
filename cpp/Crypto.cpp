// Code from
// <https://www.intel.com/content/dam/doc/white-paper/advanced-encryption-standard-new-instructions-set-paper.pdf>.
#include <emmintrin.h>
#include <wmmintrin.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>

#include "Crypto.hpp"
#include "Exception.hpp"


void generateSecretKey(std::array<uint8_t, 16>& key) {
    int fd = open("/dev/urandom", O_RDONLY);
    if (fd < 0) {
        throw SYSCALL_EXCEPTION("open");
    }
    ssize_t soFar = 0;
    while (soFar < key.size()) {
        ssize_t x = read(fd, &key[soFar], key.size()-soFar);
        if (x < 0) {
            throw SYSCALL_EXCEPTION("read");
        }
        soFar += x;
    }
    if (close(fd) < 0) {
        throw SYSCALL_EXCEPTION("close");
    }
}

inline __m128i AES_128_ASSIST(__m128i temp1, __m128i temp2) {
    __m128i temp3;
    temp2 = _mm_shuffle_epi32(temp2 ,0xff);
    temp3 = _mm_slli_si128(temp1, 0x4);
    temp1 = _mm_xor_si128(temp1, temp3);
    temp3 = _mm_slli_si128(temp3, 0x4);
    temp1 = _mm_xor_si128(temp1, temp3);
    temp3 = _mm_slli_si128(temp3, 0x4);
    temp1 = _mm_xor_si128(temp1, temp3);
    temp1 = _mm_xor_si128(temp1, temp2);
    return temp1;
}

void expandKey(const std::array<uint8_t, 16>& userkey, AES128Key& key) {
    __m128i temp1, temp2;
    __m128i *Key_Schedule = (__m128i*)&key;
    temp1 = _mm_loadu_si128((__m128i*)&userkey[0]);
    Key_Schedule[0] = temp1;
    temp2 = _mm_aeskeygenassist_si128(temp1, 0x1);
    temp1 = AES_128_ASSIST(temp1, temp2);
    Key_Schedule[1] = temp1;
    temp2 = _mm_aeskeygenassist_si128(temp1, 0x2);
    temp1 = AES_128_ASSIST(temp1, temp2);
    Key_Schedule[2] = temp1;
    temp2 = _mm_aeskeygenassist_si128(temp1, 0x4);
    temp1 = AES_128_ASSIST(temp1, temp2);
    Key_Schedule[3] = temp1;
    temp2 = _mm_aeskeygenassist_si128(temp1, 0x8);
    temp1 = AES_128_ASSIST(temp1, temp2);
    Key_Schedule[4] = temp1;
    temp2 = _mm_aeskeygenassist_si128(temp1, 0x10);
    temp1 = AES_128_ASSIST(temp1, temp2);
    Key_Schedule[5] = temp1;
    temp2 = _mm_aeskeygenassist_si128(temp1, 0x20);
    temp1 = AES_128_ASSIST(temp1, temp2);
    Key_Schedule[6] = temp1;
    temp2 = _mm_aeskeygenassist_si128(temp1, 0x40);
    temp1 = AES_128_ASSIST(temp1, temp2);
    Key_Schedule[7] = temp1;
    temp2 = _mm_aeskeygenassist_si128(temp1, 0x80);
    temp1 = AES_128_ASSIST(temp1, temp2);
    Key_Schedule[8] = temp1;
    temp2 = _mm_aeskeygenassist_si128(temp1, 0x1b);
    temp1 = AES_128_ASSIST(temp1, temp2);
    Key_Schedule[9] = temp1;
    temp2 = _mm_aeskeygenassist_si128(temp1, 0x36);
    temp1 = AES_128_ASSIST(temp1, temp2);
    Key_Schedule[10] = temp1;
}

std::array<uint8_t, 8> cbcmac(const AES128Key& key, const uint8_t* data, size_t len) {
    __m128i xmmKey[11];
    for (int i = 0; i < 11; i++) {
        xmmKey[i] = _mm_load_si128((__m128i*)(&key) + i);
    }
    __m128i block = _mm_setzero_si128();
    __m128i dataBlock;
    size_t i;
    ALIGNED(16) uint8_t scratch[16];
    for (i = 0; i < len; i += 16) {
        // load data + CBC xor
        if (unlikely(len-i < 16)) {
            memset(scratch, 0, 16);
            memcpy(scratch, data+i, len-i);
            dataBlock = _mm_load_si128((__m128i*)scratch);
        } else {
            dataBlock = _mm_loadu_si128((__m128i*)(data+i));
        }
        // CBC xor
        block = _mm_xor_si128(block, dataBlock);
        // encrypt
        block = _mm_xor_si128(block, xmmKey[0]);         // Whitening step (Round 0)
        for (int i = 1; i < 10; i++) {
            block = _mm_aesenc_si128(block, xmmKey[i]);  // Round i
        }
        block = _mm_aesenclast_si128(block, xmmKey[10]); // Round 10
    }
    _mm_store_si128((__m128i*)scratch, block);
    std::array<uint8_t, 8> mac;
    memcpy(&mac[0], scratch, 8);
    return mac;
}