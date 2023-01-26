# This is pretty ugly overall, but the ugliest thing
# is that the libraries still depend on system headers to build!
#
# We should fix this but it'd be fairly ad-hoc (setting CFLAGS
# manually).
find_program(MAKE_EXE NAMES gmake nmake make)

# We use this
# Deps: none
ExternalProject_Add(make_uring
    DOWNLOAD_DIR ${CMAKE_CURRENT_BINARY_DIR}
    URL https://github.com/axboe/liburing/archive/refs/tags/liburing-2.3.tar.gz
    URL_HASH SHA256=60b367dbdc6f2b0418a6e0cd203ee0049d9d629a36706fcf91dfb9428bae23c8
    PREFIX thirdparty/uring
    UPDATE_COMMAND ""
    SOURCE_DIR ${make_uring_SOURCE_DIR}
    CONFIGURE_COMMAND ./configure --prefix=<INSTALL_DIR>
    BUILD_IN_SOURCE 1
    BUILD_COMMAND ${MAKE_EXE} -j
    BUILD_BYPRODUCTS <INSTALL_DIR>/lib/liburing.a
    LOG_DOWNLOAD ON
    LOG_CONFIGURE ON
    LOG_INSTALL ON
    LOG_BUILD ON
    LOG_OUTPUT_ON_FAILURE ON
)
add_library(uring STATIC IMPORTED)
ExternalProject_Get_property(make_uring INSTALL_DIR)
include_directories(SYSTEM ${INSTALL_DIR}/include)
set_target_properties(uring PROPERTIES IMPORTED_LOCATION ${INSTALL_DIR}/lib/liburing.a)

# Hard dependency for libelf
# Deps: none
ExternalProject_Add(make_zlib
    DOWNLOAD_DIR ${CMAKE_CURRENT_BINARY_DIR}
    URL http://zlib.net/zlib-1.2.13.tar.gz
    URL_HASH SHA256=b3a24de97a8fdbc835b9833169501030b8977031bcb54b3b3ac13740f846ab30
    PREFIX thirdparty/zlib
    UPDATE_COMMAND ""
    SOURCE_DIR ${make_zlib_SOURCE_DIR}
    CONFIGURE_COMMAND ./configure --prefix=<INSTALL_DIR>
    BUILD_IN_SOURCE 1
    BUILD_COMMAND ${MAKE_EXE} -j
    BUILD_BYPRODUCTS <INSTALL_DIR>/lib/libz.a
    LOG_DOWNLOAD ON
    LOG_CONFIGURE ON
    LOG_INSTALL ON
    LOG_BUILD ON
    LOG_OUTPUT_ON_FAILURE ON
)
add_library(zlib STATIC IMPORTED)
ExternalProject_Get_property(make_zlib INSTALL_DIR)
set(zlib_INSTALL_DIR ${INSTALL_DIR})
include_directories(SYSTEM ${INSTALL_DIR}/include)
set_target_properties(zlib PROPERTIES IMPORTED_LOCATION ${INSTALL_DIR}/lib/libz.a)

# Optional dependency for libelf and libunwind, such a pain to disable it
# in libunwind that we just include it.
# Deps: none
ExternalProject_Add(make_lzma
    DOWNLOAD_DIR ${CMAKE_CURRENT_BINARY_DIR}
    URL https://tukaani.org/xz/xz-5.4.1.tar.gz
    URL_HASH SHA256=e4b0f81582efa155ccf27bb88275254a429d44968e488fc94b806f2a61cd3e22
    PREFIX thirdparty/lzma
    UPDATE_COMMAND ""
    SOURCE_DIR ${make_lzma_SOURCE_DIR}
    CONFIGURE_COMMAND ./configure --prefix=<INSTALL_DIR>
    BUILD_IN_SOURCE 1
    BUILD_COMMAND ${MAKE_EXE} -j
    BUILD_BYPRODUCTS <INSTALL_DIR>/lib/liblzma.a
    LOG_DOWNLOAD ON
    LOG_CONFIGURE ON
    LOG_INSTALL ON
    LOG_BUILD ON
    LOG_OUTPUT_ON_FAILURE ON
)
add_library(lzma STATIC IMPORTED)
ExternalProject_Get_property(make_lzma INSTALL_DIR)
set(lzma_INSTALL_DIR ${INSTALL_DIR})
include_directories(SYSTEM ${INSTALL_DIR}/include)
set_target_properties(lzma PROPERTIES IMPORTED_LOCATION ${INSTALL_DIR}/lib/liblzma.a)

