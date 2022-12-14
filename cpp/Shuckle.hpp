#pragma once

#include "Msgs.hpp"
#include "MsgsGen.hpp"

// The host here is the scheme + host + port, e.g. `http://localhost:5000`.
// If it returns false, the request fails, and `errString` contains why.
bool fetchBlockServices(
    const std::string& host,
    uint64_t timeoutMs,
    std::string &errString,
    UpdateBlockServicesEntry& blocks
);
