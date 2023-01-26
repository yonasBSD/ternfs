cmake_minimum_required(VERSION 3.21)
include(ExternalProject)

# _must_ be done before everything else <https://gitlab.kitware.com/cmake/community/-/wikis/FAQ#method-3-avoid-use-set>
set(CMAKE_C_COMPILER clang)
set(CMAKE_CXX_COMPILER clang++)

project(eggsfs)

# Yes, this means that it won't work for multi-configuration stuff, which is fine for now.
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING
      "Choose the type of build, options are: Debug, Release, Valgrind, Sanitize. Note that Debug is automatically sanitized, Sanitize is Release, but sanitized."
      FORCE)
endif()

if (NOT (${CMAKE_BUILD_TYPE} MATCHES "^(Debug|Release|Sanitized|Valgrind)$"))
    message(FATAL_ERROR "Build type must be one of Debug, Release, Sanitized, got ${CMAKE_BUILD_TYPE}")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_REQUIRED true)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
option(BUILD_SHARED_LIBS OFF)
set(POSITION_INDEPENDENT_CODE off)
add_compile_options(-fno-pie -fdiagnostics-color=always -fno-stack-protector -g -gdwarf -Wall -Wextra -Wno-sign-compare -Wno-unused-parameter -Wno-unused-variable -Wno-gnu-string-literal-operator-template)
add_link_options(-fuse-ld=lld -no-pie)

add_compile_options("$<$<CONFIG:Debug,Release,Sanitized>:-march=skylake>")
add_compile_options("$<$<CONFIG:Valgrind>:-march=haswell;-maes>") # Valgrind cannot digest certain instructions

add_compile_options("$<$<CONFIG:Release,Sanitized,Valgrind>:-O3>")

add_compile_options("$<$<CONFIG:Debug>:-O;-DEGGS_DEBUG>")

# asan does not support static linkage, and valgrind does not play well with it either:
# <https://stackoverflow.com/questions/7506134/valgrind-errors-when-linked-with-static-why>
#
# Also, regarding the thread stuff: <https://stackoverflow.com/a/45271521>.
add_link_options("$<$<CONFIG:Release,Debug>:-static;-lrt;-pthread;-Wl,--whole-archive;-lpthread;-Wl,--no-whole-archive>")
add_link_options("$<$<CONFIG:Valgrind,Sanitized>:-pthread>")

set(SANITIZE_OPTIONS "-fsanitize=undefined,address,integer,function;-fno-sanitize-recover=all;-fsanitize-blacklist=${CMAKE_SOURCE_DIR}/ubsan-ignorelist")
add_compile_options("$<$<CONFIG:Sanitized>:${SANITIZE_OPTIONS}>")
add_link_options("$<$<CONFIG:Sanitized>:${SANITIZE_OPTIONS}>")

include(thirdparty.cmake)

add_subdirectory(core)
add_subdirectory(shard)
add_subdirectory(cdc)
add_subdirectory(tests)
