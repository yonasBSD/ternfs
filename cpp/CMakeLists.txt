cmake_minimum_required(VERSION 3.21)
include(ExternalProject)

# _must_ be done before everything else <https://gitlab.kitware.com/cmake/community/-/wikis/FAQ#method-3-avoid-use-set>
set(CMAKE_C_COMPILER clang)
set(CMAKE_CXX_COMPILER clang++)

project(eggsfs)

# Yes, this means that it won't work for multi-configuration stuff, which is fine for now.
if(NOT CMAKE_BUILD_TYPE)
    set(
        CMAKE_BUILD_TYPE release CACHE STRING
        "Choose the type of build, options are: 'debug', 'release', 'alpine', 'valgrind', 'sanitized'."
        FORCE
    )
endif()

if (NOT (${CMAKE_BUILD_TYPE} MATCHES "^(debug|release|alpine|sanitized|valgrind)$"))
    message(FATAL_ERROR "Build type must be one of debug, release, sanitized, alpine, got ${CMAKE_BUILD_TYPE}")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_REQUIRED true)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
option(BUILD_SHARED_LIBS OFF)
set(POSITION_INDEPENDENT_CODE off)
add_compile_options(-fno-pie -fno-stack-protector -fdiagnostics-color=always -fno-stack-protector -g -gdwarf -Wall -Wextra -Wno-sign-compare -Wno-unused-parameter -Wno-unused-variable -Wno-gnu-string-literal-operator-template)
add_link_options(-fuse-ld=lld -pthread)

# valgrind we currently use cannot digest certain instructions
add_compile_options("$<$<CONFIG:valgrind>:-march=haswell;-maes>")
add_compile_options("$<$<NOT:$<CONFIG:valgrind>>:-march=skylake>")

# performance/debug stuff
add_compile_options("$<$<NOT:$<CONFIG:debug>>:-O3>")
add_compile_options("$<$<CONFIG:debug>:-O;-DEGGS_DEBUG>")

# We build the release build statically in Alpine
add_compile_options("$<$<CONFIG:alpine>:-DEGGS_ALPINE>")
add_link_options("$<$<CONFIG:alpine>:-static>")
add_link_options("$<$<NOT:$<CONFIG:alpine>>:-no-pie>")

# sanitizer options
set(SANITIZE_OPTIONS "-fsanitize=undefined,address,integer,function;-fno-sanitize-recover=all;-fsanitize-blacklist=${CMAKE_SOURCE_DIR}/ubsan-ignorelist")
add_compile_options("$<$<CONFIG:sanitized>:${SANITIZE_OPTIONS}>")
add_link_options("$<$<CONFIG:sanitized>:${SANITIZE_OPTIONS}>")

include(thirdparty.cmake)

add_subdirectory(core)
add_subdirectory(shard)
add_subdirectory(cdc)
add_subdirectory(tests)
