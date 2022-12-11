#pragma once

#include <rocksdb/db.h>

#include "Assert.hpp"

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
    std::array<char, T::MAX_SIZE> _data;
    T _val;
public:
    StaticValue() {
        _val._data = &_data[0];
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

    T* operator->() {
        return &_val;
    }

    rocksdb::Slice toSlice() {
        return rocksdb::Slice(_val._data, _val.size());
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

    T* operator->() {
        return &_val;
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
    const BincodeBytes name() const { \
        BincodeBytes bs; \
        bs.length = (uint8_t)(int)*(_data+offset); \
        bs.data = (const uint8_t*)(_data+offset+1); \
        return bs; \
    } \
    void setName(const BincodeBytes& bs) { \
        *(_data+offset) = (char)(int)bs.length; \
        memcpy(_data+offset+1, bs.data, bs.length); \
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

