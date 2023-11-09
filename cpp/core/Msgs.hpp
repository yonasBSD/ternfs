#pragma once

#include <variant>

#include "Assert.hpp"
#include "Common.hpp"
#include "Bincode.hpp"
#include "Time.hpp"

enum class InodeType : uint8_t {
    RESERVED = 0,
    DIRECTORY = 1,
    FILE = 2,
    SYMLINK = 3,
};

struct ShardId {
    uint8_t u8;

    constexpr ShardId(): u8(0) {}

    constexpr ShardId(uint8_t id): u8(id) {}

    bool operator==(ShardId rhs) const {
        return u8 == rhs.u8;
    }

    void pack(BincodeBuf& buf) const {
        buf.packScalar<uint8_t>(u8);
    }

    void unpack(BincodeBuf& buf) {
        u8 = buf.unpackScalar<uint8_t>();
    }
};

std::ostream& operator<<(std::ostream& out, ShardId shard);

// 63-bit:
// TTIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIISSSSSSSS
struct InodeId {
    uint64_t u64;

    constexpr InodeId(): u64(0) {}

    static constexpr InodeId FromU64(uint64_t data) {
        InodeId x;
        x.u64 = data;
        ALWAYS_ASSERT(x.valid());
        return x;
    }

    static constexpr InodeId FromU64Unchecked(uint64_t data) {
        InodeId x;
        x.u64 = data;
        return x;
    }

    constexpr InodeId(InodeType type, ShardId shard, uint64_t id): u64(((uint64_t)type << 61) | (id << 8) | (uint64_t)shard.u8) {
        ALWAYS_ASSERT(valid());
    }

    constexpr bool valid() const {
        return null() || ((id() < (1ull<<53)-1) && (type() != InodeType::RESERVED));
    }

    constexpr InodeType type() const {
        return (InodeType)((u64 >> 61) & 0x03);
    }

    constexpr uint64_t id() const {
        return (u64 & ~(7ull << 61)) >> 8;
    }

    constexpr ShardId shard() const {
        return ShardId(u64 & 0xFF);
    }

    constexpr bool null() const {
        return u64 == 0;
    }

    bool operator==(InodeId rhs) const {
        return u64 == rhs.u64;
    }

    bool operator!=(InodeId rhs) const {
        return u64 != rhs.u64;
    }

    void pack(BincodeBuf& buf) const {
        buf.packScalar<uint64_t>(u64);
    }

    void unpack(BincodeBuf& buf) {
        u64 = buf.unpackScalar<uint64_t>();
        if (unlikely(!valid())) {
            throw BINCODE_EXCEPTION("bad InodeId %s", u64);
        }
    }

    size_t packedSize() const {
        return sizeof(*this);
    }
};

template<>
struct std::hash<InodeId> {
    std::size_t operator()(InodeId id) const noexcept {
        return std::hash<uint64_t>{}(id.u64);
    }
};

std::ostream& operator<<(std::ostream& out, InodeId id);

constexpr InodeId NULL_INODE_ID = InodeId::FromU64(0);
constexpr InodeId ROOT_DIR_INODE_ID(InodeType::DIRECTORY, ShardId(0), 0);

// 63-bit:
// OTTIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIIISSSSSSSS
struct InodeIdExtra {
    uint64_t u64;

    InodeIdExtra(): u64(0) {}

    static InodeIdExtra FromU64(uint64_t data) {
        InodeIdExtra x;
        x.u64 = data;
        ALWAYS_ASSERT(x.id().valid());
        return x;
    }

    InodeIdExtra(InodeId id, bool extra) {
        u64 = id.u64 | (extra ? (1ull<<63) : 0);
    }

    InodeId id() const {
        return InodeId::FromU64(u64 & ~(1ull<<63));
    }

    bool extra() const {
        return u64 >> 63;
    }

    void pack(BincodeBuf& buf) const {
        buf.packScalar<uint64_t>(u64);
    }

    void unpack(BincodeBuf& buf) {
        u64 = buf.unpackScalar<uint64_t>();
        if (unlikely(!id().valid())) {
            throw BINCODE_EXCEPTION("bad OwnedInodeId %s", u64);
        }
    }

    bool operator==(InodeIdExtra rhs) const {
        return u64 == rhs.u64;
    }
};

std::ostream& operator<<(std::ostream& out, InodeIdExtra id);

struct Parity {
    uint8_t u8;

    constexpr Parity(): u8(0) {}

    constexpr Parity(uint8_t data): u8(data) {}

    constexpr Parity(uint8_t dataBlocks, uint8_t parityBlocks): u8(dataBlocks | (parityBlocks << 4)) {
        ALWAYS_ASSERT(dataBlocks != 0 && dataBlocks < 16);
        ALWAYS_ASSERT(parityBlocks < 16);
    }

    constexpr uint8_t dataBlocks() const {
        return u8 & 0x0F;
    }

    constexpr uint8_t parityBlocks() const {
        return u8 >> 4;
    }

    constexpr uint8_t blocks() const {
        return dataBlocks()+parityBlocks();
    }

    void pack(BincodeBuf& buf) const {
        buf.packScalar<uint8_t>(u8);
    }

    void unpack(BincodeBuf& buf) {
        u8 = buf.unpackScalar<uint8_t>();
    }

    bool operator==(Parity rhs) const {
        return u8 == rhs.u8;
    }
};

std::ostream& operator<<(std::ostream& out, Parity parity);

constexpr Parity NO_PARITY(0);

constexpr uint8_t EMPTY_STORAGE = 0;
constexpr uint8_t INLINE_STORAGE = 1;

uint8_t storageClassByName(const char* name);

struct Crc {
    uint32_t u32;

    Crc() : u32(0) {}
    Crc(uint32_t x) : u32(x) {}

