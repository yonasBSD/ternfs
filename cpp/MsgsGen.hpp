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
};

std::ostream& operator<<(std::ostream& out, EggsError err);

struct TransientFile {
    InodeId id;
    BincodeFixedBytes<8> cookie;
    EggsTime deadlineTime;

    static constexpr uint16_t STATIC_SIZE = 8 + BincodeFixedBytes<8>::STATIC_SIZE + 8; // id + cookie + deadlineTime

    TransientFile() { clear(); }
    uint16_t packedSize() const {
        uint16_t _size = 0;
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

struct FetchedBlock {
    uint8_t blockServiceIx;
    uint64_t blockId;
    BincodeFixedBytes<4> crc32;

    static constexpr uint16_t STATIC_SIZE = 1 + 8 + BincodeFixedBytes<4>::STATIC_SIZE; // blockServiceIx + blockId + crc32

    FetchedBlock() { clear(); }
    uint16_t packedSize() const {
        uint16_t _size = 0;
        _size += 1; // blockServiceIx
        _size += 8; // blockId
        _size += BincodeFixedBytes<4>::STATIC_SIZE; // crc32
        return _size;
    }
    void pack(BincodeBuf& buf) const;
    void unpack(BincodeBuf& buf);
    void clear();
    bool operator==(const FetchedBlock&rhs) const;
};

std::ostream& operator<<(std::ostream& out, const FetchedBlock& x);

struct CurrentEdge {
    InodeId targetId;
    uint64_t nameHash;
    BincodeBytes name;
    EggsTime creationTime;

    static constexpr uint16_t STATIC_SIZE = 8 + 8 + BincodeBytes::STATIC_SIZE + 8; // targetId + nameHash + name + creationTime

    CurrentEdge() { clear(); }
    uint16_t packedSize() const {
        uint16_t _size = 0;
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

struct Edge {
    bool current;
    InodeIdExtra targetId;
    uint64_t nameHash;
    BincodeBytes name;
    EggsTime creationTime;

    static constexpr uint16_t STATIC_SIZE = 1 + 8 + 8 + BincodeBytes::STATIC_SIZE + 8; // current + targetId + nameHash + name + creationTime

    Edge() { clear(); }
    uint16_t packedSize() const {
        uint16_t _size = 0;
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

struct FetchedSpan {
    uint64_t byteOffset;
    Parity parity;
    uint8_t storageClass;
    BincodeFixedBytes<4> crc32;
    uint64_t size;
    uint64_t blockSize;
    BincodeBytes bodyBytes;
    BincodeList<FetchedBlock> bodyBlocks;

    static constexpr uint16_t STATIC_SIZE = 1 + 1 + BincodeFixedBytes<4>::STATIC_SIZE + BincodeBytes::STATIC_SIZE + BincodeList<FetchedBlock>::STATIC_SIZE; // parity + storageClass + crc32 + bodyBytes + bodyBlocks

    FetchedSpan() { clear(); }
    uint16_t packedSize() const {
        uint16_t _size = 0;
        _size += varU61Size(byteOffset); // byteOffset
        _size += 1; // parity
        _size += 1; // storageClass
        _size += BincodeFixedBytes<4>::STATIC_SIZE; // crc32
        _size += varU61Size(size); // size
        _size += varU61Size(blockSize); // blockSize
        _size += bodyBytes.packedSize(); // bodyBytes
        _size += bodyBlocks.packedSize(); // bodyBlocks
        return _size;
    }
    void pack(BincodeBuf& buf) const;
    void unpack(BincodeBuf& buf);
    void clear();
    bool operator==(const FetchedSpan&rhs) const;
};

std::ostream& operator<<(std::ostream& out, const FetchedSpan& x);

struct BlockInfo {
    BincodeFixedBytes<4> blockServiceIp;
    uint16_t blockServicePort;
    uint64_t blockServiceId;
    uint64_t blockId;
    BincodeFixedBytes<8> certificate;

    static constexpr uint16_t STATIC_SIZE = BincodeFixedBytes<4>::STATIC_SIZE + 2 + 8 + 8 + BincodeFixedBytes<8>::STATIC_SIZE; // blockServiceIp + blockServicePort + blockServiceId + blockId + certificate

    BlockInfo() { clear(); }
    uint16_t packedSize() const {
        uint16_t _size = 0;
        _size += BincodeFixedBytes<4>::STATIC_SIZE; // blockServiceIp
        _size += 2; // blockServicePort
        _size += 8; // blockServiceId
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

struct NewBlockInfo {
    BincodeFixedBytes<4> crc32;

    static constexpr uint16_t STATIC_SIZE = BincodeFixedBytes<4>::STATIC_SIZE; // crc32

    NewBlockInfo() { clear(); }
    uint16_t packedSize() const {
        uint16_t _size = 0;
        _size += BincodeFixedBytes<4>::STATIC_SIZE; // crc32
        return _size;
    }
    void pack(BincodeBuf& buf) const;
    void unpack(BincodeBuf& buf);
    void clear();
    bool operator==(const NewBlockInfo&rhs) const;
};

std::ostream& operator<<(std::ostream& out, const NewBlockInfo& x);

struct BlockProof {
    uint64_t blockId;
    BincodeFixedBytes<8> proof;

    static constexpr uint16_t STATIC_SIZE = 8 + BincodeFixedBytes<8>::STATIC_SIZE; // blockId + proof

    BlockProof() { clear(); }
    uint16_t packedSize() const {
        uint16_t _size = 0;
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

struct SpanPolicy {
    uint64_t maxSize;
    uint8_t storageClass;
    Parity parity;

    static constexpr uint16_t STATIC_SIZE = 8 + 1 + 1; // maxSize + storageClass + parity

    SpanPolicy() { clear(); }
    uint16_t packedSize() const {
        uint16_t _size = 0;
        _size += 8; // maxSize
        _size += 1; // storageClass
        _size += 1; // parity
        return _size;
    }
    void pack(BincodeBuf& buf) const;
    void unpack(BincodeBuf& buf);
    void clear();
    bool operator==(const SpanPolicy&rhs) const;
};

std::ostream& operator<<(std::ostream& out, const SpanPolicy& x);

struct DirectoryInfoBody {
    uint8_t version;
    uint64_t deleteAfterTime;
    uint16_t deleteAfterVersions;
    BincodeList<SpanPolicy> spanPolicies;

    static constexpr uint16_t STATIC_SIZE = 1 + 8 + 2 + BincodeList<SpanPolicy>::STATIC_SIZE; // version + deleteAfterTime + deleteAfterVersions + spanPolicies

    DirectoryInfoBody() { clear(); }
    uint16_t packedSize() const {
        uint16_t _size = 0;
        _size += 1; // version
        _size += 8; // deleteAfterTime
        _size += 2; // deleteAfterVersions
        _size += spanPolicies.packedSize(); // spanPolicies
        return _size;
    }
    void pack(BincodeBuf& buf) const;
    void unpack(BincodeBuf& buf);
    void clear();
    bool operator==(const DirectoryInfoBody&rhs) const;
};

std::ostream& operator<<(std::ostream& out, const DirectoryInfoBody& x);

struct SetDirectoryInfo {
    bool inherited;
    BincodeBytes body;

    static constexpr uint16_t STATIC_SIZE = 1 + BincodeBytes::STATIC_SIZE; // inherited + body

    SetDirectoryInfo() { clear(); }
    uint16_t packedSize() const {
        uint16_t _size = 0;
        _size += 1; // inherited
        _size += body.packedSize(); // body
        return _size;
    }
    void pack(BincodeBuf& buf) const;
    void unpack(BincodeBuf& buf);
    void clear();
    bool operator==(const SetDirectoryInfo&rhs) const;
};

std::ostream& operator<<(std::ostream& out, const SetDirectoryInfo& x);

struct BlockServiceBlacklist {
    BincodeFixedBytes<4> ip;
    uint16_t port;
    uint64_t id;

    static constexpr uint16_t STATIC_SIZE = BincodeFixedBytes<4>::STATIC_SIZE + 2 + 8; // ip + port + id

    BlockServiceBlacklist() { clear(); }
    uint16_t packedSize() const {
        uint16_t _size = 0;
        _size += BincodeFixedBytes<4>::STATIC_SIZE; // ip
        _size += 2; // port
        _size += 8; // id
        return _size;
    }
    void pack(BincodeBuf& buf) const;
    void unpack(BincodeBuf& buf);
    void clear();
    bool operator==(const BlockServiceBlacklist&rhs) const;
};

std::ostream& operator<<(std::ostream& out, const BlockServiceBlacklist& x);

struct BlockService {
    BincodeFixedBytes<4> ip;
    uint16_t port;
    uint64_t id;
    uint8_t flags;

    static constexpr uint16_t STATIC_SIZE = BincodeFixedBytes<4>::STATIC_SIZE + 2 + 8 + 1; // ip + port + id + flags

    BlockService() { clear(); }
    uint16_t packedSize() const {
        uint16_t _size = 0;
        _size += BincodeFixedBytes<4>::STATIC_SIZE; // ip
        _size += 2; // port
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

struct FullReadDirCursor {
    bool current;
    uint64_t startHash;
    BincodeBytes startName;
    EggsTime startTime;

    static constexpr uint16_t STATIC_SIZE = 1 + 8 + BincodeBytes::STATIC_SIZE + 8; // current + startHash + startName + startTime

    FullReadDirCursor() { clear(); }
    uint16_t packedSize() const {
        uint16_t _size = 0;
        _size += 1; // current
        _size += 8; // startHash
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

struct EntryNewBlockInfo {
    uint64_t blockServiceId;
    BincodeFixedBytes<4> crc32;

    static constexpr uint16_t STATIC_SIZE = 8 + BincodeFixedBytes<4>::STATIC_SIZE; // blockServiceId + crc32

    EntryNewBlockInfo() { clear(); }
    uint16_t packedSize() const {
        uint16_t _size = 0;
        _size += 8; // blockServiceId
        _size += BincodeFixedBytes<4>::STATIC_SIZE; // crc32
        return _size;
    }
    void pack(BincodeBuf& buf) const;
    void unpack(BincodeBuf& buf);
    void clear();
    bool operator==(const EntryNewBlockInfo&rhs) const;
};

std::ostream& operator<<(std::ostream& out, const EntryNewBlockInfo& x);

struct SnapshotLookupEdge {
    InodeIdExtra targetId;
    EggsTime creationTime;

    static constexpr uint16_t STATIC_SIZE = 8 + 8; // targetId + creationTime

    SnapshotLookupEdge() { clear(); }
    uint16_t packedSize() const {
        uint16_t _size = 0;
        _size += 8; // targetId
        _size += 8; // creationTime
        return _size;
    }
    void pack(BincodeBuf& buf) const;
    void unpack(BincodeBuf& buf);
    void clear();
    bool operator==(const SnapshotLookupEdge&rhs) const;
};

std::ostream& operator<<(std::ostream& out, const SnapshotLookupEdge& x);

struct BlockServiceInfo {
    uint64_t id;
    BincodeFixedBytes<4> ip;
    uint16_t port;
    uint8_t storageClass;
    BincodeFixedBytes<16> failureDomain;
    BincodeFixedBytes<16> secretKey;

    static constexpr uint16_t STATIC_SIZE = 8 + BincodeFixedBytes<4>::STATIC_SIZE + 2 + 1 + BincodeFixedBytes<16>::STATIC_SIZE + BincodeFixedBytes<16>::STATIC_SIZE; // id + ip + port + storageClass + failureDomain + secretKey

    BlockServiceInfo() { clear(); }
    uint16_t packedSize() const {
        uint16_t _size = 0;
        _size += 8; // id
        _size += BincodeFixedBytes<4>::STATIC_SIZE; // ip
        _size += 2; // port
        _size += 1; // storageClass
        _size += BincodeFixedBytes<16>::STATIC_SIZE; // failureDomain
        _size += BincodeFixedBytes<16>::STATIC_SIZE; // secretKey
        return _size;
    }
    void pack(BincodeBuf& buf) const;
    void unpack(BincodeBuf& buf);
    void clear();
    bool operator==(const BlockServiceInfo&rhs) const;
};

std::ostream& operator<<(std::ostream& out, const BlockServiceInfo& x);

struct ShardInfo {
    BincodeFixedBytes<4> ip;
    uint16_t port;

    static constexpr uint16_t STATIC_SIZE = BincodeFixedBytes<4>::STATIC_SIZE + 2; // ip + port

    ShardInfo() { clear(); }
    uint16_t packedSize() const {
        uint16_t _size = 0;
        _size += BincodeFixedBytes<4>::STATIC_SIZE; // ip
        _size += 2; // port
        return _size;
    }
    void pack(BincodeBuf& buf) const;
    void unpack(BincodeBuf& buf);
    void clear();
    bool operator==(const ShardInfo&rhs) const;
};

std::ostream& operator<<(std::ostream& out, const ShardInfo& x);

struct LookupReq {
    InodeId dirId;
    BincodeBytes name;

    static constexpr uint16_t STATIC_SIZE = 8 + BincodeBytes::STATIC_SIZE; // dirId + name

    LookupReq() { clear(); }
    uint16_t packedSize() const {
        uint16_t _size = 0;
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
    uint16_t packedSize() const {
        uint16_t _size = 0;
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
    uint16_t packedSize() const {
        uint16_t _size = 0;
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
    uint64_t size;

    static constexpr uint16_t STATIC_SIZE = 8 + 8; // mtime + size

    StatFileResp() { clear(); }
    uint16_t packedSize() const {
        uint16_t _size = 0;
        _size += 8; // mtime
        _size += 8; // size
        return _size;
    }
    void pack(BincodeBuf& buf) const;
    void unpack(BincodeBuf& buf);
    void clear();
    bool operator==(const StatFileResp&rhs) const;
};

std::ostream& operator<<(std::ostream& out, const StatFileResp& x);

struct StatTransientFileReq {
    InodeId id;

    static constexpr uint16_t STATIC_SIZE = 8; // id

    StatTransientFileReq() { clear(); }
    uint16_t packedSize() const {
        uint16_t _size = 0;
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
    uint16_t packedSize() const {
        uint16_t _size = 0;
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

struct StatDirectoryReq {
    InodeId id;

    static constexpr uint16_t STATIC_SIZE = 8; // id

    StatDirectoryReq() { clear(); }
    uint16_t packedSize() const {
        uint16_t _size = 0;
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
    BincodeBytes info;

    static constexpr uint16_t STATIC_SIZE = 8 + 8 + BincodeBytes::STATIC_SIZE; // mtime + owner + info

    StatDirectoryResp() { clear(); }
    uint16_t packedSize() const {
        uint16_t _size = 0;
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

    static constexpr uint16_t STATIC_SIZE = 8 + 8; // dirId + startHash

    ReadDirReq() { clear(); }
    uint16_t packedSize() const {
        uint16_t _size = 0;
        _size += 8; // dirId
        _size += 8; // startHash
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
    uint16_t packedSize() const {
        uint16_t _size = 0;
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
    uint16_t packedSize() const {
        uint16_t _size = 0;
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
    uint16_t packedSize() const {
        uint16_t _size = 0;
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
    uint8_t storageClass;
    BincodeList<BlockServiceBlacklist> blacklist;
    Parity parity;
    BincodeFixedBytes<4> crc32;
    uint64_t size;
    uint64_t blockSize;
    BincodeBytes bodyBytes;
    BincodeList<NewBlockInfo> bodyBlocks;

    static constexpr uint16_t STATIC_SIZE = 8 + BincodeFixedBytes<8>::STATIC_SIZE + 1 + BincodeList<BlockServiceBlacklist>::STATIC_SIZE + 1 + BincodeFixedBytes<4>::STATIC_SIZE + BincodeBytes::STATIC_SIZE + BincodeList<NewBlockInfo>::STATIC_SIZE; // fileId + cookie + storageClass + blacklist + parity + crc32 + bodyBytes + bodyBlocks

    AddSpanInitiateReq() { clear(); }
    uint16_t packedSize() const {
        uint16_t _size = 0;
        _size += 8; // fileId
        _size += BincodeFixedBytes<8>::STATIC_SIZE; // cookie
        _size += varU61Size(byteOffset); // byteOffset
        _size += 1; // storageClass
        _size += blacklist.packedSize(); // blacklist
        _size += 1; // parity
        _size += BincodeFixedBytes<4>::STATIC_SIZE; // crc32
        _size += varU61Size(size); // size
        _size += varU61Size(blockSize); // blockSize
        _size += bodyBytes.packedSize(); // bodyBytes
        _size += bodyBlocks.packedSize(); // bodyBlocks
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
    uint16_t packedSize() const {
        uint16_t _size = 0;
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

    static constexpr uint16_t STATIC_SIZE = 8 + BincodeFixedBytes<8>::STATIC_SIZE + BincodeList<BlockProof>::STATIC_SIZE; // fileId + cookie + proofs

    AddSpanCertifyReq() { clear(); }
    uint16_t packedSize() const {
        uint16_t _size = 0;
        _size += 8; // fileId
        _size += BincodeFixedBytes<8>::STATIC_SIZE; // cookie
        _size += varU61Size(byteOffset); // byteOffset
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
    uint16_t packedSize() const {
        uint16_t _size = 0;
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
    uint16_t packedSize() const {
        uint16_t _size = 0;
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
    uint16_t packedSize() const {
        uint16_t _size = 0;
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
    uint16_t packedSize() const {
        uint16_t _size = 0;
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

    static constexpr uint16_t STATIC_SIZE = 0; // 

    SoftUnlinkFileResp() { clear(); }
    uint16_t packedSize() const {
        uint16_t _size = 0;
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

    static constexpr uint16_t STATIC_SIZE = 8; // fileId

    FileSpansReq() { clear(); }
    uint16_t packedSize() const {
        uint16_t _size = 0;
        _size += 8; // fileId
        _size += varU61Size(byteOffset); // byteOffset
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

    static constexpr uint16_t STATIC_SIZE = BincodeList<BlockService>::STATIC_SIZE + BincodeList<FetchedSpan>::STATIC_SIZE; // blockServices + spans

    FileSpansResp() { clear(); }
    uint16_t packedSize() const {
        uint16_t _size = 0;
        _size += varU61Size(nextOffset); // nextOffset
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
    uint16_t packedSize() const {
        uint16_t _size = 0;
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
    uint16_t packedSize() const {
        uint16_t _size = 0;
        _size += 8; // newCreationTime
        return _size;
    }
    void pack(BincodeBuf& buf) const;
    void unpack(BincodeBuf& buf);
    void clear();
    bool operator==(const SameDirectoryRenameResp&rhs) const;
};

std::ostream& operator<<(std::ostream& out, const SameDirectoryRenameResp& x);

struct SetDirectoryInfoReq {
    InodeId id;
    SetDirectoryInfo info;

    static constexpr uint16_t STATIC_SIZE = 8 + SetDirectoryInfo::STATIC_SIZE; // id + info

    SetDirectoryInfoReq() { clear(); }
    uint16_t packedSize() const {
        uint16_t _size = 0;
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
    uint16_t packedSize() const {
        uint16_t _size = 0;
        return _size;
    }
    void pack(BincodeBuf& buf) const;
    void unpack(BincodeBuf& buf);
    void clear();
    bool operator==(const SetDirectoryInfoResp&rhs) const;
};

std::ostream& operator<<(std::ostream& out, const SetDirectoryInfoResp& x);

struct SnapshotLookupReq {
    InodeId dirId;
    BincodeBytes name;
    EggsTime startFrom;

    static constexpr uint16_t STATIC_SIZE = 8 + BincodeBytes::STATIC_SIZE + 8; // dirId + name + startFrom

    SnapshotLookupReq() { clear(); }
    uint16_t packedSize() const {
        uint16_t _size = 0;
        _size += 8; // dirId
        _size += name.packedSize(); // name
        _size += 8; // startFrom
        return _size;
    }
    void pack(BincodeBuf& buf) const;
    void unpack(BincodeBuf& buf);
    void clear();
    bool operator==(const SnapshotLookupReq&rhs) const;
};

std::ostream& operator<<(std::ostream& out, const SnapshotLookupReq& x);

struct SnapshotLookupResp {
    EggsTime nextTime;
    BincodeList<SnapshotLookupEdge> edges;

    static constexpr uint16_t STATIC_SIZE = 8 + BincodeList<SnapshotLookupEdge>::STATIC_SIZE; // nextTime + edges

    SnapshotLookupResp() { clear(); }
    uint16_t packedSize() const {
        uint16_t _size = 0;
        _size += 8; // nextTime
        _size += edges.packedSize(); // edges
        return _size;
    }
    void pack(BincodeBuf& buf) const;
    void unpack(BincodeBuf& buf);
    void clear();
    bool operator==(const SnapshotLookupResp&rhs) const;
};

std::ostream& operator<<(std::ostream& out, const SnapshotLookupResp& x);

struct ExpireTransientFileReq {
    InodeId id;

    static constexpr uint16_t STATIC_SIZE = 8; // id

    ExpireTransientFileReq() { clear(); }
    uint16_t packedSize() const {
        uint16_t _size = 0;
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
    uint16_t packedSize() const {
        uint16_t _size = 0;
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

    static constexpr uint16_t STATIC_SIZE = 8; // beginId

    VisitDirectoriesReq() { clear(); }
    uint16_t packedSize() const {
        uint16_t _size = 0;
        _size += 8; // beginId
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
    uint16_t packedSize() const {
        uint16_t _size = 0;
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

    static constexpr uint16_t STATIC_SIZE = 8; // beginId

    VisitFilesReq() { clear(); }
    uint16_t packedSize() const {
        uint16_t _size = 0;
        _size += 8; // beginId
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
    uint16_t packedSize() const {
        uint16_t _size = 0;
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

    static constexpr uint16_t STATIC_SIZE = 8; // beginId

    VisitTransientFilesReq() { clear(); }
    uint16_t packedSize() const {
        uint16_t _size = 0;
        _size += 8; // beginId
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
    uint16_t packedSize() const {
        uint16_t _size = 0;
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

struct FullReadDirReq {
    InodeId dirId;
    FullReadDirCursor cursor;

    static constexpr uint16_t STATIC_SIZE = 8 + FullReadDirCursor::STATIC_SIZE; // dirId + cursor

    FullReadDirReq() { clear(); }
    uint16_t packedSize() const {
        uint16_t _size = 0;
        _size += 8; // dirId
        _size += cursor.packedSize(); // cursor
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
    uint16_t packedSize() const {
        uint16_t _size = 0;
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

struct RemoveNonOwnedEdgeReq {
    InodeId dirId;
    InodeId targetId;
    BincodeBytes name;
    EggsTime creationTime;

    static constexpr uint16_t STATIC_SIZE = 8 + 8 + BincodeBytes::STATIC_SIZE + 8; // dirId + targetId + name + creationTime

    RemoveNonOwnedEdgeReq() { clear(); }
    uint16_t packedSize() const {
        uint16_t _size = 0;
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
    uint16_t packedSize() const {
        uint16_t _size = 0;
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
    uint16_t packedSize() const {
        uint16_t _size = 0;
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
    uint16_t packedSize() const {
        uint16_t _size = 0;
        return _size;
    }
    void pack(BincodeBuf& buf) const;
    void unpack(BincodeBuf& buf);
    void clear();
    bool operator==(const SameShardHardFileUnlinkResp&rhs) const;
};

std::ostream& operator<<(std::ostream& out, const SameShardHardFileUnlinkResp& x);

struct RemoveSpanInitiateReq {
    InodeId fileId;
    BincodeFixedBytes<8> cookie;

    static constexpr uint16_t STATIC_SIZE = 8 + BincodeFixedBytes<8>::STATIC_SIZE; // fileId + cookie

    RemoveSpanInitiateReq() { clear(); }
    uint16_t packedSize() const {
        uint16_t _size = 0;
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

    static constexpr uint16_t STATIC_SIZE = BincodeList<BlockInfo>::STATIC_SIZE; // blocks

    RemoveSpanInitiateResp() { clear(); }
    uint16_t packedSize() const {
        uint16_t _size = 0;
        _size += varU61Size(byteOffset); // byteOffset
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

    static constexpr uint16_t STATIC_SIZE = 8 + BincodeFixedBytes<8>::STATIC_SIZE + BincodeList<BlockProof>::STATIC_SIZE; // fileId + cookie + proofs

    RemoveSpanCertifyReq() { clear(); }
    uint16_t packedSize() const {
        uint16_t _size = 0;
        _size += 8; // fileId
        _size += BincodeFixedBytes<8>::STATIC_SIZE; // cookie
        _size += varU61Size(byteOffset); // byteOffset
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
    uint16_t packedSize() const {
        uint16_t _size = 0;
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
    uint16_t packedSize() const {
        uint16_t _size = 0;
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
    uint16_t packedSize() const {
        uint16_t _size = 0;
        return _size;
    }
    void pack(BincodeBuf& buf) const;
    void unpack(BincodeBuf& buf);
    void clear();
    bool operator==(const SwapBlocksResp&rhs) const;
};

std::ostream& operator<<(std::ostream& out, const SwapBlocksResp& x);

struct BlockServiceFilesReq {
    uint64_t blockServiceId;
    InodeId startFrom;

    static constexpr uint16_t STATIC_SIZE = 8 + 8; // blockServiceId + startFrom

    BlockServiceFilesReq() { clear(); }
    uint16_t packedSize() const {
        uint16_t _size = 0;
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
    uint16_t packedSize() const {
        uint16_t _size = 0;
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
    uint16_t packedSize() const {
        uint16_t _size = 0;
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
    uint16_t packedSize() const {
        uint16_t _size = 0;
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
    SetDirectoryInfo info;

    static constexpr uint16_t STATIC_SIZE = 8 + 8 + SetDirectoryInfo::STATIC_SIZE; // id + ownerId + info

    CreateDirectoryInodeReq() { clear(); }
    uint16_t packedSize() const {
        uint16_t _size = 0;
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
    uint16_t packedSize() const {
        uint16_t _size = 0;
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
    uint16_t packedSize() const {
        uint16_t _size = 0;
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
    uint16_t packedSize() const {
        uint16_t _size = 0;
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
    BincodeBytes info;

    static constexpr uint16_t STATIC_SIZE = 8 + BincodeBytes::STATIC_SIZE; // dirId + info

    RemoveDirectoryOwnerReq() { clear(); }
    uint16_t packedSize() const {
        uint16_t _size = 0;
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
    uint16_t packedSize() const {
        uint16_t _size = 0;
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

    static constexpr uint16_t STATIC_SIZE = 8 + BincodeBytes::STATIC_SIZE + 8; // dirId + name + targetId

    CreateLockedCurrentEdgeReq() { clear(); }
    uint16_t packedSize() const {
        uint16_t _size = 0;
        _size += 8; // dirId
        _size += name.packedSize(); // name
        _size += 8; // targetId
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
    uint16_t packedSize() const {
        uint16_t _size = 0;
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
    uint16_t packedSize() const {
        uint16_t _size = 0;
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
    uint16_t packedSize() const {
        uint16_t _size = 0;
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
    uint16_t packedSize() const {
        uint16_t _size = 0;
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
    uint16_t packedSize() const {
        uint16_t _size = 0;
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
    uint16_t packedSize() const {
        uint16_t _size = 0;
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
    uint16_t packedSize() const {
        uint16_t _size = 0;
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
    uint16_t packedSize() const {
        uint16_t _size = 0;
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
    uint16_t packedSize() const {
        uint16_t _size = 0;
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
    SetDirectoryInfo info;

    static constexpr uint16_t STATIC_SIZE = 8 + BincodeBytes::STATIC_SIZE + SetDirectoryInfo::STATIC_SIZE; // ownerId + name + info

    MakeDirectoryReq() { clear(); }
    uint16_t packedSize() const {
        uint16_t _size = 0;
        _size += 8; // ownerId
        _size += name.packedSize(); // name
        _size += info.packedSize(); // info
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
    uint16_t packedSize() const {
        uint16_t _size = 0;
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
    uint16_t packedSize() const {
        uint16_t _size = 0;
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
    uint16_t packedSize() const {
        uint16_t _size = 0;
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
    uint16_t packedSize() const {
        uint16_t _size = 0;
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
    uint16_t packedSize() const {
        uint16_t _size = 0;
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
    uint16_t packedSize() const {
        uint16_t _size = 0;
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
    uint16_t packedSize() const {
        uint16_t _size = 0;
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
    uint16_t packedSize() const {
        uint16_t _size = 0;
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
    uint16_t packedSize() const {
        uint16_t _size = 0;
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
    uint16_t packedSize() const {
        uint16_t _size = 0;
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
    uint16_t packedSize() const {
        uint16_t _size = 0;
        return _size;
    }
    void pack(BincodeBuf& buf) const;
    void unpack(BincodeBuf& buf);
    void clear();
    bool operator==(const CrossShardHardUnlinkFileResp&rhs) const;
};

std::ostream& operator<<(std::ostream& out, const CrossShardHardUnlinkFileResp& x);

struct BlockServicesForShardReq {
    ShardId shard;

    static constexpr uint16_t STATIC_SIZE = 1; // shard

    BlockServicesForShardReq() { clear(); }
    uint16_t packedSize() const {
        uint16_t _size = 0;
        _size += 1; // shard
        return _size;
    }
    void pack(BincodeBuf& buf) const;
    void unpack(BincodeBuf& buf);
    void clear();
    bool operator==(const BlockServicesForShardReq&rhs) const;
};

std::ostream& operator<<(std::ostream& out, const BlockServicesForShardReq& x);

struct BlockServicesForShardResp {
    BincodeList<BlockServiceInfo> blockServices;

    static constexpr uint16_t STATIC_SIZE = BincodeList<BlockServiceInfo>::STATIC_SIZE; // blockServices

    BlockServicesForShardResp() { clear(); }
    uint16_t packedSize() const {
        uint16_t _size = 0;
        _size += blockServices.packedSize(); // blockServices
        return _size;
    }
    void pack(BincodeBuf& buf) const;
    void unpack(BincodeBuf& buf);
    void clear();
    bool operator==(const BlockServicesForShardResp&rhs) const;
};

std::ostream& operator<<(std::ostream& out, const BlockServicesForShardResp& x);

struct RegisterBlockServiceReq {
    BlockServiceInfo blockService;

    static constexpr uint16_t STATIC_SIZE = BlockServiceInfo::STATIC_SIZE; // blockService

    RegisterBlockServiceReq() { clear(); }
    uint16_t packedSize() const {
        uint16_t _size = 0;
        _size += blockService.packedSize(); // blockService
        return _size;
    }
    void pack(BincodeBuf& buf) const;
    void unpack(BincodeBuf& buf);
    void clear();
    bool operator==(const RegisterBlockServiceReq&rhs) const;
};

std::ostream& operator<<(std::ostream& out, const RegisterBlockServiceReq& x);

struct RegisterBlockServiceResp {

    static constexpr uint16_t STATIC_SIZE = 0; // 

    RegisterBlockServiceResp() { clear(); }
    uint16_t packedSize() const {
        uint16_t _size = 0;
        return _size;
    }
    void pack(BincodeBuf& buf) const;
    void unpack(BincodeBuf& buf);
    void clear();
    bool operator==(const RegisterBlockServiceResp&rhs) const;
};

std::ostream& operator<<(std::ostream& out, const RegisterBlockServiceResp& x);

struct ShardsReq {

    static constexpr uint16_t STATIC_SIZE = 0; // 

    ShardsReq() { clear(); }
    uint16_t packedSize() const {
        uint16_t _size = 0;
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
    uint16_t packedSize() const {
        uint16_t _size = 0;
        _size += shards.packedSize(); // shards
        return _size;
    }
    void pack(BincodeBuf& buf) const;
    void unpack(BincodeBuf& buf);
    void clear();
    bool operator==(const ShardsResp&rhs) const;
};

std::ostream& operator<<(std::ostream& out, const ShardsResp& x);

struct RegisterShardReq {
    ShardId id;
    ShardInfo info;

    static constexpr uint16_t STATIC_SIZE = 1 + ShardInfo::STATIC_SIZE; // id + info

    RegisterShardReq() { clear(); }
    uint16_t packedSize() const {
        uint16_t _size = 0;
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
    uint16_t packedSize() const {
        uint16_t _size = 0;
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
    uint16_t packedSize() const {
        uint16_t _size = 0;
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
    uint16_t packedSize() const {
        uint16_t _size = 0;
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
    BincodeFixedBytes<4> ip;
    uint16_t port;

    static constexpr uint16_t STATIC_SIZE = BincodeFixedBytes<4>::STATIC_SIZE + 2; // ip + port

    RegisterCdcReq() { clear(); }
    uint16_t packedSize() const {
        uint16_t _size = 0;
        _size += BincodeFixedBytes<4>::STATIC_SIZE; // ip
        _size += 2; // port
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
    uint16_t packedSize() const {
        uint16_t _size = 0;
        return _size;
    }
    void pack(BincodeBuf& buf) const;
    void unpack(BincodeBuf& buf);
    void clear();
    bool operator==(const RegisterCdcResp&rhs) const;
};

std::ostream& operator<<(std::ostream& out, const RegisterCdcResp& x);

struct CdcReq {

    static constexpr uint16_t STATIC_SIZE = 0; // 

    CdcReq() { clear(); }
    uint16_t packedSize() const {
        uint16_t _size = 0;
        return _size;
    }
    void pack(BincodeBuf& buf) const;
    void unpack(BincodeBuf& buf);
    void clear();
    bool operator==(const CdcReq&rhs) const;
};

std::ostream& operator<<(std::ostream& out, const CdcReq& x);

struct CdcResp {
    BincodeFixedBytes<4> ip;
    uint16_t port;

    static constexpr uint16_t STATIC_SIZE = BincodeFixedBytes<4>::STATIC_SIZE + 2; // ip + port

    CdcResp() { clear(); }
    uint16_t packedSize() const {
        uint16_t _size = 0;
        _size += BincodeFixedBytes<4>::STATIC_SIZE; // ip
        _size += 2; // port
        return _size;
    }
    void pack(BincodeBuf& buf) const;
    void unpack(BincodeBuf& buf);
    void clear();
    bool operator==(const CdcResp&rhs) const;
};

std::ostream& operator<<(std::ostream& out, const CdcResp& x);

enum class ShardMessageKind : uint8_t {
    ERROR = 0,
    LOOKUP = 1,
    STAT_FILE = 2,
    STAT_TRANSIENT_FILE = 10,
    STAT_DIRECTORY = 8,
    READ_DIR = 3,
    CONSTRUCT_FILE = 4,
    ADD_SPAN_INITIATE = 5,
    ADD_SPAN_CERTIFY = 6,
    LINK_FILE = 7,
    SOFT_UNLINK_FILE = 12,
    FILE_SPANS = 13,
    SAME_DIRECTORY_RENAME = 14,
    SET_DIRECTORY_INFO = 15,
    SNAPSHOT_LOOKUP = 9,
    EXPIRE_TRANSIENT_FILE = 11,
    VISIT_DIRECTORIES = 21,
    VISIT_FILES = 32,
    VISIT_TRANSIENT_FILES = 22,
    FULL_READ_DIR = 33,
    REMOVE_NON_OWNED_EDGE = 23,
    SAME_SHARD_HARD_FILE_UNLINK = 24,
    REMOVE_SPAN_INITIATE = 25,
    REMOVE_SPAN_CERTIFY = 26,
    SWAP_BLOCKS = 34,
    BLOCK_SERVICE_FILES = 35,
    REMOVE_INODE = 36,
    CREATE_DIRECTORY_INODE = 128,
    SET_DIRECTORY_OWNER = 129,
    REMOVE_DIRECTORY_OWNER = 137,
    CREATE_LOCKED_CURRENT_EDGE = 130,
    LOCK_CURRENT_EDGE = 131,
    UNLOCK_CURRENT_EDGE = 132,
    REMOVE_OWNED_SNAPSHOT_FILE_EDGE = 134,
    MAKE_FILE_TRANSIENT = 135,
};

std::ostream& operator<<(std::ostream& out, ShardMessageKind kind);

struct ShardReqContainer {
private:
    ShardMessageKind _kind = (ShardMessageKind)0;
    std::tuple<LookupReq, StatFileReq, StatTransientFileReq, StatDirectoryReq, ReadDirReq, ConstructFileReq, AddSpanInitiateReq, AddSpanCertifyReq, LinkFileReq, SoftUnlinkFileReq, FileSpansReq, SameDirectoryRenameReq, SetDirectoryInfoReq, SnapshotLookupReq, ExpireTransientFileReq, VisitDirectoriesReq, VisitFilesReq, VisitTransientFilesReq, FullReadDirReq, RemoveNonOwnedEdgeReq, SameShardHardFileUnlinkReq, RemoveSpanInitiateReq, RemoveSpanCertifyReq, SwapBlocksReq, BlockServiceFilesReq, RemoveInodeReq, CreateDirectoryInodeReq, SetDirectoryOwnerReq, RemoveDirectoryOwnerReq, CreateLockedCurrentEdgeReq, LockCurrentEdgeReq, UnlockCurrentEdgeReq, RemoveOwnedSnapshotFileEdgeReq, MakeFileTransientReq> _data;
public:
    ShardMessageKind kind() const { return _kind; }
    const LookupReq& getLookup() const;
    LookupReq& setLookup();
    const StatFileReq& getStatFile() const;
    StatFileReq& setStatFile();
    const StatTransientFileReq& getStatTransientFile() const;
    StatTransientFileReq& setStatTransientFile();
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
    const SetDirectoryInfoReq& getSetDirectoryInfo() const;
    SetDirectoryInfoReq& setSetDirectoryInfo();
    const SnapshotLookupReq& getSnapshotLookup() const;
    SnapshotLookupReq& setSnapshotLookup();
    const ExpireTransientFileReq& getExpireTransientFile() const;
    ExpireTransientFileReq& setExpireTransientFile();
    const VisitDirectoriesReq& getVisitDirectories() const;
    VisitDirectoriesReq& setVisitDirectories();
    const VisitFilesReq& getVisitFiles() const;
    VisitFilesReq& setVisitFiles();
    const VisitTransientFilesReq& getVisitTransientFiles() const;
    VisitTransientFilesReq& setVisitTransientFiles();
    const FullReadDirReq& getFullReadDir() const;
    FullReadDirReq& setFullReadDir();
    const RemoveNonOwnedEdgeReq& getRemoveNonOwnedEdge() const;
    RemoveNonOwnedEdgeReq& setRemoveNonOwnedEdge();
    const SameShardHardFileUnlinkReq& getSameShardHardFileUnlink() const;
    SameShardHardFileUnlinkReq& setSameShardHardFileUnlink();
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
};

std::ostream& operator<<(std::ostream& out, const ShardReqContainer& x);

struct ShardRespContainer {
private:
    ShardMessageKind _kind = (ShardMessageKind)0;
    std::tuple<LookupResp, StatFileResp, StatTransientFileResp, StatDirectoryResp, ReadDirResp, ConstructFileResp, AddSpanInitiateResp, AddSpanCertifyResp, LinkFileResp, SoftUnlinkFileResp, FileSpansResp, SameDirectoryRenameResp, SetDirectoryInfoResp, SnapshotLookupResp, ExpireTransientFileResp, VisitDirectoriesResp, VisitFilesResp, VisitTransientFilesResp, FullReadDirResp, RemoveNonOwnedEdgeResp, SameShardHardFileUnlinkResp, RemoveSpanInitiateResp, RemoveSpanCertifyResp, SwapBlocksResp, BlockServiceFilesResp, RemoveInodeResp, CreateDirectoryInodeResp, SetDirectoryOwnerResp, RemoveDirectoryOwnerResp, CreateLockedCurrentEdgeResp, LockCurrentEdgeResp, UnlockCurrentEdgeResp, RemoveOwnedSnapshotFileEdgeResp, MakeFileTransientResp> _data;
public:
    ShardMessageKind kind() const { return _kind; }
    const LookupResp& getLookup() const;
    LookupResp& setLookup();
    const StatFileResp& getStatFile() const;
    StatFileResp& setStatFile();
    const StatTransientFileResp& getStatTransientFile() const;
    StatTransientFileResp& setStatTransientFile();
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
    const SetDirectoryInfoResp& getSetDirectoryInfo() const;
    SetDirectoryInfoResp& setSetDirectoryInfo();
    const SnapshotLookupResp& getSnapshotLookup() const;
    SnapshotLookupResp& setSnapshotLookup();
    const ExpireTransientFileResp& getExpireTransientFile() const;
    ExpireTransientFileResp& setExpireTransientFile();
    const VisitDirectoriesResp& getVisitDirectories() const;
    VisitDirectoriesResp& setVisitDirectories();
    const VisitFilesResp& getVisitFiles() const;
    VisitFilesResp& setVisitFiles();
    const VisitTransientFilesResp& getVisitTransientFiles() const;
    VisitTransientFilesResp& setVisitTransientFiles();
    const FullReadDirResp& getFullReadDir() const;
    FullReadDirResp& setFullReadDir();
    const RemoveNonOwnedEdgeResp& getRemoveNonOwnedEdge() const;
    RemoveNonOwnedEdgeResp& setRemoveNonOwnedEdge();
    const SameShardHardFileUnlinkResp& getSameShardHardFileUnlink() const;
    SameShardHardFileUnlinkResp& setSameShardHardFileUnlink();
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
};

std::ostream& operator<<(std::ostream& out, const ShardRespContainer& x);

enum class CDCMessageKind : uint8_t {
    ERROR = 0,
    MAKE_DIRECTORY = 1,
    RENAME_FILE = 2,
    SOFT_UNLINK_DIRECTORY = 3,
    RENAME_DIRECTORY = 4,
    HARD_UNLINK_DIRECTORY = 5,
    CROSS_SHARD_HARD_UNLINK_FILE = 6,
};

std::ostream& operator<<(std::ostream& out, CDCMessageKind kind);

struct CDCReqContainer {
private:
    CDCMessageKind _kind = (CDCMessageKind)0;
    std::tuple<MakeDirectoryReq, RenameFileReq, SoftUnlinkDirectoryReq, RenameDirectoryReq, HardUnlinkDirectoryReq, CrossShardHardUnlinkFileReq> _data;
public:
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
};

std::ostream& operator<<(std::ostream& out, const CDCReqContainer& x);

struct CDCRespContainer {
private:
    CDCMessageKind _kind = (CDCMessageKind)0;
    std::tuple<MakeDirectoryResp, RenameFileResp, SoftUnlinkDirectoryResp, RenameDirectoryResp, HardUnlinkDirectoryResp, CrossShardHardUnlinkFileResp> _data;
public:
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
};

std::ostream& operator<<(std::ostream& out, const CDCRespContainer& x);

enum class ShuckleMessageKind : uint8_t {
    ERROR = 0,
    BLOCK_SERVICES_FOR_SHARD = 1,
    REGISTER_BLOCK_SERVICE = 2,
    SHARDS = 3,
    REGISTER_SHARD = 4,
    ALL_BLOCK_SERVICES = 5,
    REGISTER_CDC = 6,
    CDC = 7,
};

std::ostream& operator<<(std::ostream& out, ShuckleMessageKind kind);

struct ShuckleReqContainer {
private:
    ShuckleMessageKind _kind = (ShuckleMessageKind)0;
    std::tuple<BlockServicesForShardReq, RegisterBlockServiceReq, ShardsReq, RegisterShardReq, AllBlockServicesReq, RegisterCdcReq, CdcReq> _data;
public:
    ShuckleMessageKind kind() const { return _kind; }
    const BlockServicesForShardReq& getBlockServicesForShard() const;
    BlockServicesForShardReq& setBlockServicesForShard();
    const RegisterBlockServiceReq& getRegisterBlockService() const;
    RegisterBlockServiceReq& setRegisterBlockService();
    const ShardsReq& getShards() const;
    ShardsReq& setShards();
    const RegisterShardReq& getRegisterShard() const;
    RegisterShardReq& setRegisterShard();
    const AllBlockServicesReq& getAllBlockServices() const;
    AllBlockServicesReq& setAllBlockServices();
    const RegisterCdcReq& getRegisterCdc() const;
    RegisterCdcReq& setRegisterCdc();
    const CdcReq& getCdc() const;
    CdcReq& setCdc();

    void clear() { _kind = (ShuckleMessageKind)0; };

    size_t packedSize() const;
    void pack(BincodeBuf& buf) const;
    void unpack(BincodeBuf& buf, ShuckleMessageKind kind);
};

std::ostream& operator<<(std::ostream& out, const ShuckleReqContainer& x);

struct ShuckleRespContainer {
private:
    ShuckleMessageKind _kind = (ShuckleMessageKind)0;
    std::tuple<BlockServicesForShardResp, RegisterBlockServiceResp, ShardsResp, RegisterShardResp, AllBlockServicesResp, RegisterCdcResp, CdcResp> _data;
public:
    ShuckleMessageKind kind() const { return _kind; }
    const BlockServicesForShardResp& getBlockServicesForShard() const;
    BlockServicesForShardResp& setBlockServicesForShard();
    const RegisterBlockServiceResp& getRegisterBlockService() const;
    RegisterBlockServiceResp& setRegisterBlockService();
    const ShardsResp& getShards() const;
    ShardsResp& setShards();
    const RegisterShardResp& getRegisterShard() const;
    RegisterShardResp& setRegisterShard();
    const AllBlockServicesResp& getAllBlockServices() const;
    AllBlockServicesResp& setAllBlockServices();
    const RegisterCdcResp& getRegisterCdc() const;
    RegisterCdcResp& setRegisterCdc();
    const CdcResp& getCdc() const;
    CdcResp& setCdc();

    void clear() { _kind = (ShuckleMessageKind)0; };

    size_t packedSize() const;
    void pack(BincodeBuf& buf) const;
    void unpack(BincodeBuf& buf, ShuckleMessageKind kind);
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
    MAKE_FILE_TRANSIENT = 19,
    REMOVE_SPAN_CERTIFY = 20,
    REMOVE_OWNED_SNAPSHOT_FILE_EDGE = 21,
    SWAP_BLOCKS = 22,
    EXPIRE_TRANSIENT_FILE = 23,
};

std::ostream& operator<<(std::ostream& out, ShardLogEntryKind err);

struct ConstructFileEntry {
    uint8_t type;
    EggsTime deadlineTime;
    BincodeBytes note;

    static constexpr uint16_t STATIC_SIZE = 1 + 8 + BincodeBytes::STATIC_SIZE; // type + deadlineTime + note

    ConstructFileEntry() { clear(); }
    uint16_t packedSize() const {
        uint16_t _size = 0;
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
    uint16_t packedSize() const {
        uint16_t _size = 0;
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
    uint16_t packedSize() const {
        uint16_t _size = 0;
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
    uint16_t packedSize() const {
        uint16_t _size = 0;
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
    SetDirectoryInfo info;

    static constexpr uint16_t STATIC_SIZE = 8 + 8 + SetDirectoryInfo::STATIC_SIZE; // id + ownerId + info

    CreateDirectoryInodeEntry() { clear(); }
    uint16_t packedSize() const {
        uint16_t _size = 0;
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

    static constexpr uint16_t STATIC_SIZE = 8 + BincodeBytes::STATIC_SIZE + 8; // dirId + name + targetId

    CreateLockedCurrentEdgeEntry() { clear(); }
    uint16_t packedSize() const {
        uint16_t _size = 0;
        _size += 8; // dirId
        _size += name.packedSize(); // name
        _size += 8; // targetId
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
    uint16_t packedSize() const {
        uint16_t _size = 0;
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
    uint16_t packedSize() const {
        uint16_t _size = 0;
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
    BincodeBytes info;

    static constexpr uint16_t STATIC_SIZE = 8 + BincodeBytes::STATIC_SIZE; // dirId + info

    RemoveDirectoryOwnerEntry() { clear(); }
    uint16_t packedSize() const {
        uint16_t _size = 0;
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
    uint16_t packedSize() const {
        uint16_t _size = 0;
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
    uint16_t packedSize() const {
        uint16_t _size = 0;
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
    SetDirectoryInfo info;

    static constexpr uint16_t STATIC_SIZE = 8 + SetDirectoryInfo::STATIC_SIZE; // dirId + info

    SetDirectoryInfoEntry() { clear(); }
    uint16_t packedSize() const {
        uint16_t _size = 0;
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
    uint16_t packedSize() const {
        uint16_t _size = 0;
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
    uint16_t packedSize() const {
        uint16_t _size = 0;
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
    uint16_t packedSize() const {
        uint16_t _size = 0;
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
    uint16_t packedSize() const {
        uint16_t _size = 0;
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
    uint8_t storageClass;
    Parity parity;
    BincodeFixedBytes<4> crc32;
    uint32_t size;
    uint32_t blockSize;
    BincodeBytes bodyBytes;
    BincodeList<EntryNewBlockInfo> bodyBlocks;

    static constexpr uint16_t STATIC_SIZE = 8 + 8 + 1 + 1 + BincodeFixedBytes<4>::STATIC_SIZE + 4 + 4 + BincodeBytes::STATIC_SIZE + BincodeList<EntryNewBlockInfo>::STATIC_SIZE; // fileId + byteOffset + storageClass + parity + crc32 + size + blockSize + bodyBytes + bodyBlocks

    AddSpanInitiateEntry() { clear(); }
    uint16_t packedSize() const {
        uint16_t _size = 0;
        _size += 8; // fileId
        _size += 8; // byteOffset
        _size += 1; // storageClass
        _size += 1; // parity
        _size += BincodeFixedBytes<4>::STATIC_SIZE; // crc32
        _size += 4; // size
        _size += 4; // blockSize
        _size += bodyBytes.packedSize(); // bodyBytes
        _size += bodyBlocks.packedSize(); // bodyBlocks
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
    uint16_t packedSize() const {
        uint16_t _size = 0;
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

struct MakeFileTransientEntry {
    InodeId id;
    BincodeBytes note;

    static constexpr uint16_t STATIC_SIZE = 8 + BincodeBytes::STATIC_SIZE; // id + note

    MakeFileTransientEntry() { clear(); }
    uint16_t packedSize() const {
        uint16_t _size = 0;
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
    uint16_t packedSize() const {
        uint16_t _size = 0;
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
    uint16_t packedSize() const {
        uint16_t _size = 0;
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
    uint16_t packedSize() const {
        uint16_t _size = 0;
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
    uint16_t packedSize() const {
        uint16_t _size = 0;
        _size += 8; // id
        return _size;
    }
    void pack(BincodeBuf& buf) const;
    void unpack(BincodeBuf& buf);
    void clear();
    bool operator==(const ExpireTransientFileEntry&rhs) const;
};

std::ostream& operator<<(std::ostream& out, const ExpireTransientFileEntry& x);

struct ShardLogEntryContainer {
private:
    ShardLogEntryKind _kind = (ShardLogEntryKind)0;
    std::tuple<ConstructFileEntry, LinkFileEntry, SameDirectoryRenameEntry, SoftUnlinkFileEntry, CreateDirectoryInodeEntry, CreateLockedCurrentEdgeEntry, UnlockCurrentEdgeEntry, LockCurrentEdgeEntry, RemoveDirectoryOwnerEntry, RemoveInodeEntry, SetDirectoryOwnerEntry, SetDirectoryInfoEntry, RemoveNonOwnedEdgeEntry, SameShardHardFileUnlinkEntry, RemoveSpanInitiateEntry, UpdateBlockServicesEntry, AddSpanInitiateEntry, AddSpanCertifyEntry, MakeFileTransientEntry, RemoveSpanCertifyEntry, RemoveOwnedSnapshotFileEdgeEntry, SwapBlocksEntry, ExpireTransientFileEntry> _data;
public:
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

    void clear() { _kind = (ShardLogEntryKind)0; };

    size_t packedSize() const;
    void pack(BincodeBuf& buf) const;
    void unpack(BincodeBuf& buf, ShardLogEntryKind kind);
};

std::ostream& operator<<(std::ostream& out, const ShardLogEntryContainer& x);

