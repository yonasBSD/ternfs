#pragma once

#include <rocksdb/db.h>
#include <rocksdb/utilities/transaction.h>

#include "Assert.hpp"
#include "Bincode.hpp"
#include "Msgs.hpp"

#define ROCKS_DB_CHECKED_MSG(status, ...) \
    do { \
        if (!status.ok()) { \
            throw RocksDBException(__LINE__, SHORT_FILE, removeTemplates(__PRETTY_FUNCTION__).c_str(), status, VALIDATE_FORMAT(__VA_ARGS__)); \
        } \
    } while (false)

#define ROCKS_DB_CHECKED(status) \
    do { \
        if (!status.ok()) { \
            throw RocksDBException(__LINE__, SHORT_FILE, removeTemplates(__PRETTY_FUNCTION__).c_str(), status); \
        } \
    } while (false)

class RocksDBException : public AbstractException {
public:
    template <typename ... Args>
    RocksDBException(int line, const char *file, const char *function, rocksdb::Status status, const char *fmt, Args ... args) {
        std::stringstream ss;
        ss << "RocksDBException(" << file << "@" << line << ", " << status.ToString() << "):\n";
        format_pack(ss, fmt, args...);
        _msg = ss.str();
    }

    RocksDBException(int line, const char *file, const char *function, rocksdb::Status status) {
        std::stringstream ss;
        ss << "RocksDBException(" << file << "@" << line << ", " << status.ToString() << ")";
        _msg = ss.str();
    }

    virtual const char *what() const throw() override {
        return _msg.c_str();
    };
private:
    std::string _msg;
};

// Just to avoid having to call delete manually
struct WrappedIterator {
    WrappedIterator(rocksdb::Iterator* it): _it(it) {
        ALWAYS_ASSERT(_it);
    }

    ~WrappedIterator() {
        delete _it;
    }

    rocksdb::Iterator* operator->() {
        return _it;
    }

private:
    rocksdb::Iterator* _it;
};

// Just to avoid having to call delete manually
struct WrappedTransaction {
    WrappedTransaction(rocksdb::Transaction* txn): _txn(txn) {
        ALWAYS_ASSERT(_txn);
    }

    ~WrappedTransaction() {
        delete _txn;
    }

    rocksdb::Transaction* operator->() {
        return _txn;
    }

    rocksdb::Transaction& operator*() {
        return *_txn;
    }

private:
    rocksdb::Transaction* _txn;
};

// Just to avoid having to call release manually. `db` must outlive the snapshot.
struct WrappedSnapshot {
private:
    rocksdb::DB* _db;

public:
    const rocksdb::Snapshot* snapshot;

    WrappedSnapshot(rocksdb::DB* db): _db(db), snapshot(db->GetSnapshot()) {
        ALWAYS_ASSERT(snapshot);
    }

    ~WrappedSnapshot() {
        _db->ReleaseSnapshot(snapshot);
    }

    const rocksdb::Snapshot* operator->() {
        return snapshot;
    }
};


// We use byteswap to go from LE to BE, and vice-versa.
static_assert(std::endian::native == std::endian::little);
inline uint64_t byteswapU64(uint64_t x) {
    return __builtin_bswap64(x);
}

template<typename T>
struct StaticValue {
private:
    static_assert(sizeof(T) == sizeof(char*));
    static_assert(std::is_same_v<decltype(((T*)nullptr)->_data), char*>);
    std::array<char, T::MAX_SIZE> _data;
public:
    rocksdb::Slice toSlice() const {
        return rocksdb::Slice(&_data[0], operator()().size());
    }

    T operator()() {
        T val;
        val._data = &_data[0];
        return val;
    }

    const T operator()() const {
        T val;
        val._data = (char*)&_data[0];
        return val;
    }
};

template<typename T>
struct ExternalValue {
private:
    static_assert(sizeof(T) == sizeof(char*));
    static_assert(std::is_same_v<decltype(((T*)nullptr)->_data), char*>);
    T _val;
public:
    ExternalValue() {
        _val._data = nullptr;
    }
    ExternalValue(char* data, size_t size) {
        _val._data = data;
        _val.checkSize(size);
    }
    ExternalValue(std::string& s): ExternalValue(s.data(), s.size()) {}

    static const ExternalValue<T> FromSlice(const rocksdb::Slice& slice) {
        return ExternalValue((char*)slice.data(), slice.size());
    }

    T operator()() {
        return _val;
    }

    rocksdb::Slice toSlice() {
        return rocksdb::Slice(_val._data, _val.size());
    }
};

template<typename T>
struct OwnedValue {
    static_assert(sizeof(T) == sizeof(char*));
    static_assert(std::is_same_v<decltype(((T*)nullptr)->_data), char*>);
    T _val;
public:
    OwnedValue() = delete;
    OwnedValue<T>& operator=(const OwnedValue<T>&) = delete;

    template<typename ...Args>
    OwnedValue(Args&&... args) {
        size_t sz = T::calcSize(std::forward<Args>(args)...);
        _val._data = (char*)malloc(sz);
        ALWAYS_ASSERT(_val._data);
        _val.afterAlloc(std::forward<Args>(args)...);
    }

