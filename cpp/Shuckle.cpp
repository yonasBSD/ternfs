#include <sstream>
#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <sys/time.h>
#include <array>

#include "Bincode.hpp"
#include "Msgs.hpp"
#include "MsgsGen.hpp"
#include "Shuckle.hpp"
#include "Exception.hpp"

static std::string generateErrString(const std::string& what, int err) {
    char errbuf[64];
    const char *errmsg = strerror_r(err, errbuf, sizeof(errbuf));
    std::stringstream ss;
    ss << "could not " << what << ": " << err << "/" << translateErrno(err) << "=" << errmsg;
    return ss.str();
}

static int shuckleSock(const std::string& host, uint16_t port, Duration timeout, std::string& errString) {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        throw SYSCALL_EXCEPTION("socket");
    }

    struct timeval tv;      
    tv.tv_sec = timeout.ns/1'000'000'000ull;
    tv.tv_usec = (timeout.ns%1'000'000'000ull)/1'000;

    if (setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0) {
        throw SYSCALL_EXCEPTION("setsockopt");
    }

    struct hostent* he = gethostbyname(host.c_str());
    if (he == nullptr) {
        throw EGGS_EXCEPTION("could not get address for host %s", host);
    }

    struct sockaddr_in shuckleAddr;
    shuckleAddr.sin_family = AF_INET;
    shuckleAddr.sin_addr = *(struct in_addr*)he->h_addr_list[0];
    shuckleAddr.sin_port = htons(port);

    if (connect(sock, (struct sockaddr*)&shuckleAddr, sizeof(shuckleAddr)) < 0) {
        errString = generateErrString("connect to " + host, errno);
        close(sock);
        return -1;
    }

    return sock;
}

static std::string writeShuckleRequest(int fd, const ShuckleReqContainer& req) {
    static_assert(std::endian::native == std::endian::little);
    // Serialize
    std::vector<char> buf(req.packedSize());
    BincodeBuf bbuf(buf.data(), buf.size());
    req.pack(bbuf);
    // Write out
#define WRITE_OUT(buf, len) \
    do { \
        ssize_t written = write(fd, buf, len); \
        if (written < 0) { \
            return generateErrString("write request", errno); \
        } \
        if (written != len) { \
            return "couldn't write full request"; \
        } \
    } while (false)
    WRITE_OUT(&SHUCKLE_REQ_PROTOCOL_VERSION, sizeof(SHUCKLE_REQ_PROTOCOL_VERSION));
    uint32_t len = 1 + bbuf.len();
    WRITE_OUT(&len, sizeof(len));
    auto kind = req.kind();
    WRITE_OUT(&kind, sizeof(kind));
    WRITE_OUT(bbuf.data, bbuf.len());
#undef WRITE_OUT
    return {};
}

static std::string readShuckleResponse(int fd, ShuckleRespContainer& resp) {
    static_assert(std::endian::native == std::endian::little);
#define READ_IN(buf, count) \
    do { \
        ssize_t readSoFar = 0; \
        while (readSoFar < count) { \
            ssize_t r = read(fd, buf+readSoFar, count-readSoFar); \
            if (r < 0) { \
                return generateErrString("read response", errno); \
            } \
            if (r == 0) { \
                return "unexpected EOF"; \
            } \
            readSoFar += r; \
        } \
    } while (0)
    uint32_t protocol;
    READ_IN(&protocol, sizeof(protocol));
    if (protocol != SHUCKLE_RESP_PROTOCOL_VERSION) {
        throw BINCODE_EXCEPTION("bad shuckle protocol (expected %s, got %s)", SHUCKLE_RESP_PROTOCOL_VERSION, protocol);
    }
    uint32_t len;
    READ_IN(&len, sizeof(len));
    ShuckleMessageKind kind;
    READ_IN(&kind, sizeof(kind));
    std::vector<char> buf(len-1);
    READ_IN(buf.data(), buf.size());
#undef READ_IN
    BincodeBuf bbuf(buf.data(), buf.size());
    resp.unpack(bbuf, kind);
    return {};
}

std::string fetchBlockServices(const std::string& host, uint16_t port, Duration timeout, ShardId shid, UpdateBlockServicesEntry& blocks) {
    std::string errString;
    int sock = shuckleSock(host, port, timeout, errString);
    if (sock < 0) {
        return errString;
    }

    ShuckleReqContainer reqContainer;
    auto& req = reqContainer.setBlockServicesForShard();
    req.shard = shid;
    errString = writeShuckleRequest(sock, reqContainer);
    if (!errString.empty()) {
        return errString;
    }

    ShuckleRespContainer respContainer;
    errString = readShuckleResponse(sock, respContainer);
    if (!errString.empty()) {
        return errString;
    }

    blocks.blockServices = respContainer.getBlockServicesForShard().blockServices;

    return {};
}

std::string registerShard(
    const std::string& host, uint16_t port, Duration timeout, ShardId shid, const std::array<uint8_t, 4>& shardAddr, uint16_t shardPort
) {
    std::string errString;
    int sock = shuckleSock(host, port, timeout, errString);
    if (sock < 0) {
        return errString;
    }

    ShuckleReqContainer reqContainer;
    auto& req = reqContainer.setRegisterShard();
    req.id = shid;
    req.info.ip.data = shardAddr;
    req.info.port = shardPort;
    errString = writeShuckleRequest(sock, reqContainer);
    if (!errString.empty()) {
        return errString;
    }

    ShuckleRespContainer respContainer;
    errString = readShuckleResponse(sock, respContainer);
    if (!errString.empty()) {
        return errString;
    }
    respContainer.getRegisterShard();

    return {};
}

std::string registerCDC(const std::string& host, uint16_t port, Duration timeout, const std::array<uint8_t, 4>& cdcAddr, uint16_t cdcPort) {
    std::string errString;
    int sock = shuckleSock(host, port, timeout, errString);
    if (sock < 0) {
        return errString;
    }

    ShuckleReqContainer reqContainer;
    auto& req = reqContainer.setRegisterCdc();
    req.ip.data = cdcAddr;
    req.port = cdcPort;
    errString = writeShuckleRequest(sock, reqContainer);
    if (!errString.empty()) {
        return errString;
    }

    ShuckleRespContainer respContainer;
    errString = readShuckleResponse(sock, respContainer);
    if (!errString.empty()) {
        return errString;
    }
    respContainer.getRegisterCdc();

    return {};
}

std::string fetchShards(const std::string& host, uint16_t port, Duration timeout, std::vector<ShardInfo>& shards) {
    std::string errString;
    int sock = shuckleSock(host, port, timeout, errString);
    if (sock < 0) {
        return errString;
    }

    ShuckleReqContainer reqContainer;
    reqContainer.setShards();
    errString = writeShuckleRequest(sock, reqContainer);
    if (!errString.empty()) {
        return errString;
    }

    ShuckleRespContainer respContainer;
    errString = readShuckleResponse(sock, respContainer);
    if (!errString.empty()) {
        return errString;
    }
    shards = respContainer.getShards().shards.els;

    return {};
}