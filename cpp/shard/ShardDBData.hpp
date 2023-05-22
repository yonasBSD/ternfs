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
    FIELDS(
        LE, ShardId, shardId, setShardId,
        FBYTES, 16,  secretKey, setSecretKey,
        END_STATIC
    )
};

struct BlockServiceKey {
    FIELDS(
        BE, ShardMetadataKey, key, setKey, // always BLOCK_SERVICE_KEY
        BE, uint64_t,         blockServiceId, setBlockServiceId,
        END_STATIC
    )
};

struct BlockServiceBody {
    FIELDS(
        LE, uint64_t, id,               setId,
        FBYTES, 4,    ip1,              setIp1,
        LE, uint16_t, port1,            setPort1,
        FBYTES, 4,    ip2,              setIp2,
        LE, uint16_t, port2,            setPort2,
        LE, uint8_t,  storageClass,     setStorageClass,
        FBYTES, 16,   failureDomain,    setFailureDomain,
        FBYTES, 16,   secretKey,        setSecretKey,
        END_STATIC
    )
};

struct CurrentBlockServicesBody {
    FIELDS(
        LE, uint8_t, length, setLength,
        EMIT_OFFSET, MIN_SIZE,
        END
    )

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
    FIELDS(
        LE, uint8_t,   version, setVersion,
        LE, uint64_t,  fileSize, setFileSize,
        LE, EggsTime,  mtime, setMtime,
        LE, EggsTime,  deadline, setDeadline,
        LE, SpanState, lastSpanState, setLastSpanState,
        EMIT_OFFSET, STATIC_SIZE,
        BYTES, note, setNoteDangerous, // dangerous because we might not have enough space
        EMIT_SIZE, size,
        END
    )

    static constexpr size_t MIN_SIZE =
        STATIC_SIZE +
        sizeof(uint8_t); // noteLength
    static constexpr size_t MAX_SIZE = MIN_SIZE + 255; // max note size

    void checkSize(size_t sz) {
        ALWAYS_ASSERT(sz >= MIN_SIZE);
        ALWAYS_ASSERT(sz == size());
    }
};

struct FileBody {
    FIELDS(
        LE, uint8_t,  version, setVersion,
        LE, uint64_t, fileSize, setFileSize,
        LE, EggsTime, mtime, setMtime,
        END_STATIC
    )
};

struct SpanKey {
    FIELDS(
        BE, InodeId,  fileId, setFileId,
        BE, uint64_t, offset, setOffset,
        END_STATIC
    )
};

struct BlockBody {
    FIELDS(
        LE, BlockServiceId, blockService, setBlockService,
        LE, uint64_t,       blockId, setBlockId,
        LE, uint32_t,       crc, setCrc,
        END_STATIC
    )
    constexpr static size_t SIZE = MAX_SIZE;
};

struct SpanBlocksBody {
    FIELDS(
        LE, Parity,   parity, setParity,
        LE, uint8_t,  stripes, setStripes,
        LE, uint32_t, cellSize, setCellSize,
        EMIT_OFFSET, MIN_SIZE,
        END
    )
    // after this:
    // * []BlockBody blocks
    // * []u32 stripesCrc

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
    FIELDS(
        LE, uint8_t,  version, setVersion,
        LE, uint32_t, spanSize, setSpanSize,
        LE, uint32_t, crc, setCrc,
        LE, uint8_t,  storageClass, setStorageClass,
        EMIT_OFFSET, MIN_SIZE,
        END
    )
    // after this:
    // * Inline body for inline spans (bytes)
    // * Blocks for normal spans

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
    FIELDS(
        LE, uint8_t,  version, setVersion,
        LE, InodeId,  ownerId, setOwnerId,
        LE, EggsTime, mtime, setMtime,
        LE, HashMode, hashMode, setHashMode,
        LE, uint16_t, infoLength, setInfoLength,
        EMIT_OFFSET, MIN_SIZE,
        END
    )
    // After the static data we have a u16 length of the directory info, and then
    // the directory info in bincode form, that is to say [tag: u8; len: u8; bytes; ...].

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
    FIELDS(
        // first 63 bits: dir id, then whether the edge is current
        BE, uint64_t,  dirIdWithCurrentU64, setDirIdWithCurrentU64,
        BE, uint64_t,  nameHash, setNameHash,
        EMIT_OFFSET, STATIC_SIZE,
        BYTES,        name, setName,
        BE, EggsTime, creationTimeUnchecked, setCreationTimeUnchecked,
        END
    )

    static constexpr size_t MIN_SIZE =
        STATIC_SIZE +
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
    FIELDS(
        LE, uint8_t,      version, setVersion,
        LE, InodeIdExtra, targetIdWithOwned, setTargetIdWithOwned,
        END_STATIC
    )
};

struct CurrentEdgeBody {
    FIELDS(
        LE, uint8_t,      version, setVersion,
        LE, InodeIdExtra, targetIdWithLocked, setTargetIdWithLocked,
        LE, EggsTime,     creationTime, setCreationTime,
        END_STATIC
    )

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

    BE_VAL(BlockServiceId, blockServiceId, setBlockServiceId, 0)
    BE_VAL(InodeId,        fileId,         setFileId,         8)
};