    ~OwnedValue() {
        free(_val._data);
    }

    rocksdb::Slice toSlice() const {
        return rocksdb::Slice(_val._data, _val.size());
    }

    T operator()() {
        return _val;
    }
};

#define LE_VAL(type, name, setName, offset) \
    static_assert(sizeof(type) > 1); \
    type name() const { \
        type x; \
        memcpy(&x, _data+offset, sizeof(x)); \
        return x; \
    } \
    void setName(type x) { \
        memcpy(_data+offset, &x, sizeof(x)); \
    }

#define U8_VAL(type, name, setName, offset) \
    static_assert(sizeof(type) == sizeof(uint8_t)); \
    type name() const { \
        type x; \
        memcpy(&x, _data+offset, sizeof(x)); \
        return x; \
    } \
    void setName(type x) { \
        memcpy(_data+offset, &x, sizeof(x)); \
    }

#define BYTES_VAL(name, setName, offset) \
    BincodeBytesRef name() const { \
        return BincodeBytesRef((const char*)(_data+offset+1), (uint8_t)(int)*(_data+offset)); \
    } \
    void setName(const BincodeBytesRef& bytes) { \
        *(_data+offset) = (char)(int)bytes.size(); \
        memcpy(_data+offset+1, bytes.data(), bytes.size()); \
    }

#define FBYTES_VAL(sz, getName, setName, offset) \
    std::array<uint8_t, sz> getName() const { \
        std::array<uint8_t, sz> bs; \
        memcpy(&bs[0], _data+offset, sz); \
        return bs; \
    } \
    void setName(const std::array<uint8_t, sz>& bs) { \
        memcpy(_data+offset, &bs[0], sz); \
    }

#define BE64_VAL(type, name, setName, offset) \
    static_assert(sizeof(type) == sizeof(uint64_t)); \
    type name() const { \
        uint64_t x; \
        memcpy(&x, _data+offset, sizeof(x)); \
        x = byteswapU64(x); /* BE -> LE */ \
        type v; \
        memcpy(&v, &x, sizeof(uint64_t)); \
        return v; \
    } \
    void setName(type v) { \
        uint64_t x; \
        memcpy(&x, &v, sizeof(uint64_t)); \
        x = byteswapU64(x); /* LE -> BE */ \
        memcpy(_data+offset, &x, sizeof(x)); \
    }

// When we need a simple u64 value (e.g. log index)
struct U64Value {
    char* _data;

    static constexpr size_t MAX_SIZE = sizeof(uint64_t);
    size_t size() const { return MAX_SIZE; }
    void checkSize(size_t size) { ALWAYS_ASSERT(size == MAX_SIZE); }

    LE_VAL(uint64_t, u64, setU64, 0)

    static StaticValue<U64Value> Static(uint64_t x) {
        auto v = StaticValue<U64Value>();
        v().setU64(x);
        return v;
    }
};

// When we need a simple u64 key (e.g. log index)
struct U64Key {
    char* _data;

    static constexpr size_t MAX_SIZE = sizeof(uint64_t);
    size_t size() const { return MAX_SIZE; }
    void checkSize(size_t size) { ALWAYS_ASSERT(size == MAX_SIZE); }

    BE64_VAL(uint64_t, u64, setU64, 0)

    static StaticValue<U64Key> Static(uint64_t x) {
        auto v = StaticValue<U64Key>();
        v().setU64(x);
        return v;
    }
};

// When we need an InodeId key. We mostly do not need them to be ordered
// (and therefore BE), but it does make it a bit nicer to be able to traverse
// them like that.
struct InodeIdKey {
    char* _data;

    static constexpr size_t MAX_SIZE = sizeof(InodeId);
    size_t size() const { return MAX_SIZE; }
    void checkSize(size_t size) { ALWAYS_ASSERT(size == MAX_SIZE); }

    BE64_VAL(InodeId, id, setId, 0)

    static StaticValue<InodeIdKey> Static(InodeId id) {
        auto x = StaticValue<InodeIdKey>();
        x().setId(id);
        return x;
    }
};

template<typename A>
std::string bincodeToRocksValue(const A& v) {
    std::string buf;
    buf.resize(v.packedSize());
    BincodeBuf bbuf(buf);
    v.pack(bbuf);
    ALWAYS_ASSERT(bbuf.remaining() == 0, "expected no remaining bytes, got %s", bbuf.remaining());
    return buf;
}

template<typename A>
void bincodeFromRocksValue(const rocksdb::Slice& value, A& v) {
    BincodeBuf bbuf((char*)value.data(), value.size());
    v.unpack(bbuf);
    ALWAYS_ASSERT(bbuf.remaining() == 0);
}

struct InodeIdValue {
    char* _data;

    static constexpr size_t MAX_SIZE = sizeof(InodeId);
    size_t size() const { return MAX_SIZE; }
    void checkSize(size_t size) { ALWAYS_ASSERT(size == MAX_SIZE); }

    LE_VAL(InodeId, id, setId, 0)

    static StaticValue<InodeIdValue> Static(InodeId id) {
        auto x = StaticValue<InodeIdValue>();
        x().setId(id);
        return x;
    }
};
