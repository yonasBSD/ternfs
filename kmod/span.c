
#include "bincode.h"
#include "common.h"
#include "span.h"
#include "metadata.h"
#include "rs.h"
#include "latch.h"
#include "crc.h"
#include "err.h"

static struct eggsfs_span* eggsfs_lookup_span(struct rb_root* spans, u64 offset) {
    struct rb_node* node = spans->rb_node;
    while (node) {
        struct eggsfs_span* span = container_of(node, struct eggsfs_span, node);
        if (offset < span->start) { node = node->rb_left; }
        else if (offset >= span->end) { node = node->rb_right; }
        else {
            eggsfs_debug_print("off=%llu span=%p", offset, span);
            return span;
        }
    }
    eggsfs_debug_print("off=%llu no_span", offset);
    return NULL;
}

static bool eggsfs_insert_span(struct rb_root* spans, struct eggsfs_span* span) {
    struct rb_node** new = &(spans->rb_node);
    struct rb_node* parent = NULL;
    while (*new) {
        struct eggsfs_span* this = container_of(*new, struct eggsfs_span, node);
        parent = *new;
        if (span->end <= this->start) { new = &((*new)->rb_left); }
        else if (span->start >= this->end) { new = &((*new)->rb_right); }
        else {
            // TODO: be loud if there are overlaping spans
            eggsfs_debug_print("span=%p (%llu-%llu) already present", span, span->start, span->end);
            return false;
        }
    }

    rb_link_node(&span->node, parent, new);
    rb_insert_color(&span->node, spans);
    eggsfs_debug_print("span=%p (%llu-%llu) inserted", span, span->start, span->end);
    return true;
}

static void eggsfs_free_span(struct eggsfs_span* span) {
    if (span->storage_class == EGGSFS_INLINE_STORAGE) {
        kfree(EGGSFS_INLINE_SPAN(span));
    } else {
        kfree(EGGSFS_BLOCK_SPAN(span));
    }
}

struct eggsfs_get_span_ctx {
    struct list_head spans;
    int err;
};

void eggsfs_file_spans_cb_span(void* data, u64 offset, u32 size, u32 crc, u8 storage_class, u8 parity, u8 stripes, u32 cell_size, const uint32_t* stripes_crcs) {
    eggsfs_debug_print("offset=%llu size=%u crc=%08x storage_class=%d parity=%d stripes=%d cell_size=%u", offset, size, crc, storage_class, parity, stripes, cell_size);

    struct eggsfs_get_span_ctx* ctx = (struct eggsfs_get_span_ctx*)data;
    if (ctx->err) { return; }

    struct eggsfs_block_span* span = kmalloc(sizeof(struct eggsfs_block_span), GFP_KERNEL);
    if (!span) { ctx->err = -ENOMEM; return; }

    if (eggsfs_data_blocks(parity) > EGGSFS_MAX_DATA || eggsfs_parity_blocks(parity) > EGGSFS_MAX_PARITY) {
        eggsfs_warn_print("D=%d > %d || P=%d > %d", eggsfs_data_blocks(parity), EGGSFS_MAX_DATA, eggsfs_data_blocks(parity), EGGSFS_MAX_PARITY);
        ctx->err = -EIO;
        return;
    }

    span->span.start = offset;
    span->span.end = offset + size;
    span->span.storage_class = storage_class;

    xa_init(&span->pages);
    int i;
    for (i = 0; i < stripes; i++) {
        eggsfs_latch_init(&span->stripe_latches[i]);
    }
    span->cell_size = cell_size;
    memcpy(span->stripes_crc, stripes_crcs, sizeof(uint32_t)*stripes);
    span->stripes = stripes;
    span->parity = parity;

    eggsfs_debug_print("adding normal span");
    list_add_tail(&span->span.lru, &ctx->spans);
}

void eggsfs_file_spans_cb_block(
    void* data, int block_ix,
    u64 bs_id, u32 ip1, u16 port1, u32 ip2, u16 port2,
    u64 block_id, u32 crc
) {
    struct eggsfs_get_span_ctx* ctx = (struct eggsfs_get_span_ctx*)data;
    if (ctx->err) { return; }

    struct eggsfs_span* span = list_last_entry(&ctx->spans, struct eggsfs_span, lru);
    struct eggsfs_block_span* block_span = EGGSFS_BLOCK_SPAN(span);

    struct eggsfs_block* block = &block_span->blocks[block_ix];
    block->bs.id = bs_id;
    block->bs.ip1 = ip1;
    block->bs.port1 = port1;
    block->bs.ip2 = ip2;
    block->bs.port2 = port2;
    block->id = block_id;
    block->crc = crc;
}

