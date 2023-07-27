// Automatically generated with go run bincodegen.
// Run `go generate ./...` from the go/ directory to regenerate it.
#pragma once
#include "Msgs.hpp"

enum class EggsError : uint16_t {
    INTERNAL_ERROR = 10,
    FATAL_ERROR = 11,
    TIMEOUT = 12,
    MALFORMED_REQUEST = 13,
    MALFORMED_RESPONSE = 14,
    NOT_AUTHORISED = 15,
    UNRECOGNIZED_REQUEST = 16,
    FILE_NOT_FOUND = 17,
    DIRECTORY_NOT_FOUND = 18,
    NAME_NOT_FOUND = 19,
    EDGE_NOT_FOUND = 20,
    EDGE_IS_LOCKED = 21,
    TYPE_IS_DIRECTORY = 22,
    TYPE_IS_NOT_DIRECTORY = 23,
    BAD_COOKIE = 24,
    INCONSISTENT_STORAGE_CLASS_PARITY = 25,
    LAST_SPAN_STATE_NOT_CLEAN = 26,
    COULD_NOT_PICK_BLOCK_SERVICES = 27,
    BAD_SPAN_BODY = 28,
    SPAN_NOT_FOUND = 29,
    BLOCK_SERVICE_NOT_FOUND = 30,
    CANNOT_CERTIFY_BLOCKLESS_SPAN = 31,
    BAD_NUMBER_OF_BLOCKS_PROOFS = 32,
    BAD_BLOCK_PROOF = 33,
    CANNOT_OVERRIDE_NAME = 34,
    NAME_IS_LOCKED = 35,
    MTIME_IS_TOO_RECENT = 36,
    MISMATCHING_TARGET = 37,
    MISMATCHING_OWNER = 38,
    MISMATCHING_CREATION_TIME = 39,
    DIRECTORY_NOT_EMPTY = 40,
    FILE_IS_TRANSIENT = 41,
    OLD_DIRECTORY_NOT_FOUND = 42,
    NEW_DIRECTORY_NOT_FOUND = 43,
    LOOP_IN_DIRECTORY_RENAME = 44,
    DIRECTORY_HAS_OWNER = 45,
    FILE_IS_NOT_TRANSIENT = 46,
    FILE_NOT_EMPTY = 47,
    CANNOT_REMOVE_ROOT_DIRECTORY = 48,
    FILE_EMPTY = 49,
    CANNOT_REMOVE_DIRTY_SPAN = 50,
    BAD_SHARD = 51,
    BAD_NAME = 52,
    MORE_RECENT_SNAPSHOT_EDGE = 53,
    MORE_RECENT_CURRENT_EDGE = 54,
    BAD_DIRECTORY_INFO = 55,
    DEADLINE_NOT_PASSED = 56,
    SAME_SOURCE_AND_DESTINATION = 57,
    SAME_DIRECTORIES = 58,
    SAME_SHARD = 59,
    BAD_PROTOCOL_VERSION = 60,
    BAD_CERTIFICATE = 61,
    BLOCK_TOO_RECENT_FOR_DELETION = 62,
    BLOCK_FETCH_OUT_OF_BOUNDS = 63,
    BAD_BLOCK_CRC = 64,
    BLOCK_TOO_BIG = 65,
    BLOCK_NOT_FOUND = 66,
    CANNOT_UNSET_DECOMMISSIONED = 67,
    CANNOT_REGISTER_DECOMMISSIONED = 68,
};

std::ostream& operator<<(std::ostream& out, EggsError err);

enum class ShardMessageKind : uint8_t {
    ERROR = 0,
    LOOKUP = 1,
    STAT_FILE = 2,
    STAT_DIRECTORY = 4,
    READ_DIR = 5,
    CONSTRUCT_FILE = 6,
    ADD_SPAN_INITIATE = 7,
    ADD_SPAN_CERTIFY = 8,
    LINK_FILE = 9,
    SOFT_UNLINK_FILE = 10,
    FILE_SPANS = 11,
    SAME_DIRECTORY_RENAME = 12,
    ADD_INLINE_SPAN = 16,
    SET_TIME = 17,
    FULL_READ_DIR = 115,
    MOVE_SPAN = 123,
    REMOVE_NON_OWNED_EDGE = 116,
    SAME_SHARD_HARD_FILE_UNLINK = 117,
    STAT_TRANSIENT_FILE = 3,
    SET_DIRECTORY_INFO = 13,
    EXPIRE_TRANSIENT_FILE = 15,
    VISIT_DIRECTORIES = 112,
    VISIT_FILES = 113,
    VISIT_TRANSIENT_FILES = 114,
    REMOVE_SPAN_INITIATE = 118,
    REMOVE_SPAN_CERTIFY = 119,
    SWAP_BLOCKS = 120,
    BLOCK_SERVICE_FILES = 121,
    REMOVE_INODE = 122,
    CREATE_DIRECTORY_INODE = 128,
    SET_DIRECTORY_OWNER = 129,
    REMOVE_DIRECTORY_OWNER = 137,
    CREATE_LOCKED_CURRENT_EDGE = 130,
    LOCK_CURRENT_EDGE = 131,
    UNLOCK_CURRENT_EDGE = 132,
    REMOVE_OWNED_SNAPSHOT_FILE_EDGE = 134,
    MAKE_FILE_TRANSIENT = 135,
};

const std::vector<ShardMessageKind> allShardMessageKind {
    ShardMessageKind::LOOKUP,
    ShardMessageKind::STAT_FILE,
    ShardMessageKind::STAT_DIRECTORY,
    ShardMessageKind::READ_DIR,
    ShardMessageKind::CONSTRUCT_FILE,
    ShardMessageKind::ADD_SPAN_INITIATE,
    ShardMessageKind::ADD_SPAN_CERTIFY,
    ShardMessageKind::LINK_FILE,
    ShardMessageKind::SOFT_UNLINK_FILE,
    ShardMessageKind::FILE_SPANS,
    ShardMessageKind::SAME_DIRECTORY_RENAME,
    ShardMessageKind::ADD_INLINE_SPAN,
    ShardMessageKind::SET_TIME,
    ShardMessageKind::FULL_READ_DIR,
    ShardMessageKind::MOVE_SPAN,
    ShardMessageKind::REMOVE_NON_OWNED_EDGE,
    ShardMessageKind::SAME_SHARD_HARD_FILE_UNLINK,
    ShardMessageKind::STAT_TRANSIENT_FILE,
    ShardMessageKind::SET_DIRECTORY_INFO,
    ShardMessageKind::EXPIRE_TRANSIENT_FILE,
    ShardMessageKind::VISIT_DIRECTORIES,
    ShardMessageKind::VISIT_FILES,
    ShardMessageKind::VISIT_TRANSIENT_FILES,
    ShardMessageKind::REMOVE_SPAN_INITIATE,
    ShardMessageKind::REMOVE_SPAN_CERTIFY,
    ShardMessageKind::SWAP_BLOCKS,
    ShardMessageKind::BLOCK_SERVICE_FILES,
    ShardMessageKind::REMOVE_INODE,
    ShardMessageKind::CREATE_DIRECTORY_INODE,
    ShardMessageKind::SET_DIRECTORY_OWNER,
    ShardMessageKind::REMOVE_DIRECTORY_OWNER,
    ShardMessageKind::CREATE_LOCKED_CURRENT_EDGE,
    ShardMessageKind::LOCK_CURRENT_EDGE,
    ShardMessageKind::UNLOCK_CURRENT_EDGE,
    ShardMessageKind::REMOVE_OWNED_SNAPSHOT_FILE_EDGE,
    ShardMessageKind::MAKE_FILE_TRANSIENT,
};

constexpr int maxShardMessageKind = 137;

std::ostream& operator<<(std::ostream& out, ShardMessageKind kind);

enum class CDCMessageKind : uint8_t {
    ERROR = 0,
    MAKE_DIRECTORY = 1,
    RENAME_FILE = 2,
    SOFT_UNLINK_DIRECTORY = 3,
    RENAME_DIRECTORY = 4,
    HARD_UNLINK_DIRECTORY = 5,
    CROSS_SHARD_HARD_UNLINK_FILE = 6,
};

const std::vector<CDCMessageKind> allCDCMessageKind {
    CDCMessageKind::MAKE_DIRECTORY,
    CDCMessageKind::RENAME_FILE,
    CDCMessageKind::SOFT_UNLINK_DIRECTORY,
    CDCMessageKind::RENAME_DIRECTORY,
    CDCMessageKind::HARD_UNLINK_DIRECTORY,
    CDCMessageKind::CROSS_SHARD_HARD_UNLINK_FILE,
};

constexpr int maxCDCMessageKind = 6;

std::ostream& operator<<(std::ostream& out, CDCMessageKind kind);

enum class ShuckleMessageKind : uint8_t {
    ERROR = 0,
    SHARDS = 3,
    CDC = 7,
    INFO = 8,
    REGISTER_BLOCK_SERVICES = 2,
    REGISTER_SHARD = 4,
    ALL_BLOCK_SERVICES = 5,
    REGISTER_CDC = 6,
    SET_BLOCK_SERVICE_FLAGS = 9,
    BLOCK_SERVICE = 10,
    INSERT_STATS = 11,
    SHARD = 12,
    GET_STATS = 13,
};

const std::vector<ShuckleMessageKind> allShuckleMessageKind {
    ShuckleMessageKind::SHARDS,
    ShuckleMessageKind::CDC,
    ShuckleMessageKind::INFO,
    ShuckleMessageKind::REGISTER_BLOCK_SERVICES,
    ShuckleMessageKind::REGISTER_SHARD,
    ShuckleMessageKind::ALL_BLOCK_SERVICES,
    ShuckleMessageKind::REGISTER_CDC,
    ShuckleMessageKind::SET_BLOCK_SERVICE_FLAGS,
    ShuckleMessageKind::BLOCK_SERVICE,
    ShuckleMessageKind::INSERT_STATS,
    ShuckleMessageKind::SHARD,
    ShuckleMessageKind::GET_STATS,
};

constexpr int maxShuckleMessageKind = 13;

std::ostream& operator<<(std::ostream& out, ShuckleMessageKind kind);

enum class BlocksMessageKind : uint8_t {
    ERROR = 0,
    FETCH_BLOCK = 2,
    WRITE_BLOCK = 3,
    ERASE_BLOCK = 1,
    TEST_WRITE = 5,
};

const std::vector<BlocksMessageKind> allBlocksMessageKind {
    BlocksMessageKind::FETCH_BLOCK,
    BlocksMessageKind::WRITE_BLOCK,
    BlocksMessageKind::ERASE_BLOCK,
    BlocksMessageKind::TEST_WRITE,
};

constexpr int maxBlocksMessageKind = 5;

std::ostream& operator<<(std::ostream& out, BlocksMessageKind kind);

struct FailureDomain {
    BincodeFixedBytes<16> name;

    static constexpr uint16_t STATIC_SIZE = BincodeFixedBytes<16>::STATIC_SIZE; // name

    FailureDomain() { clear(); }
    size_t packedSize() const {
        size_t _size = 0;
        _size += BincodeFixedBytes<16>::STATIC_SIZE; // name
        return _size;
    }
    void pack(BincodeBuf& buf) const;
    void unpack(BincodeBuf& buf);
    void clear();
    bool operator==(const FailureDomain&rhs) const;
};

std::ostream& operator<<(std::ostream& out, const FailureDomain& x);

struct DirectoryInfoEntry {
    uint8_t tag;
    BincodeBytes body;

    static constexpr uint16_t STATIC_SIZE = 1 + BincodeBytes::STATIC_SIZE; // tag + body

    DirectoryInfoEntry() { clear(); }
    size_t packedSize() const {
        size_t _size = 0;
        _size += 1; // tag
        _size += body.packedSize(); // body
        return _size;
    }
    void pack(BincodeBuf& buf) const;
    void unpack(BincodeBuf& buf);
    void clear();
    bool operator==(const DirectoryInfoEntry&rhs) const;
};

std::ostream& operator<<(std::ostream& out, const DirectoryInfoEntry& x);

struct DirectoryInfo {
    BincodeList<DirectoryInfoEntry> entries;

    static constexpr uint16_t STATIC_SIZE = BincodeList<DirectoryInfoEntry>::STATIC_SIZE; // entries

    DirectoryInfo() { clear(); }
    size_t packedSize() const {
        size_t _size = 0;
        _size += entries.packedSize(); // entries
        return _size;
    }
    void pack(BincodeBuf& buf) const;
    void unpack(BincodeBuf& buf);
    void clear();
    bool operator==(const DirectoryInfo&rhs) const;
};

std::ostream& operator<<(std::ostream& out, const DirectoryInfo& x);

struct CurrentEdge {
    InodeId targetId;
    uint64_t nameHash;
    BincodeBytes name;
    EggsTime creationTime;

    static constexpr uint16_t STATIC_SIZE = 8 + 8 + BincodeBytes::STATIC_SIZE + 8; // targetId + nameHash + name + creationTime

    CurrentEdge() { clear(); }
    size_t packedSize() const {
        size_t _size = 0;
        _size += 8; // targetId
        _size += 8; // nameHash
        _size += name.packedSize(); // name
        _size += 8; // creationTime
        return _size;
    }
    void pack(BincodeBuf& buf) const;
    void unpack(BincodeBuf& buf);
    void clear();
    bool operator==(const CurrentEdge&rhs) const;
};

std::ostream& operator<<(std::ostream& out, const CurrentEdge& x);

struct BlockInfo {
    BincodeFixedBytes<4> blockServiceIp1;
    uint16_t blockServicePort1;
    BincodeFixedBytes<4> blockServiceIp2;
    uint16_t blockServicePort2;
    BlockServiceId blockServiceId;
    FailureDomain blockServiceFailureDomain;
    uint64_t blockId;
    BincodeFixedBytes<8> certificate;

    static constexpr uint16_t STATIC_SIZE = BincodeFixedBytes<4>::STATIC_SIZE + 2 + BincodeFixedBytes<4>::STATIC_SIZE + 2 + 8 + FailureDomain::STATIC_SIZE + 8 + BincodeFixedBytes<8>::STATIC_SIZE; // blockServiceIp1 + blockServicePort1 + blockServiceIp2 + blockServicePort2 + blockServiceId + blockServiceFailureDomain + blockId + certificate

    BlockInfo() { clear(); }
    size_t packedSize() const {
        size_t _size = 0;
        _size += BincodeFixedBytes<4>::STATIC_SIZE; // blockServiceIp1
        _size += 2; // blockServicePort1
        _size += BincodeFixedBytes<4>::STATIC_SIZE; // blockServiceIp2
        _size += 2; // blockServicePort2
        _size += 8; // blockServiceId
        _size += blockServiceFailureDomain.packedSize(); // blockServiceFailureDomain
        _size += 8; // blockId
        _size += BincodeFixedBytes<8>::STATIC_SIZE; // certificate
        return _size;
    }
    void pack(BincodeBuf& buf) const;
    void unpack(BincodeBuf& buf);
    void clear();
    bool operator==(const BlockInfo&rhs) const;
};

std::ostream& operator<<(std::ostream& out, const BlockInfo& x);

struct BlockProof {
    uint64_t blockId;
    BincodeFixedBytes<8> proof;

    static constexpr uint16_t STATIC_SIZE = 8 + BincodeFixedBytes<8>::STATIC_SIZE; // blockId + proof

    BlockProof() { clear(); }
    size_t packedSize() const {
        size_t _size = 0;
        _size += 8; // blockId
        _size += BincodeFixedBytes<8>::STATIC_SIZE; // proof
        return _size;
    }
    void pack(BincodeBuf& buf) const;
    void unpack(BincodeBuf& buf);
    void clear();
    bool operator==(const BlockProof&rhs) const;
};

std::ostream& operator<<(std::ostream& out, const BlockProof& x);

struct BlockService {
    BincodeFixedBytes<4> ip1;
    uint16_t port1;
    BincodeFixedBytes<4> ip2;
    uint16_t port2;
    BlockServiceId id;
    uint8_t flags;

    static constexpr uint16_t STATIC_SIZE = BincodeFixedBytes<4>::STATIC_SIZE + 2 + BincodeFixedBytes<4>::STATIC_SIZE + 2 + 8 + 1; // ip1 + port1 + ip2 + port2 + id + flags

    BlockService() { clear(); }
    size_t packedSize() const {
        size_t _size = 0;
        _size += BincodeFixedBytes<4>::STATIC_SIZE; // ip1
        _size += 2; // port1
        _size += BincodeFixedBytes<4>::STATIC_SIZE; // ip2
        _size += 2; // port2
        _size += 8; // id
        _size += 1; // flags
        return _size;
    }
    void pack(BincodeBuf& buf) const;
    void unpack(BincodeBuf& buf);
    void clear();
    bool operator==(const BlockService&rhs) const;
};

std::ostream& operator<<(std::ostream& out, const BlockService& x);

struct ShardInfo {
    BincodeFixedBytes<4> ip1;
    uint16_t port1;
    BincodeFixedBytes<4> ip2;
    uint16_t port2;
    EggsTime lastSeen;

    static constexpr uint16_t STATIC_SIZE = BincodeFixedBytes<4>::STATIC_SIZE + 2 + BincodeFixedBytes<4>::STATIC_SIZE + 2 + 8; // ip1 + port1 + ip2 + port2 + lastSeen

    ShardInfo() { clear(); }
    size_t packedSize() const {
        size_t _size = 0;
        _size += BincodeFixedBytes<4>::STATIC_SIZE; // ip1
        _size += 2; // port1
        _size += BincodeFixedBytes<4>::STATIC_SIZE; // ip2
        _size += 2; // port2
        _size += 8; // lastSeen
        return _size;
    }
    void pack(BincodeBuf& buf) const;
    void unpack(BincodeBuf& buf);
    void clear();
    bool operator==(const ShardInfo&rhs) const;
};

std::ostream& operator<<(std::ostream& out, const ShardInfo& x);

struct BlockPolicyEntry {
    uint8_t storageClass;
    uint32_t minSize;

    static constexpr uint16_t STATIC_SIZE = 1 + 4; // storageClass + minSize

    BlockPolicyEntry() { clear(); }
    size_t packedSize() const {
        size_t _size = 0;
        _size += 1; // storageClass
        _size += 4; // minSize
        return _size;
    }
    void pack(BincodeBuf& buf) const;
    void unpack(BincodeBuf& buf);
    void clear();
    bool operator==(const BlockPolicyEntry&rhs) const;
};

std::ostream& operator<<(std::ostream& out, const BlockPolicyEntry& x);

struct SpanPolicyEntry {
    uint32_t maxSize;
    Parity parity;

    static constexpr uint16_t STATIC_SIZE = 4 + 1; // maxSize + parity

    SpanPolicyEntry() { clear(); }
    size_t packedSize() const {
        size_t _size = 0;
        _size += 4; // maxSize
        _size += 1; // parity
        return _size;
    }
    void pack(BincodeBuf& buf) const;
    void unpack(BincodeBuf& buf);
    void clear();
    bool operator==(const SpanPolicyEntry&rhs) const;
};

std::ostream& operator<<(std::ostream& out, const SpanPolicyEntry& x);

struct StripePolicy {
    uint32_t targetStripeSize;

    static constexpr uint16_t STATIC_SIZE = 4; // targetStripeSize

    StripePolicy() { clear(); }
    size_t packedSize() const {
        size_t _size = 0;
        _size += 4; // targetStripeSize
        return _size;
    }
    void pack(BincodeBuf& buf) const;
    void unpack(BincodeBuf& buf);
    void clear();
    bool operator==(const StripePolicy&rhs) const;
};

std::ostream& operator<<(std::ostream& out, const StripePolicy& x);

struct FetchedBlock {
    uint8_t blockServiceIx;
    uint64_t blockId;
    Crc crc;

    static constexpr uint16_t STATIC_SIZE = 1 + 8 + 4; // blockServiceIx + blockId + crc

    FetchedBlock() { clear(); }
    size_t packedSize() const {
        size_t _size = 0;
        _size += 1; // blockServiceIx
        _size += 8; // blockId
        _size += 4; // crc
        return _size;
    }
    void pack(BincodeBuf& buf) const;
    void unpack(BincodeBuf& buf);
    void clear();
    bool operator==(const FetchedBlock&rhs) const;
};

