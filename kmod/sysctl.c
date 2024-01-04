#include <linux/sysctl.h>
#include <linux/version.h>

#include "dir.h"
#include "span.h"
#include "log.h"
#include "super.h"
#include "sysctl.h"
#include "net.h"
#include "file.h"
#include "block.h"
#include "metadata.h"

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 10, 0))
#define __sysctl_buffer __kernel
#else
#define __sysctl_buffer __user
#endif

int eggsfs_debug_output = 0;
int eggsfs_prefetch = 0;

#define eggsfs_do_sysctl(__callback) \
    int ret; \
    ret = proc_dointvec_minmax(table, write, buffer, len, ppos); \
    if (ret) { \
        return ret; \
    } \
    if (write) { \
        __callback(); \
    } \
    return 0;

static int drop_cached_spans;
static int eggsfs_drop_spans_sysctl(struct ctl_table* table, int write, void __sysctl_buffer* buffer, size_t* len, loff_t* ppos) {
    eggsfs_do_sysctl(eggsfs_drop_all_spans);
}

static int drop_fetch_block_sockets;
static int eggsfs_drop_fetch_block_sockets_sysctl(struct ctl_table* table, int write, void __sysctl_buffer* buffer, size_t* len, loff_t* ppos) {
    eggsfs_do_sysctl(eggsfs_drop_fetch_block_sockets);
}

static int drop_write_block_sockets;
static int eggsfs_drop_write_block_sockets_sysctl(struct ctl_table* table, int write, void __sysctl_buffer* buffer, size_t* len, loff_t* ppos) {
    eggsfs_do_sysctl(eggsfs_drop_write_block_sockets);
}

#define EGGSFS_CTL_ULONG(_name) \
    { \
        .procname = #_name, \
        .data = &eggsfs_##_name, \
        .maxlen = sizeof(eggsfs_##_name), \
        .mode = 0644, \
        .proc_handler = proc_doulongvec_minmax,  \
    }

#define EGGSFS_CTL_UINT(_name) \
    { \
        .procname = #_name, \
        .data = &eggsfs_##_name, \
        .maxlen = sizeof(eggsfs_##_name), \
        .mode = 0644, \
        .proc_handler = proc_douintvec,  \
    }

static int bool_off = 0;
static int bool_on = 1;

#define EGGSFS_CTL_BOOL(_name) \
    { \
        .procname = #_name, \
        .data = &eggsfs_##_name, \
        .maxlen = sizeof(eggsfs_##_name), \
        .mode = 0644, \
        .proc_handler = proc_dointvec_minmax,  \
        .extra1 = &bool_off,  \
        .extra2 = &bool_on,  \
    }

#define EGGSFS_CTL_INT_JIFFIES(_name) \
    { \
        .procname = #_name "_ms", \
        .data = &eggsfs_##_name##_jiffies, \
        .maxlen = sizeof(eggsfs_##_name##_jiffies), \
        .mode = 0644, \
        .proc_handler = proc_dointvec_ms_jiffies,  \
    }

static struct ctl_table eggsfs_cb_sysctls[] = {
    {
        .procname = "debug",
        .data = &eggsfs_debug_output,
        .maxlen = sizeof(eggsfs_debug_output),
        .mode = 0644,
        .proc_handler = proc_dointvec_minmax,
        .extra1 = &bool_off,
        .extra2 = &bool_on,
    },

    {
        .procname = "rs_cpu_level",
        .data = &eggsfs_rs_cpu_level,
        .maxlen = sizeof(eggsfs_rs_cpu_level),
        .mode = 0644,
        .proc_handler = proc_dointvec_minmax,
        .extra1 = &eggsfs_rs_cpu_level_min,
        .extra2 = &eggsfs_rs_cpu_level_max,
    },

    {
        .procname = "drop_cached_spans",
        .data = &drop_cached_spans,
        .maxlen = sizeof(int),
        .mode = 0200,
        .proc_handler = eggsfs_drop_spans_sysctl,
    },

    {
        .procname = "drop_fetch_block_sockets",
        .data = &drop_fetch_block_sockets,
        .maxlen = sizeof(int),
        .mode = 0200,
        .proc_handler = eggsfs_drop_fetch_block_sockets_sysctl,
    },

    {
        .procname = "drop_write_block_sockets",
        .data = &drop_write_block_sockets,
        .maxlen = sizeof(int),
        .mode = 0200,
        .proc_handler = eggsfs_drop_write_block_sockets_sysctl,
    },

    EGGSFS_CTL_BOOL(prefetch),

    EGGSFS_CTL_INT_JIFFIES(dir_refresh_time),
    EGGSFS_CTL_INT_JIFFIES(file_refresh_time),
    EGGSFS_CTL_INT_JIFFIES(shuckle_refresh_time),
    EGGSFS_CTL_INT_JIFFIES(initial_shard_timeout),
    EGGSFS_CTL_INT_JIFFIES(max_shard_timeout),
    EGGSFS_CTL_INT_JIFFIES(overall_shard_timeout),
    EGGSFS_CTL_INT_JIFFIES(initial_cdc_timeout),
    EGGSFS_CTL_INT_JIFFIES(max_cdc_timeout),
    EGGSFS_CTL_INT_JIFFIES(overall_cdc_timeout),

    EGGSFS_CTL_UINT(max_write_span_attempts),
    EGGSFS_CTL_UINT(atime_update_interval_sec),

    EGGSFS_CTL_ULONG(span_cache_max_size_async),
    EGGSFS_CTL_ULONG(span_cache_min_avail_mem_async),
    EGGSFS_CTL_ULONG(span_cache_max_size_sync),
    EGGSFS_CTL_ULONG(span_cache_min_avail_mem_sync),
    EGGSFS_CTL_ULONG(span_cache_max_size_drop),
    EGGSFS_CTL_ULONG(span_cache_min_avail_mem_drop),

    {
        .procname = "mtu",
        .data = &eggsfs_mtu,
        .maxlen = sizeof(eggsfs_mtu),
        .mode = 0644,
        .proc_handler = proc_douintvec_minmax,
        .extra1 = &eggsfs_default_mtu,
        .extra2 = &eggsfs_max_mtu,
    },

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
    eggsfs_debug("sysctl exit");

    unregister_sysctl_table(eggsfs_callback_sysctl_table);
    eggsfs_callback_sysctl_table = NULL;
}

