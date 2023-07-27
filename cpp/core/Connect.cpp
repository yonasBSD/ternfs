#include <memory>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/tcp.h>

#include "Common.hpp"
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

int connectToHost(
    const std::string& host,
    uint16_t port,
    std::string& errString
) {
    int fd = -1;
    int synRetries = 3;
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
                return fd;
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

        if (synRetries > 0) {
            if (setsockopt(infoSock, IPPROTO_TCP, TCP_SYNCNT, &synRetries, sizeof(synRetries)) < 0) {
                errString = generateErrString("setsockopt", errno);
                continue;
            }
        }

        if (connect(infoSock, info->ai_addr, info->ai_addrlen) < 0) {
            errString = generateErrString("connect", errno);
            close(infoSock);
        } else {
            fd = infoSock;
            break;
        }
    }

    // we've managed to connect, clear earlier errors
    if (fd != -1) {
        errString = "";
    }
    return fd;
}