void eggsfs_file_spans_cb_inline_span(void* data, u64 offset, u32 size, u8 len, const char* body) {
    eggsfs_debug_print("offset=%llu size=%u len=%u body=%*pE", offset, size, len, len, body);

    struct eggsfs_get_span_ctx* ctx = (struct eggsfs_get_span_ctx*)data;
    if (ctx->err) { return; }
 
    struct eggsfs_inline_span* span = kmalloc(sizeof(struct eggsfs_inline_span), GFP_KERNEL);
    if (!span) { ctx->err = -ENOMEM; return; }

    span->span.start = offset;
    span->span.end = offset + size;
    span->span.storage_class = EGGSFS_INLINE_STORAGE;
    span->len = len;
    memcpy(span->body, body, len);
    eggsfs_debug_print("adding inline span");
    list_add_tail(&span->span.lru, &ctx->spans);
}

struct eggsfs_span* eggsfs_get_span(struct eggsfs_inode* enode, u64 offset) {
    unsigned seq;
    struct eggsfs_span* span;
    struct eggsfs_inode_file* file = &enode->file;

    eggsfs_debug_print("ino=%016lx, pid=%d, mu=%p, off=%llu getting span", enode->inode.i_ino, get_current()->pid, &file->spans_wlock, offset);

retry: // seqlock protects against spurious NULLs, if we get a span it is valid
    seq = read_seqcount_begin(&file->spans_seqcount);
    span = eggsfs_lookup_span(&file->spans, offset);
    if (likely(span)) { return span; }
    if (read_seqcount_retry(&file->spans_seqcount, seq)) { goto retry; }

    int err = mutex_lock_killable(&file->spans_wlock);
    if (err) { return ERR_PTR(err); }

    span = eggsfs_lookup_span(&file->spans, offset);
    if (!span) { // somebody might have gotten here first
        struct eggsfs_span* tmp;
        struct eggsfs_get_span_ctx ctx = { .err = 0, };
        INIT_LIST_HEAD(&ctx.spans);
        int err = eggsfs_error_to_linux(eggsfs_shard_file_spans(
            (struct eggsfs_fs_info*)enode->inode.i_sb->s_fs_info, enode->inode.i_ino, offset, &ctx
        ));
        err = err ?: ctx.err;
        if (unlikely(err)) {
            eggsfs_debug_print("failed to get file spans err=%d", err);
            list_for_each_entry_safe(span, tmp, &ctx.spans, lru) {
                list_del(&span->lru);
                eggsfs_free_span(span);
            }
            span = ERR_PTR(err);
            goto out;
        }

        // Need to disable preemption around seqcount_t write critical section,
        // see docs for seqcount_t.
        preempt_disable();
        write_seqcount_begin(&file->spans_seqcount);
        list_for_each_entry_safe(span, tmp, &ctx.spans, lru) {
            eggsfs_debug_print("inserting span start=%llu, end=%llu", span->start, span->end);
            if (!eggsfs_insert_span(&file->spans, span)) {
                // span already cached
                list_del(&span->lru);
                eggsfs_free_span(span);
            } else {
            }
        }
        write_seqcount_end(&file->spans_seqcount);
        preempt_enable();

        span = eggsfs_lookup_span(&file->spans, offset);

        // TODO: add to reclaimer LRU _after_ we've looked it up        
    }

out:
    mutex_unlock(&file->spans_wlock);
    return span;
}

struct page* eggsfs_get_span_page(struct eggsfs_block_span* span, u32 page_ix) {
    // this should be guaranteed by the caller but we rely on it below, so let's check
    if (span->cell_size%PAGE_SIZE != 0) {
        eggsfs_warn_print("cell_size=%u, PAGE_SIZE=%lu, span->cell_size%%PAGE_SIZE=%lu", span->cell_size, PAGE_SIZE, span->cell_size%PAGE_SIZE);
        return ERR_PTR(-EIO);
    }

    struct page* page;
    u32 start_page, end_page, curr_page;
    int err = 0;

again:
    page = xa_load(&span->pages, page_ix);
    if (unlikely(page != NULL)) { return page; }

