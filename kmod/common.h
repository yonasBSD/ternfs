#ifndef _EGGSFS_COMMON_H
#define _EGGSFS_COMMON_H

#define EGGSFS_MAX_FILENAME 255

//#define eggsfs_debug_print(fmt, args...) trace_printk(fmt "\n", ##args)
#define eggsfs_warn_print(fmt, args...) printk(KERN_WARNING "eggsfs: %s: " fmt "\n", __func__, ##args)
#define eggsfs_info_print(fmt, args...) printk(KERN_INFO "eggsfs: %s: " fmt "\n", __func__, ##args)

#define eggsfs_debug_print(fmt, args...) do {} while(0)
// #define eggsfs_debug_print(fmt, args...) printk(KERN_DEBUG "eggsfs: %s: " fmt "\n", __func__, ##args)

extern struct workqueue_struct* eggsfs_wq;

#endif
