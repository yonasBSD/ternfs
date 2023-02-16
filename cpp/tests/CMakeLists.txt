include_directories(${eggsfs_SOURCE_DIR}/rs ${eggsfs_SOURCE_DIR}/core ${eggsfs_SOURCE_DIR}/shard)

add_executable(tests tests.cpp doctest.h)
target_link_libraries(tests PRIVATE core shard cdc rs)