    // We need to load the stripe
    int D = eggsfs_data_blocks(span->parity);
    u32 span_offset = page_ix * PAGE_SIZE;
    u32 stripe = span_offset / (span->cell_size*D);
    // TODO better error?
    if (stripe > span->stripes) {
        eggsfs_warn_print("span_offset=%u, stripe=%u, stripes=%u", span_offset, stripe, span->stripes);
        return ERR_PTR(-EIO);
    }

    start_page = (span->cell_size/PAGE_SIZE)*D*stripe;
    end_page = (span->cell_size/PAGE_SIZE)*D*((int)stripe + 1);

    int seqno;
    if (!eggsfs_latch_try_acquire(&span->stripe_latches[stripe], seqno)) {
        int err = eggsfs_latch_wait_killable(&span->stripe_latches[stripe], seqno);
        if (err) { return ERR_PTR(err); }
        goto again;
    }

    struct eggsfs_block_socket* socks[EGGSFS_MAX_DATA];
    memset(socks, 0, sizeof(struct eggsfs_block_socket*)*EGGSFS_MAX_DATA);
    struct eggsfs_fetch_block_request* reqs[EGGSFS_MAX_DATA];
    memset(reqs, 0, sizeof(struct eggsfs_fetch_block_request*)*EGGSFS_MAX_DATA);

    // get block services sockets
    int i;
    for (i = 0; i < D; i++) {
        socks[i] = eggsfs_get_fetch_block_socket(&span->blocks[i].bs);
        if (IS_ERR(socks[i])) {
            err = PTR_ERR(socks[i]);
            goto out_err;
        }
    }

    // fetch them all at the right offset
    u32 block_offset = stripe * span->cell_size;
    for (i = 0; i < D; i++) {
        struct eggsfs_block* block = &span->blocks[i];
        reqs[i] = eggsfs_fetch_block(socks[i], block->bs.id, block->id, block_offset, span->cell_size);
        if (IS_ERR(reqs[i])) {
            err = PTR_ERR(reqs[i]);
            goto out_err;
        }
    }

    // Wait for all the block requests
    for (i = 0; i < D; i++) {
        struct eggsfs_fetch_block_request* req = reqs[i];
        err = wait_for_completion_killable(&req->comp);
        if (err) { goto out_err; }
        err = req->err;
        if (err) {
            eggsfs_debug_print("request failed: %d", err);
            goto out_err;
        }
    }

    // Store all the pages into the xarray
    curr_page = start_page;
    u32 crc = 0;
    for (i = 0; i < D; i++) {
        struct eggsfs_fetch_block_request* req = reqs[i];
        crc = eggsfs_crc32c_append(crc, req->crc, span->cell_size);
        struct page* page;
        list_for_each_entry(page, &req->pages, lru) {
            struct page* old_page = xa_store(&span->pages, curr_page, page, GFP_KERNEL);
            if (IS_ERR(old_page)) {
                err = PTR_ERR(old_page);
                eggsfs_debug_print("xa_store failed: %d", err);
                goto out_err;
            }
            BUG_ON(old_page != NULL); // the latch protects against this
            curr_page++;
        }
    }

    // Check the crc
    if (crc != span->stripes_crc[stripe]) {
        err = -EIO;
        eggsfs_debug_print("crc check failed failed, expected %08x, got %08x", span->stripes_crc[stripe], crc);
        goto out_err;
    }

    // OK, we're good, now take ownership of the pages by emptying the pages in the requests
    // so that they won't be free by `eggsfs_put_fetch_block_request`
    for (i = 0; i < D; i++) {
        struct list_head* pages = &reqs[i]->pages;
        while (!list_empty(pages)) {
            list_del(&lru_to_page(pages)->lru);
        }
    }

    page = xa_load(&span->pages, page_ix);
    BUG_ON(page == NULL);

out:
    // Unlock and free everything
    eggsfs_latch_release(&span->stripe_latches[stripe], seqno);
    for (i = 0; i < D; i++) {
        if (reqs[i] == NULL) { continue; }
        eggsfs_put_fetch_block_request(reqs[i]);
    }
    for (i = 0; i < D; i++) {
        if (socks[i] == NULL) { continue; }
        eggsfs_put_fetch_block_socket(socks[i]);
    }
    return err == 0 ? page : ERR_PTR(err);

out_err:
    eggsfs_debug_print("getting span page failed, err=%d", err);
    // we might have partially written the stripe, forget about them
    for (curr_page = start_page; curr_page < end_page; curr_page++) {
        xa_erase(&span->pages, curr_page);
    }
    goto out;
}