#pragma once

#include <cstdint>
#include <ostream>
#include <rocksdb/slice.h>
#include <sys/types.h>

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
};
constexpr ShardMetadataKey SHARD_INFO_KEY = ShardMetadataKey::INFO;
constexpr ShardMetadataKey LAST_APPLIED_LOG_ENTRY_KEY = ShardMetadataKey::LAST_APPLIED_LOG_ENTRY;
constexpr ShardMetadataKey NEXT_FILE_ID_KEY = ShardMetadataKey::NEXT_FILE_ID;
constexpr ShardMetadataKey NEXT_SYMLINK_ID_KEY = ShardMetadataKey::NEXT_SYMLINK_ID;
constexpr ShardMetadataKey NEXT_BLOCK_ID_KEY = ShardMetadataKey::NEXT_BLOCK_ID;

inline rocksdb::Slice shardMetadataKey(const ShardMetadataKey* k) {
    return rocksdb::Slice((const char*)k, sizeof(*k));
}

struct ShardInfoBody {
    FIELDS(
        LE, ShardId, shardId, setShardId,
        FBYTES, 16,  secretKey, setSecretKey,
        END_STATIC
    )
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
        LE, EggsTime, atime, setAtime,
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

struct SpanBlocksBodyV0 {
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

    SpanBlocksBodyV0(char* data) : _data(data) {}

    static size_t calcSize(Parity parity, uint8_t stripes) {
        ALWAYS_ASSERT(stripes > 0 && stripes < 16);
        ALWAYS_ASSERT(parity.dataBlocks() > 0);
        return MIN_SIZE + BlockBody::SIZE*parity.blocks() + sizeof(uint32_t)*stripes;
    }

