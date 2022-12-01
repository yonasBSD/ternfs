#ifndef _EGGSFS_LATCH_H
#define _EGGSFS_LATCH_H

#include <linux/kernel.h>
#include <linux/wait.h>

struct eggsfs_latch {
    atomic64_t counter;
    wait_queue_head_t wq;
};

#define eggsfs_latch_init(_latch) ({ \
        atomic64_set(&(_latch)->counter, 0); \
        init_waitqueue_head(&(_latch)->wq); \
    })

#define eggsfs_latch_try_acquire(_latch, _seqno) ({ \
        (_seqno) = atomic64_fetch_or(1, &(_latch)->counter); \
        smp_mb__after_atomic(); \
        !(_seqno & 1); \
    })

#define eggsfs_latch_release(_latch, _seqno) ({ \
        smp_mb__before_atomic(); \
        atomic64_set(&(_latch)->counter, (_seqno) + 2); \
        wake_up_all(&(_latch)->wq); \
    })

#define eggsfs_latch_wait_killable(_latch, _seqno) ({ \
        u64 goal = ((_seqno) | 1); \
        wait_event_killable((_latch)->wq, atomic64_read(&(_latch)->counter) > goal); \
    })

#endif

