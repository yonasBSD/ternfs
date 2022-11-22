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
