include_directories(${eggsfs_SOURCE_DIR}/core ${eggsfs_SOURCE_DIR}/crc32c ${eggsfs_SOURCE_DIR}/wyhash)

add_library(shard Shard.cpp Shard.hpp ShardDB.cpp ShardDB.hpp ShardDBData.cpp ShardDBData.hpp)
target_link_libraries(shard PRIVATE core)

add_executable(eggsshard eggsshard.cpp)
target_link_libraries(eggsshard PRIVATE core shard crc32c ${EGGSFS_JEMALLOC_LIBS})
