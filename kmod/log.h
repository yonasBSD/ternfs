#ifndef _EGGSFS_LOG_H
#define _EGGSFS_LOG_H

extern int eggsfs_debug_output;

#define eggsfs_warn_print(fmt, args...) printk(KERN_WARNING "eggsfs: %s: " fmt "\n", __func__, ##args)
#define eggsfs_info_print(fmt, args...) printk(KERN_INFO "eggsfs: %s: " fmt "\n", __func__, ##args)
#define eggsfs_debug_print(fmt, args...) \
    if (unlikely(eggsfs_debug_output)) { \
        printk(KERN_DEBUG "eggsfs: %s: " fmt "\n", __func__, ##args); \
    }

#endif