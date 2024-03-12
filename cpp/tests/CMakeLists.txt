include_directories(${eggsfs_SOURCE_DIR}/core ${eggsfs_SOURCE_DIR}/shard ${eggsfs_SOURCE_DIR}/wyhash)

add_executable(tests tests.cpp doctest.h)
target_link_libraries(tests PRIVATE core shard cdc)

add_executable(logsdbtests logsdbtests.cpp doctest.h)
target_link_libraries(logsdbtests PRIVATE core)
