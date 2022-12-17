#pragma once

#include <cstdint>
#include <ostream>
#include <rocksdb/slice.h>

#include "Assert.hpp"
#include "Common.hpp"
#include "Bincode.hpp"
#include "Msgs.hpp"
#include "MsgsGen.hpp"
#include "Time.hpp"
#include "RocksDBUtils.hpp"

enum class ShardMetadataKey : uint8_t {
    INFO = 0,
    LAST_APPLIED_LOG_ENTRY = 1,
    NEXT_FILE_ID = 2,
    NEXT_SYMLINK_ID = 3,
    NEXT_BLOCK_ID = 4,
    CURRENT_BLOCK_SERVICES = 5,
    BLOCK_SERVICE = 6, // postfixed with the block service id
};
constexpr ShardMetadataKey SHARD_INFO_KEY = ShardMetadataKey::INFO;
constexpr ShardMetadataKey LAST_APPLIED_LOG_ENTRY_KEY = ShardMetadataKey::LAST_APPLIED_LOG_ENTRY;
constexpr ShardMetadataKey NEXT_FILE_ID_KEY = ShardMetadataKey::NEXT_FILE_ID;
constexpr ShardMetadataKey NEXT_SYMLINK_ID_KEY = ShardMetadataKey::NEXT_SYMLINK_ID;
constexpr ShardMetadataKey NEXT_BLOCK_ID_KEY = ShardMetadataKey::NEXT_BLOCK_ID;
constexpr ShardMetadataKey CURRENT_BLOCK_SERVICES_KEY = ShardMetadataKey::CURRENT_BLOCK_SERVICES;
constexpr ShardMetadataKey BLOCK_SERVICE_KEY = ShardMetadataKey::BLOCK_SERVICE;

inline rocksdb::Slice shardMetadataKey(const ShardMetadataKey* k) {
    ALWAYS_ASSERT(*k != BLOCK_SERVICE_KEY);
    return rocksdb::Slice((const char*)k, sizeof(*k));
}

struct ShardInfoBody {
    char* _data;

    static constexpr size_t MAX_SIZE =
        sizeof(ShardId) + // shardId
        sizeof(std::array<uint8_t, 16>); // secretKey

    size_t size() const { return MAX_SIZE; }
    void checkSize(size_t size) { ALWAYS_ASSERT(size == MAX_SIZE); }

    U8_VAL    (ShardId, shardId,      setShardId,   0)
    FBYTES_VAL(16,      secretKey,    setSecretKey, 1)
};

struct BlockServiceKey {
    char* _data;

    static constexpr size_t MAX_SIZE = sizeof(ShardMetadataKey) + sizeof(uint64_t);
    size_t size() const { return MAX_SIZE; }
    void checkSize(size_t size) { ALWAYS_ASSERT(size == MAX_SIZE); }

    U8_VAL(ShardMetadataKey, key,            setKey,            0) // always BLOCK_SERVICE_KEY
    BE64_VAL(uint64_t,       blockServiceId, setBlockServiceId, 1)
};

struct BlockServiceBody {
    char* _data;

    static constexpr size_t MAX_SIZE =
        sizeof(uint64_t) + // id
        sizeof(char[4]) +  // ip
        sizeof(uint16_t) + // port
        sizeof(uint8_t) +  // storage class
        sizeof(char[16]) + // failure domain
        sizeof(char[16]);  // secret key

    size_t size() const { return MAX_SIZE; }
    void checkSize(size_t size) { ALWAYS_ASSERT(size == MAX_SIZE); }

    LE_VAL(uint64_t, id,               setId,             0)
    FBYTES_VAL(4,    ip,               setIp,             8)
    LE_VAL(uint16_t, port,             setPort,          12)
    U8_VAL(uint8_t,  storageClass,     setStorageClass,  14)
    FBYTES_VAL(16,   failureDomain,    setFailureDomain, 15)
    FBYTES_VAL(16,   secretKey,        setSecretKey,     31)
};

struct CurrentBlockServicesBody {
    char* _data;

    static constexpr size_t MIN_SIZE =
        sizeof(uint8_t);               // number of current block services 

    void checkSize(size_t sz) {
        ALWAYS_ASSERT(sz >= MIN_SIZE, "sz < MIN_SIZE (%s < %s)", sz, MIN_SIZE);
        ALWAYS_ASSERT(sz == size(), "sz != size() (%s, %s)", sz, size());
    }

