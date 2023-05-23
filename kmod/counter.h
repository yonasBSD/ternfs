#ifndef _EGGSFS_COUNTER_H
#define _EGGSFS_COUNTER_H

#include <linux/compiler.h>
#include <linux/percpu.h>

#define EGGSFS_DECLARE_COUNTER(_name) DECLARE_PER_CPU(u64, _name)
#define EGGSFS_DEFINE_COUNTER(_name) DEFINE_PER_CPU(u64, _name)

#define eggsfs_counter_inc(_counter) raw_cpu_inc(_counter)
#define eggsfs_counter_dec(_counter) raw_cpu_dec(_counter)

#define eggsfs_counter_get(_counter) ({ \
        u64 total = 0; \
        int cpu; \
        for_each_possible_cpu(cpu) { \
            total += *per_cpu_ptr(_counter, cpu); \
        } \
        total; \
    })

#endif

