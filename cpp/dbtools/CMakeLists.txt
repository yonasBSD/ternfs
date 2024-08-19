include_directories(${eggsfs_SOURCE_DIR}/core ${eggsfs_SOURCE_DIR}/shard ${eggsfs_SOURCE_DIR}/cdc)

add_library(sharddbtools ShardDBTools.hpp ShardDBTools.cpp LogsDBTools.hpp LogsDBTools.cpp CDCDBTools.hpp CDCDBTools.cpp)
target_link_libraries(sharddbtools PRIVATE core shard cdc)

add_executable(eggsdbtools eggsdbtools.cpp)
target_link_libraries(eggsdbtools PRIVATE core shard sharddbtools ${EGGSFS_JEMALLOC_LIBS})
