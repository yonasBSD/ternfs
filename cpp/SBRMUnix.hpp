#pragma once

#include <stddef.h>
#include <utility>
#include <unistd.h>
#include <sys/mman.h>

#include "Exception.hpp"
#include "Common.hpp"

class FDHolder {
public:
    FDHolder() : _fd(-1) {}
    FDHolder(int fd) : _fd(fd) {}
    FDHolder(FDHolder && rhs) : _fd(-1) { std::swap(_fd, rhs._fd); }
    FDHolder & operator=(FDHolder && rhs) { std::swap(_fd, rhs._fd); return *this; }
    FDHolder & operator=(int fd) { return *this = FDHolder(fd); }
    ~FDHolder() { if (_fd >= 0) close(_fd); }
    int operator*() const { return _fd; }
    explicit operator bool() const { return _fd >= 0; }
    void reset() { *this = FDHolder(); }
    FDHolder clone() const { int ret = dup(_fd); if (ret == -1) throw SYSCALL_EXCEPTION("dup"); return ret; }

private:
    int _fd;
};


template<typename Type, size_t Size>
class MMapHolder {
public:
    static constexpr size_t SIZE = Size;
    
    MMapHolder() : _ptr(MAP_FAILED) {}
    MMapHolder(void * ptr) : _ptr(ptr) {}
    MMapHolder(MMapHolder && rhs) : _ptr(MAP_FAILED) { std::swap(_ptr, rhs._ptr); }
    MMapHolder & operator=(MMapHolder && rhs) { std::swap(_ptr, rhs._ptr); return *this; }
    MMapHolder & operator=(void * ptr) { return *this = MMapHolder(ptr); }
    ~MMapHolder() { if (_ptr != MAP_FAILED) munmap(_ptr, Size); }
    Type operator*() const { return reinterpret_cast<Type>(_ptr); }
    Type operator->() const { return reinterpret_cast<Type>(_ptr); }
    typename std::remove_pointer<Type>::type & operator[](size_t i) const { return reinterpret_cast<Type>(_ptr)[i]; }
    explicit operator bool() const { return _ptr != MAP_FAILED; }
    void reset() { *this = MMapHolder(); }

private:
    void * _ptr;
};


template<typename Type>
class DynamicMMapHolder {
public:
    DynamicMMapHolder() : _ptr(MAP_FAILED), _sz(0) {}
    DynamicMMapHolder(void * ptr, size_t sz) : _ptr(ptr), _sz(sz) {}
    DynamicMMapHolder(DynamicMMapHolder && rhs) : _ptr(MAP_FAILED), _sz(0) { std::swap(_ptr, rhs._ptr); std::swap(_sz, rhs._sz); }
    DynamicMMapHolder & operator=(DynamicMMapHolder && rhs) { std::swap(_ptr, rhs._ptr); std::swap(_sz, rhs._sz); return *this; }
    ~DynamicMMapHolder() { if (_ptr != MAP_FAILED) munmap(_ptr, _sz); }
    Type operator*() const { return reinterpret_cast<Type>(_ptr); }
    Type operator->() const { return reinterpret_cast<Type>(_ptr); }
    typename std::remove_pointer<Type>::type & operator[](size_t i) const { return reinterpret_cast<Type>(_ptr)[i]; }
    explicit operator bool() const { return _ptr != MAP_FAILED; }
    void reset() { *this = DynamicMMapHolder(); }
    size_t size() const { return _sz; }

private:
    void * _ptr;
    size_t _sz;
};

