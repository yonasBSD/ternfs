#pragma once

#include <bit>
#include <cstddef>
#include <rocksdb/slice.h>

#include "Assert.hpp"
#include "Bincode.hpp"
#include "Exception.hpp"
#include "MsgsGen.hpp"
#include "RocksDBUtils.hpp"
#include "Msgs.hpp"
#include "Time.hpp"

enum class CDCMetadataKey : uint8_t {
    LAST_APPLIED_LOG_ENTRY = 0,
    LAST_TXN = 1,
    FIRST_TXN_IN_QUEUE = 1,
    LAST_TXN_IN_QUEUE = 2,
    EXECUTING_TXN = 3,
    EXECUTING_TXN_STATE = 4,
    LAST_DIRECTORY_ID = 5,
};
constexpr CDCMetadataKey LAST_APPLIED_LOG_ENTRY_KEY = CDCMetadataKey::LAST_APPLIED_LOG_ENTRY;
constexpr CDCMetadataKey LAST_TXN_KEY = CDCMetadataKey::LAST_TXN;
constexpr CDCMetadataKey FIRST_TXN_IN_QUEUE_KEY = CDCMetadataKey::FIRST_TXN_IN_QUEUE;
constexpr CDCMetadataKey LAST_TXN_IN_QUEUE_KEY = CDCMetadataKey::LAST_TXN_IN_QUEUE;
constexpr CDCMetadataKey EXECUTING_TXN_KEY = CDCMetadataKey::EXECUTING_TXN;
constexpr CDCMetadataKey EXECUTING_TXN_STATE_KEY = CDCMetadataKey::EXECUTING_TXN_STATE;
constexpr CDCMetadataKey NEXT_DIRECTORY_ID_KEY = CDCMetadataKey::LAST_DIRECTORY_ID;

inline rocksdb::Slice cdcMetadataKey(const CDCMetadataKey* k) {
    return rocksdb::Slice((const char*)k, sizeof(*k));
}

struct MakeDirectoryState {
    char* _data;

    static constexpr size_t MAX_SIZE =
        sizeof(InodeId) +  // dir id we've generated
        sizeof(EggsError); // exit error if we're rolling back
    
    size_t size() const { return MAX_SIZE; }
    void checkSize(size_t size) { ALWAYS_ASSERT(size == MAX_SIZE); }

    LE_VAL(InodeId,   dirId,     setDirId,     0)
    LE_VAL(EggsError, exitError, setExitError, 8)

    void start() {
        setDirId(NULL_INODE_ID);
        setExitError(NO_ERROR);
    }
};

struct RenameFileState {
    char* _data;

    static constexpr size_t MAX_SIZE =
        sizeof(EggsError); // exit error if we're rolling back
    
    size_t size() const { return MAX_SIZE; }
    void checkSize(size_t size) { ALWAYS_ASSERT(size == MAX_SIZE); }

    LE_VAL(EggsError, exitError,           setExitError,           0)

    void start() {
        setExitError(NO_ERROR);
    }
};

struct SoftUnlinkDirectoryState {
    char* _data;

    static constexpr size_t MAX_SIZE =
        sizeof(EggsError); // exit error if we're rolling back
    
    size_t size() const { return MAX_SIZE; }
    void checkSize(size_t size) { ALWAYS_ASSERT(size == MAX_SIZE); }

    LE_VAL(EggsError, exitError,           setExitError,           0)

    void start() {
        setExitError(NO_ERROR);
    }
};

struct RenameDirectoryState {
    char* _data;

    static constexpr size_t MAX_SIZE =
        sizeof(InodeId) +  // current directory we're traversing upwards when looking for cycles
        sizeof(EggsError); // exit error if we're rolling back
    
    size_t size() const { return MAX_SIZE; }
    void checkSize(size_t size) { ALWAYS_ASSERT(size == MAX_SIZE); }

    LE_VAL(InodeId,   currentDirectory, setCurrentDirectory, 0)
    LE_VAL(EggsError, exitError,        setExitError,        8)

    void start() {
        setCurrentDirectory(NULL_INODE_ID);
        setExitError(NO_ERROR);
    }
};

struct HardUnlinkDirectoryState {
    char* _data;
    static constexpr size_t MAX_SIZE = 0;
    size_t size() const { return MAX_SIZE; }
    void checkSize(size_t size) { ALWAYS_ASSERT(size == MAX_SIZE); }
    void start() {}
};

template<typename Type, typename ...Types>
constexpr size_t maxMaxSize() {
    if constexpr (sizeof...(Types) > 0) {
        return std::max<size_t>(Type::MAX_SIZE, maxMaxSize<Types...>());
    } else {
        return Type::MAX_SIZE;
    }
}

struct TxnState {
    char* _data;

    static constexpr size_t MIN_SIZE =
        sizeof(CDCMessageKind) + // request type
        sizeof(uint8_t);         // step
    static constexpr size_t MAX_SIZE =
        MIN_SIZE +
        maxMaxSize<MakeDirectoryState, RenameFileState, SoftUnlinkDirectoryState, RenameDirectoryState, HardUnlinkDirectoryState>();

