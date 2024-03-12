#pragma once

#include <array>
#include <cstdint>
#include <netinet/in.h>
#include <sys/socket.h>
#include <vector>


#include "Assert.hpp"
#include "Bincode.hpp"
#include "Common.hpp"
#include "Env.hpp"

struct Message {
    Message(uint64_t socketId_, sockaddr_in* clientAddr_, BincodeBuf buf_) : socketId(socketId_), clientAddr(clientAddr_), buf(buf_) {}
    uint64_t socketId;
    sockaddr_in* clientAddr;
    BincodeBuf buf;
};

template<size_t ProtocolCount, std::array<uint32_t, ProtocolCount> Protocols>
class MultiplexedChannel {
public:
    MultiplexedChannel(Env& env, size_t reserveSize = 100) : _env(env) {
        for (auto& v : _messages) {
            v.reserve(reserveSize);
        }
    }

    void demultiplexMessage(uint64_t socketId, mmsghdr& hdr) {
        sockaddr_in* clientAddr = (sockaddr_in*)hdr.msg_hdr.msg_name;
        BincodeBuf reqBbuf((char*)hdr.msg_hdr.msg_iov[0].iov_base, hdr.msg_len);
        uint32_t protocol = reqBbuf.unpackScalar<uint32_t>();
        reqBbuf.cursor = reqBbuf.data;
        auto protocolIndex = lookupProtocol(protocol);
        if (unlikely(protocolIndex == ProtocolCount)) {
            RAISE_ALERT(_env, "Could not parse request header. Unexpected protocol %s from %s, dropping it.", protocol, *clientAddr);
        }
        _messages[protocolIndex].emplace_back(socketId, clientAddr, reqBbuf);
    }

    std::vector<Message>& getProtocolMessages(uint32_t protocol) {
        auto protocolIdx = lookupProtocol(protocol);
        ALWAYS_ASSERT(protocolIdx != ProtocolCount);
        return _messages[protocolIdx];
    }

    void clear() {
        for (auto& v : _messages) {
            v.clear();
        }
    }

private:
    Env& _env;
    std::array<std::vector<Message>, ProtocolCount> _messages;

    static constexpr size_t lookupProtocol(uint32_t prot) {
        for (size_t i = 0 ; i < Protocols.size(); ++i) {
            if (Protocols[i] == prot) {
                return i;
            }
        }
        return ProtocolCount;
    }
};
