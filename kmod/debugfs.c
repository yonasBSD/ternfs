#include <linux/debugfs.h>
#include <linux/slab.h>
#include <linux/init.h>

#include "debugfs.h"

#define EGGSFS_ROOT_DEBUGFS_NAME "eggsfs"

#define EGGSFS_SHARD_COUNTERS_SIZE (256 * EGGSFS_SHARD_KIND_MAX * EGGSFS_COUNTERS_NUM_COUNTERS * 8)
#define EGGSFS_CDC_COUNTERS_SIZE (EGGSFS_CDC_KIND_MAX * EGGSFS_COUNTERS_NUM_COUNTERS * 8)

#define EGGSFS_SHARD_LATENCIES_SIZE (256 * EGGSFS_SHARD_KIND_MAX * EGGSFS_LATENCIES_NUM_BUCKETS * 8)
#define EGGSFS_CDC_LATENCIES_SIZE (EGGSFS_CDC_KIND_MAX * EGGSFS_LATENCIES_NUM_BUCKETS * 8)

static struct dentry* eggsfs_debugfs_root;

static struct debugfs_blob_wrapper* debug_wrapper_shard_stats_header = NULL;
static struct debugfs_blob_wrapper* debug_wrapper_cdc_stats_header = NULL;

static struct debugfs_blob_wrapper* debug_wrapper_shard_counters = NULL;
static struct debugfs_blob_wrapper* debug_wrapper_cdc_counters = NULL;

static struct debugfs_blob_wrapper* debug_wrapper_shard_latencies = NULL;
static struct debugfs_blob_wrapper* debug_wrapper_cdc_latencies = NULL;

static struct eggsfs_stats_header* shard_stats_header;
static struct eggsfs_stats_header* cdc_stats_header;

u64* shard_counters;
u64* cdc_counters;

u64* shard_latencies;
u64* cdc_latencies;

