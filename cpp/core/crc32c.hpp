#pragma once

#include <stdint.h>
#include <stdlib.h>
#include <array>

std::array<uint8_t, 4> crc32c(const char* buf, size_t len);

std::array<uint8_t, 4> crc32cCombine(std::array<uint8_t, 4> crcA, std::array<uint8_t, 4> crcB, size_t lenB);
