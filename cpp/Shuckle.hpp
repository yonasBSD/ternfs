#pragma once

#include "Msgs.hpp"
#include "MsgsGen.hpp"

// The host here is the scheme + host + port, e.g. `http://localhost:5000`.
// If returns false, errString will contain some info on why things failed.
bool fetchBlockServices(
    const std::string& host,
    uint64_t timeoutMs,
    std::string& errString,
    UpdateBlockServicesEntry& blocks
);