# We use this for rocksdb compression
# Deps: none
ExternalProject_Add(make_lz4
    DOWNLOAD_DIR ${CMAKE_CURRENT_BINARY_DIR}
    URL https://github.com/lz4/lz4/archive/refs/tags/v1.9.4.tar.gz
    URL_HASH SHA256=0b0e3aa07c8c063ddf40b082bdf7e37a1562bda40a0ff5272957f3e987e0e54b
    PREFIX thirdparty/lz4
    UPDATE_COMMAND ""
    SOURCE_DIR ${make_lz4_SOURCE_DIR}
    CONFIGURE_COMMAND ""
    BUILD_IN_SOURCE 1
    BUILD_COMMAND ${MAKE_EXE} -j
    BUILD_BYPRODUCTS <INSTALL_DIR>/lib/liblz4.a
    INSTALL_COMMAND ${MAKE_EXE} install PREFIX=<INSTALL_DIR>
    LOG_DOWNLOAD ON
    LOG_CONFIGURE ON
    LOG_INSTALL ON
    LOG_BUILD ON
    LOG_OUTPUT_ON_FAILURE ON
)
add_library(lz4 STATIC IMPORTED)
ExternalProject_Get_property(make_lz4 INSTALL_DIR)
set(lz4_INSTALL_DIR ${INSTALL_DIR})
include_directories(SYSTEM ${INSTALL_DIR}/include)
set_target_properties(lz4 PROPERTIES IMPORTED_LOCATION ${INSTALL_DIR}/lib/liblz4.a)

# We use this for rocksdb compression, libdwarf also insists on it
# Deps: none
ExternalProject_Add(make_zstd
    DOWNLOAD_DIR ${CMAKE_CURRENT_BINARY_DIR}
    URL https://github.com/facebook/zstd/archive/refs/tags/v1.5.2.tar.gz
    URL_HASH SHA256=f7de13462f7a82c29ab865820149e778cbfe01087b3a55b5332707abf9db4a6e
    PREFIX thirdparty/zstd
    UPDATE_COMMAND ""
    SOURCE_DIR ${make_zstd_SOURCE_DIR}
    CONFIGURE_COMMAND ""
    BUILD_IN_SOURCE 1
    BUILD_COMMAND ${MAKE_EXE} -j
    BUILD_BYPRODUCTS <INSTALL_DIR>/lib/libzstd.a
    INSTALL_COMMAND ${MAKE_EXE} install PREFIX=<INSTALL_DIR>
    LOG_DOWNLOAD ON
    LOG_CONFIGURE ON
    LOG_INSTALL ON
    LOG_BUILD ON
    LOG_OUTPUT_ON_FAILURE ON
)
add_library(zstd STATIC IMPORTED)
ExternalProject_Get_property(make_zstd INSTALL_DIR)
include_directories(SYSTEM ${INSTALL_DIR}/include)
set_target_properties(zstd PROPERTIES IMPORTED_LOCATION ${INSTALL_DIR}/lib/libzstd.a)

