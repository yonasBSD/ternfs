add_library(crc32c crc32c.h crc32c.c iscsi.h)

add_executable(crc32c-tables tables.cpp iscsi.h)

add_executable(crc32c-tests tests.cpp)
target_link_libraries(crc32c-tests PRIVATE crc32c)
target_include_directories(crc32c-tests PRIVATE ${ternfs_SOURCE_DIR}/wyhash)