std::ostream& operator<<(std::ostream& out, const FetchedBlock& x);

struct FetchedSpanHeader {
    uint64_t byteOffset;
    uint32_t size;
    Crc crc;
    uint8_t storageClass;

    static constexpr uint16_t STATIC_SIZE = 8 + 4 + 4 + 1; // byteOffset + size + crc + storageClass

    FetchedSpanHeader() { clear(); }
    size_t packedSize() const {
        size_t _size = 0;
        _size += 8; // byteOffset
        _size += 4; // size
        _size += 4; // crc
        _size += 1; // storageClass
        return _size;
    }
    void pack(BincodeBuf& buf) const;
    void unpack(BincodeBuf& buf);
    void clear();
    bool operator==(const FetchedSpanHeader&rhs) const;
};

std::ostream& operator<<(std::ostream& out, const FetchedSpanHeader& x);

struct FetchedInlineSpan {
    BincodeBytes body;

    static constexpr uint16_t STATIC_SIZE = BincodeBytes::STATIC_SIZE; // body

    FetchedInlineSpan() { clear(); }
    size_t packedSize() const {
        size_t _size = 0;
        _size += body.packedSize(); // body
        return _size;
    }
    void pack(BincodeBuf& buf) const;
    void unpack(BincodeBuf& buf);
    void clear();
    bool operator==(const FetchedInlineSpan&rhs) const;
};

std::ostream& operator<<(std::ostream& out, const FetchedInlineSpan& x);

struct FetchedBlocksSpan {
    Parity parity;
    uint8_t stripes;
    uint32_t cellSize;
    BincodeList<FetchedBlock> blocks;
    BincodeList<Crc> stripesCrc;

    static constexpr uint16_t STATIC_SIZE = 1 + 1 + 4 + BincodeList<FetchedBlock>::STATIC_SIZE + BincodeList<Crc>::STATIC_SIZE; // parity + stripes + cellSize + blocks + stripesCrc

    FetchedBlocksSpan() { clear(); }
    size_t packedSize() const {
        size_t _size = 0;
        _size += 1; // parity
        _size += 1; // stripes
        _size += 4; // cellSize
        _size += blocks.packedSize(); // blocks
        _size += stripesCrc.packedSize(); // stripesCrc
        return _size;
    }
    void pack(BincodeBuf& buf) const;
    void unpack(BincodeBuf& buf);
    void clear();
    bool operator==(const FetchedBlocksSpan&rhs) const;
};

std::ostream& operator<<(std::ostream& out, const FetchedBlocksSpan& x);


struct FetchedSpan {
public:
    FetchedSpanHeader header;
private:
    alignas(FetchedInlineSpan) alignas(FetchedBlocksSpan) char* body[std::max(sizeof(FetchedInlineSpan), sizeof(FetchedBlocksSpan))];

    void destructBody() {
        if (header.storageClass == INLINE_STORAGE) {
            ((FetchedInlineSpan*)body)->~FetchedInlineSpan();
        } else if (header.storageClass > INLINE_STORAGE) {
            ((FetchedBlocksSpan*)body)->~FetchedBlocksSpan();
        }
    }

public:
    static constexpr uint16_t STATIC_SIZE = FetchedSpanHeader::STATIC_SIZE;

    FetchedSpan() { clear(); }

    const FetchedInlineSpan& getInlineSpan() const {
        ALWAYS_ASSERT(header.storageClass == INLINE_STORAGE);
        return *(const FetchedInlineSpan*)body;
    }
    FetchedInlineSpan& setInlineSpan() {
        destructBody();
        header.storageClass = INLINE_STORAGE;
        return *(new (body) FetchedInlineSpan());
    }

    const FetchedBlocksSpan& getBlocksSpan() const {
        ALWAYS_ASSERT(header.storageClass > INLINE_STORAGE);
        return *(const FetchedBlocksSpan*)body;
    }
    FetchedBlocksSpan& setBlocksSpan(uint8_t s) {
        ALWAYS_ASSERT(s > INLINE_STORAGE);
        destructBody();
        header.storageClass = s;
        return *(new (body) FetchedBlocksSpan());
    }

    void clear() {
        header.clear();
        destructBody();
    }

    size_t packedSize() const {
        ALWAYS_ASSERT(header.storageClass != 0);
        size_t size = STATIC_SIZE;
        if (header.storageClass == INLINE_STORAGE) {
            size += getInlineSpan().packedSize();
        } else if (header.storageClass > INLINE_STORAGE) {
            size += getBlocksSpan().packedSize();
        }
        return size;
    }

    void pack(BincodeBuf& buf) const {
        ALWAYS_ASSERT(header.storageClass != 0);
        header.pack(buf);
        if (header.storageClass == INLINE_STORAGE) {
            getInlineSpan().pack(buf);
        } else if (header.storageClass > INLINE_STORAGE) {
            getBlocksSpan().pack(buf);
        }
    }

    void unpack(BincodeBuf& buf) {
        header.unpack(buf);
        if (header.storageClass == 0) {
            throw BINCODE_EXCEPTION("Unexpected EMPTY storage class");
        }
        if (header.storageClass == INLINE_STORAGE) {
            setInlineSpan().unpack(buf);
        } else if (header.storageClass > INLINE_STORAGE) {
            setBlocksSpan(header.storageClass).unpack(buf);
        }
    }

    bool operator==(const FetchedSpan& other) const {
        if (header != other.header) {
            return false;
        }
        ALWAYS_ASSERT(header.storageClass != 0);
        if (header.storageClass != other.header.storageClass) {
            return false;
        }
        if (header.storageClass == INLINE_STORAGE) {
            return getInlineSpan() == other.getInlineSpan();
        } else if (header.storageClass > INLINE_STORAGE) {
            return getBlocksSpan() == other.getBlocksSpan();
        }
        return true;
    }
};

UNUSED
static std::ostream& operator<<(std::ostream& out, const FetchedSpan& span) {
    ALWAYS_ASSERT(span.header.storageClass != 0);
    out << "FetchedSpan(" << "Header=" << span.header;
    if (span.header.storageClass == INLINE_STORAGE) {
        out << ", Body=" << span.getInlineSpan();
    } else if (span.header.storageClass > INLINE_STORAGE) {
        out << ", Body=" << span.getBlocksSpan();
    }
    out << ")";
    return out;
}
struct BlacklistEntry {
    FailureDomain failureDomain;
    BlockServiceId blockService;

    static constexpr uint16_t STATIC_SIZE = FailureDomain::STATIC_SIZE + 8; // failureDomain + blockService

    BlacklistEntry() { clear(); }
    size_t packedSize() const {
        size_t _size = 0;
        _size += failureDomain.packedSize(); // failureDomain
        _size += 8; // blockService
        return _size;
    }
    void pack(BincodeBuf& buf) const;
    void unpack(BincodeBuf& buf);
    void clear();
    bool operator==(const BlacklistEntry&rhs) const;
};

std::ostream& operator<<(std::ostream& out, const BlacklistEntry& x);

struct Edge {
    bool current;
    InodeIdExtra targetId;
    uint64_t nameHash;
    BincodeBytes name;
    EggsTime creationTime;

    static constexpr uint16_t STATIC_SIZE = 1 + 8 + 8 + BincodeBytes::STATIC_SIZE + 8; // current + targetId + nameHash + name + creationTime

    Edge() { clear(); }
    size_t packedSize() const {
        size_t _size = 0;
        _size += 1; // current
        _size += 8; // targetId
        _size += 8; // nameHash
        _size += name.packedSize(); // name
        _size += 8; // creationTime
        return _size;
    }
    void pack(BincodeBuf& buf) const;
    void unpack(BincodeBuf& buf);
    void clear();
    bool operator==(const Edge&rhs) const;
};

std::ostream& operator<<(std::ostream& out, const Edge& x);

struct FullReadDirCursor {
    bool current;
    BincodeBytes startName;
    EggsTime startTime;

    static constexpr uint16_t STATIC_SIZE = 1 + BincodeBytes::STATIC_SIZE + 8; // current + startName + startTime

    FullReadDirCursor() { clear(); }
    size_t packedSize() const {
        size_t _size = 0;
        _size += 1; // current
        _size += startName.packedSize(); // startName
        _size += 8; // startTime
        return _size;
    }
    void pack(BincodeBuf& buf) const;
    void unpack(BincodeBuf& buf);
    void clear();
    bool operator==(const FullReadDirCursor&rhs) const;
};

std::ostream& operator<<(std::ostream& out, const FullReadDirCursor& x);

struct TransientFile {
    InodeId id;
    BincodeFixedBytes<8> cookie;
    EggsTime deadlineTime;

    static constexpr uint16_t STATIC_SIZE = 8 + BincodeFixedBytes<8>::STATIC_SIZE + 8; // id + cookie + deadlineTime

    TransientFile() { clear(); }
    size_t packedSize() const {
        size_t _size = 0;
        _size += 8; // id
        _size += BincodeFixedBytes<8>::STATIC_SIZE; // cookie
        _size += 8; // deadlineTime
        return _size;
    }
    void pack(BincodeBuf& buf) const;
    void unpack(BincodeBuf& buf);
    void clear();
    bool operator==(const TransientFile&rhs) const;
};

std::ostream& operator<<(std::ostream& out, const TransientFile& x);

struct EntryNewBlockInfo {
    BlockServiceId blockServiceId;
    Crc crc;

    static constexpr uint16_t STATIC_SIZE = 8 + 4; // blockServiceId + crc

    EntryNewBlockInfo() { clear(); }
    size_t packedSize() const {
        size_t _size = 0;
        _size += 8; // blockServiceId
        _size += 4; // crc
        return _size;
    }
    void pack(BincodeBuf& buf) const;
    void unpack(BincodeBuf& buf);
    void clear();
    bool operator==(const EntryNewBlockInfo&rhs) const;
};

std::ostream& operator<<(std::ostream& out, const EntryNewBlockInfo& x);

struct BlockServiceInfo {
    BlockServiceId id;
    BincodeFixedBytes<4> ip1;
    uint16_t port1;
    BincodeFixedBytes<4> ip2;
    uint16_t port2;
    uint8_t storageClass;
    BincodeFixedBytes<16> failureDomain;
    BincodeFixedBytes<16> secretKey;
    uint8_t flags;
    uint64_t capacityBytes;
    uint64_t availableBytes;
    uint64_t blocks;
    BincodeBytes path;
    EggsTime lastSeen;

    static constexpr uint16_t STATIC_SIZE = 8 + BincodeFixedBytes<4>::STATIC_SIZE + 2 + BincodeFixedBytes<4>::STATIC_SIZE + 2 + 1 + BincodeFixedBytes<16>::STATIC_SIZE + BincodeFixedBytes<16>::STATIC_SIZE + 1 + 8 + 8 + 8 + BincodeBytes::STATIC_SIZE + 8; // id + ip1 + port1 + ip2 + port2 + storageClass + failureDomain + secretKey + flags + capacityBytes + availableBytes + blocks + path + lastSeen

    BlockServiceInfo() { clear(); }
    size_t packedSize() const {
        size_t _size = 0;
        _size += 8; // id
        _size += BincodeFixedBytes<4>::STATIC_SIZE; // ip1
        _size += 2; // port1
        _size += BincodeFixedBytes<4>::STATIC_SIZE; // ip2
        _size += 2; // port2
        _size += 1; // storageClass
        _size += BincodeFixedBytes<16>::STATIC_SIZE; // failureDomain
        _size += BincodeFixedBytes<16>::STATIC_SIZE; // secretKey
        _size += 1; // flags
        _size += 8; // capacityBytes
        _size += 8; // availableBytes
        _size += 8; // blocks
        _size += path.packedSize(); // path
        _size += 8; // lastSeen
        return _size;
    }
    void pack(BincodeBuf& buf) const;
    void unpack(BincodeBuf& buf);
    void clear();
    bool operator==(const BlockServiceInfo&rhs) const;
};

std::ostream& operator<<(std::ostream& out, const BlockServiceInfo& x);

struct RegisterShardInfo {
    BincodeFixedBytes<4> ip1;
    uint16_t port1;
    BincodeFixedBytes<4> ip2;
    uint16_t port2;

    static constexpr uint16_t STATIC_SIZE = BincodeFixedBytes<4>::STATIC_SIZE + 2 + BincodeFixedBytes<4>::STATIC_SIZE + 2; // ip1 + port1 + ip2 + port2

    RegisterShardInfo() { clear(); }
    size_t packedSize() const {
        size_t _size = 0;
        _size += BincodeFixedBytes<4>::STATIC_SIZE; // ip1
        _size += 2; // port1
        _size += BincodeFixedBytes<4>::STATIC_SIZE; // ip2
        _size += 2; // port2
        return _size;
    }
    void pack(BincodeBuf& buf) const;
    void unpack(BincodeBuf& buf);
    void clear();
    bool operator==(const RegisterShardInfo&rhs) const;
};

std::ostream& operator<<(std::ostream& out, const RegisterShardInfo& x);

struct SpanPolicy {
    BincodeList<SpanPolicyEntry> entries;

    static constexpr uint16_t STATIC_SIZE = BincodeList<SpanPolicyEntry>::STATIC_SIZE; // entries

    SpanPolicy() { clear(); }
    size_t packedSize() const {
        size_t _size = 0;
        _size += entries.packedSize(); // entries
        return _size;
    }
    void pack(BincodeBuf& buf) const;
    void unpack(BincodeBuf& buf);
    void clear();
    bool operator==(const SpanPolicy&rhs) const;
};

std::ostream& operator<<(std::ostream& out, const SpanPolicy& x);

struct BlockPolicy {
    BincodeList<BlockPolicyEntry> entries;

    static constexpr uint16_t STATIC_SIZE = BincodeList<BlockPolicyEntry>::STATIC_SIZE; // entries

    BlockPolicy() { clear(); }
    size_t packedSize() const {
        size_t _size = 0;
        _size += entries.packedSize(); // entries
        return _size;
    }
    void pack(BincodeBuf& buf) const;
    void unpack(BincodeBuf& buf);
    void clear();
    bool operator==(const BlockPolicy&rhs) const;
};

std::ostream& operator<<(std::ostream& out, const BlockPolicy& x);

struct SnapshotPolicy {
    uint64_t deleteAfterTime;
    uint16_t deleteAfterVersions;

    static constexpr uint16_t STATIC_SIZE = 8 + 2; // deleteAfterTime + deleteAfterVersions

    SnapshotPolicy() { clear(); }
    size_t packedSize() const {
        size_t _size = 0;
        _size += 8; // deleteAfterTime
        _size += 2; // deleteAfterVersions
        return _size;
    }
    void pack(BincodeBuf& buf) const;
    void unpack(BincodeBuf& buf);
    void clear();
    bool operator==(const SnapshotPolicy&rhs) const;
};

std::ostream& operator<<(std::ostream& out, const SnapshotPolicy& x);

struct Stat {
    BincodeBytes name;
    EggsTime time;
    BincodeList<uint8_t> value;

    static constexpr uint16_t STATIC_SIZE = BincodeBytes::STATIC_SIZE + 8 + BincodeList<uint8_t>::STATIC_SIZE; // name + time + value

    Stat() { clear(); }
    size_t packedSize() const {
        size_t _size = 0;
        _size += name.packedSize(); // name
        _size += 8; // time
        _size += value.packedSize(); // value
        return _size;
    }
    void pack(BincodeBuf& buf) const;
    void unpack(BincodeBuf& buf);
    void clear();
    bool operator==(const Stat&rhs) const;
};

std::ostream& operator<<(std::ostream& out, const Stat& x);

struct LookupReq {
    InodeId dirId;
    BincodeBytes name;

    static constexpr uint16_t STATIC_SIZE = 8 + BincodeBytes::STATIC_SIZE; // dirId + name

    LookupReq() { clear(); }
    size_t packedSize() const {
        size_t _size = 0;
        _size += 8; // dirId
        _size += name.packedSize(); // name
        return _size;
    }
    void pack(BincodeBuf& buf) const;
    void unpack(BincodeBuf& buf);
    void clear();
    bool operator==(const LookupReq&rhs) const;
};

std::ostream& operator<<(std::ostream& out, const LookupReq& x);

struct LookupResp {
    InodeId targetId;
    EggsTime creationTime;

    static constexpr uint16_t STATIC_SIZE = 8 + 8; // targetId + creationTime

    LookupResp() { clear(); }
    size_t packedSize() const {
        size_t _size = 0;
        _size += 8; // targetId
        _size += 8; // creationTime
        return _size;
    }
    void pack(BincodeBuf& buf) const;
    void unpack(BincodeBuf& buf);
    void clear();
    bool operator==(const LookupResp&rhs) const;
};

std::ostream& operator<<(std::ostream& out, const LookupResp& x);

struct StatFileReq {
    InodeId id;

    static constexpr uint16_t STATIC_SIZE = 8; // id

    StatFileReq() { clear(); }
    size_t packedSize() const {
        size_t _size = 0;
        _size += 8; // id
        return _size;
    }
    void pack(BincodeBuf& buf) const;
    void unpack(BincodeBuf& buf);
    void clear();
    bool operator==(const StatFileReq&rhs) const;
};

std::ostream& operator<<(std::ostream& out, const StatFileReq& x);

struct StatFileResp {
    EggsTime mtime;
    EggsTime atime;
    uint64_t size;

    static constexpr uint16_t STATIC_SIZE = 8 + 8 + 8; // mtime + atime + size

    StatFileResp() { clear(); }
    size_t packedSize() const {
        size_t _size = 0;
        _size += 8; // mtime
        _size += 8; // atime
        _size += 8; // size
        return _size;
    }
    void pack(BincodeBuf& buf) const;
    void unpack(BincodeBuf& buf);
    void clear();
    bool operator==(const StatFileResp&rhs) const;
};

std::ostream& operator<<(std::ostream& out, const StatFileResp& x);

struct StatDirectoryReq {
    InodeId id;

    static constexpr uint16_t STATIC_SIZE = 8; // id

    StatDirectoryReq() { clear(); }
    size_t packedSize() const {
        size_t _size = 0;
        _size += 8; // id
        return _size;
    }
    void pack(BincodeBuf& buf) const;
    void unpack(BincodeBuf& buf);
    void clear();
    bool operator==(const StatDirectoryReq&rhs) const;
};

std::ostream& operator<<(std::ostream& out, const StatDirectoryReq& x);

struct StatDirectoryResp {
    EggsTime mtime;
    InodeId owner;
    DirectoryInfo info;

    static constexpr uint16_t STATIC_SIZE = 8 + 8 + DirectoryInfo::STATIC_SIZE; // mtime + owner + info

    StatDirectoryResp() { clear(); }
    size_t packedSize() const {
        size_t _size = 0;
        _size += 8; // mtime
        _size += 8; // owner
        _size += info.packedSize(); // info
        return _size;
    }
    void pack(BincodeBuf& buf) const;
    void unpack(BincodeBuf& buf);
    void clear();
    bool operator==(const StatDirectoryResp&rhs) const;
};

std::ostream& operator<<(std::ostream& out, const StatDirectoryResp& x);

struct ReadDirReq {
    InodeId dirId;
    uint64_t startHash;
    uint16_t mtu;

    static constexpr uint16_t STATIC_SIZE = 8 + 8 + 2; // dirId + startHash + mtu

    ReadDirReq() { clear(); }
    size_t packedSize() const {
        size_t _size = 0;
        _size += 8; // dirId
        _size += 8; // startHash
        _size += 2; // mtu
        return _size;
    }
    void pack(BincodeBuf& buf) const;
    void unpack(BincodeBuf& buf);
    void clear();
    bool operator==(const ReadDirReq&rhs) const;
};

std::ostream& operator<<(std::ostream& out, const ReadDirReq& x);

struct ReadDirResp {
    uint64_t nextHash;
    BincodeList<CurrentEdge> results;

    static constexpr uint16_t STATIC_SIZE = 8 + BincodeList<CurrentEdge>::STATIC_SIZE; // nextHash + results

