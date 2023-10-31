#ifndef _EGGSFS_DEBUGFS_H
#define _EGGSFS_DEBUGFS_H

#include <linux/init.h>

#include "bincode.h"

#define EGGSFS_COUNTERS_DATA_VERSION 0
#define EGGSFS_LATENCIES_DATA_VERSION 0

#define EGGSFS_COUNTERS_IDX_COMPLETED 0
#define EGGSFS_COUNTERS_IDX_ATTEMPTED 1
#define EGGSFS_COUNTERS_IDX_TIMEOUTS 2
#define EGGSFS_COUNTERS_IDX_FAILURES 3
#define EGGSFS_COUNTERS_IDX_NET_FAILURES 3

#define EGGSFS_COUNTERS_NUM_COUNTERS 5

#define EGGSFS_LATENCIES_NUM_BUCKETS 64

struct eggsfs_stats_header {
    u8 version;
    u8 n_buckets;
    u8 kind_index_mapping[256];
    u64 upper_bound_values[EGGSFS_LATENCIES_NUM_BUCKETS];
};

extern u64* shard_counters;
extern u64* cdc_counters;

extern u64* shard_latencies;
extern u64* cdc_latencies;

int __init eggsfs_debugfs_init(void);
void __cold eggsfs_debugfs_exit(void);

#endif
