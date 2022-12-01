#ifndef _EGGSFS_SYSCTL_H
#define _EGGSFS_SYSCTL_H

#include <linux/init.h>

int __init eggsfs_sysctl_init(void);
void __cold eggsfs_sysctl_exit(void);

#endif

