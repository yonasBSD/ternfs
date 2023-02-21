#pragma once

#include <cstdint>
#include <ostream>
#include <rocksdb/slice.h>

#include "Assert.hpp"
#include "Common.hpp"
#include "Bincode.hpp"
#include "Msgs.hpp"
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
    FBYTES_VAL(16,    secretKey,   setSecretKey, 1)
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
        sizeof(char[4]) +  // ip1
        sizeof(uint16_t) + // port1
        sizeof(char[4]) +  // ip2
        sizeof(uint16_t) + // port2
        sizeof(uint8_t) +  // storage class
        sizeof(char[16]) + // failure domain
        sizeof(char[16]);  // secret key

    size_t size() const { return MAX_SIZE; }
    void checkSize(size_t size) { ALWAYS_ASSERT(size == MAX_SIZE); }

    LE_VAL(uint64_t, id,               setId,             0)
    FBYTES_VAL(4,    ip1,              setIp1,            8)
    LE_VAL(uint16_t, port1,            setPort1,         12)
    FBYTES_VAL(4,    ip2,              setIp2,           14)
    LE_VAL(uint16_t, port2,            setPort2,         18)
    U8_VAL(uint8_t,  storageClass,     setStorageClass,  20)
    FBYTES_VAL(16,   failureDomain,    setFailureDomain, 21)
    FBYTES_VAL(16,   secretKey,        setSecretKey,     37)
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

struct BlockBody {
    char* _data;

    static constexpr size_t SIZE =
        sizeof(uint64_t) +
        sizeof(uint64_t) +
        sizeof(uint32_t);

    LE_VAL(uint64_t, blockService, setBlockService, 0);
    LE_VAL(uint64_t, blockId,      setBlockId,      8);
    LE_VAL(uint32_t, crc,          setCrc,         16);
};

struct SpanBlocksBody {
    char* _data;

    static constexpr size_t MIN_SIZE =
        sizeof(uint8_t) +               // parity
        sizeof(uint8_t) +               // stripes
        sizeof(uint32_t);               // cellSize
        // after this:
        // * []BlockBody blocks
        // * []u32 stripesCrc

    U8_VAL(Parity,   parity,    setParity,      0)
    U8_VAL(uint8_t,  stripes,   setStripes,     1)
    LE_VAL(uint32_t, cellSize,  setCellSize,    2)

    SpanBlocksBody(char* data) : _data(data) {}

    static size_t calcSize(Parity parity, uint8_t stripes) {
        ALWAYS_ASSERT(stripes > 0 && stripes < 16);
        ALWAYS_ASSERT(parity.dataBlocks() > 0);
        return MIN_SIZE + BlockBody::SIZE*parity.blocks() + sizeof(uint32_t)*stripes;
    }

    void afterAlloc(Parity parity, uint8_t stripes) {
        setParity(parity.u8);
        setStripes(stripes);
    }

    size_t size() const {
        return MIN_SIZE + BlockBody::SIZE*parity().blocks() + sizeof(uint32_t)*stripes();
    }

    const BlockBody block(uint64_t ix) const {
        ALWAYS_ASSERT(ix < parity().blocks());
        BlockBody b;
        b._data = _data + MIN_SIZE + ix * BlockBody::SIZE;
        return b;
    }
    BlockBody block(uint64_t ix) {
        ALWAYS_ASSERT(ix < parity().blocks());
        BlockBody b;
        b._data = _data + MIN_SIZE + ix * BlockBody::SIZE;
        return b;
    }

    uint32_t stripeCrc(uint64_t ix) const {
        static_assert(std::endian::native == std::endian::little);
        ALWAYS_ASSERT(ix < stripes());
        uint32_t crc;
        memcpy(&crc, _data + MIN_SIZE + parity().blocks()*BlockBody::SIZE + ix*sizeof(uint32_t), sizeof(uint32_t));
        return crc;
    }

    void setStripeCrc(uint64_t ix, uint32_t crc) {
        static_assert(std::endian::native == std::endian::little);
        ALWAYS_ASSERT(ix < stripes());
        memcpy(_data + MIN_SIZE + parity().blocks()*BlockBody::SIZE + ix*sizeof(uint32_t), &crc, sizeof(uint32_t));
    }
};

struct SpanBody {
    char* _data;

    static constexpr size_t MIN_SIZE =
        sizeof(uint32_t) +               // size
        sizeof(uint32_t) +               // crc
        sizeof(uint8_t);                 // storageClass
        // after this:
        // * Inline body for inline spans (bytes)
        // * Blocks for normal spans