    ReadDirResp() { clear(); }
    size_t packedSize() const {
        size_t _size = 0;
        _size += 8; // nextHash
        _size += results.packedSize(); // results
        return _size;
    }
    void pack(BincodeBuf& buf) const;
    void unpack(BincodeBuf& buf);
    void clear();
    bool operator==(const ReadDirResp&rhs) const;
};

std::ostream& operator<<(std::ostream& out, const ReadDirResp& x);

struct ConstructFileReq {
    uint8_t type;
    BincodeBytes note;

    static constexpr uint16_t STATIC_SIZE = 1 + BincodeBytes::STATIC_SIZE; // type + note

    ConstructFileReq() { clear(); }
    size_t packedSize() const {
        size_t _size = 0;
        _size += 1; // type
        _size += note.packedSize(); // note
        return _size;
    }
    void pack(BincodeBuf& buf) const;
    void unpack(BincodeBuf& buf);
    void clear();
    bool operator==(const ConstructFileReq&rhs) const;
};

std::ostream& operator<<(std::ostream& out, const ConstructFileReq& x);

struct ConstructFileResp {
    InodeId id;
    BincodeFixedBytes<8> cookie;

    static constexpr uint16_t STATIC_SIZE = 8 + BincodeFixedBytes<8>::STATIC_SIZE; // id + cookie

    ConstructFileResp() { clear(); }
    size_t packedSize() const {
        size_t _size = 0;
        _size += 8; // id
        _size += BincodeFixedBytes<8>::STATIC_SIZE; // cookie
        return _size;
    }
    void pack(BincodeBuf& buf) const;
    void unpack(BincodeBuf& buf);
    void clear();
    bool operator==(const ConstructFileResp&rhs) const;
};

std::ostream& operator<<(std::ostream& out, const ConstructFileResp& x);

struct AddSpanInitiateReq {
    InodeId fileId;
    BincodeFixedBytes<8> cookie;
    uint64_t byteOffset;
    uint32_t size;
    Crc crc;
    uint8_t storageClass;
    BincodeList<BlacklistEntry> blacklist;
    Parity parity;
    uint8_t stripes;
    uint32_t cellSize;
    BincodeList<Crc> crcs;

    static constexpr uint16_t STATIC_SIZE = 8 + BincodeFixedBytes<8>::STATIC_SIZE + 8 + 4 + 4 + 1 + BincodeList<BlacklistEntry>::STATIC_SIZE + 1 + 1 + 4 + BincodeList<Crc>::STATIC_SIZE; // fileId + cookie + byteOffset + size + crc + storageClass + blacklist + parity + stripes + cellSize + crcs

    AddSpanInitiateReq() { clear(); }
    size_t packedSize() const {
        size_t _size = 0;
        _size += 8; // fileId
        _size += BincodeFixedBytes<8>::STATIC_SIZE; // cookie
        _size += 8; // byteOffset
        _size += 4; // size
        _size += 4; // crc
        _size += 1; // storageClass
        _size += blacklist.packedSize(); // blacklist
        _size += 1; // parity
        _size += 1; // stripes
        _size += 4; // cellSize
        _size += crcs.packedSize(); // crcs
        return _size;
    }
    void pack(BincodeBuf& buf) const;
    void unpack(BincodeBuf& buf);
    void clear();
    bool operator==(const AddSpanInitiateReq&rhs) const;
};

std::ostream& operator<<(std::ostream& out, const AddSpanInitiateReq& x);

struct AddSpanInitiateResp {
    BincodeList<BlockInfo> blocks;

    static constexpr uint16_t STATIC_SIZE = BincodeList<BlockInfo>::STATIC_SIZE; // blocks

    AddSpanInitiateResp() { clear(); }
    size_t packedSize() const {
        size_t _size = 0;
        _size += blocks.packedSize(); // blocks
        return _size;
    }
    void pack(BincodeBuf& buf) const;
    void unpack(BincodeBuf& buf);
    void clear();
    bool operator==(const AddSpanInitiateResp&rhs) const;
};

std::ostream& operator<<(std::ostream& out, const AddSpanInitiateResp& x);

struct AddSpanCertifyReq {
    InodeId fileId;
    BincodeFixedBytes<8> cookie;
    uint64_t byteOffset;
    BincodeList<BlockProof> proofs;

    static constexpr uint16_t STATIC_SIZE = 8 + BincodeFixedBytes<8>::STATIC_SIZE + 8 + BincodeList<BlockProof>::STATIC_SIZE; // fileId + cookie + byteOffset + proofs

    AddSpanCertifyReq() { clear(); }
    size_t packedSize() const {
        size_t _size = 0;
        _size += 8; // fileId
        _size += BincodeFixedBytes<8>::STATIC_SIZE; // cookie
        _size += 8; // byteOffset
        _size += proofs.packedSize(); // proofs
        return _size;
    }
    void pack(BincodeBuf& buf) const;
    void unpack(BincodeBuf& buf);
    void clear();
    bool operator==(const AddSpanCertifyReq&rhs) const;
};

std::ostream& operator<<(std::ostream& out, const AddSpanCertifyReq& x);

struct AddSpanCertifyResp {

    static constexpr uint16_t STATIC_SIZE = 0; // 

    AddSpanCertifyResp() { clear(); }
    size_t packedSize() const {
        size_t _size = 0;
        return _size;
    }
    void pack(BincodeBuf& buf) const;
    void unpack(BincodeBuf& buf);
    void clear();
    bool operator==(const AddSpanCertifyResp&rhs) const;
};

std::ostream& operator<<(std::ostream& out, const AddSpanCertifyResp& x);

struct LinkFileReq {
    InodeId fileId;
    BincodeFixedBytes<8> cookie;
    InodeId ownerId;
    BincodeBytes name;

    static constexpr uint16_t STATIC_SIZE = 8 + BincodeFixedBytes<8>::STATIC_SIZE + 8 + BincodeBytes::STATIC_SIZE; // fileId + cookie + ownerId + name

    LinkFileReq() { clear(); }
    size_t packedSize() const {
        size_t _size = 0;
        _size += 8; // fileId
        _size += BincodeFixedBytes<8>::STATIC_SIZE; // cookie
        _size += 8; // ownerId
        _size += name.packedSize(); // name
        return _size;
    }
    void pack(BincodeBuf& buf) const;
    void unpack(BincodeBuf& buf);
    void clear();
    bool operator==(const LinkFileReq&rhs) const;
};

std::ostream& operator<<(std::ostream& out, const LinkFileReq& x);

struct LinkFileResp {
    EggsTime creationTime;

    static constexpr uint16_t STATIC_SIZE = 8; // creationTime

    LinkFileResp() { clear(); }
    size_t packedSize() const {
        size_t _size = 0;
        _size += 8; // creationTime
        return _size;
    }
    void pack(BincodeBuf& buf) const;
    void unpack(BincodeBuf& buf);
    void clear();
    bool operator==(const LinkFileResp&rhs) const;
};

std::ostream& operator<<(std::ostream& out, const LinkFileResp& x);

struct SoftUnlinkFileReq {
    InodeId ownerId;
    InodeId fileId;
    BincodeBytes name;
    EggsTime creationTime;

    static constexpr uint16_t STATIC_SIZE = 8 + 8 + BincodeBytes::STATIC_SIZE + 8; // ownerId + fileId + name + creationTime

    SoftUnlinkFileReq() { clear(); }
    size_t packedSize() const {
        size_t _size = 0;
        _size += 8; // ownerId
        _size += 8; // fileId
        _size += name.packedSize(); // name
        _size += 8; // creationTime
        return _size;
    }
    void pack(BincodeBuf& buf) const;
    void unpack(BincodeBuf& buf);
    void clear();
    bool operator==(const SoftUnlinkFileReq&rhs) const;
};

std::ostream& operator<<(std::ostream& out, const SoftUnlinkFileReq& x);

struct SoftUnlinkFileResp {
    EggsTime deleteCreationTime;

    static constexpr uint16_t STATIC_SIZE = 8; // deleteCreationTime

    SoftUnlinkFileResp() { clear(); }
    size_t packedSize() const {
        size_t _size = 0;
        _size += 8; // deleteCreationTime
        return _size;
    }
    void pack(BincodeBuf& buf) const;
    void unpack(BincodeBuf& buf);
    void clear();
    bool operator==(const SoftUnlinkFileResp&rhs) const;
};

std::ostream& operator<<(std::ostream& out, const SoftUnlinkFileResp& x);

struct FileSpansReq {
    InodeId fileId;
    uint64_t byteOffset;
    uint32_t limit;
    uint16_t mtu;

    static constexpr uint16_t STATIC_SIZE = 8 + 8 + 4 + 2; // fileId + byteOffset + limit + mtu

    FileSpansReq() { clear(); }
    size_t packedSize() const {
        size_t _size = 0;
        _size += 8; // fileId
        _size += 8; // byteOffset
        _size += 4; // limit
        _size += 2; // mtu
        return _size;
    }
    void pack(BincodeBuf& buf) const;
    void unpack(BincodeBuf& buf);
    void clear();
    bool operator==(const FileSpansReq&rhs) const;
};

std::ostream& operator<<(std::ostream& out, const FileSpansReq& x);

struct FileSpansResp {
    uint64_t nextOffset;
    BincodeList<BlockService> blockServices;
    BincodeList<FetchedSpan> spans;

    static constexpr uint16_t STATIC_SIZE = 8 + BincodeList<BlockService>::STATIC_SIZE + BincodeList<FetchedSpan>::STATIC_SIZE; // nextOffset + blockServices + spans

    FileSpansResp() { clear(); }
    size_t packedSize() const {
        size_t _size = 0;
        _size += 8; // nextOffset
        _size += blockServices.packedSize(); // blockServices
        _size += spans.packedSize(); // spans
        return _size;
    }
    void pack(BincodeBuf& buf) const;
    void unpack(BincodeBuf& buf);
    void clear();
    bool operator==(const FileSpansResp&rhs) const;
};

std::ostream& operator<<(std::ostream& out, const FileSpansResp& x);

struct SameDirectoryRenameReq {
    InodeId targetId;
    InodeId dirId;
    BincodeBytes oldName;
    EggsTime oldCreationTime;
    BincodeBytes newName;

    static constexpr uint16_t STATIC_SIZE = 8 + 8 + BincodeBytes::STATIC_SIZE + 8 + BincodeBytes::STATIC_SIZE; // targetId + dirId + oldName + oldCreationTime + newName

    SameDirectoryRenameReq() { clear(); }
    size_t packedSize() const {
        size_t _size = 0;
        _size += 8; // targetId
        _size += 8; // dirId
        _size += oldName.packedSize(); // oldName
        _size += 8; // oldCreationTime
        _size += newName.packedSize(); // newName
        return _size;
    }
    void pack(BincodeBuf& buf) const;
    void unpack(BincodeBuf& buf);
    void clear();
    bool operator==(const SameDirectoryRenameReq&rhs) const;
};

std::ostream& operator<<(std::ostream& out, const SameDirectoryRenameReq& x);

struct SameDirectoryRenameResp {
    EggsTime newCreationTime;

    static constexpr uint16_t STATIC_SIZE = 8; // newCreationTime

    SameDirectoryRenameResp() { clear(); }
    size_t packedSize() const {
        size_t _size = 0;
        _size += 8; // newCreationTime
        return _size;
    }
    void pack(BincodeBuf& buf) const;
    void unpack(BincodeBuf& buf);
    void clear();
    bool operator==(const SameDirectoryRenameResp&rhs) const;
};

std::ostream& operator<<(std::ostream& out, const SameDirectoryRenameResp& x);

struct AddInlineSpanReq {
    InodeId fileId;
    BincodeFixedBytes<8> cookie;
    uint8_t storageClass;
    uint64_t byteOffset;
    uint32_t size;
    Crc crc;
    BincodeBytes body;

    static constexpr uint16_t STATIC_SIZE = 8 + BincodeFixedBytes<8>::STATIC_SIZE + 1 + 8 + 4 + 4 + BincodeBytes::STATIC_SIZE; // fileId + cookie + storageClass + byteOffset + size + crc + body

    AddInlineSpanReq() { clear(); }
    size_t packedSize() const {
        size_t _size = 0;
        _size += 8; // fileId
        _size += BincodeFixedBytes<8>::STATIC_SIZE; // cookie
        _size += 1; // storageClass
        _size += 8; // byteOffset
        _size += 4; // size
        _size += 4; // crc
        _size += body.packedSize(); // body
        return _size;
    }
    void pack(BincodeBuf& buf) const;
    void unpack(BincodeBuf& buf);
    void clear();
    bool operator==(const AddInlineSpanReq&rhs) const;
};

std::ostream& operator<<(std::ostream& out, const AddInlineSpanReq& x);

struct AddInlineSpanResp {

    static constexpr uint16_t STATIC_SIZE = 0; // 

    AddInlineSpanResp() { clear(); }
    size_t packedSize() const {
        size_t _size = 0;
        return _size;
    }
    void pack(BincodeBuf& buf) const;
    void unpack(BincodeBuf& buf);
    void clear();
    bool operator==(const AddInlineSpanResp&rhs) const;
};

std::ostream& operator<<(std::ostream& out, const AddInlineSpanResp& x);

struct SetTimeReq {
    InodeId id;
    uint64_t mtime;
    uint64_t atime;

    static constexpr uint16_t STATIC_SIZE = 8 + 8 + 8; // id + mtime + atime

    SetTimeReq() { clear(); }
    size_t packedSize() const {
        size_t _size = 0;
        _size += 8; // id
        _size += 8; // mtime
        _size += 8; // atime
        return _size;
    }
    void pack(BincodeBuf& buf) const;
    void unpack(BincodeBuf& buf);
    void clear();
    bool operator==(const SetTimeReq&rhs) const;
};

std::ostream& operator<<(std::ostream& out, const SetTimeReq& x);

struct SetTimeResp {

    static constexpr uint16_t STATIC_SIZE = 0; // 

    SetTimeResp() { clear(); }
    size_t packedSize() const {
        size_t _size = 0;
        return _size;
    }
    void pack(BincodeBuf& buf) const;
    void unpack(BincodeBuf& buf);
    void clear();
    bool operator==(const SetTimeResp&rhs) const;
};

std::ostream& operator<<(std::ostream& out, const SetTimeResp& x);

struct FullReadDirReq {
    InodeId dirId;
    uint8_t flags;
    BincodeBytes startName;
    EggsTime startTime;
    uint16_t limit;
    uint16_t mtu;

    static constexpr uint16_t STATIC_SIZE = 8 + 1 + BincodeBytes::STATIC_SIZE + 8 + 2 + 2; // dirId + flags + startName + startTime + limit + mtu

    FullReadDirReq() { clear(); }
    size_t packedSize() const {
        size_t _size = 0;
        _size += 8; // dirId
        _size += 1; // flags
        _size += startName.packedSize(); // startName
        _size += 8; // startTime
        _size += 2; // limit
        _size += 2; // mtu
        return _size;
    }
    void pack(BincodeBuf& buf) const;
    void unpack(BincodeBuf& buf);
    void clear();
    bool operator==(const FullReadDirReq&rhs) const;
};

std::ostream& operator<<(std::ostream& out, const FullReadDirReq& x);

struct FullReadDirResp {
    FullReadDirCursor next;
    BincodeList<Edge> results;

    static constexpr uint16_t STATIC_SIZE = FullReadDirCursor::STATIC_SIZE + BincodeList<Edge>::STATIC_SIZE; // next + results

    FullReadDirResp() { clear(); }
    size_t packedSize() const {
        size_t _size = 0;
        _size += next.packedSize(); // next
        _size += results.packedSize(); // results
        return _size;
    }
    void pack(BincodeBuf& buf) const;
    void unpack(BincodeBuf& buf);
    void clear();
    bool operator==(const FullReadDirResp&rhs) const;
};

std::ostream& operator<<(std::ostream& out, const FullReadDirResp& x);

struct MoveSpanReq {
    uint32_t spanSize;
    InodeId fileId1;
    uint64_t byteOffset1;
    BincodeFixedBytes<8> cookie1;
    InodeId fileId2;
    uint64_t byteOffset2;
    BincodeFixedBytes<8> cookie2;

    static constexpr uint16_t STATIC_SIZE = 4 + 8 + 8 + BincodeFixedBytes<8>::STATIC_SIZE + 8 + 8 + BincodeFixedBytes<8>::STATIC_SIZE; // spanSize + fileId1 + byteOffset1 + cookie1 + fileId2 + byteOffset2 + cookie2

    MoveSpanReq() { clear(); }
    size_t packedSize() const {
        size_t _size = 0;
        _size += 4; // spanSize
        _size += 8; // fileId1
        _size += 8; // byteOffset1
        _size += BincodeFixedBytes<8>::STATIC_SIZE; // cookie1
        _size += 8; // fileId2
        _size += 8; // byteOffset2
        _size += BincodeFixedBytes<8>::STATIC_SIZE; // cookie2
        return _size;
    }
    void pack(BincodeBuf& buf) const;
    void unpack(BincodeBuf& buf);
    void clear();
    bool operator==(const MoveSpanReq&rhs) const;
};

std::ostream& operator<<(std::ostream& out, const MoveSpanReq& x);

struct MoveSpanResp {

    static constexpr uint16_t STATIC_SIZE = 0; // 

    MoveSpanResp() { clear(); }
    size_t packedSize() const {
        size_t _size = 0;
        return _size;
    }
    void pack(BincodeBuf& buf) const;
    void unpack(BincodeBuf& buf);
    void clear();
    bool operator==(const MoveSpanResp&rhs) const;
};

std::ostream& operator<<(std::ostream& out, const MoveSpanResp& x);

struct RemoveNonOwnedEdgeReq {
    InodeId dirId;
    InodeId targetId;
    BincodeBytes name;
    EggsTime creationTime;

    static constexpr uint16_t STATIC_SIZE = 8 + 8 + BincodeBytes::STATIC_SIZE + 8; // dirId + targetId + name + creationTime

    RemoveNonOwnedEdgeReq() { clear(); }
    size_t packedSize() const {
        size_t _size = 0;
        _size += 8; // dirId
        _size += 8; // targetId
        _size += name.packedSize(); // name
        _size += 8; // creationTime
        return _size;
    }
    void pack(BincodeBuf& buf) const;
    void unpack(BincodeBuf& buf);
    void clear();
    bool operator==(const RemoveNonOwnedEdgeReq&rhs) const;
};

std::ostream& operator<<(std::ostream& out, const RemoveNonOwnedEdgeReq& x);

struct RemoveNonOwnedEdgeResp {

    static constexpr uint16_t STATIC_SIZE = 0; // 

    RemoveNonOwnedEdgeResp() { clear(); }
    size_t packedSize() const {
        size_t _size = 0;
        return _size;
    }
    void pack(BincodeBuf& buf) const;
    void unpack(BincodeBuf& buf);
    void clear();
    bool operator==(const RemoveNonOwnedEdgeResp&rhs) const;
};

std::ostream& operator<<(std::ostream& out, const RemoveNonOwnedEdgeResp& x);

struct SameShardHardFileUnlinkReq {
    InodeId ownerId;
    InodeId targetId;
    BincodeBytes name;
    EggsTime creationTime;

    static constexpr uint16_t STATIC_SIZE = 8 + 8 + BincodeBytes::STATIC_SIZE + 8; // ownerId + targetId + name + creationTime

    SameShardHardFileUnlinkReq() { clear(); }
    size_t packedSize() const {
        size_t _size = 0;
        _size += 8; // ownerId
        _size += 8; // targetId
        _size += name.packedSize(); // name
        _size += 8; // creationTime
        return _size;
    }
    void pack(BincodeBuf& buf) const;
    void unpack(BincodeBuf& buf);
    void clear();
    bool operator==(const SameShardHardFileUnlinkReq&rhs) const;
};

std::ostream& operator<<(std::ostream& out, const SameShardHardFileUnlinkReq& x);

struct SameShardHardFileUnlinkResp {

    static constexpr uint16_t STATIC_SIZE = 0; // 

    SameShardHardFileUnlinkResp() { clear(); }
    size_t packedSize() const {
        size_t _size = 0;
        return _size;
    }
    void pack(BincodeBuf& buf) const;
    void unpack(BincodeBuf& buf);
    void clear();
    bool operator==(const SameShardHardFileUnlinkResp&rhs) const;
};

