// Copyright 2025 XTX Markets Technologies Limited
//
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#pragma once

#include "Common.hpp"

#include <cstdint>
#include <cstddef>

// Simple Lehmer generator, which passes BigCrush
class RandomGenerator {
public:
    RandomGenerator(); // initialize state from /dev/urandom
    RandomGenerator(uint64_t seed); // initialize state from seedi

    UNUSED uint64_t generate64(); // [0, INT64_MAX]
    UNUSED double generateDouble(); // [0.0, 1.0]
    void generateBytes(char *buf, size_t len);

private:
    uint64_t _state[2];
};