    U8_VAL(CDCMessageKind, reqKind,   setReqKind,   0);
    U8_VAL(uint8_t,        step,      setStep,      1);

    size_t size() const {
        size_t sz = MIN_SIZE;
        switch (reqKind()) {
        case CDCMessageKind::MAKE_DIRECTORY:
            sz += MakeDirectoryState::MAX_SIZE; break;
        case CDCMessageKind::RENAME_FILE:
            sz += RenameFileState::MAX_SIZE; break;
        case CDCMessageKind::SOFT_UNLINK_DIRECTORY:
            sz += SoftUnlinkDirectoryState::MAX_SIZE; break;
        case CDCMessageKind::RENAME_DIRECTORY:
            sz += RenameDirectoryState::MAX_SIZE; break;
        case CDCMessageKind::HARD_UNLINK_DIRECTORY:
            sz += RenameDirectoryState::MAX_SIZE; break;
        default:
            throw EGGS_EXCEPTION("bad cdc message kind %s", reqKind());
        }
        return sz;
    }

    void checkSize(size_t sz) {
        ALWAYS_ASSERT(sz >= MIN_SIZE);
        ALWAYS_ASSERT(sz == size());
    }

    #define TXN_STATE(kind, type, get, startName) \
        const type get() { \
            ALWAYS_ASSERT(reqKind() == CDCMessageKind::kind); \
            type v; \
            v._data = _data + MIN_SIZE; \
            return v; \
        } \
        type startName() { \
            setStep(0); \
            setReqKind(CDCMessageKind::kind); \
            type v; \
            v._data = _data + MIN_SIZE; \
            v.start(); \
            return v; \
        }
    
    TXN_STATE(MAKE_DIRECTORY,        MakeDirectoryState,        getMakeDirectory,       startMakeDirectory)
    TXN_STATE(RENAME_FILE,           RenameFileState,           getRenameFile,          startRenameFile)
    TXN_STATE(SOFT_UNLINK_DIRECTORY, SoftUnlinkDirectoryState,  getSoftUnlinkDirectory, startSoftUnlinkDirectory)
    TXN_STATE(RENAME_DIRECTORY,      RenameDirectoryState,      getRenameDirectory,     startRenameDirectory)
    TXN_STATE(HARD_UNLINK_DIRECTORY, HardUnlinkDirectoryState,  getHardUnlinkDirectory, startHardUnlinkDirectory)

    #undef TXN_STATE

    void start(CDCMessageKind kind) {
        setReqKind(kind);
        switch (reqKind()) {
        case CDCMessageKind::MAKE_DIRECTORY: startMakeDirectory(); break;
        case CDCMessageKind::RENAME_FILE: startRenameFile(); break;
        case CDCMessageKind::SOFT_UNLINK_DIRECTORY: startSoftUnlinkDirectory(); break;
        case CDCMessageKind::RENAME_DIRECTORY: startRenameDirectory(); break;
        case CDCMessageKind::HARD_UNLINK_DIRECTORY: startHardUnlinkDirectory(); break;
        default:
            throw EGGS_EXCEPTION("bad cdc message kind %s", reqKind());
        }
    }
};

/*
struct TxnIdKey {
    char* _data;

    static constexpr size_t MAX_SIZE = sizeof(uint64_t);
    size_t size() const { return MAX_SIZE; }
    void checkSize(size_t size) { ALWAYS_ASSERT(size == MAX_SIZE); }

    BE64_VAL(uint64_t, txnId, setTxnId, 0)

    static StaticValue<TxnIdKey> Static(uint64_t txnId) {
        auto x = StaticValue<TxnIdKey>();
        x->setTxnId(txnId);
        return x;
    }
};

std::string cdcReqToValue(uint64_t requestId, const CDCReqContainer& req);
vodi cdcReqFromValue()

struct TxnIdKey {
    // the txn id, in big endian
    char* _data;

    TxnIdKey(const rocksdb::Slice& slice) {
        ALWAYS_ASSERT(slice.size() == sizeof(_data));
        memcpy(&_data, slice.data(), sizeof(_data));
    }

    TxnIdKey(uint64_t txnId) {
        txnId = byteswapU64(txnId); // LE -> BE
        memcpy(&_data, &txnId, sizeof(txnId));
    }

    uint64_t txnId() const {
        uint64_t txnId;
        memcpy(&txnId, _data, sizeof(txnId));
        return byteswapU64(); // BE -> LE
    }

    rocksdb::Slice toSlice() const {
        return rocksdb::Slice((const char*)_data, sizeof(_data));
    }
};

struct CDCReqValue {
    
};

struct NoState {
    NoState(CDCMessageKind kind, char* data, size_t len) {
        ALWAYS_ASSERT(len == 0);
    }
};

struct MakeDirectoryState {
    char* _data;

    // 0-1: step
    // 1-9: dir id
    MakeDirectoryState(CDCMessageKind kind, char* data, size_t len) {
        ALWAYS_ASSERT(kind == CDCMessageKind::MAKE_DIRECTORY);

    }

}
*/