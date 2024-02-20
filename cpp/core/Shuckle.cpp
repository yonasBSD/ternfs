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
#include <unordered_set>

#include "Bincode.hpp"
#include "Env.hpp"
#include "Msgs.hpp"
#include "Shuckle.hpp"
#include "Exception.hpp"
#include "Connect.hpp"

static std::string explicitGenerateErrString(const std::string& what, int err, const char* str) {
    std::stringstream ss;
    ss << "could not " << what << ": " << err << "/" << str;
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
    sock.fd = connectToHost(host, port, errString);

    if (sock.fd >= 0) {
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
            if (errno == EAGAIN) { \
                continue; \
            } \
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
                if (errno == EAGAIN) { \
                    continue; \
                } \
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
        std::ostringstream ss;
        ss << "bad shuckle protocol (expected " << SHUCKLE_RESP_PROTOCOL_VERSION << ", got " << protocol << ")";
        return ss.str();
    }
    uint32_t len;
    READ_IN(&len, sizeof(len));
    ShuckleMessageKind kind;
    READ_IN(&kind, sizeof(kind));
    std::vector<char> buf(len-1);
    READ_IN(buf.data(), buf.size());
#undef READ_IN
    BincodeBuf bbuf(buf.data(), buf.size());
    if (kind == ShuckleMessageKind::ERROR) {
        EggsError error = (EggsError)bbuf.unpackScalar<uint16_t>();
        std::stringstream ss;
        ss << "got error " << error;
        return ss.str();
    }
    resp.unpack(bbuf, kind);
    return {};
}

std::string fetchBlockServices(const std::string& addr, uint16_t port, Duration timeout, ShardId shid, UpdateBlockServicesEntry& blocks) {
    std::string errString;
    auto sock = shuckleSock(addr, port, timeout, errString);
    if (sock.fd < 0) {
        return errString;
    }

    // all block services
    {
        ShuckleReqContainer reqContainer;
        auto& req = reqContainer.setAllBlockServices();
        errString = writeShuckleRequest(sock.fd, reqContainer);
        if (!errString.empty()) {
            return errString;
        }

        ShuckleRespContainer respContainer;
        errString = readShuckleResponse(sock.fd, respContainer);
        if (!errString.empty()) {
            return errString;
        }

        blocks.blockServices = respContainer.getAllBlockServices().blockServices;
    }

    // current block services
    {
        ShuckleReqContainer reqContainer;
        auto& req = reqContainer.setShardBlockServices();
        req.shardId = shid;
        errString = writeShuckleRequest(sock.fd, reqContainer);
        if (!errString.empty()) {
            return errString;
        }

        ShuckleRespContainer respContainer;
        errString = readShuckleResponse(sock.fd, respContainer);
        if (!errString.empty()) {
            return errString;
        }

        blocks.currentBlockServices = respContainer.getShardBlockServices().blockServices;
    }

    // check that all current block services are known -- there's a small race here
    // the caller should just retry in these cases.
    {
        std::unordered_set<uint64_t> knownBlockServices;
        for (const auto& bs : blocks.blockServices.els) {
            knownBlockServices.insert(bs.info.id.u64);
        }
        for (BlockServiceId bsId : blocks.currentBlockServices.els) {
            if (!knownBlockServices.contains(bsId.u64)) {
                std::stringstream ss;
                ss << "got unknown block service " << bsId << " in current block services, was probably added in the meantime, please retry";
                return ss.str();
            }
        }
    }

    return {};
}

std::string registerShard(
    const std::string& addr, uint16_t port, Duration timeout, ShardId shid,
    uint32_t ip1, uint16_t port1, uint32_t ip2, uint16_t port2
) {
    std::string errString;
    auto sock = shuckleSock(addr, port, timeout, errString);
    if (sock.fd < 0) {
        return errString;
    }

    ShuckleReqContainer reqContainer;
    auto& req = reqContainer.setRegisterShard();
    req.id = shid;
    {
        uint32_t ip = htonl(ip1);
        memcpy(req.info.ip1.data.data(), &ip, 4);
    }
    req.info.port1 = port1;
    {
        uint32_t ip = htonl(ip2);
        memcpy(req.info.ip2.data.data(), &ip, 4);
    }
    req.info.port2 = port2;
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

std::string registerCDC(const std::string& host, uint16_t port, Duration timeout, uint32_t ip1, uint16_t port1, uint32_t ip2, uint16_t port2) {
    std::string errString;
    auto sock = shuckleSock(host, port, timeout, errString);
    if (sock.fd < 0) {
        return errString;
    }

    ShuckleReqContainer reqContainer;
    auto& req = reqContainer.setRegisterCdc();
    {
        uint32_t ip = htonl(ip1);
        memcpy(req.ip1.data.data(), &ip, 4);
    }
    req.port1 = port1;
    {
        uint32_t ip = htonl(ip2);
        memcpy(req.ip2.data.data(), &ip, 4);
    }
    req.port2 = port2;
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

std::string insertStats(
    const std::string& host,
    uint16_t port,
    Duration timeout,
    const std::vector<Stat>& stats
) {
    std::string errString;
    auto sock = shuckleSock(host, port, timeout, errString);
    if (sock.fd < 0) {
        return errString;
    }

    ShuckleReqContainer reqContainer;
    auto& req = reqContainer.setInsertStats();
    req.stats.els.insert(req.stats.els.end(), std::make_move_iterator(stats.begin()), std::make_move_iterator(stats.end()));
    errString = writeShuckleRequest(sock.fd, reqContainer);
    if (!errString.empty()) {
        return errString;
    }

    ShuckleRespContainer respContainer;
    errString = readShuckleResponse(sock.fd, respContainer);
    if (!errString.empty()) {
        return errString;
    }

    return {};

}
