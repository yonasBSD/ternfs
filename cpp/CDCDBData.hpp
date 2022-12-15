#pragma once

#include <bit>
#include <rocksdb/slice.h>

#include "Assert.hpp"
#include "Bincode.hpp"
#include "RocksDBUtils.hpp"

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