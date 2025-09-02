add_library(rs rs.h rs.cpp gf_tables.c)

add_executable(rs-tests tests.cpp)
target_link_libraries(rs-tests PRIVATE rs)
target_include_directories(rs-tests PRIVATE ${ternfs_SOURCE_DIR}/wyhash)

add_executable(rs-bench bench.cpp)
target_link_libraries(rs-bench PRIVATE rs)
target_include_directories(rs-bench PRIVATE ${ternfs_SOURCE_DIR}/wyhash)