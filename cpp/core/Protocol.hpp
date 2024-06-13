#pragma once

#include "MsgsGen.hpp"

// >>> format(struct.unpack('<I', b'SHA\0')[0], 'x')
// '414853'
constexpr uint32_t SHARD_REQ_PROTOCOL_VERSION = 0x414853;

// >>> format(struct.unpack('<I', b'SHA\1')[0], 'x')
// '1414853'
constexpr uint32_t SHARD_RESP_PROTOCOL_VERSION = 0x1414853;

// >>> format(struct.unpack('<I', b'SHA\2')[0], 'x')
// '2414853'
constexpr uint32_t SHARD_LOG_PROTOCOL_VERSION = 0x2414853;

// >>> format(struct.unpack('<I', b'CDC\0')[0], 'x')
// '434443'
constexpr uint32_t CDC_REQ_PROTOCOL_VERSION = 0x434443;

// >>> format(struct.unpack('<I', b'CDC\1')[0], 'x')
// '1434443'
constexpr uint32_t CDC_RESP_PROTOCOL_VERSION = 0x1434443;

// >>> format(struct.unpack('<I', b'SHU\0')[0], 'x')
// '554853'
constexpr uint32_t SHUCKLE_REQ_PROTOCOL_VERSION = 0x554853;

// >>> format(struct.unpack('<I', b'SHU\1')[0], 'x')
// '1554853'
constexpr uint32_t SHUCKLE_RESP_PROTOCOL_VERSION = 0x1554853;

// >>> format(struct.unpack('<I', b'LOG\0')[0], 'x')
// '474f4c'
constexpr uint32_t  LOG_REQ_PROTOCOL_VERSION = 0x474f4c;

// >>> format(struct.unpack('<I', b'LOG\1')[0], 'x')
// '1474f4c'
constexpr uint32_t LOG_RESP_PROTOCOL_VERSION = 0x1474f4c;

using ShardReqMsg = ProtocolMessage<SHARD_REQ_PROTOCOL_VERSION, ShardReqContainer>;
using SignedShardReqMsg = SignedProtocolMessage<SHARD_REQ_PROTOCOL_VERSION, ShardReqContainer>;
using ShardRespMsg = ProtocolMessage<SHARD_RESP_PROTOCOL_VERSION, ShardRespContainer>;
using CDCReqMsg = ProtocolMessage<CDC_REQ_PROTOCOL_VERSION, CDCReqContainer>;
using CDCRespMsg = ProtocolMessage<CDC_RESP_PROTOCOL_VERSION, CDCRespContainer>;
using LogReqMsg = SignedProtocolMessage<LOG_REQ_PROTOCOL_VERSION, LogReqContainer>;
using LogRespMsg = SignedProtocolMessage<LOG_RESP_PROTOCOL_VERSION, LogRespContainer>;
