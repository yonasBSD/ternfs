#ifndef _EGGSFS_SYSCTL_H
#define _EGGSFS_SYSCTL_H

#include <linux/init.h>

extern int eggsfs_prefetch;
extern int eggsfs_debug_output;

int __init eggsfs_sysctl_init(void);
void __cold eggsfs_sysctl_exit(void);

#endif

