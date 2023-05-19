#include <linux/sysctl.h>

#include "dir.h"
#include "span.h"
#include "log.h"

int eggsfs_debug_output = 0;
extern int eggsfs_rs_cpu_level;

#if 0
bool eggsfs_reclaim_span(void) { return false; }

static int eggsfs_drop_spancache_var;

static int eggsfs_drop_spancache_sysctl(struct ctl_table* table, int write, void __user* buffer, size_t* len, loff_t* ppos) {
    int ret;
    ret = proc_dointvec_minmax(table, write, buffer, len, ppos);
    if (ret)
        return ret;
    if (write) {
        int n_spans = 0;
        while (eggsfs_reclaim_span()) { ++n_spans; }
        printk(KERN_INFO "eggsfs: reclaimed %d spans\n", n_spans);
    }
    return 0;
}
#endif

#define EGGSFS_CTL_ULONG(_name) \
    { \
        .procname = #_name, \
        .data = &eggsfs_##_name, \
        .maxlen = sizeof(eggsfs_##_name), \
        .mode = 0644, \
        .proc_handler = proc_doulongvec_minmax,  \
    }

#define EGGSFS_CTL_INT_TIME(_name) \
    { \
        .procname = #_name "_ms", \
        .data = &eggsfs_##_name, \
        .maxlen = sizeof(eggsfs_##_name), \
        .mode = 0644, \
        .proc_handler = proc_dointvec_ms_jiffies,  \
    }

static struct ctl_table eggsfs_cb_sysctls[] = {
    {
        .procname = "debug",
        .data = &eggsfs_debug_output,
        .maxlen = sizeof(eggsfs_debug_output),
        .mode = 0644,
        .proc_handler = proc_dointvec,
    },

    {
        .procname = "rs_cpu_level",
        .data = &eggsfs_rs_cpu_level,
        .maxlen = sizeof(eggsfs_rs_cpu_level),
        .mode = 0644,
        .proc_handler = proc_dointvec,
    },

    EGGSFS_CTL_INT_TIME(dir_refresh_time),

#if 0
    EGGSFS_CTL_ULONG(spancache_max_size_async),
    EGGSFS_CTL_ULONG(spancache_min_avail_mem_async),
    EGGSFS_CTL_ULONG(spancache_max_size_sync),
    EGGSFS_CTL_ULONG(spancache_min_avail_mem_sync),

    {
        .procname = "drop_spancache",
        .data = &eggsfs_drop_spancache_var,
        .maxlen = sizeof(int),
        .mode = 0200,
        .proc_handler = eggsfs_drop_spancache_sysctl,
    },
#endif

    {}
};

static struct ctl_table eggsfs_cb_sysctl_dir[] = {
    {
        .procname = "eggsfs",
        .mode = 0555,
        .child = eggsfs_cb_sysctls,
    },
    { }
};

static struct ctl_table eggsfs_cb_sysctl_root[] = {
    {
        .procname = "fs",
        .mode = 0555,
        .child = eggsfs_cb_sysctl_dir,
    },
    { }
};

static struct ctl_table_header* eggsfs_callback_sysctl_table;

int __init eggsfs_sysctl_init(void) {
    eggsfs_callback_sysctl_table = register_sysctl_table(eggsfs_cb_sysctl_root);
    if (eggsfs_callback_sysctl_table == NULL) {
        return -ENOMEM;
    }
    return 0;
}

void __cold eggsfs_sysctl_exit(void) {
    unregister_sysctl_table(eggsfs_callback_sysctl_table);
    eggsfs_callback_sysctl_table = NULL;
}

