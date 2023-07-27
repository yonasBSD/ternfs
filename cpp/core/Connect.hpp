#pragma once

#include "Common.hpp"
#include "Time.hpp"

// For errors that we probably shouldn't retry, an exception is thrown. Otherwise
// -1 fd and error string.
int connectToHost(
    const std::string& host,
    uint16_t port,
    std::string& errString
);