int __init eggsfs_debugfs_init(void) {
    int err;

    // Headers
    shard_stats_header = kzalloc(sizeof(struct eggsfs_stats_header), GFP_KERNEL);
    if (IS_ERR(shard_stats_header)) {
        err = PTR_ERR(shard_stats_header);
        goto out_shard_stats_header;
    }

    cdc_stats_header = kzalloc(sizeof(struct eggsfs_stats_header), GFP_KERNEL);
    if (IS_ERR(cdc_stats_header)) {
        err = PTR_ERR(cdc_stats_header);
        goto out_cdc_stats_header;
    }

    int j;
    shard_stats_header->version = (u8)EGGSFS_COUNTERS_DATA_VERSION;
    static_assert(sizeof(__eggsfs_shard_kind_index_mappings) == sizeof(shard_stats_header->kind_index_mapping));
    memcpy(shard_stats_header->kind_index_mapping, __eggsfs_shard_kind_index_mappings, sizeof(__eggsfs_shard_kind_index_mappings));
    shard_stats_header->n_buckets = (u8)EGGSFS_LATENCIES_NUM_BUCKETS;
    for (j = EGGSFS_LATENCIES_NUM_BUCKETS - 1; j >= 0; j--) {
        shard_stats_header->upper_bound_values[j] = 1ull << (EGGSFS_LATENCIES_NUM_BUCKETS - 1 - j);
    }

    cdc_stats_header->version = (u8)EGGSFS_COUNTERS_DATA_VERSION;
    static_assert(sizeof(__eggsfs_cdc_kind_index_mappings) == sizeof(cdc_stats_header->kind_index_mapping));
    memcpy(cdc_stats_header->kind_index_mapping, __eggsfs_cdc_kind_index_mappings, sizeof(__eggsfs_cdc_kind_index_mappings));
    cdc_stats_header->n_buckets = (u8)EGGSFS_LATENCIES_NUM_BUCKETS;
    for (j = EGGSFS_LATENCIES_NUM_BUCKETS - 1; j >= 0; j--) {
        cdc_stats_header->upper_bound_values[j] = 1ull << (EGGSFS_LATENCIES_NUM_BUCKETS - 1 - j);
    }

    // Metrics
    shard_counters = (u64 *)vzalloc(EGGSFS_SHARD_COUNTERS_SIZE);
    if (IS_ERR(shard_counters)) {
        err = PTR_ERR(shard_counters);
        goto out_shard_counters;
    }

    cdc_counters = (u64 *)vzalloc(EGGSFS_CDC_COUNTERS_SIZE);
    if (IS_ERR(cdc_counters)) {
        err = PTR_ERR(cdc_counters);
        goto out_cdc_counters;
    }

    // Timings
    shard_latencies = vzalloc(EGGSFS_SHARD_LATENCIES_SIZE);
    if (IS_ERR(shard_latencies)) {
        err = PTR_ERR(shard_latencies);
        goto out_shard_latencies;
    }

    cdc_latencies = vzalloc(EGGSFS_CDC_LATENCIES_SIZE);
    if (IS_ERR(cdc_latencies)) {
        err = PTR_ERR(cdc_latencies);
        goto out_cdc_latencies;
    }

    eggsfs_debugfs_root = debugfs_create_dir(EGGSFS_ROOT_DEBUGFS_NAME, NULL);
    if (IS_ERR(eggsfs_debugfs_root)) {
        err = PTR_ERR(eggsfs_debugfs_root);
        goto out_err; // debugfs_remove_recursive handles NULL parameter value
    }

    struct dentry *d;
#define EGGSFS_DEBUGFS_FILE(_name, _size, _stat_name) ({ \
        debug_wrapper_##_name = kzalloc(sizeof(struct debugfs_blob_wrapper), GFP_KERNEL); \
        if (IS_ERR(debug_wrapper_##_name)) { \
            return PTR_ERR(debug_wrapper_##_name); \
        } \
        debug_wrapper_##_name->data = _name; \
        debug_wrapper_##_name->size = _size; \
        d = debugfs_create_blob(_stat_name, 0644, eggsfs_debugfs_root, debug_wrapper_##_name); \
        if (IS_ERR(d)){ \
            err = PTR_ERR(d); \
            goto out_err; \
        } \
    })

    EGGSFS_DEBUGFS_FILE(shard_stats_header, sizeof(struct eggsfs_stats_header), "shard_stats_header");
    EGGSFS_DEBUGFS_FILE(cdc_stats_header, sizeof(struct eggsfs_stats_header), "cdc_stats_header");
    EGGSFS_DEBUGFS_FILE(shard_counters, EGGSFS_SHARD_COUNTERS_SIZE, "shard_counters");
    EGGSFS_DEBUGFS_FILE(cdc_counters, EGGSFS_CDC_COUNTERS_SIZE, "cdc_counters");
    EGGSFS_DEBUGFS_FILE(shard_latencies, EGGSFS_SHARD_LATENCIES_SIZE, "shard_latencies");
    EGGSFS_DEBUGFS_FILE(cdc_latencies, EGGSFS_CDC_LATENCIES_SIZE, "cdc_latencies");

    return 0;

out_cdc_latencies:
    vfree(shard_latencies);
out_shard_latencies:
    vfree(cdc_counters);
out_cdc_counters:
    vfree(shard_counters);
out_shard_counters:
    kfree(cdc_stats_header);
out_cdc_stats_header:
    kfree(shard_stats_header);
out_shard_stats_header:
    return err;

out_err:
    eggsfs_debugfs_exit();
    return err;

#undef EGGSFS_DEBUGFS_FILE
}

void __cold eggsfs_debugfs_exit(void) {
    debugfs_remove_recursive(eggsfs_debugfs_root);

    kfree(shard_stats_header);
    kfree(cdc_stats_header);

    vfree(shard_counters);
    vfree(cdc_counters);

    vfree(shard_latencies);
    vfree(cdc_latencies);

    kfree(debug_wrapper_shard_counters);
    kfree(debug_wrapper_shard_stats_header);
    kfree(debug_wrapper_cdc_counters);
    kfree(debug_wrapper_cdc_stats_header);
}