std::ostream& operator<<(std::ostream& out, const SameShardHardFileUnlinkResp& x);

struct StatTransientFileReq {
    InodeId id;

    static constexpr uint16_t STATIC_SIZE = 8; // id

    StatTransientFileReq() { clear(); }
    size_t packedSize() const {
        size_t _size = 0;
        _size += 8; // id
        return _size;
    }
    void pack(BincodeBuf& buf) const;
    void unpack(BincodeBuf& buf);
    void clear();
    bool operator==(const StatTransientFileReq&rhs) const;
};

std::ostream& operator<<(std::ostream& out, const StatTransientFileReq& x);

struct StatTransientFileResp {
    EggsTime mtime;
    uint64_t size;
    BincodeBytes note;

    static constexpr uint16_t STATIC_SIZE = 8 + 8 + BincodeBytes::STATIC_SIZE; // mtime + size + note

    StatTransientFileResp() { clear(); }
    size_t packedSize() const {
        size_t _size = 0;
        _size += 8; // mtime
        _size += 8; // size
        _size += note.packedSize(); // note
        return _size;
    }
    void pack(BincodeBuf& buf) const;
    void unpack(BincodeBuf& buf);
    void clear();
    bool operator==(const StatTransientFileResp&rhs) const;
};

std::ostream& operator<<(std::ostream& out, const StatTransientFileResp& x);

struct SetDirectoryInfoReq {
    InodeId id;
    DirectoryInfo info;

    static constexpr uint16_t STATIC_SIZE = 8 + DirectoryInfo::STATIC_SIZE; // id + info

    SetDirectoryInfoReq() { clear(); }
    size_t packedSize() const {
        size_t _size = 0;
        _size += 8; // id
        _size += info.packedSize(); // info
        return _size;
    }
    void pack(BincodeBuf& buf) const;
    void unpack(BincodeBuf& buf);
    void clear();
    bool operator==(const SetDirectoryInfoReq&rhs) const;
};

std::ostream& operator<<(std::ostream& out, const SetDirectoryInfoReq& x);

struct SetDirectoryInfoResp {

    static constexpr uint16_t STATIC_SIZE = 0; // 

    SetDirectoryInfoResp() { clear(); }
    size_t packedSize() const {
        size_t _size = 0;
        return _size;
    }
    void pack(BincodeBuf& buf) const;
    void unpack(BincodeBuf& buf);
    void clear();
    bool operator==(const SetDirectoryInfoResp&rhs) const;
};

std::ostream& operator<<(std::ostream& out, const SetDirectoryInfoResp& x);

struct ExpireTransientFileReq {
    InodeId id;

    static constexpr uint16_t STATIC_SIZE = 8; // id

    ExpireTransientFileReq() { clear(); }
    size_t packedSize() const {
        size_t _size = 0;
        _size += 8; // id
        return _size;
    }
    void pack(BincodeBuf& buf) const;
    void unpack(BincodeBuf& buf);
    void clear();
    bool operator==(const ExpireTransientFileReq&rhs) const;
};

std::ostream& operator<<(std::ostream& out, const ExpireTransientFileReq& x);

struct ExpireTransientFileResp {

    static constexpr uint16_t STATIC_SIZE = 0; // 

    ExpireTransientFileResp() { clear(); }
    size_t packedSize() const {
        size_t _size = 0;
        return _size;
    }
    void pack(BincodeBuf& buf) const;
    void unpack(BincodeBuf& buf);
    void clear();
    bool operator==(const ExpireTransientFileResp&rhs) const;
};

std::ostream& operator<<(std::ostream& out, const ExpireTransientFileResp& x);

struct VisitDirectoriesReq {
    InodeId beginId;
    uint16_t mtu;

    static constexpr uint16_t STATIC_SIZE = 8 + 2; // beginId + mtu

    VisitDirectoriesReq() { clear(); }
    size_t packedSize() const {
        size_t _size = 0;
        _size += 8; // beginId
        _size += 2; // mtu
        return _size;
    }
    void pack(BincodeBuf& buf) const;
    void unpack(BincodeBuf& buf);
    void clear();
    bool operator==(const VisitDirectoriesReq&rhs) const;
};

std::ostream& operator<<(std::ostream& out, const VisitDirectoriesReq& x);

struct VisitDirectoriesResp {
    InodeId nextId;
    BincodeList<InodeId> ids;

    static constexpr uint16_t STATIC_SIZE = 8 + BincodeList<InodeId>::STATIC_SIZE; // nextId + ids

    VisitDirectoriesResp() { clear(); }
    size_t packedSize() const {
        size_t _size = 0;
        _size += 8; // nextId
        _size += ids.packedSize(); // ids
        return _size;
    }
    void pack(BincodeBuf& buf) const;
    void unpack(BincodeBuf& buf);
    void clear();
    bool operator==(const VisitDirectoriesResp&rhs) const;
};

std::ostream& operator<<(std::ostream& out, const VisitDirectoriesResp& x);

struct VisitFilesReq {
    InodeId beginId;
    uint16_t mtu;

    static constexpr uint16_t STATIC_SIZE = 8 + 2; // beginId + mtu

    VisitFilesReq() { clear(); }
    size_t packedSize() const {
        size_t _size = 0;
        _size += 8; // beginId
        _size += 2; // mtu
        return _size;
    }
    void pack(BincodeBuf& buf) const;
    void unpack(BincodeBuf& buf);
    void clear();
    bool operator==(const VisitFilesReq&rhs) const;
};

std::ostream& operator<<(std::ostream& out, const VisitFilesReq& x);

struct VisitFilesResp {
    InodeId nextId;
    BincodeList<InodeId> ids;

    static constexpr uint16_t STATIC_SIZE = 8 + BincodeList<InodeId>::STATIC_SIZE; // nextId + ids

    VisitFilesResp() { clear(); }
    size_t packedSize() const {
        size_t _size = 0;
        _size += 8; // nextId
        _size += ids.packedSize(); // ids
        return _size;
    }
    void pack(BincodeBuf& buf) const;
    void unpack(BincodeBuf& buf);
    void clear();
    bool operator==(const VisitFilesResp&rhs) const;
};

std::ostream& operator<<(std::ostream& out, const VisitFilesResp& x);

struct VisitTransientFilesReq {
    InodeId beginId;
    uint16_t mtu;

    static constexpr uint16_t STATIC_SIZE = 8 + 2; // beginId + mtu

    VisitTransientFilesReq() { clear(); }
    size_t packedSize() const {
        size_t _size = 0;
        _size += 8; // beginId
        _size += 2; // mtu
        return _size;
    }
    void pack(BincodeBuf& buf) const;
    void unpack(BincodeBuf& buf);
    void clear();
    bool operator==(const VisitTransientFilesReq&rhs) const;
};

std::ostream& operator<<(std::ostream& out, const VisitTransientFilesReq& x);

struct VisitTransientFilesResp {
    InodeId nextId;
    BincodeList<TransientFile> files;

    static constexpr uint16_t STATIC_SIZE = 8 + BincodeList<TransientFile>::STATIC_SIZE; // nextId + files

    VisitTransientFilesResp() { clear(); }
    size_t packedSize() const {
        size_t _size = 0;
        _size += 8; // nextId
        _size += files.packedSize(); // files
        return _size;
    }
    void pack(BincodeBuf& buf) const;
    void unpack(BincodeBuf& buf);
    void clear();
    bool operator==(const VisitTransientFilesResp&rhs) const;
};

std::ostream& operator<<(std::ostream& out, const VisitTransientFilesResp& x);

struct RemoveSpanInitiateReq {
    InodeId fileId;
    BincodeFixedBytes<8> cookie;

    static constexpr uint16_t STATIC_SIZE = 8 + BincodeFixedBytes<8>::STATIC_SIZE; // fileId + cookie

    RemoveSpanInitiateReq() { clear(); }
    size_t packedSize() const {
        size_t _size = 0;
        _size += 8; // fileId
        _size += BincodeFixedBytes<8>::STATIC_SIZE; // cookie
        return _size;
    }
    void pack(BincodeBuf& buf) const;
    void unpack(BincodeBuf& buf);
    void clear();
    bool operator==(const RemoveSpanInitiateReq&rhs) const;
};

std::ostream& operator<<(std::ostream& out, const RemoveSpanInitiateReq& x);

struct RemoveSpanInitiateResp {
    uint64_t byteOffset;
    BincodeList<BlockInfo> blocks;

    static constexpr uint16_t STATIC_SIZE = 8 + BincodeList<BlockInfo>::STATIC_SIZE; // byteOffset + blocks

    RemoveSpanInitiateResp() { clear(); }
    size_t packedSize() const {
        size_t _size = 0;
        _size += 8; // byteOffset
        _size += blocks.packedSize(); // blocks
        return _size;
    }
    void pack(BincodeBuf& buf) const;
    void unpack(BincodeBuf& buf);
    void clear();
    bool operator==(const RemoveSpanInitiateResp&rhs) const;
};

std::ostream& operator<<(std::ostream& out, const RemoveSpanInitiateResp& x);

struct RemoveSpanCertifyReq {
    InodeId fileId;
    BincodeFixedBytes<8> cookie;
    uint64_t byteOffset;
    BincodeList<BlockProof> proofs;

    static constexpr uint16_t STATIC_SIZE = 8 + BincodeFixedBytes<8>::STATIC_SIZE + 8 + BincodeList<BlockProof>::STATIC_SIZE; // fileId + cookie + byteOffset + proofs

    RemoveSpanCertifyReq() { clear(); }
    size_t packedSize() const {
        size_t _size = 0;
        _size += 8; // fileId
        _size += BincodeFixedBytes<8>::STATIC_SIZE; // cookie
        _size += 8; // byteOffset
        _size += proofs.packedSize(); // proofs
        return _size;
    }
    void pack(BincodeBuf& buf) const;
    void unpack(BincodeBuf& buf);
    void clear();
    bool operator==(const RemoveSpanCertifyReq&rhs) const;
};

std::ostream& operator<<(std::ostream& out, const RemoveSpanCertifyReq& x);

struct RemoveSpanCertifyResp {

    static constexpr uint16_t STATIC_SIZE = 0; // 

    RemoveSpanCertifyResp() { clear(); }
    size_t packedSize() const {
        size_t _size = 0;
        return _size;
    }
    void pack(BincodeBuf& buf) const;
    void unpack(BincodeBuf& buf);
    void clear();
    bool operator==(const RemoveSpanCertifyResp&rhs) const;
};

std::ostream& operator<<(std::ostream& out, const RemoveSpanCertifyResp& x);

struct SwapBlocksReq {
    InodeId fileId1;
    uint64_t byteOffset1;
    uint64_t blockId1;
    InodeId fileId2;
    uint64_t byteOffset2;
    uint64_t blockId2;

    static constexpr uint16_t STATIC_SIZE = 8 + 8 + 8 + 8 + 8 + 8; // fileId1 + byteOffset1 + blockId1 + fileId2 + byteOffset2 + blockId2

    SwapBlocksReq() { clear(); }
    size_t packedSize() const {
        size_t _size = 0;
        _size += 8; // fileId1
        _size += 8; // byteOffset1
        _size += 8; // blockId1
        _size += 8; // fileId2
        _size += 8; // byteOffset2
        _size += 8; // blockId2
        return _size;
    }
    void pack(BincodeBuf& buf) const;
    void unpack(BincodeBuf& buf);
    void clear();
    bool operator==(const SwapBlocksReq&rhs) const;
};

std::ostream& operator<<(std::ostream& out, const SwapBlocksReq& x);

struct SwapBlocksResp {

    static constexpr uint16_t STATIC_SIZE = 0; // 

    SwapBlocksResp() { clear(); }
    size_t packedSize() const {
        size_t _size = 0;
        return _size;
    }
    void pack(BincodeBuf& buf) const;
    void unpack(BincodeBuf& buf);
    void clear();
    bool operator==(const SwapBlocksResp&rhs) const;
};

std::ostream& operator<<(std::ostream& out, const SwapBlocksResp& x);

struct BlockServiceFilesReq {
    BlockServiceId blockServiceId;
    InodeId startFrom;

    static constexpr uint16_t STATIC_SIZE = 8 + 8; // blockServiceId + startFrom

    BlockServiceFilesReq() { clear(); }
    size_t packedSize() const {
        size_t _size = 0;
        _size += 8; // blockServiceId
        _size += 8; // startFrom
        return _size;
    }
    void pack(BincodeBuf& buf) const;
    void unpack(BincodeBuf& buf);
    void clear();
    bool operator==(const BlockServiceFilesReq&rhs) const;
};

std::ostream& operator<<(std::ostream& out, const BlockServiceFilesReq& x);

struct BlockServiceFilesResp {
    BincodeList<InodeId> fileIds;

    static constexpr uint16_t STATIC_SIZE = BincodeList<InodeId>::STATIC_SIZE; // fileIds

    BlockServiceFilesResp() { clear(); }
    size_t packedSize() const {
        size_t _size = 0;
        _size += fileIds.packedSize(); // fileIds
        return _size;
    }
    void pack(BincodeBuf& buf) const;
    void unpack(BincodeBuf& buf);
    void clear();
    bool operator==(const BlockServiceFilesResp&rhs) const;
};

std::ostream& operator<<(std::ostream& out, const BlockServiceFilesResp& x);

struct RemoveInodeReq {
    InodeId id;

    static constexpr uint16_t STATIC_SIZE = 8; // id

    RemoveInodeReq() { clear(); }
    size_t packedSize() const {
        size_t _size = 0;
        _size += 8; // id
        return _size;
    }
    void pack(BincodeBuf& buf) const;
    void unpack(BincodeBuf& buf);
    void clear();
    bool operator==(const RemoveInodeReq&rhs) const;
};

std::ostream& operator<<(std::ostream& out, const RemoveInodeReq& x);

struct RemoveInodeResp {

    static constexpr uint16_t STATIC_SIZE = 0; // 

    RemoveInodeResp() { clear(); }
    size_t packedSize() const {
        size_t _size = 0;
        return _size;
    }
    void pack(BincodeBuf& buf) const;
    void unpack(BincodeBuf& buf);
    void clear();
    bool operator==(const RemoveInodeResp&rhs) const;
};

std::ostream& operator<<(std::ostream& out, const RemoveInodeResp& x);

struct CreateDirectoryInodeReq {
    InodeId id;
    InodeId ownerId;
    DirectoryInfo info;

    static constexpr uint16_t STATIC_SIZE = 8 + 8 + DirectoryInfo::STATIC_SIZE; // id + ownerId + info

    CreateDirectoryInodeReq() { clear(); }
    size_t packedSize() const {
        size_t _size = 0;
        _size += 8; // id
        _size += 8; // ownerId
        _size += info.packedSize(); // info
        return _size;
    }
    void pack(BincodeBuf& buf) const;
    void unpack(BincodeBuf& buf);
    void clear();
    bool operator==(const CreateDirectoryInodeReq&rhs) const;
};

std::ostream& operator<<(std::ostream& out, const CreateDirectoryInodeReq& x);

struct CreateDirectoryInodeResp {
    EggsTime mtime;

    static constexpr uint16_t STATIC_SIZE = 8; // mtime

    CreateDirectoryInodeResp() { clear(); }
    size_t packedSize() const {
        size_t _size = 0;
        _size += 8; // mtime
        return _size;
    }
    void pack(BincodeBuf& buf) const;
    void unpack(BincodeBuf& buf);
    void clear();
    bool operator==(const CreateDirectoryInodeResp&rhs) const;
};

std::ostream& operator<<(std::ostream& out, const CreateDirectoryInodeResp& x);

struct SetDirectoryOwnerReq {
    InodeId dirId;
    InodeId ownerId;

    static constexpr uint16_t STATIC_SIZE = 8 + 8; // dirId + ownerId

    SetDirectoryOwnerReq() { clear(); }
    size_t packedSize() const {
        size_t _size = 0;
        _size += 8; // dirId
        _size += 8; // ownerId
        return _size;
    }
    void pack(BincodeBuf& buf) const;
    void unpack(BincodeBuf& buf);
    void clear();
    bool operator==(const SetDirectoryOwnerReq&rhs) const;
};

std::ostream& operator<<(std::ostream& out, const SetDirectoryOwnerReq& x);

struct SetDirectoryOwnerResp {

    static constexpr uint16_t STATIC_SIZE = 0; // 

    SetDirectoryOwnerResp() { clear(); }
    size_t packedSize() const {
        size_t _size = 0;
        return _size;
    }
    void pack(BincodeBuf& buf) const;
    void unpack(BincodeBuf& buf);
    void clear();
    bool operator==(const SetDirectoryOwnerResp&rhs) const;
};

std::ostream& operator<<(std::ostream& out, const SetDirectoryOwnerResp& x);

struct RemoveDirectoryOwnerReq {
    InodeId dirId;
    DirectoryInfo info;

    static constexpr uint16_t STATIC_SIZE = 8 + DirectoryInfo::STATIC_SIZE; // dirId + info

    RemoveDirectoryOwnerReq() { clear(); }
    size_t packedSize() const {
        size_t _size = 0;
        _size += 8; // dirId
        _size += info.packedSize(); // info
        return _size;
    }
    void pack(BincodeBuf& buf) const;
    void unpack(BincodeBuf& buf);
    void clear();
    bool operator==(const RemoveDirectoryOwnerReq&rhs) const;
};

std::ostream& operator<<(std::ostream& out, const RemoveDirectoryOwnerReq& x);

struct RemoveDirectoryOwnerResp {

    static constexpr uint16_t STATIC_SIZE = 0; // 

    RemoveDirectoryOwnerResp() { clear(); }
    size_t packedSize() const {
        size_t _size = 0;
        return _size;
    }
    void pack(BincodeBuf& buf) const;
    void unpack(BincodeBuf& buf);
    void clear();
    bool operator==(const RemoveDirectoryOwnerResp&rhs) const;
};

std::ostream& operator<<(std::ostream& out, const RemoveDirectoryOwnerResp& x);

struct CreateLockedCurrentEdgeReq {
    InodeId dirId;
    BincodeBytes name;
    InodeId targetId;
    EggsTime oldCreationTime;

    static constexpr uint16_t STATIC_SIZE = 8 + BincodeBytes::STATIC_SIZE + 8 + 8; // dirId + name + targetId + oldCreationTime

    CreateLockedCurrentEdgeReq() { clear(); }
    size_t packedSize() const {
        size_t _size = 0;
        _size += 8; // dirId
        _size += name.packedSize(); // name
        _size += 8; // targetId
        _size += 8; // oldCreationTime
        return _size;
    }
    void pack(BincodeBuf& buf) const;
    void unpack(BincodeBuf& buf);
    void clear();
    bool operator==(const CreateLockedCurrentEdgeReq&rhs) const;
};

std::ostream& operator<<(std::ostream& out, const CreateLockedCurrentEdgeReq& x);

struct CreateLockedCurrentEdgeResp {
    EggsTime creationTime;

    static constexpr uint16_t STATIC_SIZE = 8; // creationTime

    CreateLockedCurrentEdgeResp() { clear(); }
    size_t packedSize() const {
        size_t _size = 0;
        _size += 8; // creationTime
        return _size;
    }
    void pack(BincodeBuf& buf) const;
    void unpack(BincodeBuf& buf);
    void clear();
    bool operator==(const CreateLockedCurrentEdgeResp&rhs) const;
};

std::ostream& operator<<(std::ostream& out, const CreateLockedCurrentEdgeResp& x);

struct LockCurrentEdgeReq {
    InodeId dirId;
    InodeId targetId;
    EggsTime creationTime;
    BincodeBytes name;

    static constexpr uint16_t STATIC_SIZE = 8 + 8 + 8 + BincodeBytes::STATIC_SIZE; // dirId + targetId + creationTime + name

    LockCurrentEdgeReq() { clear(); }
    size_t packedSize() const {
        size_t _size = 0;
        _size += 8; // dirId
        _size += 8; // targetId
        _size += 8; // creationTime
        _size += name.packedSize(); // name
        return _size;
    }
    void pack(BincodeBuf& buf) const;
    void unpack(BincodeBuf& buf);
    void clear();
    bool operator==(const LockCurrentEdgeReq&rhs) const;
};

std::ostream& operator<<(std::ostream& out, const LockCurrentEdgeReq& x);

