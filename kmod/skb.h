#ifndef _EGGSFS_SKB_H
#define _EGGSFS_SKB_H

#include <linux/skbuff.h>

struct eggsfs_block_request;

// TODO possibly move this one to blocksimple.c
u32 eggsfs_skb_copy(void* dstp, struct sk_buff* skb, u32 offset, u32 len);

#if 0
// TODO remove this thing below
u32 eggsfs_skb_copy_to_pages(struct eggsfs_block_request* req, struct sk_buff* skb, u32 offset);
#endif

#endif
