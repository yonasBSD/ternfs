#include <fcntl.h>
#include <sstream>
#include <string>
#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <sys/time.h>
#include <array>
#include <charconv>
#include <stdio.h>
#include <memory>
#include <netinet/tcp.h>

#include "Bincode.hpp"
#include "Env.hpp"
#include "Msgs.hpp"
#include "MsgsGen.hpp"
#include "Shuckle.hpp"
#include "Exception.hpp"

static std::string explicitGenerateErrString(const std::string& what, int err, const char* str) {
    const char *errmsg = safe_strerror(err);
    std::stringstream ss;
    ss << "could not " << what << ": " << err << "/" << str << "=" << errmsg;
    return ss.str();
}

static std::string generateErrString(const std::string& what, int err) {
    return explicitGenerateErrString(what, err, (std::string(translateErrno(err)) + "=" + safe_strerror(err)).c_str());
}

struct ShuckleSock {
    int fd;
    ~ShuckleSock() { close(fd); }
};

static ShuckleSock shuckleSock(const std::string& host, uint16_t port, Duration timeout, std::string& errString) {
    ShuckleSock sock;
    sock.fd = -1;

    std::unique_ptr<struct addrinfo, decltype(&freeaddrinfo)> infos(nullptr, &freeaddrinfo);
    {
        char portStr[10];
        snprintf(portStr, sizeof(portStr), "%d", port);
        struct addrinfo hint;
        memset(&hint, 0, sizeof(hint));
        hint.ai_family = AF_INET;
        struct addrinfo* infosRaw;
        int res = getaddrinfo(host.c_str(), portStr, &hint, &infosRaw);
        if (res != 0) {
            if (res == EAI_SYSTEM) { // errno is filled in in this case
                throw SYSCALL_EXCEPTION("getaddrinfo");
            }
            std::string prefix = "resolve host " + host + ":" + std::to_string(port);
            if (res == EAI_ADDRFAMILY || res == EAI_AGAIN || res == EAI_NONAME) { // things that might be worth retrying
                errString = explicitGenerateErrString(prefix, res, gai_strerror(res));
                return sock;
            }
            throw EGGS_EXCEPTION("%s: %s/%s", prefix, res, gai_strerror(res)); // we're probably hosed
        }
        infos.reset(infosRaw);
    }

    for (struct addrinfo* info = infos.get(); info != nullptr; info = info->ai_next) {
        int infoSock = socket(AF_INET, SOCK_STREAM, 0);
        if (infoSock < 0) {
            throw SYSCALL_EXCEPTION("socket");
        }

        // We retry upstream anyway, and we want prompt termination of `connect`.
        // If we don't do this, mistakenly trying to connect to an iceland shuckle (currently
        // the default) will hang for a long time.
        int synRetries = 0;
        setsockopt(infoSock, IPPROTO_TCP, TCP_SYNCNT, &synRetries, sizeof(synRetries));

        if (connect(infoSock, info->ai_addr, info->ai_addrlen) < 0) {
            close(infoSock);
        } else {
            sock.fd = infoSock;
            break;
        }
    }

    if (sock.fd == -1) {
        errString = generateErrString("connect to " + host, errno);
    } else {
        struct timeval tv;
        tv.tv_sec = timeout.ns/1'000'000'000ull;
        tv.tv_usec = (timeout.ns%1'000'000'000ull)/1'000;

        if (setsockopt(sock.fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0) {
            throw SYSCALL_EXCEPTION("setsockopt");
        }
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

std::string fetchBlockServices(const std::string& addr, uint16_t port, Duration timeout, ShardId shid, UpdateBlockServicesEntry& blocks) {
    std::string errString;
    auto sock = shuckleSock(addr, port, timeout, errString);
    if (sock.fd < 0) {
        return errString;
    }

    ShuckleReqContainer reqContainer;
    auto& req = reqContainer.setBlockServicesForShard();
    req.shard = shid;
    errString = writeShuckleRequest(sock.fd, reqContainer);
    if (!errString.empty()) {
        return errString;
    }

    ShuckleRespContainer respContainer;
    errString = readShuckleResponse(sock.fd, respContainer);
    if (!errString.empty()) {
        return errString;
    }

    blocks.blockServices = respContainer.getBlockServicesForShard().blockServices;

    return {};
}

std::string registerShard(
    const std::string& addr, uint16_t port, Duration timeout, ShardId shid, const std::array<uint8_t, 4>& shardAddr, uint16_t shardPort
) {
    std::string errString;
    auto sock = shuckleSock(addr, port, timeout, errString);
    if (sock.fd < 0) {
        return errString;
    }

    ShuckleReqContainer reqContainer;
    auto& req = reqContainer.setRegisterShard();
    req.id = shid;
    req.info.ip.data = shardAddr;
    req.info.port = shardPort;
    errString = writeShuckleRequest(sock.fd, reqContainer);
    if (!errString.empty()) {
        return errString;
    }

    ShuckleRespContainer respContainer;
    errString = readShuckleResponse(sock.fd, respContainer);
    if (!errString.empty()) {
        return errString;
    }
    respContainer.getRegisterShard();

    return {};
}

std::string registerCDC(const std::string& host, uint16_t port, Duration timeout, const std::array<uint8_t, 4>& cdcAddr, uint16_t cdcPort) {
    std::string errString;
    auto sock = shuckleSock(host, port, timeout, errString);
    if (sock.fd < 0) {
        return errString;
    }

    ShuckleReqContainer reqContainer;
    auto& req = reqContainer.setRegisterCdc();
    req.ip.data = cdcAddr;
    req.port = cdcPort;
    errString = writeShuckleRequest(sock.fd, reqContainer);
    if (!errString.empty()) {
        return errString;
    }

    ShuckleRespContainer respContainer;
    errString = readShuckleResponse(sock.fd, respContainer);
    if (!errString.empty()) {
        return errString;
    }
    respContainer.getRegisterCdc();

    return {};
}

std::string fetchShards(const std::string& host, uint16_t port, Duration timeout, std::array<ShardInfo, 256>& shards) {
    std::string errString;
    auto sock = shuckleSock(host, port, timeout, errString);
    if (sock.fd < 0) {
        return errString;
    }

    ShuckleReqContainer reqContainer;
    reqContainer.setShards();
    errString = writeShuckleRequest(sock.fd, reqContainer);
    if (!errString.empty()) {
        return errString;
    }

    ShuckleRespContainer respContainer;
    errString = readShuckleResponse(sock.fd, respContainer);
    if (!errString.empty()) {
        return errString;
    }
    if (respContainer.getShards().shards.els.size() != shards.size()) {
        throw EGGS_EXCEPTION("expecting %s shards, got %s", shards.size(), respContainer.getShards().shards.els.size());
    }
    for (int i = 0; i < shards.size(); i++) {
        shards[i] = respContainer.getShards().shards.els[i];
    }

    return {};
}

bool parseShuckleAddress(const std::string& fullShuckleAddress, std::string& shuckleHost, uint16_t& shucklePort) {
    // split host:port
    auto colon = fullShuckleAddress.find(":");
    if (colon == fullShuckleAddress.size()) {
        std::cerr << "Could not find colon" << std::endl;
        return false;
    }
    // parse port
    if (fullShuckleAddress.size() - colon > 6) {
        return false; // port too long
    }
    uint32_t port = 0;
    for (int i = colon + 1; i < fullShuckleAddress.size(); i++) {
        char ch = fullShuckleAddress[i];
        if ((i == colon + 1 && ch == '0') || ch < '0' || ch > '9') {
            return false;
        }
        port = port*10 + (ch-'0');
    }
    if (port == 0 || port > 65535) {
        return false;
    }
    shucklePort = port;
    shuckleHost = {fullShuckleAddress.begin(), fullShuckleAddress.begin()+colon};
    return true;
}