struct LockCurrentEdgeResp {

    static constexpr uint16_t STATIC_SIZE = 0; // 

    LockCurrentEdgeResp() { clear(); }
    size_t packedSize() const {
        size_t _size = 0;
        return _size;
    }
    void pack(BincodeBuf& buf) const;
    void unpack(BincodeBuf& buf);
    void clear();
    bool operator==(const LockCurrentEdgeResp&rhs) const;
};

std::ostream& operator<<(std::ostream& out, const LockCurrentEdgeResp& x);

struct UnlockCurrentEdgeReq {
    InodeId dirId;
    BincodeBytes name;
    EggsTime creationTime;
    InodeId targetId;
    bool wasMoved;

    static constexpr uint16_t STATIC_SIZE = 8 + BincodeBytes::STATIC_SIZE + 8 + 8 + 1; // dirId + name + creationTime + targetId + wasMoved

    UnlockCurrentEdgeReq() { clear(); }
    size_t packedSize() const {
        size_t _size = 0;
        _size += 8; // dirId
        _size += name.packedSize(); // name
        _size += 8; // creationTime
        _size += 8; // targetId
        _size += 1; // wasMoved
        return _size;
    }
    void pack(BincodeBuf& buf) const;
    void unpack(BincodeBuf& buf);
    void clear();
    bool operator==(const UnlockCurrentEdgeReq&rhs) const;
};

std::ostream& operator<<(std::ostream& out, const UnlockCurrentEdgeReq& x);

struct UnlockCurrentEdgeResp {

    static constexpr uint16_t STATIC_SIZE = 0; // 

    UnlockCurrentEdgeResp() { clear(); }
    size_t packedSize() const {
        size_t _size = 0;
        return _size;
    }
    void pack(BincodeBuf& buf) const;
    void unpack(BincodeBuf& buf);
    void clear();
    bool operator==(const UnlockCurrentEdgeResp&rhs) const;
};

std::ostream& operator<<(std::ostream& out, const UnlockCurrentEdgeResp& x);

struct RemoveOwnedSnapshotFileEdgeReq {
    InodeId ownerId;
    InodeId targetId;
    BincodeBytes name;
    EggsTime creationTime;

    static constexpr uint16_t STATIC_SIZE = 8 + 8 + BincodeBytes::STATIC_SIZE + 8; // ownerId + targetId + name + creationTime

    RemoveOwnedSnapshotFileEdgeReq() { clear(); }
    size_t packedSize() const {
        size_t _size = 0;
        _size += 8; // ownerId
        _size += 8; // targetId
        _size += name.packedSize(); // name
        _size += 8; // creationTime
        return _size;
    }
    void pack(BincodeBuf& buf) const;
    void unpack(BincodeBuf& buf);
    void clear();
    bool operator==(const RemoveOwnedSnapshotFileEdgeReq&rhs) const;
};

std::ostream& operator<<(std::ostream& out, const RemoveOwnedSnapshotFileEdgeReq& x);

struct RemoveOwnedSnapshotFileEdgeResp {

    static constexpr uint16_t STATIC_SIZE = 0; // 

    RemoveOwnedSnapshotFileEdgeResp() { clear(); }
    size_t packedSize() const {
        size_t _size = 0;
        return _size;
    }
    void pack(BincodeBuf& buf) const;
    void unpack(BincodeBuf& buf);
    void clear();
    bool operator==(const RemoveOwnedSnapshotFileEdgeResp&rhs) const;
};

std::ostream& operator<<(std::ostream& out, const RemoveOwnedSnapshotFileEdgeResp& x);

struct MakeFileTransientReq {
    InodeId id;
    BincodeBytes note;

    static constexpr uint16_t STATIC_SIZE = 8 + BincodeBytes::STATIC_SIZE; // id + note

    MakeFileTransientReq() { clear(); }
    size_t packedSize() const {
        size_t _size = 0;
        _size += 8; // id
        _size += note.packedSize(); // note
        return _size;
    }
    void pack(BincodeBuf& buf) const;
    void unpack(BincodeBuf& buf);
    void clear();
    bool operator==(const MakeFileTransientReq&rhs) const;
};

std::ostream& operator<<(std::ostream& out, const MakeFileTransientReq& x);

struct MakeFileTransientResp {

    static constexpr uint16_t STATIC_SIZE = 0; // 

    MakeFileTransientResp() { clear(); }
    size_t packedSize() const {
        size_t _size = 0;
        return _size;
    }
    void pack(BincodeBuf& buf) const;
    void unpack(BincodeBuf& buf);
    void clear();
    bool operator==(const MakeFileTransientResp&rhs) const;
};

std::ostream& operator<<(std::ostream& out, const MakeFileTransientResp& x);

struct MakeDirectoryReq {
    InodeId ownerId;
    BincodeBytes name;

    static constexpr uint16_t STATIC_SIZE = 8 + BincodeBytes::STATIC_SIZE; // ownerId + name

    MakeDirectoryReq() { clear(); }
    size_t packedSize() const {
        size_t _size = 0;
        _size += 8; // ownerId
        _size += name.packedSize(); // name
        return _size;
    }
    void pack(BincodeBuf& buf) const;
    void unpack(BincodeBuf& buf);
    void clear();
    bool operator==(const MakeDirectoryReq&rhs) const;
};

std::ostream& operator<<(std::ostream& out, const MakeDirectoryReq& x);

struct MakeDirectoryResp {
    InodeId id;
    EggsTime creationTime;

    static constexpr uint16_t STATIC_SIZE = 8 + 8; // id + creationTime

    MakeDirectoryResp() { clear(); }
    size_t packedSize() const {
        size_t _size = 0;
        _size += 8; // id
        _size += 8; // creationTime
        return _size;
    }
    void pack(BincodeBuf& buf) const;
    void unpack(BincodeBuf& buf);
    void clear();
    bool operator==(const MakeDirectoryResp&rhs) const;
};

std::ostream& operator<<(std::ostream& out, const MakeDirectoryResp& x);

struct RenameFileReq {
    InodeId targetId;
    InodeId oldOwnerId;
    BincodeBytes oldName;
    EggsTime oldCreationTime;
    InodeId newOwnerId;
    BincodeBytes newName;

    static constexpr uint16_t STATIC_SIZE = 8 + 8 + BincodeBytes::STATIC_SIZE + 8 + 8 + BincodeBytes::STATIC_SIZE; // targetId + oldOwnerId + oldName + oldCreationTime + newOwnerId + newName

    RenameFileReq() { clear(); }
    size_t packedSize() const {
        size_t _size = 0;
        _size += 8; // targetId
        _size += 8; // oldOwnerId
        _size += oldName.packedSize(); // oldName
        _size += 8; // oldCreationTime
        _size += 8; // newOwnerId
        _size += newName.packedSize(); // newName
        return _size;
    }
    void pack(BincodeBuf& buf) const;
    void unpack(BincodeBuf& buf);
    void clear();
    bool operator==(const RenameFileReq&rhs) const;
};

std::ostream& operator<<(std::ostream& out, const RenameFileReq& x);

struct RenameFileResp {
    EggsTime creationTime;

    static constexpr uint16_t STATIC_SIZE = 8; // creationTime

    RenameFileResp() { clear(); }
    size_t packedSize() const {
        size_t _size = 0;
        _size += 8; // creationTime
        return _size;
    }
    void pack(BincodeBuf& buf) const;
    void unpack(BincodeBuf& buf);
    void clear();
    bool operator==(const RenameFileResp&rhs) const;
};

std::ostream& operator<<(std::ostream& out, const RenameFileResp& x);

struct SoftUnlinkDirectoryReq {
    InodeId ownerId;
    InodeId targetId;
    EggsTime creationTime;
    BincodeBytes name;

    static constexpr uint16_t STATIC_SIZE = 8 + 8 + 8 + BincodeBytes::STATIC_SIZE; // ownerId + targetId + creationTime + name

    SoftUnlinkDirectoryReq() { clear(); }
    size_t packedSize() const {
        size_t _size = 0;
        _size += 8; // ownerId
        _size += 8; // targetId
        _size += 8; // creationTime
        _size += name.packedSize(); // name
        return _size;
    }
    void pack(BincodeBuf& buf) const;
    void unpack(BincodeBuf& buf);
    void clear();
    bool operator==(const SoftUnlinkDirectoryReq&rhs) const;
};

std::ostream& operator<<(std::ostream& out, const SoftUnlinkDirectoryReq& x);

struct SoftUnlinkDirectoryResp {

    static constexpr uint16_t STATIC_SIZE = 0; // 

    SoftUnlinkDirectoryResp() { clear(); }
    size_t packedSize() const {
        size_t _size = 0;
        return _size;
    }
    void pack(BincodeBuf& buf) const;
    void unpack(BincodeBuf& buf);
    void clear();
    bool operator==(const SoftUnlinkDirectoryResp&rhs) const;
};

std::ostream& operator<<(std::ostream& out, const SoftUnlinkDirectoryResp& x);

struct RenameDirectoryReq {
    InodeId targetId;
    InodeId oldOwnerId;
    BincodeBytes oldName;
    EggsTime oldCreationTime;
    InodeId newOwnerId;
    BincodeBytes newName;

    static constexpr uint16_t STATIC_SIZE = 8 + 8 + BincodeBytes::STATIC_SIZE + 8 + 8 + BincodeBytes::STATIC_SIZE; // targetId + oldOwnerId + oldName + oldCreationTime + newOwnerId + newName

    RenameDirectoryReq() { clear(); }
    size_t packedSize() const {
        size_t _size = 0;
        _size += 8; // targetId
        _size += 8; // oldOwnerId
        _size += oldName.packedSize(); // oldName
        _size += 8; // oldCreationTime
        _size += 8; // newOwnerId
        _size += newName.packedSize(); // newName
        return _size;
    }
    void pack(BincodeBuf& buf) const;
    void unpack(BincodeBuf& buf);
    void clear();
    bool operator==(const RenameDirectoryReq&rhs) const;
};

std::ostream& operator<<(std::ostream& out, const RenameDirectoryReq& x);

struct RenameDirectoryResp {
    EggsTime creationTime;

    static constexpr uint16_t STATIC_SIZE = 8; // creationTime

    RenameDirectoryResp() { clear(); }
    size_t packedSize() const {
        size_t _size = 0;
        _size += 8; // creationTime
        return _size;
    }
    void pack(BincodeBuf& buf) const;
    void unpack(BincodeBuf& buf);
    void clear();
    bool operator==(const RenameDirectoryResp&rhs) const;
};

std::ostream& operator<<(std::ostream& out, const RenameDirectoryResp& x);

struct HardUnlinkDirectoryReq {
    InodeId dirId;

    static constexpr uint16_t STATIC_SIZE = 8; // dirId

    HardUnlinkDirectoryReq() { clear(); }
    size_t packedSize() const {
        size_t _size = 0;
        _size += 8; // dirId
        return _size;
    }
    void pack(BincodeBuf& buf) const;
    void unpack(BincodeBuf& buf);
    void clear();
    bool operator==(const HardUnlinkDirectoryReq&rhs) const;
};

std::ostream& operator<<(std::ostream& out, const HardUnlinkDirectoryReq& x);

struct HardUnlinkDirectoryResp {

    static constexpr uint16_t STATIC_SIZE = 0; // 

    HardUnlinkDirectoryResp() { clear(); }
    size_t packedSize() const {
        size_t _size = 0;
        return _size;
    }
    void pack(BincodeBuf& buf) const;
    void unpack(BincodeBuf& buf);
    void clear();
    bool operator==(const HardUnlinkDirectoryResp&rhs) const;
};

std::ostream& operator<<(std::ostream& out, const HardUnlinkDirectoryResp& x);

struct CrossShardHardUnlinkFileReq {
    InodeId ownerId;
    InodeId targetId;
    BincodeBytes name;
    EggsTime creationTime;

    static constexpr uint16_t STATIC_SIZE = 8 + 8 + BincodeBytes::STATIC_SIZE + 8; // ownerId + targetId + name + creationTime

    CrossShardHardUnlinkFileReq() { clear(); }
    size_t packedSize() const {
        size_t _size = 0;
        _size += 8; // ownerId
        _size += 8; // targetId
        _size += name.packedSize(); // name
        _size += 8; // creationTime
        return _size;
    }
    void pack(BincodeBuf& buf) const;
    void unpack(BincodeBuf& buf);
    void clear();
    bool operator==(const CrossShardHardUnlinkFileReq&rhs) const;
};

std::ostream& operator<<(std::ostream& out, const CrossShardHardUnlinkFileReq& x);

struct CrossShardHardUnlinkFileResp {

    static constexpr uint16_t STATIC_SIZE = 0; // 

    CrossShardHardUnlinkFileResp() { clear(); }
    size_t packedSize() const {
        size_t _size = 0;
        return _size;
    }
    void pack(BincodeBuf& buf) const;
    void unpack(BincodeBuf& buf);
    void clear();
    bool operator==(const CrossShardHardUnlinkFileResp&rhs) const;
};

std::ostream& operator<<(std::ostream& out, const CrossShardHardUnlinkFileResp& x);

struct ShardsReq {

    static constexpr uint16_t STATIC_SIZE = 0; // 

    ShardsReq() { clear(); }
    size_t packedSize() const {
        size_t _size = 0;
        return _size;
    }
    void pack(BincodeBuf& buf) const;
    void unpack(BincodeBuf& buf);
    void clear();
    bool operator==(const ShardsReq&rhs) const;
};

std::ostream& operator<<(std::ostream& out, const ShardsReq& x);

struct ShardsResp {
    BincodeList<ShardInfo> shards;

    static constexpr uint16_t STATIC_SIZE = BincodeList<ShardInfo>::STATIC_SIZE; // shards

    ShardsResp() { clear(); }
    size_t packedSize() const {
        size_t _size = 0;
        _size += shards.packedSize(); // shards
        return _size;
    }
    void pack(BincodeBuf& buf) const;
    void unpack(BincodeBuf& buf);
    void clear();
    bool operator==(const ShardsResp&rhs) const;
};

std::ostream& operator<<(std::ostream& out, const ShardsResp& x);

struct CdcReq {

    static constexpr uint16_t STATIC_SIZE = 0; // 

    CdcReq() { clear(); }
    size_t packedSize() const {
        size_t _size = 0;
        return _size;
    }
    void pack(BincodeBuf& buf) const;
    void unpack(BincodeBuf& buf);
    void clear();
    bool operator==(const CdcReq&rhs) const;
};

std::ostream& operator<<(std::ostream& out, const CdcReq& x);

struct CdcResp {
    BincodeFixedBytes<4> ip1;
    uint16_t port1;
    BincodeFixedBytes<4> ip2;
    uint16_t port2;
    EggsTime lastSeen;

    static constexpr uint16_t STATIC_SIZE = BincodeFixedBytes<4>::STATIC_SIZE + 2 + BincodeFixedBytes<4>::STATIC_SIZE + 2 + 8; // ip1 + port1 + ip2 + port2 + lastSeen

    CdcResp() { clear(); }
    size_t packedSize() const {
        size_t _size = 0;
        _size += BincodeFixedBytes<4>::STATIC_SIZE; // ip1
        _size += 2; // port1
        _size += BincodeFixedBytes<4>::STATIC_SIZE; // ip2
        _size += 2; // port2
        _size += 8; // lastSeen
        return _size;
    }
    void pack(BincodeBuf& buf) const;
    void unpack(BincodeBuf& buf);
    void clear();
    bool operator==(const CdcResp&rhs) const;
};

std::ostream& operator<<(std::ostream& out, const CdcResp& x);

struct InfoReq {

    static constexpr uint16_t STATIC_SIZE = 0; // 

    InfoReq() { clear(); }
    size_t packedSize() const {
        size_t _size = 0;
        return _size;
    }
    void pack(BincodeBuf& buf) const;
    void unpack(BincodeBuf& buf);
    void clear();
    bool operator==(const InfoReq&rhs) const;
};

std::ostream& operator<<(std::ostream& out, const InfoReq& x);

struct InfoResp {
    uint32_t numBlockServices;
    uint32_t numFailureDomains;
    uint64_t capacity;
    uint64_t available;
    uint64_t blocks;

    static constexpr uint16_t STATIC_SIZE = 4 + 4 + 8 + 8 + 8; // numBlockServices + numFailureDomains + capacity + available + blocks

    InfoResp() { clear(); }
    size_t packedSize() const {
        size_t _size = 0;
        _size += 4; // numBlockServices
        _size += 4; // numFailureDomains
        _size += 8; // capacity
        _size += 8; // available
        _size += 8; // blocks
        return _size;
    }
    void pack(BincodeBuf& buf) const;
    void unpack(BincodeBuf& buf);
    void clear();
    bool operator==(const InfoResp&rhs) const;
};

std::ostream& operator<<(std::ostream& out, const InfoResp& x);

struct RegisterBlockServicesReq {
    BincodeList<BlockServiceInfo> blockServices;

    static constexpr uint16_t STATIC_SIZE = BincodeList<BlockServiceInfo>::STATIC_SIZE; // blockServices

    RegisterBlockServicesReq() { clear(); }
    size_t packedSize() const {
        size_t _size = 0;
        _size += blockServices.packedSize(); // blockServices
        return _size;
    }
    void pack(BincodeBuf& buf) const;
    void unpack(BincodeBuf& buf);
    void clear();
    bool operator==(const RegisterBlockServicesReq&rhs) const;
};

std::ostream& operator<<(std::ostream& out, const RegisterBlockServicesReq& x);

struct RegisterBlockServicesResp {

    static constexpr uint16_t STATIC_SIZE = 0; // 

    RegisterBlockServicesResp() { clear(); }
    size_t packedSize() const {
        size_t _size = 0;
        return _size;
    }
    void pack(BincodeBuf& buf) const;
    void unpack(BincodeBuf& buf);
    void clear();
    bool operator==(const RegisterBlockServicesResp&rhs) const;
};

std::ostream& operator<<(std::ostream& out, const RegisterBlockServicesResp& x);

struct RegisterShardReq {
    ShardId id;
    RegisterShardInfo info;

    static constexpr uint16_t STATIC_SIZE = 1 + RegisterShardInfo::STATIC_SIZE; // id + info

    RegisterShardReq() { clear(); }
    size_t packedSize() const {
        size_t _size = 0;
        _size += 1; // id
        _size += info.packedSize(); // info
        return _size;
    }
    void pack(BincodeBuf& buf) const;
    void unpack(BincodeBuf& buf);
    void clear();
    bool operator==(const RegisterShardReq&rhs) const;
};

std::ostream& operator<<(std::ostream& out, const RegisterShardReq& x);

struct RegisterShardResp {

    static constexpr uint16_t STATIC_SIZE = 0; // 

    RegisterShardResp() { clear(); }
    size_t packedSize() const {
        size_t _size = 0;
        return _size;
    }
    void pack(BincodeBuf& buf) const;
    void unpack(BincodeBuf& buf);
    void clear();
    bool operator==(const RegisterShardResp&rhs) const;
};

std::ostream& operator<<(std::ostream& out, const RegisterShardResp& x);

struct AllBlockServicesReq {

    static constexpr uint16_t STATIC_SIZE = 0; // 

    AllBlockServicesReq() { clear(); }
    size_t packedSize() const {
        size_t _size = 0;
        return _size;
    }
    void pack(BincodeBuf& buf) const;
    void unpack(BincodeBuf& buf);
    void clear();
    bool operator==(const AllBlockServicesReq&rhs) const;
};

std::ostream& operator<<(std::ostream& out, const AllBlockServicesReq& x);

struct AllBlockServicesResp {
    BincodeList<BlockServiceInfo> blockServices;

    static constexpr uint16_t STATIC_SIZE = BincodeList<BlockServiceInfo>::STATIC_SIZE; // blockServices

    AllBlockServicesResp() { clear(); }
    size_t packedSize() const {
        size_t _size = 0;
        _size += blockServices.packedSize(); // blockServices
        return _size;
    }
    void pack(BincodeBuf& buf) const;
    void unpack(BincodeBuf& buf);
    void clear();
    bool operator==(const AllBlockServicesResp&rhs) const;
};

std::ostream& operator<<(std::ostream& out, const AllBlockServicesResp& x);

