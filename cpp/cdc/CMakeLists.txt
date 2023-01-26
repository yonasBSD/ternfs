include_directories(${eggsfs_SOURCE_DIR}/core ${eggsfs_SOURCE_DIR}/shard)

add_library(cdc CDC.cpp CDC.hpp CDCDB.cpp CDCDB.hpp CDCDBData.hpp)
target_link_libraries(cdc PRIVATE core)

add_executable(eggscdc eggscdc.cpp)
target_link_libraries(eggscdc PRIVATE core shard cdc)
