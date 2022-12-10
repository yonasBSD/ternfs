#include <memory>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <sys/socket.h>

#include "Bincode.hpp"
#include "Crypto.hpp"
#include "Exception.hpp"
#include "Msgs.hpp"
#include "MsgsGen.hpp"
#include "Shard.hpp"
#include "Env.hpp"
#include "ShardDB.hpp"
#include "CDCKey.hpp"

Shard::Shard(Logger& logger, LogLevel level, ShardId shid, const std::string& dbDir): _stop(false), _shid(shid), _dbDir(dbDir) {
    _env = std::make_unique<Env>(logger, level);
}

void Shard::terminate() {
    _env->flush();
    _stop.store(true);
}

void Shard::onAbort() {
    _env->flush();
}

void Shard::run() {
    LOG_INFO(*_env, "will run shard %s with DB dir %s", _shid, _dbDir);

    _db = std::make_unique<ShardDB>(*_env, _shid, _dbDir);
    uint64_t currentLogIndex = _db->lastAppliedLogEntry();

    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0) {
        throw SYSCALL_EXCEPTION("cannot create socket");
    }
    {
        struct sockaddr_in serverAddr;
        serverAddr.sin_family = AF_INET;
        serverAddr.sin_addr.s_addr = htonl(INADDR_ANY);
        serverAddr.sin_port = htons(_shid.port());
        if (bind(sock, (struct sockaddr*)&serverAddr, sizeof(serverAddr)) != 0) {
            throw SYSCALL_EXCEPTION("cannot bind socket to port %s", _shid.port());
        }
    }
    
    // 10ms timeout for prompt termination
    {
        struct timeval tv;
        tv.tv_sec = 0;
        tv.tv_usec = 100000;
        if (setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO,&tv,sizeof(tv)) < 0) {
            throw SYSCALL_EXCEPTION("setsockopt");
        }

    }

    LOG_INFO(*_env, "running on port %s", _shid.port());
    
    struct sockaddr_in clientAddr;
    std::vector<char> recvBuf(UDP_MTU);
    std::vector<char> sendBuf(UDP_MTU);
    BincodeBytesScratchpad respScratch;
    auto reqContainer = std::make_unique<ShardReqContainer>();
    auto respContainer = std::make_unique<ShardRespContainer>();
    auto logEntry = std::make_unique<ShardLogEntry>();

    for (;;) {
        if (_stop.load()) {
            LOG_DEBUG(*_env, "got told to stop, stopping");
            break;
        }

        // Read one request
        memset(&clientAddr, 0, sizeof(clientAddr));
        socklen_t addrLen = sizeof(clientAddr);
        int read = recvfrom(sock, &recvBuf[0], recvBuf.size(), 0, (struct sockaddr*)&clientAddr, &addrLen);
        if (read < 0 && errno == EAGAIN) {
            continue;
        }
        if (read < 0) {
            throw SYSCALL_EXCEPTION("recvfrom");
        }
        LOG_DEBUG(*_env, "received message from %s", clientAddr);

        BincodeBuf reqBbuf(&recvBuf[0], read);
        
        // First, try to parse the header
        ShardRequestHeader reqHeader;
        try {
            reqHeader.unpack(reqBbuf);
        } catch (BincodeException err) {
            LOG_ERROR(*_env, "%s\nstacktrace:\n%s", err.what(), err.getStackTrace());
            RAISE_ALERT(*_env, "could not parse request header from %s, dropping it.", clientAddr);
            continue;
        }

        LOG_DEBUG(*_env, "received request id %s, kind %s, from %s", reqHeader.requestId, reqHeader.kind, clientAddr);

        // If this will be filled in with an actual code, it means that we couldn't process
        // the request.
        EggsError err = NO_ERROR;

        // Now, try to parse the body
        try {
            reqContainer->unpack(reqBbuf, reqHeader.kind);
            LOG_DEBUG(*_env, "parsed request: %s", *reqContainer);
        } catch (BincodeException exc) {
            LOG_ERROR(*_env, "%s\nstacktrace:\n%s", exc.what(), exc.getStackTrace());
            RAISE_ALERT(*_env, "could not parse request of kind %s from %s, will reply with error.", reqHeader.kind, clientAddr);
            err = EggsError::MALFORMED_REQUEST;
        }

        // authenticate, if necessary
        if (isPrivilegedRequestKind(reqHeader.kind)) {
            BincodeFixedBytes<8> expectedMac;
            cbcmac(CDCKey, reqBbuf.data, reqBbuf.cursor - reqBbuf.data, expectedMac.data);
            BincodeFixedBytes<8> receivedMac;
            reqBbuf.unpackFixedBytes<8>(receivedMac);
            if (expectedMac != receivedMac) {
                err = EggsError::NOT_AUTHORISED;
            }
        }

        // Make sure nothing is left
        if (reqBbuf.remaining() != 0) {
            RAISE_ALERT(*_env, "%s bytes remaining after parsing request of kind %s from %s, will reply with error", reqBbuf.remaining(), reqHeader.kind, clientAddr);
            err = EggsError::MALFORMED_REQUEST;
        }

        // Actually process the request
        if (err == NO_ERROR) {
            if (readOnlyShardReq(reqContainer->kind())) {
                err = _db->read(*reqContainer, respScratch, *respContainer);
            } else {
                err = _db->prepareLogEntry(*reqContainer, respScratch, *logEntry);
                if (err == NO_ERROR) {
                    err = _db->applyLogEntry(true, currentLogIndex+1, *logEntry, respScratch, *respContainer);
                    currentLogIndex++;
                }
            }
        }

        BincodeBuf respBbuf(&sendBuf[0], sendBuf.size());
        if (err == NO_ERROR) {
            LOG_DEBUG(*_env, "successfully processed request %s with kind %s: %s", reqHeader.requestId, respContainer->kind(), *respContainer);
            ShardResponseHeader(reqHeader.requestId, respContainer->kind()).pack(respBbuf);
            respContainer->pack(respBbuf);
        } else {
            LOG_DEBUG(*_env, "request %s failed with error %s", reqContainer->kind(), err);
            ShardResponseHeader(reqHeader.requestId, ShardMessageKind::ERROR).pack(respBbuf);
            respBbuf.packScalar<uint16_t>((uint16_t)err);
        }

        if (sendto(sock, respBbuf.data, respBbuf.len(), 0, (struct sockaddr*)&clientAddr, sizeof(clientAddr)) != respBbuf.len()) {
            throw SYSCALL_EXCEPTION("sendto");
        }
        LOG_DEBUG(*_env, "sent response %s to %s", respContainer->kind(), clientAddr);
    }

    _db->close();
}