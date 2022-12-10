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

// We use byteswap to go from LE to BE.
static_assert(std::endian::native == std::endian::little);
inline uint64_t byteswapU64(uint64_t x) {
    return __builtin_bswap64(x);
}

template<typename T>
struct StaticValue {
private:
    std::array<char, T::MAX_SIZE> _data;
    T _val;
public:
    StaticValue() {
        _val.data = &_data[0];
    }

    rocksdb::Slice toSlice() const {
        return rocksdb::Slice(&_data[0], _val.size());
    }

    T* operator->() {
        return &_val;
    }
};

template<typename T>
struct ExternalValue {
private:
    T _val;
public:
    ExternalValue() {
        _val.data = nullptr;
    }
    ExternalValue(char* data, size_t size) {
        _val.data = data;
        _val.checkSize(size);
    }
    ExternalValue(std::string& s): ExternalValue(s.data(), s.size()) {}

    static const ExternalValue<T> FromSlice(const rocksdb::Slice& slice) {
        return ExternalValue((char*)slice.data(), slice.size());
    }

    T* operator->() {
        return &_val;
    }

    rocksdb::Slice toSlice() {
        return rocksdb::Slice(_val.data, _val.size());
    }
};

template<typename T>
struct OwnedValue {
    T _val;
public:
    OwnedValue() = delete;

    template<typename ...Args>
    OwnedValue(Args&&... args) {
        size_t sz = T::calcSize(std::forward<Args>(args)...);
        _val.data = (char*)malloc(sz);
        ALWAYS_ASSERT(_val.data);
    }

    ~OwnedValue() {
        free(_val.data);
    }

    rocksdb::Slice toSlice() const {
        return rocksdb::Slice(_val.data, _val.size());
    }

    T* operator->() {
        return &_val;
    }
};

#define LE_VAL(type, name, setName, offset) \
    static_assert(sizeof(type) > 1); \
    type name() const { \
        type x; \
        memcpy(&x, data+offset, sizeof(x)); \
        return x; \
    } \
    void setName(type x) { \
        memcpy(data+offset, &x, sizeof(x)); \
    }

#define U8_VAL(type, name, setName, offset) \
    static_assert(sizeof(type) == sizeof(uint8_t)); \
    type name() const { \
        type x; \
        memcpy(&x, data+offset, sizeof(x)); \
        return x; \
    } \
    void setName(type x) { \
        memcpy(data+offset, &x, sizeof(x)); \
    }

#define BYTES_VAL(name, setName, offset) \
    const BincodeBytes name() const { \
        BincodeBytes bs; \
        bs.length = (uint8_t)(int)*(data+offset); \
        bs.data = (const uint8_t*)(data+offset+1); \
        return bs; \
    } \
    void setName(const BincodeBytes& bs) { \
        *(data+offset) = (char)(int)bs.length; \
        memcpy(data+offset+1, bs.data, bs.length); \
    }

#define FBYTES_VAL(sz, getName, setName, offset) \
    void getName(std::array<uint8_t, sz>& bs) const { \
        memcpy(&bs[0], data+offset, sz); \
    } \
    void setName(const std::array<uint8_t, sz>& bs) { \
        memcpy(data+offset, &bs[0], sz); \
    }

#define BE64_VAL(type, name, setName, offset) \
    static_assert(sizeof(type) == sizeof(uint64_t)); \
    type name() const { \
        uint64_t x; \
        memcpy(&x, data+offset, sizeof(x)); \
        x = byteswapU64(x); /* BE -> LE */ \
        type v; \
        memcpy(&v, &x, sizeof(uint64_t)); \
        return v; \
    } \
    void setName(type v) { \
        uint64_t x; \
        memcpy(&x, &v, sizeof(uint64_t)); \
        x = byteswapU64(x); /* LE -> BE */ \
        memcpy(data+offset, &x, sizeof(x)); \
    }

struct InodeIdValue {
    char* data;

    static constexpr size_t MAX_SIZE = sizeof(InodeId);
    size_t size() const { return MAX_SIZE; }
    void checkSize(size_t size) { ALWAYS_ASSERT(size == MAX_SIZE); }

    LE_VAL(InodeId, id, setId, 0)

    static StaticValue<InodeIdValue> Static(InodeId id) {
        auto x = StaticValue<InodeIdValue>();
        x->setId(id);
        return x;
    }
};

// When we need a simple u64 value (e.g. log index)
struct U64Value {
    char* data;

