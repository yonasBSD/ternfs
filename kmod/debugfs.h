#ifndef _EGGSFS_DEBUGFS_H
#define _EGGSFS_DEBUGFS_H

#include <linux/init.h>

#include "bincode.h"

#define EGGSFS_STATS_IDX_COMPLETED 0
#define EGGSFS_STATS_IDX_ATTEMPTED 1
#define EGGSFS_STATS_IDX_TIMEOUTS 2
#define EGGSFS_STATS_IDX_FAILURES 3
#define EGGSFS_STATS_IDX_NET_FAILURES 3

#define EGGSFS_STATS_NUM_COUNTERS 5

extern u64* shard_metrics;
extern u64* cdc_metrics;

struct eggsfs_metrics_header {
    u8 version;
    u8 kind_index_mapping[256];
};

int __init eggsfs_debugfs_init(void);
void __cold eggsfs_debugfs_exit(void);

#endif