struct RegisterCdcReq {
    BincodeFixedBytes<4> ip1;
    uint16_t port1;
    BincodeFixedBytes<4> ip2;
    uint16_t port2;

    static constexpr uint16_t STATIC_SIZE = BincodeFixedBytes<4>::STATIC_SIZE + 2 + BincodeFixedBytes<4>::STATIC_SIZE + 2; // ip1 + port1 + ip2 + port2

    RegisterCdcReq() { clear(); }
    size_t packedSize() const {
        size_t _size = 0;
        _size += BincodeFixedBytes<4>::STATIC_SIZE; // ip1
        _size += 2; // port1
        _size += BincodeFixedBytes<4>::STATIC_SIZE; // ip2
        _size += 2; // port2
        return _size;
    }
    void pack(BincodeBuf& buf) const;
    void unpack(BincodeBuf& buf);
    void clear();
    bool operator==(const RegisterCdcReq&rhs) const;
};

std::ostream& operator<<(std::ostream& out, const RegisterCdcReq& x);

struct RegisterCdcResp {

    static constexpr uint16_t STATIC_SIZE = 0; // 

    RegisterCdcResp() { clear(); }
    size_t packedSize() const {
        size_t _size = 0;
        return _size;
    }
    void pack(BincodeBuf& buf) const;
    void unpack(BincodeBuf& buf);
    void clear();
    bool operator==(const RegisterCdcResp&rhs) const;
};

std::ostream& operator<<(std::ostream& out, const RegisterCdcResp& x);

struct SetBlockServiceFlagsReq {
    BlockServiceId id;
    uint8_t flags;
    uint8_t flagsMask;

    static constexpr uint16_t STATIC_SIZE = 8 + 1 + 1; // id + flags + flagsMask

    SetBlockServiceFlagsReq() { clear(); }
    size_t packedSize() const {
        size_t _size = 0;
        _size += 8; // id
        _size += 1; // flags
        _size += 1; // flagsMask
        return _size;
    }
    void pack(BincodeBuf& buf) const;
    void unpack(BincodeBuf& buf);
    void clear();
    bool operator==(const SetBlockServiceFlagsReq&rhs) const;
};

std::ostream& operator<<(std::ostream& out, const SetBlockServiceFlagsReq& x);

struct SetBlockServiceFlagsResp {

    static constexpr uint16_t STATIC_SIZE = 0; // 

    SetBlockServiceFlagsResp() { clear(); }
    size_t packedSize() const {
        size_t _size = 0;
        return _size;
    }
    void pack(BincodeBuf& buf) const;
    void unpack(BincodeBuf& buf);
    void clear();
    bool operator==(const SetBlockServiceFlagsResp&rhs) const;
};

std::ostream& operator<<(std::ostream& out, const SetBlockServiceFlagsResp& x);

struct BlockServiceReq {
    BlockServiceId id;

    static constexpr uint16_t STATIC_SIZE = 8; // id

    BlockServiceReq() { clear(); }
    size_t packedSize() const {
        size_t _size = 0;
        _size += 8; // id
        return _size;
    }
    void pack(BincodeBuf& buf) const;
    void unpack(BincodeBuf& buf);
    void clear();
    bool operator==(const BlockServiceReq&rhs) const;
};

std::ostream& operator<<(std::ostream& out, const BlockServiceReq& x);

struct BlockServiceResp {
    BlockServiceInfo info;

    static constexpr uint16_t STATIC_SIZE = BlockServiceInfo::STATIC_SIZE; // info

    BlockServiceResp() { clear(); }
    size_t packedSize() const {
        size_t _size = 0;
        _size += info.packedSize(); // info
        return _size;
    }
    void pack(BincodeBuf& buf) const;
    void unpack(BincodeBuf& buf);
    void clear();
    bool operator==(const BlockServiceResp&rhs) const;
};

std::ostream& operator<<(std::ostream& out, const BlockServiceResp& x);

struct InsertStatsReq {
    BincodeList<Stat> stats;

    static constexpr uint16_t STATIC_SIZE = BincodeList<Stat>::STATIC_SIZE; // stats

    InsertStatsReq() { clear(); }
    size_t packedSize() const {
        size_t _size = 0;
        _size += stats.packedSize(); // stats
        return _size;
    }
    void pack(BincodeBuf& buf) const;
    void unpack(BincodeBuf& buf);
    void clear();
    bool operator==(const InsertStatsReq&rhs) const;
};

std::ostream& operator<<(std::ostream& out, const InsertStatsReq& x);

struct InsertStatsResp {

    static constexpr uint16_t STATIC_SIZE = 0; // 

    InsertStatsResp() { clear(); }
    size_t packedSize() const {
        size_t _size = 0;
        return _size;
    }
    void pack(BincodeBuf& buf) const;
    void unpack(BincodeBuf& buf);
    void clear();
    bool operator==(const InsertStatsResp&rhs) const;
};

std::ostream& operator<<(std::ostream& out, const InsertStatsResp& x);

struct ShardReq {
    ShardId id;

    static constexpr uint16_t STATIC_SIZE = 1; // id

    ShardReq() { clear(); }
    size_t packedSize() const {
        size_t _size = 0;
        _size += 1; // id
        return _size;
    }
    void pack(BincodeBuf& buf) const;
    void unpack(BincodeBuf& buf);
    void clear();
    bool operator==(const ShardReq&rhs) const;
};

std::ostream& operator<<(std::ostream& out, const ShardReq& x);

struct ShardResp {
    ShardInfo info;

    static constexpr uint16_t STATIC_SIZE = ShardInfo::STATIC_SIZE; // info

    ShardResp() { clear(); }
    size_t packedSize() const {
        size_t _size = 0;
        _size += info.packedSize(); // info
        return _size;
    }
    void pack(BincodeBuf& buf) const;
    void unpack(BincodeBuf& buf);
    void clear();
    bool operator==(const ShardResp&rhs) const;
};

std::ostream& operator<<(std::ostream& out, const ShardResp& x);

struct GetStatsReq {
    EggsTime startTime;
    BincodeBytes startName;
    EggsTime endTime;

    static constexpr uint16_t STATIC_SIZE = 8 + BincodeBytes::STATIC_SIZE + 8; // startTime + startName + endTime

    GetStatsReq() { clear(); }
    size_t packedSize() const {
        size_t _size = 0;
        _size += 8; // startTime
        _size += startName.packedSize(); // startName
        _size += 8; // endTime
        return _size;
    }
    void pack(BincodeBuf& buf) const;
    void unpack(BincodeBuf& buf);
    void clear();
    bool operator==(const GetStatsReq&rhs) const;
};

std::ostream& operator<<(std::ostream& out, const GetStatsReq& x);

struct GetStatsResp {
    EggsTime nextTime;
    BincodeBytes nextName;
    BincodeList<Stat> stats;

    static constexpr uint16_t STATIC_SIZE = 8 + BincodeBytes::STATIC_SIZE + BincodeList<Stat>::STATIC_SIZE; // nextTime + nextName + stats

    GetStatsResp() { clear(); }
    size_t packedSize() const {
        size_t _size = 0;
        _size += 8; // nextTime
        _size += nextName.packedSize(); // nextName
        _size += stats.packedSize(); // stats
        return _size;
    }
    void pack(BincodeBuf& buf) const;
    void unpack(BincodeBuf& buf);
    void clear();
    bool operator==(const GetStatsResp&rhs) const;
};

std::ostream& operator<<(std::ostream& out, const GetStatsResp& x);

struct FetchBlockReq {
    uint64_t blockId;
    uint32_t offset;
    uint32_t count;

    static constexpr uint16_t STATIC_SIZE = 8 + 4 + 4; // blockId + offset + count

    FetchBlockReq() { clear(); }
    size_t packedSize() const {
        size_t _size = 0;
        _size += 8; // blockId
        _size += 4; // offset
        _size += 4; // count
        return _size;
    }
    void pack(BincodeBuf& buf) const;
    void unpack(BincodeBuf& buf);
    void clear();
    bool operator==(const FetchBlockReq&rhs) const;
};

std::ostream& operator<<(std::ostream& out, const FetchBlockReq& x);

struct FetchBlockResp {

    static constexpr uint16_t STATIC_SIZE = 0; // 

    FetchBlockResp() { clear(); }
    size_t packedSize() const {
        size_t _size = 0;
        return _size;
    }
    void pack(BincodeBuf& buf) const;
    void unpack(BincodeBuf& buf);
    void clear();
    bool operator==(const FetchBlockResp&rhs) const;
};

std::ostream& operator<<(std::ostream& out, const FetchBlockResp& x);

struct WriteBlockReq {
    uint64_t blockId;
    Crc crc;
    uint32_t size;
    BincodeFixedBytes<8> certificate;

    static constexpr uint16_t STATIC_SIZE = 8 + 4 + 4 + BincodeFixedBytes<8>::STATIC_SIZE; // blockId + crc + size + certificate

    WriteBlockReq() { clear(); }
    size_t packedSize() const {
        size_t _size = 0;
        _size += 8; // blockId
        _size += 4; // crc
        _size += 4; // size
        _size += BincodeFixedBytes<8>::STATIC_SIZE; // certificate
        return _size;
    }
    void pack(BincodeBuf& buf) const;
    void unpack(BincodeBuf& buf);
    void clear();
    bool operator==(const WriteBlockReq&rhs) const;
};

std::ostream& operator<<(std::ostream& out, const WriteBlockReq& x);

struct WriteBlockResp {
    BincodeFixedBytes<8> proof;

    static constexpr uint16_t STATIC_SIZE = BincodeFixedBytes<8>::STATIC_SIZE; // proof

    WriteBlockResp() { clear(); }
    size_t packedSize() const {
        size_t _size = 0;
        _size += BincodeFixedBytes<8>::STATIC_SIZE; // proof
        return _size;
    }
    void pack(BincodeBuf& buf) const;
    void unpack(BincodeBuf& buf);
    void clear();
    bool operator==(const WriteBlockResp&rhs) const;
};

std::ostream& operator<<(std::ostream& out, const WriteBlockResp& x);

struct EraseBlockReq {
    uint64_t blockId;
    BincodeFixedBytes<8> certificate;

    static constexpr uint16_t STATIC_SIZE = 8 + BincodeFixedBytes<8>::STATIC_SIZE; // blockId + certificate

    EraseBlockReq() { clear(); }
    size_t packedSize() const {
        size_t _size = 0;
        _size += 8; // blockId
        _size += BincodeFixedBytes<8>::STATIC_SIZE; // certificate
        return _size;
    }
    void pack(BincodeBuf& buf) const;
    void unpack(BincodeBuf& buf);
    void clear();
    bool operator==(const EraseBlockReq&rhs) const;
};

std::ostream& operator<<(std::ostream& out, const EraseBlockReq& x);

struct EraseBlockResp {
    BincodeFixedBytes<8> proof;

    static constexpr uint16_t STATIC_SIZE = BincodeFixedBytes<8>::STATIC_SIZE; // proof

    EraseBlockResp() { clear(); }
    size_t packedSize() const {
        size_t _size = 0;
        _size += BincodeFixedBytes<8>::STATIC_SIZE; // proof
        return _size;
    }
    void pack(BincodeBuf& buf) const;
    void unpack(BincodeBuf& buf);
    void clear();
    bool operator==(const EraseBlockResp&rhs) const;
};

std::ostream& operator<<(std::ostream& out, const EraseBlockResp& x);

struct TestWriteReq {
    uint64_t size;

    static constexpr uint16_t STATIC_SIZE = 8; // size

    TestWriteReq() { clear(); }
    size_t packedSize() const {
        size_t _size = 0;
        _size += 8; // size
        return _size;
    }
    void pack(BincodeBuf& buf) const;
    void unpack(BincodeBuf& buf);
    void clear();
    bool operator==(const TestWriteReq&rhs) const;
};

std::ostream& operator<<(std::ostream& out, const TestWriteReq& x);

struct TestWriteResp {

    static constexpr uint16_t STATIC_SIZE = 0; // 

    TestWriteResp() { clear(); }
    size_t packedSize() const {
        size_t _size = 0;
        return _size;
    }
    void pack(BincodeBuf& buf) const;
    void unpack(BincodeBuf& buf);
    void clear();
    bool operator==(const TestWriteResp&rhs) const;
};

std::ostream& operator<<(std::ostream& out, const TestWriteResp& x);

struct ShardReqContainer {
private:
    ShardMessageKind _kind = (ShardMessageKind)0;
    std::tuple<LookupReq, StatFileReq, StatDirectoryReq, ReadDirReq, ConstructFileReq, AddSpanInitiateReq, AddSpanCertifyReq, LinkFileReq, SoftUnlinkFileReq, FileSpansReq, SameDirectoryRenameReq, AddInlineSpanReq, SetTimeReq, FullReadDirReq, MoveSpanReq, RemoveNonOwnedEdgeReq, SameShardHardFileUnlinkReq, StatTransientFileReq, SetDirectoryInfoReq, ExpireTransientFileReq, VisitDirectoriesReq, VisitFilesReq, VisitTransientFilesReq, RemoveSpanInitiateReq, RemoveSpanCertifyReq, SwapBlocksReq, BlockServiceFilesReq, RemoveInodeReq, CreateDirectoryInodeReq, SetDirectoryOwnerReq, RemoveDirectoryOwnerReq, CreateLockedCurrentEdgeReq, LockCurrentEdgeReq, UnlockCurrentEdgeReq, RemoveOwnedSnapshotFileEdgeReq, MakeFileTransientReq> _data;
public:
    ShardReqContainer();
    ShardReqContainer(const ShardReqContainer& other);
    void operator=(const ShardReqContainer& other);

    ShardMessageKind kind() const { return _kind; }

    const LookupReq& getLookup() const;
    LookupReq& setLookup();
    const StatFileReq& getStatFile() const;
    StatFileReq& setStatFile();
    const StatDirectoryReq& getStatDirectory() const;
    StatDirectoryReq& setStatDirectory();
    const ReadDirReq& getReadDir() const;
    ReadDirReq& setReadDir();
    const ConstructFileReq& getConstructFile() const;
    ConstructFileReq& setConstructFile();
    const AddSpanInitiateReq& getAddSpanInitiate() const;
    AddSpanInitiateReq& setAddSpanInitiate();
    const AddSpanCertifyReq& getAddSpanCertify() const;
    AddSpanCertifyReq& setAddSpanCertify();
    const LinkFileReq& getLinkFile() const;
    LinkFileReq& setLinkFile();
    const SoftUnlinkFileReq& getSoftUnlinkFile() const;
    SoftUnlinkFileReq& setSoftUnlinkFile();
    const FileSpansReq& getFileSpans() const;
    FileSpansReq& setFileSpans();
    const SameDirectoryRenameReq& getSameDirectoryRename() const;
    SameDirectoryRenameReq& setSameDirectoryRename();
    const AddInlineSpanReq& getAddInlineSpan() const;
    AddInlineSpanReq& setAddInlineSpan();
    const SetTimeReq& getSetTime() const;
    SetTimeReq& setSetTime();
    const FullReadDirReq& getFullReadDir() const;
    FullReadDirReq& setFullReadDir();
    const MoveSpanReq& getMoveSpan() const;
    MoveSpanReq& setMoveSpan();
    const RemoveNonOwnedEdgeReq& getRemoveNonOwnedEdge() const;
    RemoveNonOwnedEdgeReq& setRemoveNonOwnedEdge();
    const SameShardHardFileUnlinkReq& getSameShardHardFileUnlink() const;
    SameShardHardFileUnlinkReq& setSameShardHardFileUnlink();
    const StatTransientFileReq& getStatTransientFile() const;
    StatTransientFileReq& setStatTransientFile();
    const SetDirectoryInfoReq& getSetDirectoryInfo() const;
    SetDirectoryInfoReq& setSetDirectoryInfo();
    const ExpireTransientFileReq& getExpireTransientFile() const;
    ExpireTransientFileReq& setExpireTransientFile();
    const VisitDirectoriesReq& getVisitDirectories() const;
    VisitDirectoriesReq& setVisitDirectories();
    const VisitFilesReq& getVisitFiles() const;
    VisitFilesReq& setVisitFiles();
    const VisitTransientFilesReq& getVisitTransientFiles() const;
    VisitTransientFilesReq& setVisitTransientFiles();
    const RemoveSpanInitiateReq& getRemoveSpanInitiate() const;
    RemoveSpanInitiateReq& setRemoveSpanInitiate();
    const RemoveSpanCertifyReq& getRemoveSpanCertify() const;
    RemoveSpanCertifyReq& setRemoveSpanCertify();
    const SwapBlocksReq& getSwapBlocks() const;
    SwapBlocksReq& setSwapBlocks();
    const BlockServiceFilesReq& getBlockServiceFiles() const;
    BlockServiceFilesReq& setBlockServiceFiles();
    const RemoveInodeReq& getRemoveInode() const;
    RemoveInodeReq& setRemoveInode();
    const CreateDirectoryInodeReq& getCreateDirectoryInode() const;
    CreateDirectoryInodeReq& setCreateDirectoryInode();
    const SetDirectoryOwnerReq& getSetDirectoryOwner() const;
    SetDirectoryOwnerReq& setSetDirectoryOwner();
    const RemoveDirectoryOwnerReq& getRemoveDirectoryOwner() const;
    RemoveDirectoryOwnerReq& setRemoveDirectoryOwner();
    const CreateLockedCurrentEdgeReq& getCreateLockedCurrentEdge() const;
    CreateLockedCurrentEdgeReq& setCreateLockedCurrentEdge();
    const LockCurrentEdgeReq& getLockCurrentEdge() const;
    LockCurrentEdgeReq& setLockCurrentEdge();
    const UnlockCurrentEdgeReq& getUnlockCurrentEdge() const;
    UnlockCurrentEdgeReq& setUnlockCurrentEdge();
    const RemoveOwnedSnapshotFileEdgeReq& getRemoveOwnedSnapshotFileEdge() const;
    RemoveOwnedSnapshotFileEdgeReq& setRemoveOwnedSnapshotFileEdge();
    const MakeFileTransientReq& getMakeFileTransient() const;
    MakeFileTransientReq& setMakeFileTransient();

    void clear() { _kind = (ShardMessageKind)0; };

    size_t packedSize() const;
    void pack(BincodeBuf& buf) const;
    void unpack(BincodeBuf& buf, ShardMessageKind kind);
    bool operator==(const ShardReqContainer& other) const;
};

std::ostream& operator<<(std::ostream& out, const ShardReqContainer& x);

struct ShardRespContainer {
private:
    ShardMessageKind _kind = (ShardMessageKind)0;
    std::tuple<LookupResp, StatFileResp, StatDirectoryResp, ReadDirResp, ConstructFileResp, AddSpanInitiateResp, AddSpanCertifyResp, LinkFileResp, SoftUnlinkFileResp, FileSpansResp, SameDirectoryRenameResp, AddInlineSpanResp, SetTimeResp, FullReadDirResp, MoveSpanResp, RemoveNonOwnedEdgeResp, SameShardHardFileUnlinkResp, StatTransientFileResp, SetDirectoryInfoResp, ExpireTransientFileResp, VisitDirectoriesResp, VisitFilesResp, VisitTransientFilesResp, RemoveSpanInitiateResp, RemoveSpanCertifyResp, SwapBlocksResp, BlockServiceFilesResp, RemoveInodeResp, CreateDirectoryInodeResp, SetDirectoryOwnerResp, RemoveDirectoryOwnerResp, CreateLockedCurrentEdgeResp, LockCurrentEdgeResp, UnlockCurrentEdgeResp, RemoveOwnedSnapshotFileEdgeResp, MakeFileTransientResp> _data;
public:
    ShardRespContainer();
    ShardRespContainer(const ShardRespContainer& other);
    void operator=(const ShardRespContainer& other);

    ShardMessageKind kind() const { return _kind; }

