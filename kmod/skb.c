#include "skb.h"

#include <linux/highmem.h>

#include "block.h"
#include "log.h"

u32 eggsfs_skb_copy(void* dstp, struct sk_buff* skb, u32 offset, u32 len) {
    struct skb_seq_state seq;
    u32 consumed = 0;
    char* dst = dstp;

    skb_prepare_seq_read(skb, offset, min(offset + len, skb->len), &seq);
    for (;;) {
        const u8* ptr;
        int avail = skb_seq_read(consumed, &ptr, &seq);
        if (avail == 0) { return consumed; }
        BUG_ON(avail < 0);
        // avail _can_ exceed the upper bound here
        int actual_avail = min((u32)avail, len-consumed);
        memcpy(dst + consumed, ptr, actual_avail);
        consumed += actual_avail;
        if (actual_avail < avail) {
            skb_abort_seq_read(&seq);
            return consumed;
        }
    }
    BUG();
}

#if 0

u32 eggsfs_skb_copy_to_pages(struct eggsfs_block_request* req, struct sk_buff* skb, u32 offset) {
    struct skb_seq_state seq;
    u32 total_consumed = 0;
    struct page* page;
    void* page_ptr;
    u32 page_offset = req->page_offset;

    if (WARN_ON(!req->bytes_left)) { return 0; }

    skb_prepare_seq_read(skb, offset, min(offset + req->bytes_left, skb->len), &seq);

    BUG_ON(list_empty(&req->pages));
    page = list_first_entry(&req->pages, struct page, lru);
    page_ptr = kmap_atomic(page);

    while (total_consumed < req->bytes_left) {
        const u8* ptr;
        int avail = skb_seq_read(total_consumed, &ptr, &seq);
        printk(KERN_INFO "eggsfs: skb_seq_read total_consumed=%u page_offset=%u avail=%d bytes_left=%u\n", total_consumed, page_offset, avail, req->bytes_left);
        if (avail == 0) {
            break;
        }

        while (avail) {
            // FIXME: is the last one needed?
            u32 this_len = min3((u32)avail, (u32)PAGE_SIZE - page_offset, req->bytes_left - total_consumed);
            printk(KERN_INFO "eggsfs: copy to page total_consumed=%u avail=%d this_len=%d page_offset=%u\n", total_consumed, avail, this_len, page_offset);
            req->crc32 = eggsfs_crc(req->crc32, ptr, this_len);
            memcpy((char*)page_ptr + page_offset, ptr, this_len);
            ptr += this_len;
            page_offset += this_len;
            avail -= this_len;
            total_consumed += this_len;

            if (page_offset >= PAGE_SIZE) {
                BUG_ON(page_offset > PAGE_SIZE);

                kunmap_atomic(page_ptr);
                list_rotate_left(&req->pages);

                page = list_first_entry(&req->pages, struct page, lru);
                page_ptr = kmap_atomic(page);
                page_offset = 0;
            }
        }
    }

    skb_abort_seq_read(&seq);

    req->bytes_left -= total_consumed;
    req->page_offset = page_offset;

    if (!req->bytes_left && page_offset) {
        // partially filled page at the end
        memset(page_ptr + page_offset, 0, PAGE_SIZE - page_offset);
        list_rotate_left(&req->pages);
    }

    kunmap_atomic(page_ptr);

    return total_consumed;
}
#endif