    LE_VAL(uint32_t, spanSize,     setSpanSize,      0)
    LE_VAL(uint32_t, crc,          setCrc,           4)
    U8_VAL(uint8_t,  storageClass, setStorageClass,  8)

    BincodeBytesRef inlineBody() const {
        ALWAYS_ASSERT(storageClass() == INLINE_STORAGE);
        return BincodeBytesRef((const char*)(_data+MIN_SIZE+1), (uint8_t)(int)*(_data+MIN_SIZE));
    }

    void setInlineBody(const BincodeBytesRef& body) {
        ALWAYS_ASSERT(storageClass() == INLINE_STORAGE);
        size_t offset = MIN_SIZE;
        *(_data+offset) = (char)(int)body.size();
        memcpy(_data+offset+1, body.data(), body.size());
    }

    void checkSize(size_t sz) {
        ALWAYS_ASSERT(sz >= MIN_SIZE);
        if (storageClass() == INLINE_STORAGE) {
            ALWAYS_ASSERT(sz >= MIN_SIZE+1); // length
        }
        ALWAYS_ASSERT(sz == size());
    }

    // inline
    static size_t calcSize(const BincodeBytesRef& inlineBody) {
        return MIN_SIZE + 1 + inlineBody.size();
    }
    void afterAlloc(const BincodeBytesRef& inlineBody) {
        setStorageClass(INLINE_STORAGE);
        setInlineBody(inlineBody);
    }

    // blocks
    static size_t calcSize(uint8_t storageClass, Parity parity, uint8_t stripes) {
        ALWAYS_ASSERT(storageClass != EMPTY_STORAGE && storageClass != INLINE_STORAGE);
        return MIN_SIZE + SpanBlocksBody::calcSize(parity, stripes);
    }
    void afterAlloc(uint8_t storageClass, Parity parity, uint8_t stripes) {
        setStorageClass(storageClass);
        blocksBody().afterAlloc(parity, stripes);
    }
    SpanBlocksBody blocksBody() {
        ALWAYS_ASSERT(storageClass() != INLINE_STORAGE);
        return SpanBlocksBody(_data + MIN_SIZE);
    }
    const SpanBlocksBody blocksBody() const {
        ALWAYS_ASSERT(storageClass() != INLINE_STORAGE);
        return SpanBlocksBody(_data + MIN_SIZE);
    }

    size_t size() const {
        ALWAYS_ASSERT(storageClass() != EMPTY_STORAGE);
        size_t sz = MIN_SIZE;
        if (storageClass() == INLINE_STORAGE) {
            sz += 1 + (uint8_t)(int)*(_data + MIN_SIZE);
        } else {
            sz += blocksBody().size();
        }
        return sz;
    }
};

enum class HashMode : uint8_t {
    // temporary 63-bit hash since pyfuse does not seem to like 64 bits dirents.
    XXH3_63 = 1,
};

struct DirectoryBody {
    char* _data;

    // After the static data we have a u16 length of the directory info, and then
    // the directory info in bincode form, that is to say [tag: u8; len: u8; bytes; ...].

    static constexpr size_t MIN_SIZE =
        sizeof(InodeId) +  // ownerId
        sizeof(EggsTime) + // mtime
        sizeof(HashMode) + // hashMode
        sizeof(uint16_t);  // infoLength

    LE_VAL(InodeId,  ownerId,          setOwnerId,           0)
    LE_VAL(EggsTime, mtime,            setMtime,             8)
    U8_VAL(HashMode, hashMode,         setHashMode,         16)
    LE_VAL(uint16_t, infoLength,       setInfoLength,       17)

    size_t size() const {
        return MIN_SIZE + infoLength();
    }
    void checkSize(size_t sz) {
        ALWAYS_ASSERT(sz >= MIN_SIZE);
        ALWAYS_ASSERT(sz == size());
    }

    static size_t calcSize(const DirectoryInfo& dirInfo) {
        return MIN_SIZE + dirInfo.packedSize();
    }

    void afterAlloc(const DirectoryInfo& dirInfo) {
        setInfoLength(dirInfo.packedSize());
        BincodeBuf bbuf(_data+MIN_SIZE, infoLength());
        dirInfo.pack(bbuf);
    }

    void info(DirectoryInfo& i) const {
        BincodeBuf bbuf(_data+MIN_SIZE, infoLength());
        i.unpack(bbuf);
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
