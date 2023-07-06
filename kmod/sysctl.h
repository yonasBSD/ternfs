#ifndef _EGGSFS_SYSCTL_H
#define _EGGSFS_SYSCTL_H

#include <linux/init.h>

extern int eggsfs_prefetch;
extern int eggsfs_debug_output;
extern unsigned eggsfs_max_write_span_attempts;
extern unsigned eggsfs_max_write_span_attempts;
extern unsigned eggsfs_initial_shard_timeout_ms;
extern unsigned eggsfs_max_shard_timeout_ms;
extern unsigned eggsfs_overall_shard_timeout_ms;
extern unsigned eggsfs_initial_cdc_timeout_ms;
extern unsigned eggsfs_max_cdc_timeout_ms;
extern unsigned eggsfs_overall_cdc_timeout_ms;

int __init eggsfs_sysctl_init(void);
void __cold eggsfs_sysctl_exit(void);

#endif

