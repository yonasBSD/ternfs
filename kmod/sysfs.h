#ifndef _EGGSFS_SYSFS_H
#define _EGGSFS_SYSFS_H

#include <linux/init.h>

extern const char* eggsfs_revision;

int __init eggsfs_sysfs_init(void);
void __cold eggsfs_sysfs_exit(void);

#endif