    const LookupResp& getLookup() const;
    LookupResp& setLookup();
    const StatFileResp& getStatFile() const;
    StatFileResp& setStatFile();
    const StatDirectoryResp& getStatDirectory() const;
    StatDirectoryResp& setStatDirectory();
    const ReadDirResp& getReadDir() const;
    ReadDirResp& setReadDir();
    const ConstructFileResp& getConstructFile() const;
    ConstructFileResp& setConstructFile();
    const AddSpanInitiateResp& getAddSpanInitiate() const;
    AddSpanInitiateResp& setAddSpanInitiate();
    const AddSpanCertifyResp& getAddSpanCertify() const;
    AddSpanCertifyResp& setAddSpanCertify();
    const LinkFileResp& getLinkFile() const;
    LinkFileResp& setLinkFile();
    const SoftUnlinkFileResp& getSoftUnlinkFile() const;
    SoftUnlinkFileResp& setSoftUnlinkFile();
    const FileSpansResp& getFileSpans() const;
    FileSpansResp& setFileSpans();
    const SameDirectoryRenameResp& getSameDirectoryRename() const;
    SameDirectoryRenameResp& setSameDirectoryRename();
    const AddInlineSpanResp& getAddInlineSpan() const;
    AddInlineSpanResp& setAddInlineSpan();
    const SetTimeResp& getSetTime() const;
    SetTimeResp& setSetTime();
    const FullReadDirResp& getFullReadDir() const;
    FullReadDirResp& setFullReadDir();
    const MoveSpanResp& getMoveSpan() const;
    MoveSpanResp& setMoveSpan();
    const RemoveNonOwnedEdgeResp& getRemoveNonOwnedEdge() const;
    RemoveNonOwnedEdgeResp& setRemoveNonOwnedEdge();
    const SameShardHardFileUnlinkResp& getSameShardHardFileUnlink() const;
    SameShardHardFileUnlinkResp& setSameShardHardFileUnlink();
    const StatTransientFileResp& getStatTransientFile() const;
    StatTransientFileResp& setStatTransientFile();
    const SetDirectoryInfoResp& getSetDirectoryInfo() const;
    SetDirectoryInfoResp& setSetDirectoryInfo();
    const ExpireTransientFileResp& getExpireTransientFile() const;
    ExpireTransientFileResp& setExpireTransientFile();
    const VisitDirectoriesResp& getVisitDirectories() const;
    VisitDirectoriesResp& setVisitDirectories();
    const VisitFilesResp& getVisitFiles() const;
    VisitFilesResp& setVisitFiles();
    const VisitTransientFilesResp& getVisitTransientFiles() const;
    VisitTransientFilesResp& setVisitTransientFiles();
    const RemoveSpanInitiateResp& getRemoveSpanInitiate() const;
    RemoveSpanInitiateResp& setRemoveSpanInitiate();
    const RemoveSpanCertifyResp& getRemoveSpanCertify() const;
    RemoveSpanCertifyResp& setRemoveSpanCertify();
    const SwapBlocksResp& getSwapBlocks() const;
    SwapBlocksResp& setSwapBlocks();
    const BlockServiceFilesResp& getBlockServiceFiles() const;
    BlockServiceFilesResp& setBlockServiceFiles();
    const RemoveInodeResp& getRemoveInode() const;
    RemoveInodeResp& setRemoveInode();
    const CreateDirectoryInodeResp& getCreateDirectoryInode() const;
    CreateDirectoryInodeResp& setCreateDirectoryInode();
    const SetDirectoryOwnerResp& getSetDirectoryOwner() const;
    SetDirectoryOwnerResp& setSetDirectoryOwner();
    const RemoveDirectoryOwnerResp& getRemoveDirectoryOwner() const;
    RemoveDirectoryOwnerResp& setRemoveDirectoryOwner();
    const CreateLockedCurrentEdgeResp& getCreateLockedCurrentEdge() const;
    CreateLockedCurrentEdgeResp& setCreateLockedCurrentEdge();
    const LockCurrentEdgeResp& getLockCurrentEdge() const;
    LockCurrentEdgeResp& setLockCurrentEdge();
    const UnlockCurrentEdgeResp& getUnlockCurrentEdge() const;
    UnlockCurrentEdgeResp& setUnlockCurrentEdge();
    const RemoveOwnedSnapshotFileEdgeResp& getRemoveOwnedSnapshotFileEdge() const;
    RemoveOwnedSnapshotFileEdgeResp& setRemoveOwnedSnapshotFileEdge();
    const MakeFileTransientResp& getMakeFileTransient() const;
    MakeFileTransientResp& setMakeFileTransient();

    void clear() { _kind = (ShardMessageKind)0; };

    size_t packedSize() const;
    void pack(BincodeBuf& buf) const;
    void unpack(BincodeBuf& buf, ShardMessageKind kind);
    bool operator==(const ShardRespContainer& other) const;
};

std::ostream& operator<<(std::ostream& out, const ShardRespContainer& x);

struct CDCReqContainer {
private:
    CDCMessageKind _kind = (CDCMessageKind)0;
    std::tuple<MakeDirectoryReq, RenameFileReq, SoftUnlinkDirectoryReq, RenameDirectoryReq, HardUnlinkDirectoryReq, CrossShardHardUnlinkFileReq> _data;
public:
    CDCReqContainer();
    CDCReqContainer(const CDCReqContainer& other);
    void operator=(const CDCReqContainer& other);

    CDCMessageKind kind() const { return _kind; }

    const MakeDirectoryReq& getMakeDirectory() const;
    MakeDirectoryReq& setMakeDirectory();
    const RenameFileReq& getRenameFile() const;
    RenameFileReq& setRenameFile();
    const SoftUnlinkDirectoryReq& getSoftUnlinkDirectory() const;
    SoftUnlinkDirectoryReq& setSoftUnlinkDirectory();
    const RenameDirectoryReq& getRenameDirectory() const;
    RenameDirectoryReq& setRenameDirectory();
    const HardUnlinkDirectoryReq& getHardUnlinkDirectory() const;
    HardUnlinkDirectoryReq& setHardUnlinkDirectory();
    const CrossShardHardUnlinkFileReq& getCrossShardHardUnlinkFile() const;
    CrossShardHardUnlinkFileReq& setCrossShardHardUnlinkFile();

    void clear() { _kind = (CDCMessageKind)0; };

    size_t packedSize() const;
    void pack(BincodeBuf& buf) const;
    void unpack(BincodeBuf& buf, CDCMessageKind kind);
    bool operator==(const CDCReqContainer& other) const;
};

std::ostream& operator<<(std::ostream& out, const CDCReqContainer& x);

struct CDCRespContainer {
private:
    CDCMessageKind _kind = (CDCMessageKind)0;
    std::tuple<MakeDirectoryResp, RenameFileResp, SoftUnlinkDirectoryResp, RenameDirectoryResp, HardUnlinkDirectoryResp, CrossShardHardUnlinkFileResp> _data;
public:
    CDCRespContainer();
    CDCRespContainer(const CDCRespContainer& other);
    void operator=(const CDCRespContainer& other);

    CDCMessageKind kind() const { return _kind; }

    const MakeDirectoryResp& getMakeDirectory() const;
    MakeDirectoryResp& setMakeDirectory();
    const RenameFileResp& getRenameFile() const;
    RenameFileResp& setRenameFile();
    const SoftUnlinkDirectoryResp& getSoftUnlinkDirectory() const;
    SoftUnlinkDirectoryResp& setSoftUnlinkDirectory();
    const RenameDirectoryResp& getRenameDirectory() const;
    RenameDirectoryResp& setRenameDirectory();
    const HardUnlinkDirectoryResp& getHardUnlinkDirectory() const;
    HardUnlinkDirectoryResp& setHardUnlinkDirectory();
    const CrossShardHardUnlinkFileResp& getCrossShardHardUnlinkFile() const;
    CrossShardHardUnlinkFileResp& setCrossShardHardUnlinkFile();

    void clear() { _kind = (CDCMessageKind)0; };

    size_t packedSize() const;
    void pack(BincodeBuf& buf) const;
    void unpack(BincodeBuf& buf, CDCMessageKind kind);
    bool operator==(const CDCRespContainer& other) const;
};

std::ostream& operator<<(std::ostream& out, const CDCRespContainer& x);

struct ShuckleReqContainer {
private:
    ShuckleMessageKind _kind = (ShuckleMessageKind)0;
    std::tuple<ShardsReq, CdcReq, InfoReq, RegisterBlockServicesReq, RegisterShardReq, AllBlockServicesReq, RegisterCdcReq, SetBlockServiceFlagsReq, BlockServiceReq, InsertStatsReq, ShardReq, GetStatsReq> _data;
public:
    ShuckleReqContainer();
    ShuckleReqContainer(const ShuckleReqContainer& other);
    void operator=(const ShuckleReqContainer& other);

    ShuckleMessageKind kind() const { return _kind; }

    const ShardsReq& getShards() const;
    ShardsReq& setShards();
    const CdcReq& getCdc() const;
    CdcReq& setCdc();
    const InfoReq& getInfo() const;
    InfoReq& setInfo();
    const RegisterBlockServicesReq& getRegisterBlockServices() const;
    RegisterBlockServicesReq& setRegisterBlockServices();
    const RegisterShardReq& getRegisterShard() const;
    RegisterShardReq& setRegisterShard();
    const AllBlockServicesReq& getAllBlockServices() const;
    AllBlockServicesReq& setAllBlockServices();
    const RegisterCdcReq& getRegisterCdc() const;
    RegisterCdcReq& setRegisterCdc();
    const SetBlockServiceFlagsReq& getSetBlockServiceFlags() const;
    SetBlockServiceFlagsReq& setSetBlockServiceFlags();
    const BlockServiceReq& getBlockService() const;
    BlockServiceReq& setBlockService();
    const InsertStatsReq& getInsertStats() const;
    InsertStatsReq& setInsertStats();
    const ShardReq& getShard() const;
    ShardReq& setShard();
    const GetStatsReq& getGetStats() const;
    GetStatsReq& setGetStats();

    void clear() { _kind = (ShuckleMessageKind)0; };

    size_t packedSize() const;
    void pack(BincodeBuf& buf) const;
    void unpack(BincodeBuf& buf, ShuckleMessageKind kind);
    bool operator==(const ShuckleReqContainer& other) const;
};

std::ostream& operator<<(std::ostream& out, const ShuckleReqContainer& x);

struct ShuckleRespContainer {
private:
    ShuckleMessageKind _kind = (ShuckleMessageKind)0;
    std::tuple<ShardsResp, CdcResp, InfoResp, RegisterBlockServicesResp, RegisterShardResp, AllBlockServicesResp, RegisterCdcResp, SetBlockServiceFlagsResp, BlockServiceResp, InsertStatsResp, ShardResp, GetStatsResp> _data;
public:
    ShuckleRespContainer();
    ShuckleRespContainer(const ShuckleRespContainer& other);
    void operator=(const ShuckleRespContainer& other);

    ShuckleMessageKind kind() const { return _kind; }

    const ShardsResp& getShards() const;
    ShardsResp& setShards();
    const CdcResp& getCdc() const;
    CdcResp& setCdc();
    const InfoResp& getInfo() const;
    InfoResp& setInfo();
    const RegisterBlockServicesResp& getRegisterBlockServices() const;
    RegisterBlockServicesResp& setRegisterBlockServices();
    const RegisterShardResp& getRegisterShard() const;
    RegisterShardResp& setRegisterShard();
    const AllBlockServicesResp& getAllBlockServices() const;
    AllBlockServicesResp& setAllBlockServices();
    const RegisterCdcResp& getRegisterCdc() const;
    RegisterCdcResp& setRegisterCdc();
    const SetBlockServiceFlagsResp& getSetBlockServiceFlags() const;
    SetBlockServiceFlagsResp& setSetBlockServiceFlags();
    const BlockServiceResp& getBlockService() const;
    BlockServiceResp& setBlockService();
    const InsertStatsResp& getInsertStats() const;
    InsertStatsResp& setInsertStats();
    const ShardResp& getShard() const;
    ShardResp& setShard();
    const GetStatsResp& getGetStats() const;
    GetStatsResp& setGetStats();

    void clear() { _kind = (ShuckleMessageKind)0; };

    size_t packedSize() const;
    void pack(BincodeBuf& buf) const;
    void unpack(BincodeBuf& buf, ShuckleMessageKind kind);
    bool operator==(const ShuckleRespContainer& other) const;
};

std::ostream& operator<<(std::ostream& out, const ShuckleRespContainer& x);

enum class ShardLogEntryKind : uint16_t {
    CONSTRUCT_FILE = 1,
    LINK_FILE = 2,
    SAME_DIRECTORY_RENAME = 3,
    SOFT_UNLINK_FILE = 4,
    CREATE_DIRECTORY_INODE = 5,
    CREATE_LOCKED_CURRENT_EDGE = 6,
    UNLOCK_CURRENT_EDGE = 7,
    LOCK_CURRENT_EDGE = 8,
    REMOVE_DIRECTORY_OWNER = 9,
    REMOVE_INODE = 10,
    SET_DIRECTORY_OWNER = 11,
    SET_DIRECTORY_INFO = 12,
    REMOVE_NON_OWNED_EDGE = 13,
    SAME_SHARD_HARD_FILE_UNLINK = 14,
    REMOVE_SPAN_INITIATE = 15,
    UPDATE_BLOCK_SERVICES = 16,
    ADD_SPAN_INITIATE = 17,
    ADD_SPAN_CERTIFY = 18,
    ADD_INLINE_SPAN = 19,
    MAKE_FILE_TRANSIENT = 20,
    REMOVE_SPAN_CERTIFY = 21,
    REMOVE_OWNED_SNAPSHOT_FILE_EDGE = 22,
    SWAP_BLOCKS = 23,
    EXPIRE_TRANSIENT_FILE = 24,
    MOVE_SPAN = 25,
    SET_TIME = 26,
};

std::ostream& operator<<(std::ostream& out, ShardLogEntryKind err);

struct ConstructFileEntry {
    uint8_t type;
    EggsTime deadlineTime;
    BincodeBytes note;

    static constexpr uint16_t STATIC_SIZE = 1 + 8 + BincodeBytes::STATIC_SIZE; // type + deadlineTime + note

    ConstructFileEntry() { clear(); }
    size_t packedSize() const {
        size_t _size = 0;
        _size += 1; // type
        _size += 8; // deadlineTime
        _size += note.packedSize(); // note
        return _size;
    }
    void pack(BincodeBuf& buf) const;
    void unpack(BincodeBuf& buf);
    void clear();
    bool operator==(const ConstructFileEntry&rhs) const;
};

std::ostream& operator<<(std::ostream& out, const ConstructFileEntry& x);

struct LinkFileEntry {
    InodeId fileId;
    InodeId ownerId;
    BincodeBytes name;

    static constexpr uint16_t STATIC_SIZE = 8 + 8 + BincodeBytes::STATIC_SIZE; // fileId + ownerId + name

    LinkFileEntry() { clear(); }
    size_t packedSize() const {
        size_t _size = 0;
        _size += 8; // fileId
        _size += 8; // ownerId
        _size += name.packedSize(); // name
        return _size;
    }
    void pack(BincodeBuf& buf) const;
    void unpack(BincodeBuf& buf);
    void clear();
    bool operator==(const LinkFileEntry&rhs) const;
};

std::ostream& operator<<(std::ostream& out, const LinkFileEntry& x);

struct SameDirectoryRenameEntry {
    InodeId dirId;
    InodeId targetId;
    BincodeBytes oldName;
    EggsTime oldCreationTime;
    BincodeBytes newName;

    static constexpr uint16_t STATIC_SIZE = 8 + 8 + BincodeBytes::STATIC_SIZE + 8 + BincodeBytes::STATIC_SIZE; // dirId + targetId + oldName + oldCreationTime + newName

    SameDirectoryRenameEntry() { clear(); }
    size_t packedSize() const {
        size_t _size = 0;
        _size += 8; // dirId
        _size += 8; // targetId
        _size += oldName.packedSize(); // oldName
        _size += 8; // oldCreationTime
        _size += newName.packedSize(); // newName
        return _size;
    }
    void pack(BincodeBuf& buf) const;
    void unpack(BincodeBuf& buf);
    void clear();
    bool operator==(const SameDirectoryRenameEntry&rhs) const;
};

std::ostream& operator<<(std::ostream& out, const SameDirectoryRenameEntry& x);

struct SoftUnlinkFileEntry {
    InodeId ownerId;
    InodeId fileId;
    BincodeBytes name;
    EggsTime creationTime;

    static constexpr uint16_t STATIC_SIZE = 8 + 8 + BincodeBytes::STATIC_SIZE + 8; // ownerId + fileId + name + creationTime

    SoftUnlinkFileEntry() { clear(); }
    size_t packedSize() const {
        size_t _size = 0;
        _size += 8; // ownerId
        _size += 8; // fileId
        _size += name.packedSize(); // name
        _size += 8; // creationTime
        return _size;
    }
    void pack(BincodeBuf& buf) const;
    void unpack(BincodeBuf& buf);
    void clear();
    bool operator==(const SoftUnlinkFileEntry&rhs) const;
};

std::ostream& operator<<(std::ostream& out, const SoftUnlinkFileEntry& x);

struct CreateDirectoryInodeEntry {
    InodeId id;
    InodeId ownerId;
    DirectoryInfo info;

    static constexpr uint16_t STATIC_SIZE = 8 + 8 + DirectoryInfo::STATIC_SIZE; // id + ownerId + info

    CreateDirectoryInodeEntry() { clear(); }
    size_t packedSize() const {
        size_t _size = 0;
        _size += 8; // id
        _size += 8; // ownerId
        _size += info.packedSize(); // info
        return _size;
    }
    void pack(BincodeBuf& buf) const;
    void unpack(BincodeBuf& buf);
    void clear();
    bool operator==(const CreateDirectoryInodeEntry&rhs) const;
};

std::ostream& operator<<(std::ostream& out, const CreateDirectoryInodeEntry& x);

struct CreateLockedCurrentEdgeEntry {
    InodeId dirId;
    BincodeBytes name;
    InodeId targetId;
    EggsTime oldCreationTime;

    static constexpr uint16_t STATIC_SIZE = 8 + BincodeBytes::STATIC_SIZE + 8 + 8; // dirId + name + targetId + oldCreationTime

    CreateLockedCurrentEdgeEntry() { clear(); }
    size_t packedSize() const {
        size_t _size = 0;
        _size += 8; // dirId
        _size += name.packedSize(); // name
        _size += 8; // targetId
        _size += 8; // oldCreationTime
        return _size;
    }
    void pack(BincodeBuf& buf) const;
    void unpack(BincodeBuf& buf);
    void clear();
    bool operator==(const CreateLockedCurrentEdgeEntry&rhs) const;
};

std::ostream& operator<<(std::ostream& out, const CreateLockedCurrentEdgeEntry& x);

struct UnlockCurrentEdgeEntry {
    InodeId dirId;
    BincodeBytes name;
    EggsTime creationTime;
    InodeId targetId;
    bool wasMoved;

    static constexpr uint16_t STATIC_SIZE = 8 + BincodeBytes::STATIC_SIZE + 8 + 8 + 1; // dirId + name + creationTime + targetId + wasMoved

    UnlockCurrentEdgeEntry() { clear(); }
    size_t packedSize() const {
        size_t _size = 0;
        _size += 8; // dirId
        _size += name.packedSize(); // name
        _size += 8; // creationTime
        _size += 8; // targetId
        _size += 1; // wasMoved
        return _size;
    }
    void pack(BincodeBuf& buf) const;
    void unpack(BincodeBuf& buf);
    void clear();
    bool operator==(const UnlockCurrentEdgeEntry&rhs) const;
};

std::ostream& operator<<(std::ostream& out, const UnlockCurrentEdgeEntry& x);

struct LockCurrentEdgeEntry {
    InodeId dirId;
    BincodeBytes name;
    EggsTime creationTime;
    InodeId targetId;

    static constexpr uint16_t STATIC_SIZE = 8 + BincodeBytes::STATIC_SIZE + 8 + 8; // dirId + name + creationTime + targetId

    LockCurrentEdgeEntry() { clear(); }
    size_t packedSize() const {
        size_t _size = 0;
        _size += 8; // dirId
        _size += name.packedSize(); // name
        _size += 8; // creationTime
        _size += 8; // targetId
        return _size;
    }
    void pack(BincodeBuf& buf) const;
    void unpack(BincodeBuf& buf);
    void clear();
    bool operator==(const LockCurrentEdgeEntry&rhs) const;
};