# We use this for hashing
# Deps: none
ExternalProject_Add(make_xxhash
    DOWNLOAD_DIR ${CMAKE_CURRENT_BINARY_DIR}
    URL https://github.com/Cyan4973/xxHash/archive/refs/tags/v0.8.1.tar.gz
    URL_HASH SHA256=3bb6b7d6f30c591dd65aaaff1c8b7a5b94d81687998ca9400082c739a690436c
    PREFIX thirdparty/xxhash
    UPDATE_COMMAND ""
    SOURCE_DIR ${make_xxhash_SOURCE_DIR}
    CONFIGURE_COMMAND ""
    BUILD_IN_SOURCE 1
    BUILD_COMMAND ${MAKE_EXE} -j
    BUILD_BYPRODUCTS <INSTALL_DIR>/lib/libxxhash.a
    INSTALL_COMMAND ${MAKE_EXE} install PREFIX=<INSTALL_DIR>
    LOG_DOWNLOAD ON
    LOG_CONFIGURE ON
    LOG_INSTALL ON
    LOG_BUILD ON
    LOG_OUTPUT_ON_FAILURE ON
)
add_library(xxhash STATIC IMPORTED)
ExternalProject_Get_property(make_xxhash INSTALL_DIR)
include_directories(SYSTEM ${INSTALL_DIR}/include)
set_target_properties(xxhash PROPERTIES IMPORTED_LOCATION ${INSTALL_DIR}/lib/libxxhash.a)

# We use it for our backtrace stuff, also dependency of libdwarf
# Deps: zlib, lzma, zstd
ExternalProject_Add(make_elf
    DOWNLOAD_DIR ${CMAKE_CURRENT_BINARY_DIR}
    URL https://sourceware.org/elfutils/ftp/0.188/elfutils-0.188.tar.bz2
    URL_HASH SHA256=fb8b0e8d0802005b9a309c60c1d8de32dd2951b56f0c3a3cb56d21ce01595dff
    PREFIX thirdparty/elf
    UPDATE_COMMAND ""
    SOURCE_DIR ${make_elf_SOURCE_DIR}
    CONFIGURE_COMMAND ./configure --prefix=<INSTALL_DIR> --disable-libdebuginfod --disable-debuginfod --without-bzlib
    BUILD_IN_SOURCE 1
    BUILD_COMMAND ${MAKE_EXE} -j
    BUILD_BYPRODUCTS <INSTALL_DIR>/lib/libelf.a
    LOG_DOWNLOAD ON
    LOG_CONFIGURE ON
    LOG_INSTALL ON
    LOG_BUILD ON
    LOG_OUTPUT_ON_FAILURE ON
)
add_library(elf STATIC IMPORTED)
ExternalProject_Get_property(make_elf INSTALL_DIR)
set(elf_INSTALL_DIR ${INSTALL_DIR})
include_directories(SYSTEM ${INSTALL_DIR}/include)
set_target_properties(elf PROPERTIES IMPORTED_LOCATION ${INSTALL_DIR}/lib/libelf.a)

# We use this for introspection (backtraces)
# Deps: zlib, elf, zstd
ExternalProject_Add(make_dwarf
    DOWNLOAD_DIR ${CMAKE_CURRENT_BINARY_DIR}
    URL https://www.prevanders.net/libdwarf-0.5.0.tar.xz
    URL_HASH SHA256=11fa822c60317fa00e1a01a2ac9e8388f6693e8662ab72d352c5f50c7e0112a9
    PREFIX thirdparty/dwarf
    UPDATE_COMMAND ""
    SOURCE_DIR ${make_dwarf_SOURCE_DIR}
    CONFIGURE_COMMAND ./configure --prefix=<INSTALL_DIR>
    BUILD_IN_SOURCE 1
    BUILD_COMMAND ${MAKE_EXE} -j
    BUILD_BYPRODUCTS <INSTALL_DIR>/lib/libdwarf.a
    LOG_DOWNLOAD ON
    LOG_CONFIGURE ON
    LOG_INSTALL ON
    LOG_BUILD ON
    LOG_OUTPUT_ON_FAILURE ON
)
add_library(dwarf STATIC IMPORTED)
ExternalProject_Get_property(make_dwarf INSTALL_DIR)
include_directories(SYSTEM ${INSTALL_DIR}/include)
set_target_properties(dwarf PROPERTIES IMPORTED_LOCATION ${INSTALL_DIR}/lib/libdwarf.a)

