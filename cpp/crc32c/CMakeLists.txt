add_library(crc32c crc32c.h crc32c.cpp iscsi.hpp)

add_executable(crc32c-tables tables.cpp iscsi.hpp)

add_executable(crc32c-tests tests.cpp)
target_link_libraries(crc32c-tests PRIVATE crc32c)
target_include_directories(crc32c-tests PRIVATE ${eggsfs_SOURCE_DIR}/wyhash)