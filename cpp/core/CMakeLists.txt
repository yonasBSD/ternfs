file(GLOB core_sources CONFIGURE_DEPENDS "*.cpp")
file(GLOB core_headers CONFIGURE_DEPENDS "*.hpp")

add_library(core ${core_sources} ${core_headers})
add_dependencies(core thirdparty)
target_link_libraries(core PRIVATE z lzma dwarf elf rocksdb lz4 zstd uring unwind xxhash)