    void afterAlloc(Parity parity, uint8_t stripes) {
        setParity(parity);
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

struct SpanBlocksBody {
    FIELDS(
        LE, uint8_t,  location, setLocation,
        LE, uint8_t,  storageClass, setStorageClass,
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

    static size_t calcSize(uint8_t location, uint8_t storageClass, Parity parity, uint8_t stripes) {
        ALWAYS_ASSERT(stripes > 0 && stripes < 16);
        ALWAYS_ASSERT(parity.dataBlocks() > 0);
        return MIN_SIZE + BlockBody::SIZE*parity.blocks() + sizeof(uint32_t)*stripes;
    }

    void afterAlloc(uint8_t location, uint8_t storageClass, Parity parity, uint8_t stripes) {
        ALWAYS_ASSERT(storageClass != EMPTY_STORAGE && storageClass != INLINE_STORAGE);
        setLocation(location);
        setStorageClass(storageClass);
        setParity(parity);
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

struct SpanBody;

// We support two formats of BlocksBody
// We want to provide same api when reading regardless of format
// We don't want to allow changes to data in old format.
// This class covers all the above
class BlocksBodyReadOnly {
public:
    BlocksBodyReadOnly(char* span, char* spanBlockBodyData) : _spanData(span), _bodyData(spanBlockBodyData) {}

    uint8_t location() const;
    uint8_t storageClass() const;
    const Parity parity() const;
    uint8_t stripes() const;
    uint32_t cellSize() const;

    const BlockBody block(uint64_t ix) const;
    uint32_t stripeCrc(uint64_t ix) const;

    size_t size() const;
private:
    char* _spanData;
    char* _bodyData;
};

struct SpanBody {
    FIELDS(
        LE, uint8_t,  version, _setVersion,
        LE, uint32_t, spanSize, setSpanSize,
        LE, uint32_t, crc, setCrc,
        // in version 0 we store storageClas
        // in version 1 we store locationCount where locationCount 255 repressentis inline storage
        LE, uint8_t,  _storageClassOrLocationCount, _setStorageClassOrLocationCount,
        EMIT_OFFSET, MIN_SIZE,
        END
    )

    static constexpr uint8_t LOCATION_COUNT_INLINE = 255;
    // after this:
    // * Inline body for inline spans (bytes)
    // * Blocks for normal spans BlocksBodyOld (version 0)
    // * Blocks per location for normal spans []BlocksBody (version 1)

    // In new format storage type is stored in BlocksBody
    // This function maintains same external api to determine if span is inline or not.
    bool isInlineStorage() const {
        return version() == 0 ? INLINE_STORAGE == _storageClassOrLocationCount() : _storageClassOrLocationCount() == LOCATION_COUNT_INLINE;
    }

    // inline storage
    BincodeBytesRef inlineBody() const {
        ALWAYS_ASSERT(isInlineStorage());
        return BincodeBytesRef((const char*)(_data+MIN_SIZE+1), (uint8_t)(int)*(_data+MIN_SIZE));
    }

    void _setInlineBody(const BincodeBytesRef& body) {
        ALWAYS_ASSERT(isInlineStorage());
        size_t offset = MIN_SIZE;
        *((uint8_t*)_data+offset) = body.size();
        memcpy(_data+offset+1, body.data(), body.size());
    }

    void checkSize(size_t sz) {
        ALWAYS_ASSERT(sz >= MIN_SIZE);
        if (isInlineStorage()) {
            ALWAYS_ASSERT(sz >= MIN_SIZE+1); // length
        }
        ALWAYS_ASSERT(sz == size());
    }

    // inline
    static size_t calcSize(const BincodeBytesRef& inlineBody) {
        return MIN_SIZE + 1 + inlineBody.size();
    }
    void afterAlloc(const BincodeBytesRef& inlineBody) {
        _setVersion(1);
        _setStorageClassOrLocationCount(LOCATION_COUNT_INLINE);
        _setInlineBody(inlineBody);
    }


    // * Blocks per location for normal spans []BlocksBody (version 1)
    // * we don't have a usecase for creating a span with multiple locations so far so only constructor for one is provided.
    static size_t calcSize(uint8_t location, uint8_t storageClass, Parity parity, uint8_t stripes) {
        return MIN_SIZE + SpanBlocksBody::calcSize(location, storageClass, parity, stripes);
    }

    void afterAlloc(uint8_t location, uint8_t storageClass, Parity parity, uint8_t stripes) {
        _setVersion(1);
        _setStorageClassOrLocationCount(1);
        blocksBody(0).afterAlloc(location, storageClass, parity, stripes);
    }

    uint8_t locationCount() const {
        ALWAYS_ASSERT(!isInlineStorage());
        return version() == 0 ? 1 : _storageClassOrLocationCount();
    }

    const BlocksBodyReadOnly blocksBodyReadOnly(uint8_t idx) const {
        ALWAYS_ASSERT(idx < locationCount());
        return BlocksBodyReadOnly(_data, version() == 0 ? _data + MIN_SIZE : _blocksBodyOffset(idx));
    }

    // we only allow modifications of new format so non const version does not need compatibility wrapper
    SpanBlocksBody blocksBody(uint8_t idx) {
        return SpanBlocksBody(_blocksBodyOffset(idx));
    }

    char* _blocksBodyOffset(uint8_t idx) const {
        ALWAYS_ASSERT(version() == 1);
        ALWAYS_ASSERT(!isInlineStorage());
        ALWAYS_ASSERT(idx < locationCount());
        char* offset = _data + MIN_SIZE;
        for (uint8_t i = 0; i < idx; ++i) {
            size_t blocksBodySize = SpanBlocksBody(offset).size();
            offset += blocksBodySize;
        }
        return offset;
    }

    size_t size() const {
        if (isInlineStorage()) {
            return MIN_SIZE + 1 + *((uint8_t*)_data + MIN_SIZE);
        }
        if (version() == 0) {
            auto blocksBody = blocksBodyReadOnly(0);
            ALWAYS_ASSERT(version() != 0 || blocksBody.storageClass() != EMPTY_STORAGE);
            return MIN_SIZE + blocksBodyReadOnly(0).size();
        }
        ALWAYS_ASSERT(version() == 1);
        ALWAYS_ASSERT(locationCount() != 0);
        char* lastOffset = _blocksBodyOffset(locationCount() - 1);
        return lastOffset - _data + SpanBlocksBody(lastOffset).size();
    }
    SpanBody() : _data(nullptr) {}
    SpanBody(char* data) : _data(data) {}
};

inline uint8_t BlocksBodyReadOnly::location() const {
    ALWAYS_ASSERT(!SpanBody(_spanData).isInlineStorage());
    return SpanBody(_spanData).version() == 0 ? 0 : SpanBlocksBody(_bodyData).location();
}

inline uint8_t BlocksBodyReadOnly::storageClass() const {
    ALWAYS_ASSERT(!SpanBody(_spanData).isInlineStorage());
    return SpanBody(_spanData).version() == 0 ? SpanBody(_spanData)._storageClassOrLocationCount() : SpanBlocksBody(_bodyData).storageClass();
}

inline const Parity BlocksBodyReadOnly::parity() const {
    ALWAYS_ASSERT(!SpanBody(_spanData).isInlineStorage());
    return SpanBody(_spanData).version() == 0 ? SpanBlocksBodyV0(_bodyData).parity() : SpanBlocksBody(_bodyData).parity();
}

inline uint8_t BlocksBodyReadOnly::stripes() const {
    ALWAYS_ASSERT(!SpanBody(_spanData).isInlineStorage());
    return SpanBody(_spanData).version() == 0 ? SpanBlocksBodyV0(_bodyData).stripes() : SpanBlocksBody(_bodyData).stripes();
}

inline uint32_t BlocksBodyReadOnly::cellSize() const {
    ALWAYS_ASSERT(!SpanBody(_spanData).isInlineStorage());
    return SpanBody(_spanData).version() == 0 ? SpanBlocksBodyV0(_bodyData).cellSize() : SpanBlocksBody(_bodyData).cellSize();
}
inline const BlockBody BlocksBodyReadOnly::block(uint64_t ix) const {
    return SpanBody(_spanData).version() == 0 ? SpanBlocksBodyV0(_bodyData).block(ix) : SpanBlocksBody(_bodyData).block(ix);
}

inline uint32_t BlocksBodyReadOnly::stripeCrc(uint64_t ix) const {
    return SpanBody(_spanData).version() == 0 ? SpanBlocksBodyV0(_bodyData).stripeCrc(ix) : SpanBlocksBody(_bodyData).stripeCrc(ix);
}

inline size_t BlocksBodyReadOnly::size() const {
    ALWAYS_ASSERT(!SpanBody(_spanData).isInlineStorage());
    return SpanBody(_spanData).version() == 0 ? SpanBlocksBodyV0(_bodyData).size() : SpanBlocksBody(_bodyData).size();
}

enum class HashMode : uint8_t {
    // TODO add docs regarding why we like 63 bit hashes, specifically the fact
    // that they're never negative when loff_t. DO NOT add a 64 bit hash to this
    // enum!
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
        // only present for snapshot edges -- current edges have
        // the creation time in the body.
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

    static uint64_t computeNameHash(HashMode mode, const BincodeBytesRef& bytes);
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
