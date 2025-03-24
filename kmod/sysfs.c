#include "sysfs.h"

#include <linux/kobject.h>
#include <linux/fs.h>

#include "dentry.h"
#include "span.h"
#include "log.h"

static struct kset* eggsfs_kset;

struct eggsfs_sysfs_counter {
    struct kobj_attribute kobj_attr;
    u64 __percpu * ptr;
};

static ssize_t eggsfs_sysfs_counter_show(struct kobject* kobj, struct kobj_attribute* attr, char* buf) {
    struct eggsfs_sysfs_counter* ettr = container_of(attr, struct eggsfs_sysfs_counter, kobj_attr);
    u64 val = eggsfs_counter_get(ettr->ptr);
    return snprintf(buf, PAGE_SIZE, "%llu\n", val);
}

static ssize_t eggsfs_sysfs_counter_store(struct kobject *kobj, struct kobj_attribute *a, const char *buf, size_t count) {
    return -EPERM;
}

struct eggsfs_sysfs_atomic64 {
    struct kobj_attribute kobj_attr;
    atomic64_t* v;
};

static ssize_t eggsfs_sysfs_atomic64_show(struct kobject* kobj, struct kobj_attribute* attr, char* buf) {
    struct eggsfs_sysfs_atomic64* ettr = container_of(attr, struct eggsfs_sysfs_atomic64, kobj_attr);
    u64 val = atomic64_read(ettr->v);
    return snprintf(buf, PAGE_SIZE, "%llu\n", val);
}

static ssize_t eggsfs_sysfs_atomic64_store(struct kobject *kobj, struct kobj_attribute *a, const char *buf, size_t count) {
    return -EPERM;
}

static umode_t eggsfs_stat_visible(struct kobject *kobj, struct attribute *attr, int unused) {
    return attr->mode;
}

#define __INIT_KOBJ_ATTR(_name, _mode, _show, _store)			\
{									\
	.attr	= { .name = __stringify(_name), .mode = _mode },	\
	.show	= _show,						\
	.store	= _store,						\
}

#define EGGSFS_SYSFS_COUNTER(_name) \
    static struct eggsfs_sysfs_counter eggsfs_sysfs_counter_##_name = { \
        .kobj_attr = __INIT_KOBJ_ATTR(_name, S_IRUGO, eggsfs_sysfs_counter_show, eggsfs_sysfs_counter_store), \
        .ptr = &eggsfs_stat_##_name, \
    }

#define EGGSFS_SYSFS_COUNTER_PTR(_name) (&eggsfs_sysfs_counter_##_name.kobj_attr.attr)

#define EGGSFS_SYSFS_ATOMIC64(_name) \
    static struct eggsfs_sysfs_atomic64 eggsfs_sysfs_atomic64_##_name = { \
        .kobj_attr = __INIT_KOBJ_ATTR(_name, S_IRUGO, eggsfs_sysfs_atomic64_show, eggsfs_sysfs_atomic64_store), \
        .v = &eggsfs_stat_##_name, \
    }

#define EGGSFS_SYSFS_ATOMIC64_PTR(_name) (&eggsfs_sysfs_atomic64_##_name.kobj_attr.attr)

EGGSFS_SYSFS_COUNTER(dir_revalidations);

static struct attribute* eggsfs_stat_attrs[] = {
    EGGSFS_SYSFS_COUNTER_PTR(dir_revalidations),
    NULL
};

static const struct attribute_group eggsfs_stat_attr_group = {
    .name = "stats",
    .is_visible = eggsfs_stat_visible,
    .attrs = eggsfs_stat_attrs,
};

static ssize_t eggsfs_revision_show(struct kobject* kobj, struct kobj_attribute* attr, char* buf) {
    return sprintf(buf, "%s\n", eggsfs_revision);
}

static struct kobj_attribute eggsfs_revision_attr = __ATTR(revision, 0444, eggsfs_revision_show, NULL);

int __init eggsfs_sysfs_init(void) {
    int err;

    eggsfs_kset = kset_create_and_add("eggsfs", NULL, fs_kobj);
    if (!eggsfs_kset) { return -ENOMEM; }

    err = sysfs_create_group(&eggsfs_kset->kobj, &eggsfs_stat_attr_group);
    if (err) { goto out_unreg; }

    err = sysfs_create_file(&eggsfs_kset->kobj, &eggsfs_revision_attr.attr);
    if (err) { goto out_unreg; }

    return 0;

out_unreg:
    kset_unregister(eggsfs_kset);
    return err;
}

void __cold eggsfs_sysfs_exit(void) {
    eggsfs_debug("sysfs exit");
    kset_unregister(eggsfs_kset);
}