    void pack(BincodeBuf& buf) const {
        buf.packScalar(u32);
    }

    void unpack(BincodeBuf& buf) {
        u32 = buf.unpackScalar<uint32_t>();
    }

    size_t packedSize() const {
        return sizeof(u32);
    }

    bool operator==(Crc other) const {
        return u32 == other.u32;
    }
};

std::ostream& operator<<(std::ostream& out, Crc crc);

struct BlockServiceId {
    uint64_t u64;

    BlockServiceId(): u64(0) {}
    BlockServiceId(uint64_t x): u64(x) {}

    void pack(BincodeBuf& buf) const {
        buf.packScalar(u64);
    }

    void unpack(BincodeBuf& buf) {
        u64 = buf.unpackScalar<uint64_t>();
    }

    size_t packedSize() const {
        return sizeof(u64);
    }

    bool operator==(BlockServiceId other) const {
        return u64 == other.u64;
    }
};

std::ostream& operator<<(std::ostream& out, BlockServiceId crc);

#include "MsgsGen.hpp"

// We often use this as a optional<EggsError>;
constexpr EggsError NO_ERROR = (EggsError)0;

constexpr bool isPrivilegedRequestKind(ShardMessageKind kind) {
    return (uint8_t)kind & 0x80;
}

// >>> format(struct.unpack('<I', b'SHA\0')[0], 'x')
// '414853'
constexpr uint32_t SHARD_REQ_PROTOCOL_VERSION = 0x414853;

// >>> format(struct.unpack('<I', b'SHA\1')[0], 'x')
// '1414853'
constexpr uint32_t SHARD_RESP_PROTOCOL_VERSION = 0x1414853;

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

// If this doesn't parse, no point in continuing attempting to parse
// the request.
struct ShardRequestHeader {
    uint64_t requestId;
    // This is not guaranteed to be a valid shard request kind yet.
    // The caller will have to validate.
    ShardMessageKind kind;

    void pack(BincodeBuf& buf) {
        buf.packScalar<uint32_t>(SHARD_REQ_PROTOCOL_VERSION);
        buf.packScalar<uint64_t>(requestId);
        buf.packScalar<uint8_t>((uint8_t)kind);
    }

    void unpack(BincodeBuf& buf) {
        uint32_t version = buf.unpackScalar<uint32_t>();
        if (version != SHARD_REQ_PROTOCOL_VERSION) {
            throw BINCODE_EXCEPTION("bad shard req protocol version %s, expected %s", version, SHARD_REQ_PROTOCOL_VERSION);
        }
        requestId = buf.unpackScalar<uint64_t>();
        kind = (ShardMessageKind)buf.unpackScalar<uint8_t>();
    }
};

struct ShardResponseHeader {
    uint64_t requestId;
    ShardMessageKind kind;

    // protocol + requestId + kind
    static constexpr uint16_t STATIC_SIZE = 4 + 8 + 1;

    ShardResponseHeader() = default;
    ShardResponseHeader(uint64_t requestId_, ShardMessageKind kind_): requestId(requestId_), kind(kind_) {}

    void pack(BincodeBuf& buf) {
        buf.packScalar<uint32_t>(SHARD_RESP_PROTOCOL_VERSION);
        buf.packScalar<uint64_t>(requestId);
        buf.packScalar<uint8_t>((uint8_t)kind);
    }

    void unpack(BincodeBuf& buf) {
        uint32_t version = buf.unpackScalar<uint32_t>();
        if (version != SHARD_RESP_PROTOCOL_VERSION) {
            throw BINCODE_EXCEPTION("bad shard resp protocol version %s, expected %s", version, SHARD_RESP_PROTOCOL_VERSION);
        }
        requestId = buf.unpackScalar<uint64_t>();
        kind = (ShardMessageKind)buf.unpackScalar<uint8_t>();
    }
};

// If this doesn't parse, no point in continuing attempting to parse
// the request.
struct CDCRequestHeader {
    uint64_t requestId;
    // This is not guaranteed to be a valid shard request kind yet.
    // The caller will have to validate.
    CDCMessageKind kind;

    void unpack(BincodeBuf& buf) {
        uint32_t version = buf.unpackScalar<uint32_t>();
        if (version != CDC_REQ_PROTOCOL_VERSION) {
            throw BINCODE_EXCEPTION("bad shard req protocol version %s, expected %s", version, CDC_REQ_PROTOCOL_VERSION);
        }
        requestId = buf.unpackScalar<uint64_t>();
        kind = (CDCMessageKind)buf.unpackScalar<uint8_t>();
    }
};

struct CDCResponseHeader {
    uint64_t requestId;
    CDCMessageKind kind;

    // protocol + requestId + kind
    static constexpr uint16_t STATIC_SIZE = 4 + 8 + 1;

    CDCResponseHeader(uint64_t requestId_, CDCMessageKind kind_): requestId(requestId_), kind(kind_) {}

    void pack(BincodeBuf& buf) {
        buf.packScalar<uint32_t>(CDC_RESP_PROTOCOL_VERSION);
        buf.packScalar<uint64_t>(requestId);
        buf.packScalar<uint8_t>((uint8_t)kind);
    }
};

static constexpr uint8_t SNAPSHOT_POLICY_TAG = 1;
static constexpr uint8_t SPAN_POLICY_TAG = 2;
static constexpr uint8_t BLOCK_POLICY_TAG = 3;
static constexpr uint8_t STRIPE_POLICY_TAG = 4;
static std::array<uint8_t, 4> REQUIRED_DIR_INFO_TAGS = {SNAPSHOT_POLICY_TAG, SPAN_POLICY_TAG, BLOCK_POLICY_TAG, STRIPE_POLICY_TAG};