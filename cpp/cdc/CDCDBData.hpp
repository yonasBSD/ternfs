#pragma once

#include <bit>
#include <cstddef>
#include <rocksdb/slice.h>

#include "Assert.hpp"
#include "Bincode.hpp"
#include "Exception.hpp"
#include "Msgs.hpp"
#include "RocksDBUtils.hpp"
#include "Msgs.hpp"
#include "Time.hpp"

enum class CDCMetadataKey : uint8_t {
    LAST_APPLIED_LOG_ENTRY = 0,
    LAST_TXN = 1,
    FIRST_TXN_IN_QUEUE = 2,
    LAST_TXN_IN_QUEUE = 3,
    EXECUTING_TXN = 4,
    EXECUTING_TXN_STATE = 6,
    LAST_DIRECTORY_ID = 7,
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
        sizeof(EggsTime) + // creation time for the edge
        sizeof(EggsError); // exit error if we're rolling back
    
    size_t size() const { return MAX_SIZE; }
    void checkSize(size_t size) { ALWAYS_ASSERT(size == MAX_SIZE); }

    LE_VAL(InodeId,   dirId,        setDirId,         0)
    LE_VAL(EggsTime,  creationTime, setCreationTime,  8)
    LE_VAL(EggsError, exitError,    setExitError,    16)

    void start() {
        setDirId(NULL_INODE_ID);
        setCreationTime({});
        setExitError(NO_ERROR);
    }
};

struct RenameFileState {
    char* _data;

    static constexpr size_t MAX_SIZE =
        sizeof(EggsTime) + // the time at which we created the current edge
        sizeof(EggsError); // exit error if we're rolling back
    
    size_t size() const { return MAX_SIZE; }
    void checkSize(size_t size) { ALWAYS_ASSERT(size == MAX_SIZE); }

    LE_VAL(EggsTime,  newCreationTime,     setNewCreationTime,     0)
    LE_VAL(EggsError, exitError,           setExitError,           8)

    void start() {
        setExitError(NO_ERROR);
    }
};

struct SoftUnlinkDirectoryState {
    char* _data;

    static constexpr size_t MAX_SIZE =
        sizeof(InodeId) +  // what we're currently stat'ing
        sizeof(EggsError); // exit error if we're rolling back
    
    size_t size() const { return MAX_SIZE; }
    void checkSize(size_t size) { ALWAYS_ASSERT(size == MAX_SIZE); }

    LE_VAL(InodeId,   statDirId,           setStatDirId,           0)
    LE_VAL(EggsError, exitError,           setExitError,           8)

    void start() {
        setExitError(NO_ERROR);
    }
};

struct RenameDirectoryState {
    char* _data;

    static constexpr size_t MAX_SIZE =
        sizeof(EggsTime) + // time at which we created the old edge
        sizeof(EggsError); // exit error if we're rolling back
    
    size_t size() const { return MAX_SIZE; }
    void checkSize(size_t size) { ALWAYS_ASSERT(size == MAX_SIZE); }

    LE_VAL(EggsTime,  newCreationTime,  setNewCreationTime,  0)
    LE_VAL(EggsError, exitError,        setExitError,        8)

    void start() {
        setNewCreationTime({});
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

struct CrossShardHardUnlinkFileState {
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
        maxMaxSize<MakeDirectoryState, RenameFileState, SoftUnlinkDirectoryState, RenameDirectoryState, HardUnlinkDirectoryState, CrossShardHardUnlinkFileState>();

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
        case CDCMessageKind::CROSS_SHARD_HARD_UNLINK_FILE:
            sz += CrossShardHardUnlinkFileState::MAX_SIZE; break;
        default:
            throw EGGS_EXCEPTION("bad cdc message kind %s", reqKind());
        }
        return sz;
    }

    void checkSize(size_t sz) {
        ALWAYS_ASSERT(sz >= MIN_SIZE);
        ALWAYS_ASSERT(sz == size());
    }

    #define TXN_STATE(kind, type, getName, startName) \
        type getName() { \
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
    
    TXN_STATE(MAKE_DIRECTORY,               MakeDirectoryState,            getMakeDirectory,            startMakeDirectory)
    TXN_STATE(RENAME_FILE,                  RenameFileState,               getRenameFile,               startRenameFile)
    TXN_STATE(SOFT_UNLINK_DIRECTORY,        SoftUnlinkDirectoryState,      getSoftUnlinkDirectory,      startSoftUnlinkDirectory)
    TXN_STATE(RENAME_DIRECTORY,             RenameDirectoryState,          getRenameDirectory,          startRenameDirectory)
    TXN_STATE(HARD_UNLINK_DIRECTORY,        HardUnlinkDirectoryState,      getHardUnlinkDirectory,      startHardUnlinkDirectory)
    TXN_STATE(CROSS_SHARD_HARD_UNLINK_FILE, CrossShardHardUnlinkFileState, getCrossShardHardUnlinkFile, startCrossShardHardUnlinkFile)

    #undef TXN_STATE

    void start(CDCMessageKind kind) {
        setReqKind(kind);
        switch (reqKind()) {
        case CDCMessageKind::MAKE_DIRECTORY: startMakeDirectory(); break;
        case CDCMessageKind::RENAME_FILE: startRenameFile(); break;
        case CDCMessageKind::SOFT_UNLINK_DIRECTORY: startSoftUnlinkDirectory(); break;
        case CDCMessageKind::RENAME_DIRECTORY: startRenameDirectory(); break;
        case CDCMessageKind::HARD_UNLINK_DIRECTORY: startHardUnlinkDirectory(); break;
        case CDCMessageKind::CROSS_SHARD_HARD_UNLINK_FILE: startCrossShardHardUnlinkFile(); break;
        default:
            throw EGGS_EXCEPTION("bad cdc message kind %s", reqKind());
        }
        memset(_data+MIN_SIZE, 0, size()-MIN_SIZE);
    }
};
