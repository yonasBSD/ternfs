#ifndef _TERNFS_SYSCTL_H
#define _TERNFS_SYSCTL_H

#include <linux/init.h>

extern int ternfs_debug_output;

int __init ternfs_sysctl_init(void);
void __cold ternfs_sysctl_exit(void);

#endif

