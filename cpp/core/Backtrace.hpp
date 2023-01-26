#pragma once

#include <memory>

#include "SBRMUnix.hpp"
#include "Common.hpp"

struct ProgramInfo {

    ProgramInfo() = default;
    ProgramInfo(ProgramInfo&&) = default;
    ~ProgramInfo() = default;

    ProgramInfo(const ProgramInfo &) = delete;
    ProgramInfo& operator=(const ProgramInfo &) = delete;

    static std::shared_ptr<ProgramInfo> Self();
    static std::shared_ptr<ProgramInfo> ForBinary(const char *exePath, bool required = true);

    size_t getBacktraceSegment(void *ip, char* buf, size_t sz, int* count = nullptr);

// private
    DynamicMMapHolder<char*> _data;
};


size_t getBacktraceInstructionPointers(void** ips, size_t maxIPs) __attribute__((noinline));
size_t generateBacktrace(char * buf, size_t sz);
void loadAndSendEmptyProgramInfo(int out_fd);
void loadAndSendProgramInfo(const char* exePath, int out_fd);
