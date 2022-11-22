#pragma once

#include <array>

#include "Common.hpp"

void generateSecretKey(std::array<uint8_t, 16>& key);

// generates an AES-128 CBC MAC
void cbcmac(const std::array<uint8_t, 16>& key, const uint8_t* data, size_t len, std::array<uint8_t, 8>& mac);