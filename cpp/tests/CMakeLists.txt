include_directories(${ternfs_SOURCE_DIR}/core ${ternfs_SOURCE_DIR}/shard ${ternfs_SOURCE_DIR}/wyhash)

add_executable(tests tests.cpp doctest.h)
target_link_libraries(tests PRIVATE core shard cdc)

add_executable(logsdbtests logsdbtests.cpp doctest.h)
target_link_libraries(logsdbtests PRIVATE core)