    static size_t calcSize(uint64_t numBlockServices) {
        ALWAYS_ASSERT(numBlockServices < 256);
        return MIN_SIZE + numBlockServices*sizeof(uint64_t);
    }

    void afterAlloc(uint64_t numBlockServices) {
        setLength(numBlockServices);
    }

    size_t size() const {
        return MIN_SIZE + length()*sizeof(uint64_t);
    }

    U8_VAL(uint8_t, length, setLength, 0)

    uint64_t at(uint64_t ix) const {
        ALWAYS_ASSERT(ix < length());
        uint64_t v;
        memcpy(&v, _data + MIN_SIZE + (ix*sizeof(uint64_t)), sizeof(uint64_t));
        return v;
    }

    void set(uint64_t ix, uint64_t v) {
        ALWAYS_ASSERT(ix < length());
        memcpy(_data + MIN_SIZE + (ix*sizeof(uint64_t)), &v, sizeof(uint64_t));
    }
};

enum class SpanState : uint8_t {
    CLEAN = 0,
    DIRTY = 1,
    CONDEMNED = 2,
};

std::ostream& operator<<(std::ostream& out, SpanState state);

struct TransientFileBody {
    char* _data;

    static constexpr size_t MIN_SIZE =
        sizeof(uint64_t) + // size
        sizeof(EggsTime) + // mtime
        sizeof(EggsTime) + // deadline
        sizeof(SpanState) + // lastSpanState
        sizeof(uint8_t); // noteLength
    static constexpr size_t MAX_SIZE = MIN_SIZE + 255; // max note size

    size_t size() const {
        return MIN_SIZE + note().size();
    }

    void checkSize(size_t sz) {
        ALWAYS_ASSERT(sz >= MIN_SIZE);
        ALWAYS_ASSERT(sz == size());
    }

    LE_VAL(uint64_t,  fileSize,      setFileSize,       0)
    LE_VAL(EggsTime,  mtime,         setMtime,          8)
    LE_VAL(EggsTime,  deadline,      setDeadline,      16)
    U8_VAL(SpanState, lastSpanState, setLastSpanState, 24)
    BYTES_VAL(note, setNote,                           25)
};

struct FileBody {
    char* _data;

    static constexpr size_t MAX_SIZE =
        sizeof(uint64_t) + // size
        sizeof(EggsTime);  // mtime
    size_t size() const { return MAX_SIZE; }
    void checkSize(size_t sz) { ALWAYS_ASSERT(sz == MAX_SIZE); }

    LE_VAL(uint64_t, fileSize, setFileSize, 0)
    LE_VAL(EggsTime, mtime,    setMtime,    8)
};

struct SpanKey {
    char* _data;

    static constexpr size_t MAX_SIZE =
        sizeof(InodeId) + // id
        sizeof(uint64_t); // offset
    size_t size() const { return MAX_SIZE; }
    void checkSize(size_t sz) { ALWAYS_ASSERT(sz == MAX_SIZE); }

    BE64_VAL(InodeId,  fileId, setFileId, 0)
    BE64_VAL(uint64_t, offset, setOffset, 8)
};

struct PACKED BlockBody {
    uint64_t blockId;
    uint64_t blockServiceId;
    std::array<uint8_t, 4> crc32;
};

struct SpanBody {
    char* _data;

    static constexpr size_t MIN_SIZE =
        sizeof(uint32_t) +               // size
        sizeof(uint32_t) +               // blockSize
        sizeof(std::array<uint8_t, 4>) + // crc32
        sizeof(uint8_t) +                // storageClass
        sizeof(Parity);                  // parity
        // after this:
        // * Nothing for zero spans
        // * inline body for inline spans
        // * the blocks otherwise
    
    static constexpr size_t BLOCK_BODY_SIZE = sizeof(BlockBody);

    void checkSize(size_t sz) {
        ALWAYS_ASSERT(sz >= MIN_SIZE);
        if (storageClass() == INLINE_STORAGE) {
            ALWAYS_ASSERT(sz >= MIN_SIZE+1); // length
        }
        ALWAYS_ASSERT(sz == size());
    }