    static constexpr size_t MAX_SIZE = sizeof(uint64_t);
    size_t size() const { return MAX_SIZE; }
    void checkSize(size_t size) { ALWAYS_ASSERT(size == MAX_SIZE); }

    LE_VAL(uint64_t, u64, setU64, 0)

    static StaticValue<U64Value> Static(uint64_t x) {
        auto v = StaticValue<U64Value>();
        v->setU64(x);
        return v;
    }
};

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
    char* data;

    static constexpr size_t MAX_SIZE =
        sizeof(ShardId) + // shardId
        sizeof(std::array<uint8_t, 16>); // secretKey

    size_t size() const { return MAX_SIZE; }
    void checkSize(size_t size) { ALWAYS_ASSERT(size == MAX_SIZE); }

    U8_VAL    (ShardId, shardId,      setShardId,   0)
    FBYTES_VAL(16,      getSecretKey, setSecretKey, 1)
};

// When we need an InodeId key. We mostly do not need them to be ordered
// (and therefore BE), but it does make it a bit nicer to be able to traverse
// them like that.
struct InodeIdKey {
    char* data;

    static constexpr size_t MAX_SIZE = sizeof(InodeId);
    size_t size() const { return MAX_SIZE; }
    void checkSize(size_t size) { ALWAYS_ASSERT(size == MAX_SIZE); }

    BE64_VAL(InodeId, id, setId, 0)

    static StaticValue<InodeIdKey> Static(InodeId id) {
        auto x = StaticValue<InodeIdKey>();
        x->setId(id);
        return x;
    }
};

enum class SpanState : uint8_t {
    CLEAN = 0,
    DIRTY = 1,
    CONDEMNED = 2,
};

std::ostream& operator<<(std::ostream& out, SpanState state);

struct TransientFileBody {
    char* data;

    static constexpr size_t MIN_SIZE =
        sizeof(uint64_t) + // size
        sizeof(EggsTime) + // mtime
        sizeof(EggsTime) + // deadline
        sizeof(SpanState) + // lastSpanState
        sizeof(uint8_t); // noteLength
    static constexpr size_t MAX_SIZE = MIN_SIZE + 255; // max note size

    size_t size() const {
        return MIN_SIZE + note().length;
    }

    void checkSize(size_t sz) {
        ALWAYS_ASSERT(sz >= MIN_SIZE);
        ALWAYS_ASSERT(sz == size());
    }

    LE_VAL(uint64_t,  fileSize,      setFileSize,   0)
    LE_VAL(EggsTime,  mtime,         setMtime,      8)
    LE_VAL(EggsTime,  deadline,      setDeadline,  16)
    U8_VAL(SpanState, lastSpanState, setLastSpanState, 24)
    BYTES_VAL(        note,          setNote,      25)
};

struct FileBody {
    char* data;

    static constexpr size_t MAX_SIZE =
        sizeof(uint64_t) + // size
        sizeof(EggsTime);  // mtime
    size_t size() const { return MAX_SIZE; }
    void checkSize(size_t sz) { ALWAYS_ASSERT(sz == MAX_SIZE); }

    LE_VAL(uint64_t, fileSize, setFileSize, 0)
    LE_VAL(EggsTime, mtime,    setMtime,    8)
};

struct SpanKey {
    char* data;

    static constexpr size_t MAX_SIZE =
        sizeof(InodeId) + // id
        sizeof(uint64_t); // offset
    size_t size() const { return MAX_SIZE; }
    void checkSize(size_t sz) { ALWAYS_ASSERT(sz == MAX_SIZE); }

    BE64_VAL(InodeId,  id,     setId,     0)
    BE64_VAL(uint64_t, offset, setOffset, 8)
};

struct PACKED BlockBody {
    uint64_t blockServiceId;
    std::array<uint8_t, 4> crc32;
};

struct SpanBody {
    char* data;

    static constexpr size_t MIN_SIZE =
        sizeof(uint64_t) +               // size
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

    static size_t calcSize(uint8_t storageClass, Parity parity, uint8_t inlineBodyLen) {
        if (storageClass == ZERO_FILL_STORAGE) {
            return MIN_SIZE;
        } else if (storageClass == INLINE_STORAGE) {
            ALWAYS_ASSERT(parity == Parity());
            ALWAYS_ASSERT(inlineBodyLen > 0);
            return MIN_SIZE + inlineBodyLen;
        } else {
            ALWAYS_ASSERT(inlineBodyLen == 0);
            ALWAYS_ASSERT(parity.blocks() > 0);
            return MIN_SIZE + BLOCK_BODY_SIZE*parity.blocks();
        }
    }

