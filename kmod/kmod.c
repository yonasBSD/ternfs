#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/version.h>

#include "wq.h"
#include "inode.h"
#include "metadata.h"
#include "block.h"
#include "super.h"
#include "sysctl.h"
#include "sysfs.h"
#include "block.h"
#include "rs.h"
#include "log.h"
#include "span.h"

MODULE_LICENSE("GPL");

struct workqueue_struct* eggsfs_wq;

static int __init eggsfs_init(void) {
    int err;

    eggsfs_debug_print("initializing module");

    eggsfs_shard_init();

    eggsfs_wq = alloc_workqueue("eggsfs-wq", 0, 0);
    if (!eggsfs_wq) { return -ENOMEM; }

    err = eggsfs_sysfs_init();
    if (err) { goto out_sysfs; }

    err = eggsfs_sysctl_init();
    if (err) { goto out_sysctl; }

    err = eggsfs_block_init();
    if (err) { goto out_block; }

    err = eggsfs_span_init();
    if (err) { goto out_span; }

    err = eggsfs_inode_init();
    if (err) { goto out_inode; }

    err = eggsfs_fs_init();
    if (err) { goto out_fs; }

    err = eggsfs_rs_init();
    if (err) { goto out_rs; }


    return 0;

out_rs:
    eggsfs_fs_exit();
out_fs:
    eggsfs_inode_exit();
out_inode:
    eggsfs_span_exit();
out_span:
    eggsfs_block_exit();
out_block:
    eggsfs_sysctl_exit();
out_sysctl:
    eggsfs_sysfs_exit();
out_sysfs:
    destroy_workqueue(eggsfs_wq);
    return err;
}

static void __exit eggsfs_exit(void) {
    eggsfs_fs_exit();
    eggsfs_inode_exit();
    eggsfs_block_exit();
    eggsfs_sysctl_exit();
    eggsfs_sysfs_exit();
    destroy_workqueue(eggsfs_wq);
}

module_init(eggsfs_init);
module_exit(eggsfs_exit);

