// We index policies by (inode, tag), so that the automatically get updated
// when the getattr for the inode runs. They never get freed, there should
// be very few of them.
#ifndef _EGGSFS_POLICY_H
#define _EGGSFS_POLICY_H

#include <linux/kernel.h>
#include <linux/spinlock.h>

struct eggsfs_policy {
    struct hlist_node hnode;
    spinlock_t lock;
    u64 inode;
    u8 tag;
    // First byte: len, then the body. We store this separatedly
    // since we it's RCU protected.
    char* __rcu body;
};

// Creates or update a specific policy.
struct eggsfs_policy* eggsfs_upsert_policy(u64 inode, u8 tag, char* body, int len);

struct eggsfs_policy_body {
    u8 len;
    char body[255];
};

void eggsfs_get_policy_body(struct eggsfs_policy* policy, struct eggsfs_policy_body* body);

int __init eggsfs_policy_init(void);
void __cold eggsfs_policy_exit(void);

#endif