    size_t size() const {
        size_t sz = MIN_SIZE;
        if (storageClass() == ZERO_FILL_STORAGE) {
            // no-op
        } else if (storageClass() == INLINE_STORAGE) {
            sz += 1 + (uint8_t)(int)*(data + MIN_SIZE);
        } else {
            sz += BLOCK_BODY_SIZE * parity().blocks();
        }
        return sz;
    }

    LE_VAL(uint64_t, spanSize,     setSpanSize,      0)
    FBYTES_VAL(4,    crc32,        setCrc32,         8)
    U8_VAL(uint8_t,  storageClass, setStorageClass, 12)
    U8_VAL(Parity,   parity,       setParity,       13)

    const BincodeBytes inlineBody() const {
        ALWAYS_ASSERT(storageClass() == INLINE_STORAGE);
        size_t offset = MIN_SIZE;
        BincodeBytes bs;
        bs.length = (uint8_t)(int)*(data+offset);
        bs.data = (const uint8_t*)(data+offset+1);
        return bs;
    }
    void setInlineBody(const BincodeBytes& bs) {
        ALWAYS_ASSERT(storageClass() == INLINE_STORAGE);
        size_t offset = MIN_SIZE;
        *(data+offset) = (char)(int)bs.length;
        memcpy(data+offset+1, bs.data, bs.length);
    }

    void block(uint64_t ix, BlockBody& b) const {
        ALWAYS_ASSERT(storageClass() != ZERO_FILL_STORAGE && storageClass() != INLINE_STORAGE);
        ALWAYS_ASSERT(ix < parity().blocks());
        memcpy(&b, data + MIN_SIZE + ix * sizeof(BlockBody), sizeof(BlockBody));
    }
    void setBlock(uint64_t ix, const BlockBody& b) {
        ALWAYS_ASSERT(storageClass() != ZERO_FILL_STORAGE && storageClass() != INLINE_STORAGE);
        ALWAYS_ASSERT(ix < parity().blocks());
        memcpy(data + MIN_SIZE + ix * sizeof(BlockBody), &b, sizeof(BlockBody));
    }
};

enum class HashMode : uint8_t {
    // temporary 63-bit hash since pyfuse does not seem to like 64 bits dirents.
    XXH3_63 = 1,
};

struct DirectoryBody {
    char* data;

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
    BYTES_VAL(       infoUnchecked,    setInfoUnchecked,    18)

    size_t size() const {
        return MIN_SIZE + infoUnchecked().length;
    }
    void checkSize(size_t sz) {
        ALWAYS_ASSERT(sz >= MIN_SIZE);
        ALWAYS_ASSERT(sz == size());
    }
    
    bool hasInfo() const {
        return ownerId() == NULL_INODE_ID || !infoInherited();
    }

    BincodeBytes info() const {
        ALWAYS_ASSERT(hasInfo() == (infoUnchecked().length > 0));
        return infoUnchecked();
    }

    void setInfo(const BincodeBytes& bytes) {
        ALWAYS_ASSERT(hasInfo() == (bytes.length > 0));
        setInfoUnchecked(bytes);
    }
};

struct EdgeKey {
    char* data;

    static constexpr size_t MIN_SIZE =
        sizeof(uint64_t) +  // first 63 bits: dirId, then whether the edge is current
        sizeof(uint64_t) +  // nameHash
        sizeof(uint8_t);    // nameLength
    // max name size, and an optional creation time if current=false
    static constexpr size_t MAX_SIZE = MIN_SIZE + 255 + sizeof(EggsTime);

    size_t size() const {
        size_t sz = MIN_SIZE + name().length;
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
    BYTES_VAL(          name,                  setName,                  16)
    BE64_VAL(EggsTime,  creationTimeUnchecked, setCreationTimeUnchecked, (16+1+name().length))

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
    char* data;

    static constexpr size_t MAX_SIZE =
        sizeof(InodeIdExtra); // target, and if owned
    size_t size() const { return MAX_SIZE; }
    void checkSize(size_t sz) { ALWAYS_ASSERT(sz == MAX_SIZE); }

    LE_VAL(InodeIdExtra, targetIdWithOwned, setTargetIdWithOwned, 0)
};

struct CurrentEdgeBody {
    char* data;

    static constexpr size_t MAX_SIZE =
        sizeof(InodeIdExtra) + // target, and if locked
        sizeof(EggsTime);  // creationTime
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