# We use this for introspection (backtraces)
# Deps: zlib, lzma, elf
ExternalProject_Add(make_unwind
    DOWNLOAD_DIR ${CMAKE_CURRENT_BINARY_DIR}
    URL http://download.savannah.nongnu.org/releases/libunwind/libunwind-1.6.2.tar.gz
    URL_HASH SHA256=4a6aec666991fb45d0889c44aede8ad6eb108071c3554fcdff671f9c94794976
    PREFIX thirdparty/unwind
    UPDATE_COMMAND ""
    SOURCE_DIR ${make_unwind_SOURCE_DIR}
    CONFIGURE_COMMAND ./configure --prefix=<INSTALL_DIR> --enable-static --enable-setjmp=no
    BUILD_IN_SOURCE 1
    BUILD_COMMAND ${MAKE_EXE} -j
    BUILD_BYPRODUCTS <INSTALL_DIR>/lib/libunwind.a
    LOG_DOWNLOAD ON
    LOG_CONFIGURE ON
    LOG_INSTALL ON
    LOG_BUILD ON
    LOG_OUTPUT_ON_FAILURE ON
)
add_library(unwind STATIC IMPORTED)
ExternalProject_Get_property(make_unwind INSTALL_DIR)
include_directories(SYSTEM ${INSTALL_DIR}/include)
set_target_properties(unwind PROPERTIES IMPORTED_LOCATION ${INSTALL_DIR}/lib/libunwind.a)

# We use this
# Deps: lz4
if(${CMAKE_BUILD_TYPE} STREQUAL "Valgrind")
    set(ROCKS_DB_PORTABLE "PORTABLE=1") # Valgind can't support current -march=native instructions
endif()
ExternalProject_Add(make_rocksdb
    DOWNLOAD_DIR ${CMAKE_CURRENT_BINARY_DIR}
    URL https://github.com/facebook/rocksdb/archive/refs/tags/v7.9.2.tar.gz
    URL_HASH SHA256=886378093098a1b2521b824782db7f7dd86224c232cf9652fcaf88222420b292
    PREFIX thirdparty/rocksdb
    UPDATE_COMMAND ""
    SOURCE_DIR ${make_rocksdb_SOURCE_DIR}
    CONFIGURE_COMMAND ""
    BUILD_IN_SOURCE 1
    BUILD_COMMAND env ROCKSDB_DISABLE_ZLIB=y ROCKSDB_DISABLE_BZIP=1 ROCKSDB_DISABLE_SNAPPY=1 ${MAKE_EXE} ${ROCKS_DB_PORTABLE} USE_RTTI=1 EXTRA_CXXFLAGS=-DROCKSDB_NO_DYNAMIC_EXTENSION -j static_lib
    BUILD_BYPRODUCTS <INSTALL_DIR>/lib/librocksdb.a
    INSTALL_COMMAND ${MAKE_EXE} install-static PREFIX=<INSTALL_DIR>
    LOG_DOWNLOAD ON
    LOG_CONFIGURE ON
    LOG_INSTALL ON
    LOG_BUILD ON
    LOG_OUTPUT_ON_FAILURE ON
)
add_library(rocksdb STATIC IMPORTED)
ExternalProject_Get_property(make_rocksdb INSTALL_DIR)
include_directories(SYSTEM ${INSTALL_DIR}/include)
set_target_properties(rocksdb PROPERTIES IMPORTED_LOCATION ${INSTALL_DIR}/lib/librocksdb.a)

# This explicit dependency tracking is needed for ninja, which is blind to the
# include dependencies from our code into the above, apparently.
add_custom_target(thirdparty)
add_dependencies(thirdparty make_uring make_zlib make_lzma make_lz4 make_xxhash make_elf make_dwarf make_unwind make_rocksdb make_zstd)