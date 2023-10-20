#include <linux/debugfs.h>
#include <linux/slab.h>
#include <linux/init.h>

#include "debugfs.h"

#define EGGSFS_ROOT_DEBUGFS_NAME "eggsfs"

#define EGGSFS_SHARD_STATS_SIZE (256 * EGGSFS_SHARD_KIND_MAX * EGGSFS_STATS_NUM_COUNTERS * 8)
#define EGGSFS_CDC_STATS_SIZE (EGGSFS_CDC_KIND_MAX * EGGSFS_STATS_NUM_COUNTERS * 8)

static struct dentry* eggsfs_debugfs_root;

static struct debugfs_blob_wrapper* debug_wrapper_shard_metrics = NULL;
static struct debugfs_blob_wrapper* debug_wrapper_shard_header = NULL;
static struct debugfs_blob_wrapper* debug_wrapper_cdc_metrics = NULL;
static struct debugfs_blob_wrapper* debug_wrapper_cdc_header = NULL;

static struct eggsfs_metrics_header* shard_header;
static struct eggsfs_metrics_header* cdc_header;

u64* shard_metrics;
u64* cdc_metrics;

int __init eggsfs_debugfs_init(void) {
    int err;

    shard_metrics = (u64 *)vzalloc(EGGSFS_SHARD_STATS_SIZE);
    if (IS_ERR(shard_metrics)) {
        err = PTR_ERR(shard_metrics);
        goto out_shard_metrics;
    }

    cdc_metrics = (u64 *)vzalloc(EGGSFS_CDC_STATS_SIZE);
    if (IS_ERR(cdc_metrics)) {
        err = PTR_ERR(cdc_metrics);
        goto out_cdc_metrics;
    }

    shard_header = kzalloc(sizeof(struct eggsfs_metrics_header), GFP_KERNEL);
    if (IS_ERR(shard_header)) {
        err = PTR_ERR(shard_header);
        goto out_shard_header;
    }

    cdc_header = kzalloc(sizeof(struct eggsfs_metrics_header), GFP_KERNEL);
    if (IS_ERR(cdc_header)) {
        err = PTR_ERR(cdc_header);
        goto out_cdc_header;
    }

    shard_header->version = (u8)0;
    static_assert(sizeof(__eggsfs_shard_kind_index_mappings) == sizeof(shard_header->kind_index_mapping));
    memcpy(shard_header->kind_index_mapping, __eggsfs_shard_kind_index_mappings, sizeof(__eggsfs_shard_kind_index_mappings));

    cdc_header->version = (u8)0;
    static_assert(sizeof(__eggsfs_cdc_kind_index_mappings) == sizeof(cdc_header->kind_index_mapping));
    memcpy(cdc_header->kind_index_mapping, __eggsfs_cdc_kind_index_mappings, sizeof(__eggsfs_cdc_kind_index_mappings));

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

    EGGSFS_DEBUGFS_FILE(shard_metrics, EGGSFS_SHARD_STATS_SIZE, "shard_metrics");
    EGGSFS_DEBUGFS_FILE(shard_header, 256, "shard_header");
    EGGSFS_DEBUGFS_FILE(cdc_metrics, EGGSFS_CDC_STATS_SIZE, "cdc_metrics");
    EGGSFS_DEBUGFS_FILE(cdc_header, 256, "cdc_header");

    return 0;

out_cdc_header:
    vfree(shard_header);
out_shard_header:
    vfree(cdc_metrics);
out_cdc_metrics:
    vfree(shard_metrics);
out_shard_metrics:
    return err;

out_err:
    eggsfs_debugfs_exit();
    return err;

#undef EGGSFS_DEBUGFS_FILE
}

void __cold eggsfs_debugfs_exit(void) {
    debugfs_remove_recursive(eggsfs_debugfs_root);

    vfree(shard_metrics);
    vfree(cdc_metrics);

    kfree(shard_header);
    kfree(cdc_header);

    kfree(debug_wrapper_shard_metrics);
    kfree(debug_wrapper_shard_header);
    kfree(debug_wrapper_cdc_metrics);
    kfree(debug_wrapper_cdc_header);
}