    static size_t calcSize(uint8_t storageClass, Parity parity, const BincodeBytes& inlineBody) {
        ALWAYS_ASSERT(storageClass != EMPTY_STORAGE);
        if (storageClass == INLINE_STORAGE) {
            ALWAYS_ASSERT(parity == Parity());
            ALWAYS_ASSERT(inlineBody.size() > 0);
            return MIN_SIZE + 1 + inlineBody.size();
        } else {
            ALWAYS_ASSERT(inlineBody.size() == 0);
            ALWAYS_ASSERT(parity.blocks() > 0);
            return MIN_SIZE + BLOCK_BODY_SIZE*parity.blocks();
        }
    }

    void afterAlloc(uint8_t storageClass, Parity parity, const BincodeBytes& inlineBody) {
        setStorageClass(storageClass);
        setParity(parity);
        if (storageClass == INLINE_STORAGE) {
            setInlineBody(inlineBody);
        }
    }

    size_t size() const {
        ALWAYS_ASSERT(storageClass() != EMPTY_STORAGE);
        size_t sz = MIN_SIZE;
        if (storageClass() == INLINE_STORAGE) {
            sz += 1 + (uint8_t)(int)*(_data + MIN_SIZE);
        } else {
            sz += BLOCK_BODY_SIZE * parity().blocks();
        }
        return sz;
    }

    LE_VAL(uint32_t, spanSize,     setSpanSize,      0)
    LE_VAL(uint32_t, blockSize,    setBlockSize,     4)
    FBYTES_VAL(4,    crc32,        setCrc32,         8)
    U8_VAL(uint8_t,  storageClass, setStorageClass, 12)
    U8_VAL(Parity,   parity,       setParity,       13)

    BincodeBytesRef inlineBody() const {
        ALWAYS_ASSERT(storageClass() == INLINE_STORAGE);
        return BincodeBytes((const char*)(_data+MIN_SIZE+1), (uint8_t)(int)*(_data+MIN_SIZE));
    }

    void setInlineBody(const BincodeBytesRef& body) {
        ALWAYS_ASSERT(storageClass() == INLINE_STORAGE);
        size_t offset = MIN_SIZE;
        *(_data+offset) = (char)(int)body.size();
        memcpy(_data+offset+1, body.data(), body.size());
    }

    BlockBody block(uint64_t ix) const {
        ALWAYS_ASSERT(storageClass() != INLINE_STORAGE);
        ALWAYS_ASSERT(ix < parity().blocks());
        BlockBody b;
        memcpy(&b, _data + MIN_SIZE + ix * sizeof(BlockBody), sizeof(BlockBody));
        return b;
    }
    void setBlock(uint64_t ix, const BlockBody& b) {
        ALWAYS_ASSERT(storageClass() != INLINE_STORAGE);
        ALWAYS_ASSERT(ix < parity().blocks());
        memcpy(_data + MIN_SIZE + ix * sizeof(BlockBody), &b, sizeof(BlockBody));
    }
};

enum class HashMode : uint8_t {
    // temporary 63-bit hash since pyfuse does not seem to like 64 bits dirents.
    XXH3_63 = 1,
};

struct DirectoryBody {
    char* _data;

    // `infoInherited` tells us whether we should inherit the info from the
    // parent directory. Moreover, we might have a directory with no owner
    // (a snapshot directory). So, there are two cases where we have directory
    // info:
    //
    // * `inherited == false`;
    // * `ownerId == NULL_INODE_ID`.
    //
    // In any case, after the fields below we have a BincodeBytes, which must be
    // empty if neither of the two conditions above are met.

    static constexpr size_t MIN_SIZE =
        sizeof(InodeId) +  // ownerId
        sizeof(EggsTime) + // mtime
        sizeof(HashMode) + // hashMode
        sizeof(bool)+      // infoInherited
        sizeof(uint8_t);   // infoLength
    static constexpr size_t MAX_SIZE = MIN_SIZE + 255;

    LE_VAL(InodeId,  ownerId,          setOwnerId,           0)
    LE_VAL(EggsTime, mtime,            setMtime,             8)
    U8_VAL(HashMode, hashMode,         setHashMode,         16)
    U8_VAL(bool,     infoInherited,    setInfoInherited,    17)
    BYTES_VAL(infoUnchecked, setInfoUnchecked,              18)