std::ostream& operator<<(std::ostream& out, const LockCurrentEdgeEntry& x);

struct RemoveDirectoryOwnerEntry {
    InodeId dirId;
    DirectoryInfo info;

    static constexpr uint16_t STATIC_SIZE = 8 + DirectoryInfo::STATIC_SIZE; // dirId + info

    RemoveDirectoryOwnerEntry() { clear(); }
    size_t packedSize() const {
        size_t _size = 0;
        _size += 8; // dirId
        _size += info.packedSize(); // info
        return _size;
    }
    void pack(BincodeBuf& buf) const;
    void unpack(BincodeBuf& buf);
    void clear();
    bool operator==(const RemoveDirectoryOwnerEntry&rhs) const;
};

std::ostream& operator<<(std::ostream& out, const RemoveDirectoryOwnerEntry& x);

struct RemoveInodeEntry {
    InodeId id;

    static constexpr uint16_t STATIC_SIZE = 8; // id

    RemoveInodeEntry() { clear(); }
    size_t packedSize() const {
        size_t _size = 0;
        _size += 8; // id
        return _size;
    }
    void pack(BincodeBuf& buf) const;
    void unpack(BincodeBuf& buf);
    void clear();
    bool operator==(const RemoveInodeEntry&rhs) const;
};

std::ostream& operator<<(std::ostream& out, const RemoveInodeEntry& x);

struct SetDirectoryOwnerEntry {
    InodeId dirId;
    InodeId ownerId;

    static constexpr uint16_t STATIC_SIZE = 8 + 8; // dirId + ownerId

    SetDirectoryOwnerEntry() { clear(); }
    size_t packedSize() const {
        size_t _size = 0;
        _size += 8; // dirId
        _size += 8; // ownerId
        return _size;
    }
    void pack(BincodeBuf& buf) const;
    void unpack(BincodeBuf& buf);
    void clear();
    bool operator==(const SetDirectoryOwnerEntry&rhs) const;
};

std::ostream& operator<<(std::ostream& out, const SetDirectoryOwnerEntry& x);

struct SetDirectoryInfoEntry {
    InodeId dirId;
    DirectoryInfo info;

    static constexpr uint16_t STATIC_SIZE = 8 + DirectoryInfo::STATIC_SIZE; // dirId + info

    SetDirectoryInfoEntry() { clear(); }
    size_t packedSize() const {
        size_t _size = 0;
        _size += 8; // dirId
        _size += info.packedSize(); // info
        return _size;
    }
    void pack(BincodeBuf& buf) const;
    void unpack(BincodeBuf& buf);
    void clear();
    bool operator==(const SetDirectoryInfoEntry&rhs) const;
};

std::ostream& operator<<(std::ostream& out, const SetDirectoryInfoEntry& x);

struct RemoveNonOwnedEdgeEntry {
    InodeId dirId;
    InodeId targetId;
    BincodeBytes name;
    EggsTime creationTime;

    static constexpr uint16_t STATIC_SIZE = 8 + 8 + BincodeBytes::STATIC_SIZE + 8; // dirId + targetId + name + creationTime

    RemoveNonOwnedEdgeEntry() { clear(); }
    size_t packedSize() const {
        size_t _size = 0;
        _size += 8; // dirId
        _size += 8; // targetId
        _size += name.packedSize(); // name
        _size += 8; // creationTime
        return _size;
    }
    void pack(BincodeBuf& buf) const;
    void unpack(BincodeBuf& buf);
    void clear();
    bool operator==(const RemoveNonOwnedEdgeEntry&rhs) const;
};

std::ostream& operator<<(std::ostream& out, const RemoveNonOwnedEdgeEntry& x);

struct SameShardHardFileUnlinkEntry {
    InodeId ownerId;
    InodeId targetId;
    BincodeBytes name;
    EggsTime creationTime;

    static constexpr uint16_t STATIC_SIZE = 8 + 8 + BincodeBytes::STATIC_SIZE + 8; // ownerId + targetId + name + creationTime

    SameShardHardFileUnlinkEntry() { clear(); }
    size_t packedSize() const {
        size_t _size = 0;
        _size += 8; // ownerId
        _size += 8; // targetId
        _size += name.packedSize(); // name
        _size += 8; // creationTime
        return _size;
    }
    void pack(BincodeBuf& buf) const;
    void unpack(BincodeBuf& buf);
    void clear();
    bool operator==(const SameShardHardFileUnlinkEntry&rhs) const;
};

std::ostream& operator<<(std::ostream& out, const SameShardHardFileUnlinkEntry& x);

struct RemoveSpanInitiateEntry {
    InodeId fileId;

    static constexpr uint16_t STATIC_SIZE = 8; // fileId

    RemoveSpanInitiateEntry() { clear(); }
    size_t packedSize() const {
        size_t _size = 0;
        _size += 8; // fileId
        return _size;
    }
    void pack(BincodeBuf& buf) const;
    void unpack(BincodeBuf& buf);
    void clear();
    bool operator==(const RemoveSpanInitiateEntry&rhs) const;
};

std::ostream& operator<<(std::ostream& out, const RemoveSpanInitiateEntry& x);

struct UpdateBlockServicesEntry {
    BincodeList<BlockServiceInfo> blockServices;

    static constexpr uint16_t STATIC_SIZE = BincodeList<BlockServiceInfo>::STATIC_SIZE; // blockServices

    UpdateBlockServicesEntry() { clear(); }
    size_t packedSize() const {
        size_t _size = 0;
        _size += blockServices.packedSize(); // blockServices
        return _size;
    }
    void pack(BincodeBuf& buf) const;
    void unpack(BincodeBuf& buf);
    void clear();
    bool operator==(const UpdateBlockServicesEntry&rhs) const;
};

std::ostream& operator<<(std::ostream& out, const UpdateBlockServicesEntry& x);

struct AddSpanInitiateEntry {
    InodeId fileId;
    uint64_t byteOffset;
    uint32_t size;
    Crc crc;
    uint8_t storageClass;
    Parity parity;
    uint8_t stripes;
    uint32_t cellSize;
    BincodeList<EntryNewBlockInfo> bodyBlocks;
    BincodeList<Crc> bodyStripes;

    static constexpr uint16_t STATIC_SIZE = 8 + 8 + 4 + 4 + 1 + 1 + 1 + 4 + BincodeList<EntryNewBlockInfo>::STATIC_SIZE + BincodeList<Crc>::STATIC_SIZE; // fileId + byteOffset + size + crc + storageClass + parity + stripes + cellSize + bodyBlocks + bodyStripes

    AddSpanInitiateEntry() { clear(); }
    size_t packedSize() const {
        size_t _size = 0;
        _size += 8; // fileId
        _size += 8; // byteOffset
        _size += 4; // size
        _size += 4; // crc
        _size += 1; // storageClass
        _size += 1; // parity
        _size += 1; // stripes
        _size += 4; // cellSize
        _size += bodyBlocks.packedSize(); // bodyBlocks
        _size += bodyStripes.packedSize(); // bodyStripes
        return _size;
    }
    void pack(BincodeBuf& buf) const;
    void unpack(BincodeBuf& buf);
    void clear();
    bool operator==(const AddSpanInitiateEntry&rhs) const;
};

std::ostream& operator<<(std::ostream& out, const AddSpanInitiateEntry& x);

struct AddSpanCertifyEntry {
    InodeId fileId;
    uint64_t byteOffset;
    BincodeList<BlockProof> proofs;

    static constexpr uint16_t STATIC_SIZE = 8 + 8 + BincodeList<BlockProof>::STATIC_SIZE; // fileId + byteOffset + proofs

    AddSpanCertifyEntry() { clear(); }
    size_t packedSize() const {
        size_t _size = 0;
        _size += 8; // fileId
        _size += 8; // byteOffset
        _size += proofs.packedSize(); // proofs
        return _size;
    }
    void pack(BincodeBuf& buf) const;
    void unpack(BincodeBuf& buf);
    void clear();
    bool operator==(const AddSpanCertifyEntry&rhs) const;
};

std::ostream& operator<<(std::ostream& out, const AddSpanCertifyEntry& x);

struct AddInlineSpanEntry {
    InodeId fileId;
    uint8_t storageClass;
    uint64_t byteOffset;
    uint32_t size;
    Crc crc;
    BincodeBytes body;

    static constexpr uint16_t STATIC_SIZE = 8 + 1 + 8 + 4 + 4 + BincodeBytes::STATIC_SIZE; // fileId + storageClass + byteOffset + size + crc + body

    AddInlineSpanEntry() { clear(); }
    size_t packedSize() const {
        size_t _size = 0;
        _size += 8; // fileId
        _size += 1; // storageClass
        _size += 8; // byteOffset
        _size += 4; // size
        _size += 4; // crc
        _size += body.packedSize(); // body
        return _size;
    }
    void pack(BincodeBuf& buf) const;
    void unpack(BincodeBuf& buf);
    void clear();
    bool operator==(const AddInlineSpanEntry&rhs) const;
};

std::ostream& operator<<(std::ostream& out, const AddInlineSpanEntry& x);

struct MakeFileTransientEntry {
    InodeId id;
    BincodeBytes note;

    static constexpr uint16_t STATIC_SIZE = 8 + BincodeBytes::STATIC_SIZE; // id + note

    MakeFileTransientEntry() { clear(); }
    size_t packedSize() const {
        size_t _size = 0;
        _size += 8; // id
        _size += note.packedSize(); // note
        return _size;
    }
    void pack(BincodeBuf& buf) const;
    void unpack(BincodeBuf& buf);
    void clear();
    bool operator==(const MakeFileTransientEntry&rhs) const;
};

std::ostream& operator<<(std::ostream& out, const MakeFileTransientEntry& x);

struct RemoveSpanCertifyEntry {
    InodeId fileId;
    uint64_t byteOffset;
    BincodeList<BlockProof> proofs;

    static constexpr uint16_t STATIC_SIZE = 8 + 8 + BincodeList<BlockProof>::STATIC_SIZE; // fileId + byteOffset + proofs

    RemoveSpanCertifyEntry() { clear(); }
    size_t packedSize() const {
        size_t _size = 0;
        _size += 8; // fileId
        _size += 8; // byteOffset
        _size += proofs.packedSize(); // proofs
        return _size;
    }
    void pack(BincodeBuf& buf) const;
    void unpack(BincodeBuf& buf);
    void clear();
    bool operator==(const RemoveSpanCertifyEntry&rhs) const;
};

std::ostream& operator<<(std::ostream& out, const RemoveSpanCertifyEntry& x);

struct RemoveOwnedSnapshotFileEdgeEntry {
    InodeId ownerId;
    InodeId targetId;
    BincodeBytes name;
    EggsTime creationTime;

    static constexpr uint16_t STATIC_SIZE = 8 + 8 + BincodeBytes::STATIC_SIZE + 8; // ownerId + targetId + name + creationTime

    RemoveOwnedSnapshotFileEdgeEntry() { clear(); }
    size_t packedSize() const {
        size_t _size = 0;
        _size += 8; // ownerId
        _size += 8; // targetId
        _size += name.packedSize(); // name
        _size += 8; // creationTime
        return _size;
    }
    void pack(BincodeBuf& buf) const;
    void unpack(BincodeBuf& buf);
    void clear();
    bool operator==(const RemoveOwnedSnapshotFileEdgeEntry&rhs) const;
};

std::ostream& operator<<(std::ostream& out, const RemoveOwnedSnapshotFileEdgeEntry& x);

struct SwapBlocksEntry {
    InodeId fileId1;
    uint64_t byteOffset1;
    uint64_t blockId1;
    InodeId fileId2;
    uint64_t byteOffset2;
    uint64_t blockId2;

    static constexpr uint16_t STATIC_SIZE = 8 + 8 + 8 + 8 + 8 + 8; // fileId1 + byteOffset1 + blockId1 + fileId2 + byteOffset2 + blockId2

    SwapBlocksEntry() { clear(); }
    size_t packedSize() const {
        size_t _size = 0;
        _size += 8; // fileId1
        _size += 8; // byteOffset1
        _size += 8; // blockId1
        _size += 8; // fileId2
        _size += 8; // byteOffset2
        _size += 8; // blockId2
        return _size;
    }
    void pack(BincodeBuf& buf) const;
    void unpack(BincodeBuf& buf);
    void clear();
    bool operator==(const SwapBlocksEntry&rhs) const;
};

std::ostream& operator<<(std::ostream& out, const SwapBlocksEntry& x);

struct ExpireTransientFileEntry {
    InodeId id;

    static constexpr uint16_t STATIC_SIZE = 8; // id

    ExpireTransientFileEntry() { clear(); }
    size_t packedSize() const {
        size_t _size = 0;
        _size += 8; // id
        return _size;
    }
    void pack(BincodeBuf& buf) const;
    void unpack(BincodeBuf& buf);
    void clear();
    bool operator==(const ExpireTransientFileEntry&rhs) const;
};

std::ostream& operator<<(std::ostream& out, const ExpireTransientFileEntry& x);

struct MoveSpanEntry {
    uint32_t spanSize;
    InodeId fileId1;
    uint64_t byteOffset1;
    BincodeFixedBytes<8> cookie1;
    InodeId fileId2;
    uint64_t byteOffset2;
    BincodeFixedBytes<8> cookie2;

    static constexpr uint16_t STATIC_SIZE = 4 + 8 + 8 + BincodeFixedBytes<8>::STATIC_SIZE + 8 + 8 + BincodeFixedBytes<8>::STATIC_SIZE; // spanSize + fileId1 + byteOffset1 + cookie1 + fileId2 + byteOffset2 + cookie2

    MoveSpanEntry() { clear(); }
    size_t packedSize() const {
        size_t _size = 0;
        _size += 4; // spanSize
        _size += 8; // fileId1
        _size += 8; // byteOffset1
        _size += BincodeFixedBytes<8>::STATIC_SIZE; // cookie1
        _size += 8; // fileId2
        _size += 8; // byteOffset2
        _size += BincodeFixedBytes<8>::STATIC_SIZE; // cookie2
        return _size;
    }
    void pack(BincodeBuf& buf) const;
    void unpack(BincodeBuf& buf);
    void clear();
    bool operator==(const MoveSpanEntry&rhs) const;
};

std::ostream& operator<<(std::ostream& out, const MoveSpanEntry& x);

struct SetTimeEntry {
    InodeId id;
    uint64_t mtime;
    uint64_t atime;

    static constexpr uint16_t STATIC_SIZE = 8 + 8 + 8; // id + mtime + atime

    SetTimeEntry() { clear(); }
    size_t packedSize() const {
        size_t _size = 0;
        _size += 8; // id
        _size += 8; // mtime
        _size += 8; // atime
        return _size;
    }
    void pack(BincodeBuf& buf) const;
    void unpack(BincodeBuf& buf);
    void clear();
    bool operator==(const SetTimeEntry&rhs) const;
};

std::ostream& operator<<(std::ostream& out, const SetTimeEntry& x);

struct ShardLogEntryContainer {
private:
    ShardLogEntryKind _kind = (ShardLogEntryKind)0;
    std::tuple<ConstructFileEntry, LinkFileEntry, SameDirectoryRenameEntry, SoftUnlinkFileEntry, CreateDirectoryInodeEntry, CreateLockedCurrentEdgeEntry, UnlockCurrentEdgeEntry, LockCurrentEdgeEntry, RemoveDirectoryOwnerEntry, RemoveInodeEntry, SetDirectoryOwnerEntry, SetDirectoryInfoEntry, RemoveNonOwnedEdgeEntry, SameShardHardFileUnlinkEntry, RemoveSpanInitiateEntry, UpdateBlockServicesEntry, AddSpanInitiateEntry, AddSpanCertifyEntry, AddInlineSpanEntry, MakeFileTransientEntry, RemoveSpanCertifyEntry, RemoveOwnedSnapshotFileEdgeEntry, SwapBlocksEntry, ExpireTransientFileEntry, MoveSpanEntry, SetTimeEntry> _data;
public:
    ShardLogEntryContainer();
    ShardLogEntryContainer(const ShardLogEntryContainer& other);
    void operator=(const ShardLogEntryContainer& other);

    ShardLogEntryKind kind() const { return _kind; }

    const ConstructFileEntry& getConstructFile() const;
    ConstructFileEntry& setConstructFile();
    const LinkFileEntry& getLinkFile() const;
    LinkFileEntry& setLinkFile();
    const SameDirectoryRenameEntry& getSameDirectoryRename() const;
    SameDirectoryRenameEntry& setSameDirectoryRename();
    const SoftUnlinkFileEntry& getSoftUnlinkFile() const;
    SoftUnlinkFileEntry& setSoftUnlinkFile();
    const CreateDirectoryInodeEntry& getCreateDirectoryInode() const;
    CreateDirectoryInodeEntry& setCreateDirectoryInode();
    const CreateLockedCurrentEdgeEntry& getCreateLockedCurrentEdge() const;
    CreateLockedCurrentEdgeEntry& setCreateLockedCurrentEdge();
    const UnlockCurrentEdgeEntry& getUnlockCurrentEdge() const;
    UnlockCurrentEdgeEntry& setUnlockCurrentEdge();
    const LockCurrentEdgeEntry& getLockCurrentEdge() const;
    LockCurrentEdgeEntry& setLockCurrentEdge();
    const RemoveDirectoryOwnerEntry& getRemoveDirectoryOwner() const;
    RemoveDirectoryOwnerEntry& setRemoveDirectoryOwner();
    const RemoveInodeEntry& getRemoveInode() const;
    RemoveInodeEntry& setRemoveInode();
    const SetDirectoryOwnerEntry& getSetDirectoryOwner() const;
    SetDirectoryOwnerEntry& setSetDirectoryOwner();
    const SetDirectoryInfoEntry& getSetDirectoryInfo() const;
    SetDirectoryInfoEntry& setSetDirectoryInfo();
    const RemoveNonOwnedEdgeEntry& getRemoveNonOwnedEdge() const;
    RemoveNonOwnedEdgeEntry& setRemoveNonOwnedEdge();
    const SameShardHardFileUnlinkEntry& getSameShardHardFileUnlink() const;
    SameShardHardFileUnlinkEntry& setSameShardHardFileUnlink();
    const RemoveSpanInitiateEntry& getRemoveSpanInitiate() const;
    RemoveSpanInitiateEntry& setRemoveSpanInitiate();
    const UpdateBlockServicesEntry& getUpdateBlockServices() const;
    UpdateBlockServicesEntry& setUpdateBlockServices();
    const AddSpanInitiateEntry& getAddSpanInitiate() const;
    AddSpanInitiateEntry& setAddSpanInitiate();
    const AddSpanCertifyEntry& getAddSpanCertify() const;
    AddSpanCertifyEntry& setAddSpanCertify();
    const AddInlineSpanEntry& getAddInlineSpan() const;
    AddInlineSpanEntry& setAddInlineSpan();
    const MakeFileTransientEntry& getMakeFileTransient() const;
    MakeFileTransientEntry& setMakeFileTransient();
    const RemoveSpanCertifyEntry& getRemoveSpanCertify() const;
    RemoveSpanCertifyEntry& setRemoveSpanCertify();
    const RemoveOwnedSnapshotFileEdgeEntry& getRemoveOwnedSnapshotFileEdge() const;
    RemoveOwnedSnapshotFileEdgeEntry& setRemoveOwnedSnapshotFileEdge();
    const SwapBlocksEntry& getSwapBlocks() const;
    SwapBlocksEntry& setSwapBlocks();
    const ExpireTransientFileEntry& getExpireTransientFile() const;
    ExpireTransientFileEntry& setExpireTransientFile();
    const MoveSpanEntry& getMoveSpan() const;
    MoveSpanEntry& setMoveSpan();
    const SetTimeEntry& getSetTime() const;
    SetTimeEntry& setSetTime();

    void clear() { _kind = (ShardLogEntryKind)0; };

    size_t packedSize() const;
    void pack(BincodeBuf& buf) const;
    void unpack(BincodeBuf& buf, ShardLogEntryKind kind);
    bool operator==(const ShardLogEntryContainer& other) const;
};

std::ostream& operator<<(std::ostream& out, const ShardLogEntryContainer& x);

