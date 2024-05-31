// This cache is never cleared -- but it is small. For 100k disks, which is
// what we're targeting, it'd be ~5MB.
#ifndef _EGGSFS_BLOCK_SERVICE_H
#define _EGGSFS_BLOCK_SERVICE_H

#include "block.h"

struct eggsfs_stored_block_service;

// Creates or updates a specific block service. Very fast unless the block service is
// unseen so far, which should be rare.
struct eggsfs_stored_block_service* eggsfs_upsert_block_service(struct eggsfs_block_service* bs);

// Gets block service.
void eggsfs_get_block_service(struct eggsfs_stored_block_service* bs_node, struct eggsfs_block_service* bs);

int __init eggsfs_block_service_init(void);
void __cold eggsfs_block_service_exit(void);

#endif