#pragma once

#include <variant>

#include "Bincode.hpp"
#include "Msgs.hpp"
#include "Env.hpp"
#include "MsgsGen.hpp"

struct CDCReqDestination {
    uint64_t requestId;
    char destIp[4];
    uint16_t destPort;

    void clear() {
        memset(this, 0, sizeof(*this));
    }

    uint16_t packedSize() const {
        uint16_t _size = 0;
        _size += sizeof(uint64_t); // requestId
        _size += sizeof(char[4]);  // destIp
        _size += sizeof(uint16_t); // destPort
        return _size;
    }

    void pack(BincodeBuf& buf) const;
    void unpack(BincodeBuf& buf);
};

/*
struct CDCReqWithDestination {
    CDCReqDestination dest;
    CDCRespContainer resp;

    void clear() {
        dest.clear();
        resp.clear();
    }

    uint16_t packedSize() const {
        return dest.packedSize() + resp.packedSize();
    }

    void pack(BincodeBuf& buf) const;
    void unpack(BincodeBuf& buf);
};

struct CDCRespWithDestination {
    CDCReqDestination dest;
    CDCRespContainer resp;
};
*/

struct CDCNeedsShard {
    ShardId shid;
    uint64_t requestId;
    ShardReqContainer req;
};

struct CDCStep {
    // If `finished`, `resp` is to be read. Otherwise `needsShard`.
    bool finished;
    CDCRespContainer resp;
    CDCNeedsShard needsShard;
};

struct CDCDB {
private:
    void* _impl;

public:
    CDCDB() = delete;

    CDCDB(Logger& env, const std::string& path);
    ~CDCDB();
    void close();

    // Unlike with ShardDB, we don't have an explicit log preparation step here,
    // because at least for now logs are simply either CDC requests, or shard
    // responses.
    //
    // Neither of the two functions below can be called concurrently, however
    // there can be two writers, one doing `processCDCReq`, and one doing
    // `processShardResp`.

    // If an error is returned, the contents of `step` are undefined.
    EggsError processCDCReq(
        EggsTime time,
        uint64_t logIndex,
        const CDCReqDestination& dest,
        const CDCReqContainer& req,
        BincodeBytesScratchpad& scratch,
        CDCStep& step
    );

    // If an error is returned, the contents of `step` are undefined,
    // _but the contents of 
    /*
    EggsError processShardResp(
        EggsTime time,
        uint64_t logIndex,
        uint64_t requestId,
        const ShardRespContainer& req,
        BincodeBytesScratchpad& scratch,
        CDCReqDestination& dest,
        CDCStep& step
    );
    */
};