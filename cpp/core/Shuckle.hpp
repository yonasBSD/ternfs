#pragma once

#include "Msgs.hpp"
#include "MsgsGen.hpp"

// The host here is the scheme + host + port, e.g. `http://localhost:5000`.
//
// If it returns a string, the request has failed and the string contains the
// error. We only returns error strings for errors that might be transient
// (e.g. we cannot connect to the server, or the connection dies), and crash
// on things that are almost certainly not transient (e.g. bad data on
// the wire).
std::string fetchBlockServices(
    const std::string& shuckleAddr,
    uint16_t shucklePort,
    Duration timeout,
    ShardId shid,
    UpdateBlockServicesEntry& blocks
);

std::string registerShard(
    const std::string& shuckleAddr,
    uint16_t shucklePort,
    Duration timeout,
    ShardId shid,
    const std::array<uint8_t, 4>& shardAddr,
    uint16_t shardPort
);

std::string registerCDC(
    const std::string& shuckleAddr,
    uint16_t shucklePort,
    Duration timeout,
    const std::array<uint8_t, 4>& cdcAddr,
    uint16_t cdcPort
);

std::string fetchShards(
    const std::string& shuckleAddr,
    uint16_t shucklePort,
    Duration timeout,
    std::vector<ShardInfo>& shards
);