    size_t size() const {
        return MIN_SIZE + infoUnchecked().size();
    }
    void checkSize(size_t sz) {
        ALWAYS_ASSERT(sz >= MIN_SIZE);
        ALWAYS_ASSERT(sz == size());
    }
    
    bool hasInfo() const {
        return ownerId() == NULL_INODE_ID || !infoInherited();
    }

    BincodeBytesRef info() const {
        ALWAYS_ASSERT(hasInfo() == (infoUnchecked().size() > 0));
        return infoUnchecked();
    }

    void setInfo(const BincodeBytesRef& bytes) {
        ALWAYS_ASSERT(hasInfo() == (bytes.size() > 0));
        setInfoUnchecked(bytes);
    }
};

struct EdgeKey {
    char* _data;

    static constexpr size_t MIN_SIZE =
        sizeof(uint64_t) +  // first 63 bits: dirId, then whether the edge is current
        sizeof(uint64_t) +  // nameHash
        sizeof(uint8_t);    // nameLength
    // max name size, and an optional creation time if current=false
    static constexpr size_t MAX_SIZE = MIN_SIZE + 255 + sizeof(EggsTime);

    size_t size() const {
        size_t sz = MIN_SIZE + name().size();
        if (snapshot()) {
            sz += sizeof(EggsTime);
        }
        return sz;
    }

    void checkSize(size_t sz) {
        ALWAYS_ASSERT(sz >= MIN_SIZE, "expected %s >= %s", sz, MIN_SIZE);
        ALWAYS_ASSERT(sz == size());
    }

    BE64_VAL(uint64_t,  dirIdWithCurrentU64,   setDirIdWithCurrentU64,    0)
    BE64_VAL(uint64_t,  nameHash,              setNameHash,               8)
    BYTES_VAL(name, setName,                                             16)
    BE64_VAL(EggsTime,  creationTimeUnchecked, setCreationTimeUnchecked, (16+1+name().size()))

    void setDirIdWithCurrent(InodeId id, bool current) {
        setDirIdWithCurrentU64((id.u64 << 1) | current);
    }

    bool current() const {
        return dirIdWithCurrentU64() & 1ull;
    }

    bool snapshot() const {
        return !current();
    }

    InodeId dirId() const {
        return InodeId::FromU64(dirIdWithCurrentU64() >> 1);
    }

    EggsTime creationTime() const {
        ALWAYS_ASSERT(snapshot());
        return creationTimeUnchecked();
    }

    void setCreationTime(EggsTime creationTime) {
        ALWAYS_ASSERT(snapshot());
        setCreationTimeUnchecked(creationTime);
    }
};

std::ostream& operator<<(std::ostream& out, const EdgeKey& edgeKey);

struct SnapshotEdgeBody {
    char* _data;

    static constexpr size_t MAX_SIZE =
        sizeof(InodeIdExtra); // target, and if owned
    size_t size() const { return MAX_SIZE; }
    void checkSize(size_t sz) { ALWAYS_ASSERT(sz == MAX_SIZE); }

    LE_VAL(InodeIdExtra, targetIdWithOwned, setTargetIdWithOwned, 0)
};

struct CurrentEdgeBody {
    char* _data;

    static constexpr size_t MAX_SIZE =
        sizeof(InodeIdExtra) + // target, and if locked
        sizeof(EggsTime);      // creationTime
    size_t size() const { return MAX_SIZE; }
    void checkSize(size_t sz) { ALWAYS_ASSERT(sz == MAX_SIZE); }

    LE_VAL(InodeIdExtra, targetIdWithLocked, setTargetIdWithLocked, 0)
    LE_VAL(EggsTime,     creationTime,       setCreationTime,       8)

    bool locked() const {
        return targetIdWithLocked().extra();
    }

    InodeId targetId() const {
        return targetIdWithLocked().id();
    }
};

struct BlockServiceToFileKey {
    char* _data;

    static constexpr size_t MAX_SIZE =
        sizeof(uint64_t) + // block service id
        sizeof(InodeId);   // file id
    size_t size() const { return MAX_SIZE; }
    void checkSize(size_t sz) { ALWAYS_ASSERT(sz == MAX_SIZE); }

    BE64_VAL(uint64_t, blockServiceId, setBlockServiceId, 0)
    BE64_VAL(InodeId,  fileId,         setFileId,         8)
};
