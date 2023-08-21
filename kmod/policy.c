#include "policy.h"

#include <linux/spinlock.h>
#include <linux/rcupdate.h>
#include <linux/hashtable.h>
#include <linux/stringhash.h>
#include <linux/slab.h>

#include "log.h"

#define POLICY_BITS 8
#define POLICY_BUCKETS (1<<POLICY_BITS) // 256
DECLARE_HASHTABLE(policies, POLICY_BITS);
spinlock_t policies_locks[POLICY_BUCKETS];

static u64 policy_key(u64 inode, u8 tag) {
    return ((u64)tag << 48) ^ inode;
}

static struct eggsfs_policy* find_policy(u64 inode, u8 tag) {
    struct eggsfs_policy* policy;
    hash_for_each_possible_rcu(policies, policy, hnode, policy_key(inode, tag)) {
        if (likely(policy->inode && inode && policy->tag == tag)) {
            return policy;
        }
    }
    return NULL;
}

struct eggsfs_policy* eggsfs_upsert_policy(u64 inode, u8 tag, char* body, int len) {
    BUG_ON(len < 0 || len > 255);

    struct eggsfs_policy* policy = find_policy(inode, tag);
    if (likely(policy != NULL)) {
        // We found one, check if we need to update.
        rcu_read_lock();
        u8 curr_len = *(u8*)policy->body;
        if (likely(curr_len == len && memcmp(body, policy->body+1, len) == 0)) { // still the same, no update needed
            rcu_read_unlock();
            return policy;
        }
        rcu_read_unlock();

        // things differ, we do need to update, first allocate and fill the body the body
        char* new_body = kmalloc(1 + len, GFP_KERNEL);
        if (new_body == NULL) {
            return ERR_PTR(-ENOMEM);
        }
        *(u8*)new_body = (u8)len;
        memcpy(new_body+1, body, len);

        // swap the pointers TODO can this be done with an atomic?
        spin_lock(&policy->lock);
        char* old_body = rcu_dereference(policy->body);
        rcu_assign_pointer(policy->body, new_body);
        spin_unlock(&policy->lock);

        // free old thing
        synchronize_rcu();
        kfree(old_body);

        // done
        return policy;
    }
    
    // We need to add a new one. Allocate both struct and body
    struct eggsfs_policy* new_policy = kmalloc(sizeof(struct eggsfs_policy), GFP_KERNEL);
    if (new_policy == NULL) {
        return ERR_PTR(-ENOMEM);
    }
    new_policy->body = kmalloc(1 + len, GFP_KERNEL);
    if (new_policy->body == NULL) {
        kfree(new_policy);
        return ERR_PTR(-ENOMEM);
    }

    new_policy->inode = inode;
    new_policy->tag = tag;
    *(u8*)new_policy->body = len;
    memcpy(new_policy->body+1, body, len);
    spin_lock_init(&new_policy->lock);

    int bucket = hash_min(policy_key(inode, tag), HASH_BITS(policies));
    spin_lock(&policies_locks[bucket]);
    // check if somebody got to it first
    policy = find_policy(inode, tag);
    if (policy != NULL) {
        // let's not bother updating to our thing in this racy case
        spin_unlock(&policies_locks[bucket]);
        kfree(new_policy->body);
        kfree(new_policy);
        return policy;
    }
    // add it
    hlist_add_head_rcu(&new_policy->hnode, &policies[bucket]);
    spin_unlock(&policies_locks[bucket]);

    return new_policy;
}

void eggsfs_get_policy_body(struct eggsfs_policy* policy, struct eggsfs_policy_body* body_struct) {
    rcu_read_lock();
    char* body = rcu_dereference(policy->body);
    body_struct->len = *(u8*)body;
    memcpy(body_struct->body, body+1, body_struct->len);
    rcu_read_unlock();
}

int eggsfs_policy_init(void) {
    int i;
    for (i = 0; i < POLICY_BUCKETS; i++) {
        spin_lock_init(&policies_locks[i]);
    }
    return 0;
}

void eggsfs_policy_exit(void) {
    int bucket;
    struct hlist_node* tmp;
    struct eggsfs_policy* policy;
    hash_for_each_safe(policies, bucket, tmp, policy, hnode) {
        kfree(policy->body);
        kfree(policy);
    }
}