#pragma once

#include "Msgs.hpp"

// The host here is the scheme + host + port, e.g. `http://localhost:5000`.
//
// If it returns a string, the request has failed and the string contains the
// error. We only returns error strings for errors that might be transient
// (e.g. we cannot connect to the server, or the connection dies), and crash
// on things that are almost certainly not transient (e.g. bad data on
// the wire).
std::string fetchBlockServices(
    const std::string& shuckleHost,
    uint16_t shucklePort,
    Duration timeout,
    ShardId shid,
    UpdateBlockServicesEntry& blocks
);

std::string registerShard(
    const std::string& shuckleHost,
    uint16_t shucklePort,
    Duration timeout,
    ShardId shid,
    uint32_t ip1,
    uint16_t port1,
    uint32_t ip2,
    uint16_t port2
);

std::string registerCDC(
    const std::string& shuckleHost,
    uint16_t shucklePort,
    Duration timeout,
    uint32_t ip1,
    uint16_t port1,
    uint32_t ip2,
    uint16_t port2
);

std::string fetchShards(
    const std::string& shuckleHost,
    uint16_t shucklePort,
    Duration timeout,
    std::array<ShardInfo, 256>& shards
);

std::string insertStats(
    const std::string& shuckleHost,
    uint16_t shucklePort,
    Duration timeout,
    const std::vector<Stat>& stats
);

const std::string defaultShuckleAddress = "REDACTED";
bool parseShuckleAddress(const std::string& fullShuckleAddress, std::string& shuckleHost, uint16_t& shucklePort);