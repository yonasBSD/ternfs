#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/version.h>
#include <linux/tracepoint.h>

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
#include "dir.h"
#include "file.h"

MODULE_LICENSE("GPL");

struct workqueue_struct* eggsfs_wq;

static int __init eggsfs_init(void) {
    int err;

    eggsfs_debug("initializing module");

    eggsfs_shard_init();

    eggsfs_wq = alloc_workqueue("eggsfs-wq", 0, 0);
    if (!eggsfs_wq) { return -ENOMEM; }

    err = eggsfs_rs_init();
    if (err) { goto out_rs; }

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

    err = eggsfs_dir_init();
    if (err) { goto out_dir; }

    err = eggsfs_file_init();
    if (err) { goto out_file; }

    err = eggsfs_fs_init();
    if (err) { goto out_fs; }

    return 0;

out_fs:
    eggsfs_file_exit();
out_file:
    eggsfs_dir_exit();
out_dir:
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
    eggsfs_rs_exit();
out_rs:
    destroy_workqueue(eggsfs_wq);
    return err;
}

static void __exit eggsfs_exit(void) {
    eggsfs_fs_exit();
    eggsfs_file_exit();
    eggsfs_dir_exit();
    eggsfs_inode_exit();
    eggsfs_span_exit();
    eggsfs_block_exit();
    eggsfs_sysctl_exit();
    eggsfs_sysfs_exit();
    eggsfs_rs_exit();

    // tracepoint_synchronize_unregister() must be called before the end of
    // the module exit function to make sure there is no caller left using
    // the probe. This, and the fact that preemption is disabled around the
    // probe call, make sure that probe removal and module unload are safe.
    tracepoint_synchronize_unregister();

    eggsfs_debug("destroying workqueue");
    destroy_workqueue(eggsfs_wq);
}

module_init(eggsfs_init);
module_exit(eggsfs_exit);

