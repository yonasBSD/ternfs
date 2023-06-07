#ifndef _EGGSFS_LOG_H
#define _EGGSFS_LOG_H

#include "sysctl.h"

#define eggsfs_error(fmt, args...) printk(KERN_ERR "eggsfs: %s: " fmt "\n", __func__, ##args)
#define eggsfs_warn(fmt, args...) printk(KERN_WARNING "eggsfs: %s: " fmt "\n", __func__, ##args)
#define eggsfs_info(fmt, args...) printk(KERN_INFO "eggsfs: %s: " fmt "\n", __func__, ##args)
#define eggsfs_debug(fmt, args...) \
    if (unlikely(eggsfs_debug_output)) { \
        printk(KERN_DEBUG "eggsfs: %s: " fmt "\n", __func__, ##args); \
    }

#endif