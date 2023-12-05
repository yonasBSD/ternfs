#include "XmonAgent.hpp"
#include "Assert.hpp"
#include "Exception.hpp"

struct XmonRequestHeader {
    XmonRequestType msgType;
    int64_t alertId;
    Duration quietPeriod;
    bool binnable;
    uint16_t messageLen;
};

void XmonRequest::write(int fd) const {
    ALWAYS_ASSERT(message.size() < 1<<16);
    XmonRequestHeader header = {
        .msgType = msgType,
        .alertId = alertId,
        .quietPeriod = quietPeriod,
        .binnable = binnable,
        .messageLen = (uint16_t)message.size(),
    };
    static thread_local char buf[PIPE_BUF];
    ALWAYS_ASSERT(sizeof(header) + header.messageLen < PIPE_BUF);
    memcpy(buf, &header, sizeof(header));
    memcpy(buf+sizeof(header), message.data(), header.messageLen);
    {
        // pipe writes of < PIPE_BUF are guaranteed to be atomic, see pipe(7)
        int written = ::write(fd, buf, sizeof(header)+header.messageLen);
        if (written != sizeof(header)+header.messageLen) {
            throw SYSCALL_EXCEPTION("write");
        }
    }
}

bool XmonRequest::read(int fd) {
    static thread_local char buf[PIPE_BUF];
    XmonRequestHeader header;
    int read = ::read(fd, buf, sizeof(header));
    if (read < 0 && errno == EAGAIN) { return false; }
    if (read != sizeof(header)) {
        throw SYSCALL_EXCEPTION("read");
    }
    memcpy(&header, buf, sizeof(header));
    msgType = header.msgType;
    alertId = header.alertId;
    quietPeriod = header.quietPeriod;
    binnable = header.binnable;
    read = ::read(fd, buf, header.messageLen);
    if (read != header.messageLen) {
        throw SYSCALL_EXCEPTION("read");
    }
    buf[header.messageLen] = '\0';
    message = std::string(buf);
    return true;
}