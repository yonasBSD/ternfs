#include <linux/spinlock.h>
#include <linux/rcupdate.h>
#include <linux/hashtable.h>
#include <linux/stringhash.h>
#include <linux/slab.h>

#include "block_services.h"

struct eggsfs_stored_block_service {
    struct hlist_node hnode;
    spinlock_t lock;
    u64 id;
    struct eggsfs_block_service* __rcu bs;
};

#define BS_BITS 14
#define BS_BUCKETS (1<<BS_BITS) // 16k, we currently have 10k disks
static DECLARE_HASHTABLE(block_services, BS_BITS);
static spinlock_t block_services_locks[BS_BUCKETS];

static struct eggsfs_stored_block_service* find_block_service(u64 bs_id) {
    struct eggsfs_stored_block_service* bs;
    hash_for_each_possible_rcu(block_services, bs, hnode, bs_id) {
        if (likely(bs->id)) {
            return bs;
        }
    }
    return NULL;
}

struct eggsfs_stored_block_service* eggsfs_upsert_block_service(struct eggsfs_block_service* bs) {
    struct eggsfs_stored_block_service* bs_node = find_block_service(bs->id);
    if (likely(bs_node != NULL)) {
        // We found one, check if we need to update
        rcu_read_lock();
        {
            struct eggsfs_block_service* existing_bs = rcu_dereference(bs_node->bs);
            if (memcmp(bs, existing_bs, sizeof(*bs)) == 0) { // still the same, no update needed
                rcu_read_unlock();
                return bs_node;;
            }
        }
        rcu_read_unlock();

        // Things differ, we do need to update, 
        struct eggsfs_block_service* new_bs = kmalloc(sizeof(struct eggsfs_block_service), GFP_KERNEL);
        if (new_bs == NULL) {
            return ERR_PTR(-ENOMEM);
        }
        memcpy(new_bs, bs, sizeof(*bs));

        // Swap the pointers
        spin_lock(&bs_node->lock);
        struct eggsfs_block_service* old_bs = rcu_dereference_protected(bs_node->bs, lockdep_is_held(&bs_node->lock));
        rcu_assign_pointer(bs_node->bs, new_bs);
        spin_unlock(&bs_node->lock);

        // Free old thing
        synchronize_rcu();
        kfree(old_bs);
    }

    // We need to add a new one. Allocate both struct and body
    struct eggsfs_stored_block_service* new_bs_node = kmalloc(sizeof(struct eggsfs_stored_block_service), GFP_KERNEL);
    if (new_bs_node == NULL) {
        return ERR_PTR(-ENOMEM);
    }
    new_bs_node->bs = kmalloc(sizeof(struct eggsfs_block_service), GFP_KERNEL);
    if (new_bs_node->bs == NULL) {
        kfree(new_bs_node);
        return ERR_PTR(-ENOMEM);
    }

    new_bs_node->id = bs->id;
    spin_lock_init(&new_bs_node->lock);
    memcpy(new_bs_node->bs, bs, sizeof(*bs));

    // Hashing not strictly needed, the block service ids are already
    // random...
    int bucket = hash_min(bs->id, HASH_BITS(block_services));
    spin_lock(&block_services_locks[bucket]);
    // Check if somebody got to it first
    bs_node = find_block_service(bs->id);
    if (unlikely(bs_node != NULL)) {
        // Let's not bother updating to our thing in this racy case
        spin_unlock(&block_services_locks[bucket]);
        kfree(new_bs_node->bs);
        kfree(new_bs_node);
        return bs_node;
    }
    // Add it
    hlist_add_head_rcu(&new_bs_node->hnode, &block_services[bucket]);
    spin_unlock(&block_services_locks[bucket]);

    return new_bs_node;
}

void eggsfs_get_block_service(struct eggsfs_stored_block_service* bs_node, struct eggsfs_block_service* out_bs) {
    rcu_read_lock();
    struct eggsfs_block_service* bs = rcu_dereference(bs_node->bs);
    memcpy(out_bs, bs, sizeof(*bs));
    rcu_read_unlock();
}

int eggsfs_block_service_init(void) {
    int i;
    for (i = 0; i < BS_BUCKETS; i++) {
        spin_lock_init(&block_services_locks[i]);
    }
    return 0;
}

void eggsfs_block_service_exit(void) {
    int bucket;
    struct hlist_node* tmp;
    struct eggsfs_stored_block_service* bs;
    hash_for_each_safe(block_services, bucket, tmp, bs, hnode) {
        kfree(bs->bs);
        kfree(bs);
    }
}
