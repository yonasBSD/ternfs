#include "metadata.h"

#include <linux/percpu.h>

#include "net.h"
#include "super.h"
#include "bincode.h"
#include "err.h"
#include "log.h"
#include "inode.h"
#include "crc.h"
#include "rs.h"
#include "file.h"
#include "span.h"
#include "dir.h"

int eggsfs_mtu = EGGSFS_DEFAULT_MTU;
int eggsfs_default_mtu = EGGSFS_DEFAULT_MTU;
int eggsfs_max_mtu = EGGSFS_MAX_MTU;

static DEFINE_PER_CPU(u64, next_request_id);

static inline u64 alloc_request_id(void) {
    return this_cpu_add_return(next_request_id, 1024);
}

#define EGGSFS_SHARD_HEADER_SIZE (4 + 8 + 1) // protocol, reqId, kind

static void eggsfs_put_shard_header(struct eggsfs_bincode_put_ctx* ctx, u64 req_id, u8 kind) {
    eggsfs_bincode_put_u32(ctx, EGGSFS_SHARD_REQ_PROTOCOL_VERSION);
    eggsfs_bincode_put_u64(ctx, req_id);
    eggsfs_bincode_put_u8(ctx, kind);
}

static void eggsfs_read_shard_header(struct eggsfs_bincode_get_ctx* ctx, u8 kind) {
    u32 read_protocol = eggsfs_bincode_get_u32(ctx);
    eggsfs_bincode_get_u64(ctx); // req id, we already know it's right since otherwise we wouldn't have found the response
    u8 read_kind = eggsfs_bincode_get_u8(ctx);
    if (likely(ctx->err == 0)) {
        if (unlikely(
            read_protocol != EGGSFS_SHARD_RESP_PROTOCOL_VERSION ||
            (read_kind != 0 && read_kind != kind)
        )) {
            // eggsfs_debug("protocol=%u read_protocol=%u req_id=%llu read_req_id=%llu kind=%d read_kind=%d", read_protocol, EGGSFS_SHARD_RESP_PROTOCOL_VERSION, req_id, read_req_id, (int)kind, (int)read_kind);
            ctx->err = EGGSFS_ERR_MALFORMED_RESPONSE;
        } else if (unlikely(read_kind == 0)) {
            ctx->err = eggsfs_bincode_get_u16(ctx);
            eggsfs_debug("err=%d", ctx->err);
        }
    }
}

#define PREPARE_SHARD_REQ_CTX_INNER(sz) \
    eggsfs_debug("req_id=%llu, kind=%d", req_id, (int)kind); \
    struct eggsfs_bincode_put_ctx ctx = { \
        .start = req, \
        .cursor = req, \
        .end = req + EGGSFS_SHARD_HEADER_SIZE + sz, \
    }; \
    eggsfs_put_shard_header(&ctx, req_id, kind)

#define PREPARE_SHARD_REQ_CTX(sz) \
    char req[EGGSFS_SHARD_HEADER_SIZE + sz]; \
    PREPARE_SHARD_REQ_CTX_INNER(sz)

static void prepare_resp_ctx(struct sk_buff* skb, struct eggsfs_bincode_get_ctx* ctx) {
    ctx->err = 0;
    // if the skb is linear (super common case) just use the
    // skb buffer directly.
    //
    // otherwise we allocate enough space and copy in one pass -- we deem
    // this more desirable than calling `skb_copy_bits` many times when deserializing,
    // especially since I'd expect nonlinear skbs to appear only for large messages.
    if (unlikely(skb_is_nonlinear(skb))) {
        ctx->buf = kmalloc(skb->len, GFP_KERNEL);
        if (unlikely(ctx->buf == NULL)) {
            ctx->err = -ENOMEM;
            ctx->owned = NULL;
            ctx->end = NULL;
            return;
        } else {
            ctx->owned = ctx->buf;
            BUG_ON(skb_copy_bits(skb, 0, ctx->buf, skb->len) != 0);
        }
    } else {
        ctx->buf = skb->data;
        ctx->owned = NULL;
    }
    ctx->end = ctx->buf + skb->len;
}

static void prepare_shard_resp_ctx(struct sk_buff* skb, u8 kind, struct eggsfs_bincode_get_ctx* ctx) {
    prepare_resp_ctx(skb, ctx);
    if (likely(ctx->err == 0)) {
        eggsfs_read_shard_header(ctx, kind);
    };
}

#define PREPARE_SHARD_RESP_CTX() \
    struct eggsfs_bincode_get_ctx ctx; \
    prepare_shard_resp_ctx(skb, kind, &ctx);

static int finish_resp(struct sk_buff* skb, u8 kind, struct eggsfs_bincode_get_ctx* ctx) {
    if (unlikely(ctx->owned)) {
        kfree(ctx->owned);
        ctx->owned = NULL;
    }
    consume_skb(skb);
    if (unlikely(ctx->err != 0)) {
        eggsfs_debug("resp of kind %02x failed with err %d", kind, ctx->err);
        return ctx->err;
    }
    return 0;
}

#define FINISH_RESP() { \
        int err = finish_resp(skb, kind, &ctx); \
        if (err != 0) { return err; } \
    }


static struct sk_buff* eggsfs_send_shard_req(struct eggsfs_fs_info* info, int shid, u64 req_id, struct eggsfs_bincode_put_ctx* ctx, u32* attempts) {
    return eggsfs_metadata_request(&info->sock, shid, req_id, ctx->start, ctx->cursor-ctx->start, &info->shard_addrs1[shid], &info->shard_addrs2[shid], attempts);
}

#define EGGSFS_CDC_HEADER_SIZE (4 + 8 + 1) // protocol, reqId, kind

static void eggsfs_put_cdc_header(struct eggsfs_bincode_put_ctx* ctx, u64 req_id, u8 kind) {
    eggsfs_bincode_put_u32(ctx, EGGSFS_CDC_REQ_PROTOCOL_VERSION);
    eggsfs_bincode_put_u64(ctx, req_id);
    eggsfs_bincode_put_u8(ctx, kind);
}

static void eggsfs_read_cdc_header(struct eggsfs_bincode_get_ctx* ctx, u64 req_id, u8 kind) {
    u32 read_protocol = eggsfs_bincode_get_u32(ctx);
    u64 read_req_id = eggsfs_bincode_get_u64(ctx);
    u8 read_kind = eggsfs_bincode_get_u8(ctx);
    if (likely(ctx->err == 0)) {
        if (unlikely(
            read_protocol != EGGSFS_CDC_RESP_PROTOCOL_VERSION ||
            read_req_id != req_id ||
            (read_kind != 0 && read_kind != kind)
        )) {
            eggsfs_debug("protocol=%u read_protocol=%u req_id=%llu read_req_id=%llu kind=%d read_kind=%d", read_protocol, EGGSFS_SHARD_RESP_PROTOCOL_VERSION, req_id, read_req_id, (int)kind, (int)read_kind);
            ctx->err = EGGSFS_ERR_MALFORMED_RESPONSE;
        } else if (unlikely(read_kind == 0)) {
            ctx->err = eggsfs_bincode_get_u16(ctx);
        }
    }
}

#define PREPARE_CDC_REQ_CTX(sz) \
    eggsfs_debug("req_id=%llu, kind=%d", req_id, (int)kind); \
    char req[EGGSFS_CDC_HEADER_SIZE + sz]; \
    struct eggsfs_bincode_put_ctx ctx = { \
        .start = req, \
        .cursor = req, \
        .end = req + sizeof(req), \
    }; \
    eggsfs_put_cdc_header(&ctx, req_id, kind)

static void prepare_cdc_resp_ctx(struct sk_buff* skb, u64 req_id, u8 kind, struct eggsfs_bincode_get_ctx* ctx) {
    prepare_resp_ctx(skb, ctx);
    if (likely(ctx->err == 0)) {
        eggsfs_read_cdc_header(ctx, req_id, kind);
    }
}

#define PREPARE_CDC_RESP_CTX() \
    struct eggsfs_bincode_get_ctx ctx; \
    prepare_cdc_resp_ctx(skb, req_id, kind, &ctx);

static struct sk_buff* eggsfs_send_cdc_req(struct eggsfs_fs_info* info, u64 req_id, struct eggsfs_bincode_put_ctx* ctx, u32* attempts) {
    return eggsfs_metadata_request(&info->sock, -1, req_id, ctx->start, ctx->cursor-ctx->start, &info->cdc_addr1, &info->cdc_addr2, attempts);
}

int eggsfs_shard_lookup(struct eggsfs_fs_info* info, u64 dir, const char* name, int name_len, u64* ino, u64* creation_time) {
    eggsfs_debug("dir=0x%016llx, name=%*pE", dir, name_len, name);

    struct sk_buff* skb;
    u32 attempts;
    u64 req_id = alloc_request_id();
    u8 kind = EGGSFS_SHARD_LOOKUP;
    {
        PREPARE_SHARD_REQ_CTX(EGGSFS_LOOKUP_REQ_MAX_SIZE);
        eggsfs_lookup_req_put_start(&ctx, start);
        eggsfs_lookup_req_put_dir_id(&ctx, start, dir_id, dir);
        eggsfs_lookup_req_put_name(&ctx, dir_id, put_name, name, name_len);
        eggsfs_lookup_req_put_end(ctx, put_name, end);
        skb = eggsfs_send_shard_req(info, eggsfs_inode_shard(dir), req_id, &ctx, &attempts);
        if (IS_ERR(skb)) { return PTR_ERR(skb); }
    }

    {
        PREPARE_SHARD_RESP_CTX();
        eggsfs_lookup_resp_get_start(&ctx, start);
        eggsfs_lookup_resp_get_target_id(&ctx, start, target_id);
        eggsfs_lookup_resp_get_creation_time(&ctx, target_id, resp_creation_time);
        eggsfs_lookup_resp_get_end(&ctx, resp_creation_time, end);
        eggsfs_lookup_resp_get_finish(&ctx, end);
        FINISH_RESP();
        *ino = target_id.x;
        *creation_time = resp_creation_time.x;
    }

    eggsfs_debug("ino=0x%016llx, creation_time=%llu", *ino, *creation_time);

    return 0;
}

int eggsfs_shard_readdir(struct eggsfs_fs_info* info, u64 dir, u64 start_pos, void* data, u64* next_hash) {
    struct sk_buff* skb;
    u32 attempts;
    u64 req_id = alloc_request_id();
    u8 kind = EGGSFS_SHARD_READ_DIR;
    {
        PREPARE_SHARD_REQ_CTX(EGGSFS_READ_DIR_REQ_SIZE);
        eggsfs_read_dir_req_put_start(&ctx, start);
        eggsfs_read_dir_req_put_dir_id(&ctx, start, dir_id, dir);
        eggsfs_read_dir_req_put_start_hash(&ctx, dir_id, start_hash, start_pos);
        eggsfs_read_dir_req_put_mtu(&ctx, start_hash, mtu, eggsfs_mtu);
        eggsfs_read_dir_req_put_end(ctx, mtu, end);
        skb = eggsfs_send_shard_req(info, eggsfs_inode_shard(dir), req_id, &ctx, &attempts);
        if (IS_ERR(skb)) { return PTR_ERR(skb); }
    }

    {
        PREPARE_SHARD_RESP_CTX();
        eggsfs_read_dir_resp_get_start(&ctx, start);
        eggsfs_read_dir_resp_get_next_hash(&ctx, start, next_hash_resp);
        eggsfs_read_dir_resp_get_results(&ctx, next_hash_resp, results);
        int i;
        for (i = 0; i < results.len; i++) {
            eggsfs_current_edge_get_start(&ctx, edge_start);
            eggsfs_current_edge_get_target_id(&ctx, edge_start, target_id);
            eggsfs_current_edge_get_name_hash(&ctx, target_id, name_hash);
            eggsfs_current_edge_get_name(&ctx, name_hash, name);
            eggsfs_current_edge_get_creation_time(&ctx, name, creation_time);
            eggsfs_current_edge_get_end(&ctx, creation_time, end);
            eggsfs_bincode_get_finish_list_el(end);
            if (likely(ctx.err == 0)) {
                int err = eggsfs_dir_readdir_entry_cb(data, name_hash.x, name.str.buf, name.str.len, creation_time.x, target_id.x);
                if (err) {
                    consume_skb(skb);
                    return err;
                }
            }
        }
        eggsfs_read_dir_resp_get_end(&ctx, results, end);
        eggsfs_read_dir_resp_get_finish(&ctx, end);
        FINISH_RESP();
        *next_hash = next_hash_resp.x;
    }

    return 0;
}

static bool check_deleted_edge(
    struct eggsfs_fs_info* info, u64 dir, u64 target, const char* name, int name_len, u64 creation_time, bool owned
) {
    int i;
    struct sk_buff* skb;
    u32 attempts;
    u64 req_id = alloc_request_id();
    u8 kind = EGGSFS_SHARD_FULL_READ_DIR;
    {
        PREPARE_SHARD_REQ_CTX(EGGSFS_FULL_READ_DIR_REQ_MAX_SIZE);
        eggsfs_full_read_dir_req_put_start(&ctx, start);
        eggsfs_full_read_dir_req_put_dir_id(&ctx, start, dir_req, dir);
        eggsfs_full_read_dir_req_put_flags(&ctx, dir_req, flags, EGGSFS_FULL_READ_DIR_BACKWARDS|EGGSFS_FULL_READ_DIR_CURRENT|EGGSFS_FULL_READ_DIR_SAME_NAME);
        eggsfs_full_read_dir_req_put_start_name(&ctx, flags, name_req, name, name_len);
        eggsfs_full_read_dir_req_put_start_time(&ctx, name_req, start_time, 0);
        eggsfs_full_read_dir_req_put_limit(&ctx, start_time, limit, 2);
        eggsfs_full_read_dir_req_put_mtu(&ctx, limit, mtu, eggsfs_mtu);
        eggsfs_full_read_dir_req_put_end(&ctx, mtu, end);
        skb = eggsfs_send_shard_req(info, eggsfs_inode_shard(dir), req_id, &ctx, &attempts);
        if (IS_ERR(skb)) { return false; }
    }

    bool good = true;
    {
        PREPARE_SHARD_RESP_CTX();
        eggsfs_full_read_dir_resp_get_start(&ctx, start);
        eggsfs_full_read_dir_resp_get_next(&ctx, start, cursor);
        eggsfs_full_read_dir_cursor_get_current(&ctx, cursor, cur_current);
        eggsfs_full_read_dir_cursor_get_start_name(&ctx, cur_current, cur_start_name);
        eggsfs_full_read_dir_cursor_get_start_time(&ctx, cur_start_name, cur_start_time);
        eggsfs_full_read_dir_cursor_get_end(&ctx, cur_start_time, cur_end);
        eggsfs_full_read_dir_resp_get_results(&ctx, cur_end, results_len);
        if (results_len.len != 2) { good = false; }
        for (i = 0; i < results_len.len; i++) {
            eggsfs_edge_get_start(&ctx, start);
            eggsfs_edge_get_current(&ctx, start, edge_current);
            eggsfs_edge_get_target_id(&ctx, edge_current, edge_target);
            eggsfs_edge_get_name_hash(&ctx, edge_target, edge_name_hash);
            eggsfs_edge_get_name(&ctx, edge_name_hash, edge_name);
            eggsfs_edge_get_creation_time(&ctx, edge_name, edge_creation_time);
            eggsfs_edge_get_end(&ctx, edge_creation_time, end);
            eggsfs_bincode_get_finish_list_el(end);
            if (
                likely(ctx.err == 0) &&
                i == 1 && // this is the old edge
                (
                    edge_current.x ||
                    EGGSFS_GET_EXTRA(edge_target.x) != owned ||
                    EGGSFS_GET_EXTRA_ID(edge_target.x) != target ||
                    edge_creation_time.x != creation_time
                )
            ) {
                good = false;
            }
            if (
                likely(ctx.err == 0) &&
                i == 0 && // this is the deleted edge
                EGGSFS_GET_EXTRA_ID(edge_target.x) != 0
            ) {
                good = false;
            }
        }
        eggsfs_full_read_dir_resp_get_end(&ctx, results_len, end);
        eggsfs_full_read_dir_resp_get_finish(&ctx, end);
        consume_skb(skb);
        if (ctx.err != 0) { return false; }
    }

    return good;
}

static bool check_new_edge_after_rename(
    struct eggsfs_fs_info* info, u64 dir, u64 target, const char* name, int name_len, u64* creation_time
) {
    struct sk_buff* skb;
    u32 attempts;
    u64 req_id = alloc_request_id();
    u8 kind = EGGSFS_SHARD_LOOKUP;
    {
        PREPARE_SHARD_REQ_CTX(EGGSFS_LOOKUP_REQ_MAX_SIZE);
        eggsfs_lookup_req_put_start(&ctx, start);
        eggsfs_lookup_req_put_dir_id(&ctx, start, dir_req, dir);
        eggsfs_lookup_req_put_name(&ctx, dir_req, name_req, name, name_len);
        eggsfs_lookup_req_put_end(&ctx, name_req, end);
        skb = eggsfs_send_shard_req(info, eggsfs_inode_shard(dir), req_id, &ctx, &attempts);
        if (IS_ERR(skb)) { return false; }
    }

    {
        PREPARE_SHARD_RESP_CTX();
        eggsfs_lookup_resp_get_start(&ctx, start);
        eggsfs_lookup_resp_get_target_id(&ctx, start, resp_target);
        eggsfs_lookup_resp_get_creation_time(&ctx, resp_target, resp_creation_time);
        eggsfs_lookup_resp_get_end(&ctx, resp_creation_time, end);
        eggsfs_lookup_resp_get_finish(&ctx, end);
        consume_skb(skb);
        if (ctx.err != 0) { return false; }
        if (resp_target.x != target) { return false; }
        *creation_time = resp_creation_time.x;
    }

    return true;
}

int eggsfs_shard_soft_unlink_file(struct eggsfs_fs_info* info, u64 dir, u64 file, const char* name, int name_len, u64 creation_time, u64* delete_creation_time) {
    eggsfs_debug("unlink dir=%016llx file=%016llx name=%*s creation_time=%llu", dir, file, name_len, name, creation_time);
    struct sk_buff* skb;
    u32 attempts;
    u64 req_id = alloc_request_id();
    u8 kind = EGGSFS_SHARD_SOFT_UNLINK_FILE;
    {
        PREPARE_SHARD_REQ_CTX(EGGSFS_SOFT_UNLINK_FILE_REQ_MAX_SIZE);
        eggsfs_soft_unlink_file_req_put_start(&ctx, start);
        eggsfs_soft_unlink_file_req_put_owner_id(&ctx, start, owner_id, dir);
        eggsfs_soft_unlink_file_req_put_file_id(&ctx, owner_id, file_id, file);
        eggsfs_soft_unlink_file_req_put_name(&ctx, file_id, req_name, name, name_len);
        eggsfs_soft_unlink_file_req_put_creation_time(&ctx, req_name, req_creation_time, creation_time);
        eggsfs_soft_unlink_file_req_put_end(&ctx, req_creation_time, end);
        skb = eggsfs_send_shard_req(info, eggsfs_inode_shard(dir), req_id, &ctx, &attempts);
        if (IS_ERR(skb)) { return PTR_ERR(skb); }
    }

    {
        PREPARE_SHARD_RESP_CTX();
        eggsfs_soft_unlink_file_resp_get_start(&ctx, start);
        eggsfs_soft_unlink_file_resp_get_delete_creation_time(&ctx, start, delete_creation_time_resp);
        eggsfs_soft_unlink_file_resp_get_end(&ctx, delete_creation_time_resp, end);
        eggsfs_soft_unlink_file_resp_get_finish(&ctx, end);
        if (attempts > 1 && ctx.err == EGGSFS_ERR_EDGE_NOT_FOUND) {
            eggsfs_debug("got edge not found, performing followup checks");
            // See commentary in shardreq.go
            if (check_deleted_edge(info, dir, file, name, name_len, creation_time, true)) {
                ctx.err = 0;
            }
        }
        FINISH_RESP();
        *delete_creation_time = delete_creation_time_resp.x;
    }


    return 0;
}

int eggsfs_shard_rename(
    struct eggsfs_fs_info* info,
    u64 dir, u64 target, const char* old_name, int old_name_len, u64 old_creation_time, const char* new_name, int new_name_len, u64* new_creation_time
) {
    struct sk_buff* skb;
    u32 attempts;
    u64 req_id = alloc_request_id();
    u8 kind = EGGSFS_SHARD_SAME_DIRECTORY_RENAME;
    {
        PREPARE_SHARD_REQ_CTX(EGGSFS_SAME_DIRECTORY_RENAME_REQ_MAX_SIZE);
        eggsfs_same_directory_rename_req_put_start(&ctx, start);
        eggsfs_same_directory_rename_req_put_target_id(&ctx, start, target_id, target);
        eggsfs_same_directory_rename_req_put_dir_id(&ctx, target_id, dir_id, dir);
        eggsfs_same_directory_rename_req_put_old_name(&ctx, dir_id, req_old_name, old_name, old_name_len);
        eggsfs_same_directory_rename_req_put_old_creation_time(&ctx, req_old_name, req_old_creation_time, old_creation_time);
        eggsfs_same_directory_rename_req_put_new_name(&ctx, req_old_creation_time, req_new_name, new_name, new_name_len);
        skb = eggsfs_send_shard_req(info, eggsfs_inode_shard(dir), req_id, &ctx, &attempts);
        if (IS_ERR(skb)) { return PTR_ERR(skb); }
    }

    {
        PREPARE_SHARD_RESP_CTX();
        eggsfs_same_directory_rename_resp_get_start(&ctx, start);
        eggsfs_same_directory_rename_resp_get_new_creation_time(&ctx, start, resp_new_creation_time);
        eggsfs_same_directory_rename_resp_get_end(&ctx, resp_new_creation_time, end);
        eggsfs_same_directory_rename_resp_get_finish(&ctx, end);
        bool recovered = false;
        if (attempts > 1 && ctx.err == EGGSFS_ERR_EDGE_NOT_FOUND) {
            eggsfs_debug("got edge not found, performing followup checks");
            // See commentary in shardreq.go
            if (
                check_deleted_edge(info, dir, target, old_name, old_name_len, old_creation_time, false) &&
                check_new_edge_after_rename(info, dir, target, new_name, new_name_len, new_creation_time)
            ) {
                recovered = true;
                ctx.err = 0;
            }
        }
        FINISH_RESP();

        if (!recovered) { // otherwise it's set by `check_new_edge_after_rename`
            *new_creation_time = resp_new_creation_time.x;
        }
    }

    return 0;
}

int eggsfs_shard_link_file(struct eggsfs_fs_info* info, u64 file, u64 cookie, u64 dir, const char* name, int name_len, u64* creation_time) {
    struct sk_buff* skb;
    u32 attempts;

    u64 req_id = alloc_request_id();
    u8 kind = EGGSFS_SHARD_LINK_FILE;
    {
        PREPARE_SHARD_REQ_CTX(EGGSFS_LINK_FILE_REQ_MAX_SIZE);
        eggsfs_link_file_req_put_start(&ctx, start);
        eggsfs_link_file_req_put_file_id(&ctx, start, file_id, file);
        eggsfs_link_file_req_put_cookie(&ctx, file_id, req_cookie, cookie);
        eggsfs_link_file_req_put_owner_id(&ctx, req_cookie, owner_id, dir);
        eggsfs_link_file_req_put_name(&ctx, owner_id, end, name, name_len);
        skb = eggsfs_send_shard_req(info, eggsfs_inode_shard(file), req_id, &ctx, &attempts);
        if (IS_ERR(skb)) { return PTR_ERR(skb); }
    }

    {
        PREPARE_SHARD_RESP_CTX();
        eggsfs_link_file_resp_get_start(&ctx, start);
        eggsfs_link_file_resp_get_creation_time(&ctx, start, resp_creation_time);
        eggsfs_link_file_resp_get_end(&ctx, resp_creation_time, end);
        eggsfs_link_file_resp_get_finish(&ctx, end);
        FINISH_RESP();
        *creation_time = resp_creation_time.x;
    }

    return 0;
}

int eggsfs_shard_async_getattr_dir(
    struct eggsfs_fs_info* info,
    struct eggsfs_metadata_request* metadata_req,
    u64 dir
) {
    struct eggsfs_metadata_request_state state;

    u64 req_id = alloc_request_id();
    u32 shid = eggsfs_inode_shard(dir);

    metadata_req->flags = EGGSFS_METADATA_REQUEST_ASYNC_GETATTR;
    metadata_req->request_id = req_id;
    metadata_req->shard = shid;

    u8 kind = EGGSFS_SHARD_STAT_DIRECTORY;
    {
        PREPARE_SHARD_REQ_CTX(EGGSFS_STAT_DIRECTORY_REQ_SIZE);
        eggsfs_stat_directory_req_put_start(&ctx, start);
        eggsfs_stat_directory_req_put_id(&ctx, start, dir_id, dir);
        eggsfs_stat_directory_req_put_end(ctx, dir_id, end);
        eggsfs_metadata_request_init(&info->sock, metadata_req, &state);
        int err = eggsfs_metadata_send_request(&info->sock, &info->shard_addrs1[shid], &info->shard_addrs2[shid], metadata_req, ctx.start, ctx.cursor-ctx.start, &state);
        if (err) { return err; }
    }

    return 0;
}

int eggsfs_shard_parse_getattr_dir(
    struct sk_buff* skb,
    u64* mtime,
    u64* owner,
    struct eggsfs_policy_body* block_policies,
    struct eggsfs_policy_body* span_policies,
    struct eggsfs_policy_body* stripe_policy
) {
    block_policies->len = 0;
    span_policies->len = 0;
    stripe_policy->len = 0;

    u8 kind = EGGSFS_SHARD_STAT_DIRECTORY;
    PREPARE_SHARD_RESP_CTX();
    eggsfs_stat_directory_resp_get_start(&ctx, start);
    eggsfs_stat_directory_resp_get_mtime(&ctx, start, resp_mtime);
    eggsfs_stat_directory_resp_get_owner(&ctx, resp_mtime, resp_owner);
    eggsfs_stat_directory_resp_get_info(&ctx, resp_owner, resp_info);
    eggsfs_directory_info_get_entries(&ctx, resp_info, entries);
    int i;
    for (i = 0; i < entries.len; i++) {
        eggsfs_directory_info_entry_get_start(&ctx, entry_start);
        eggsfs_directory_info_entry_get_tag(&ctx, entry_start, tag);
        eggsfs_directory_info_entry_get_body(&ctx, tag, body);
        eggsfs_directory_info_entry_get_end(&ctx, body, entry_end);
        eggsfs_bincode_get_finish_list_el(entry_end);
        if (tag.x == BLOCK_POLICY_TAG) {
            block_policies->len = body.str.len;
            memcpy(block_policies->body, body.str.buf, body.str.len);
        }
        if (tag.x == SPAN_POLICY_TAG) {
            span_policies->len = body.str.len;
            memcpy(span_policies->body, body.str.buf, body.str.len);
        }
        if (tag.x == STRIPE_POLICY_TAG) {
            stripe_policy->len = body.str.len;
            memcpy(stripe_policy->body, body.str.buf, body.str.len);
        }
    }
    eggsfs_directory_info_get_end(&ctx, entries, info_end);
    eggsfs_stat_directory_resp_get_end(&ctx, info_end, end);
    eggsfs_stat_directory_resp_get_finish(&ctx, end);
    FINISH_RESP();
    *mtime = resp_mtime.x;
    *owner = resp_owner.x;

    return 0;
}

int eggsfs_shard_getattr_dir(
    struct eggsfs_fs_info* info,
    u64 dir,
    u64* mtime,
    u64* owner,
    struct eggsfs_policy_body* block_policies,
    struct eggsfs_policy_body* span_policies,
    struct eggsfs_policy_body* stripe_policy
) {
    struct sk_buff* skb;
    u32 attempts;

    u64 req_id = alloc_request_id();
    u8 kind = EGGSFS_SHARD_STAT_DIRECTORY;
    {
        PREPARE_SHARD_REQ_CTX(EGGSFS_STAT_DIRECTORY_REQ_SIZE);
        eggsfs_stat_directory_req_put_start(&ctx, start);
        eggsfs_stat_directory_req_put_id(&ctx, start, dir_id, dir);
        eggsfs_stat_directory_req_put_end(ctx, dir_id, end);
        skb = eggsfs_send_shard_req(info, eggsfs_inode_shard(dir), req_id, &ctx, &attempts);
        if (IS_ERR(skb)) { return PTR_ERR(skb); }
    }

    return eggsfs_shard_parse_getattr_dir(skb, mtime, owner, block_policies, span_policies, stripe_policy);
}

int eggsfs_shard_async_getattr_file(struct eggsfs_fs_info* info, struct eggsfs_metadata_request* metadata_req, u64 file) {
    struct eggsfs_metadata_request_state state;

    u64 req_id = alloc_request_id();

    u32 shid = eggsfs_inode_shard(file);

    metadata_req->flags = EGGSFS_METADATA_REQUEST_ASYNC_GETATTR;
    metadata_req->request_id = req_id;
    metadata_req->shard = shid;

    u8 kind = EGGSFS_SHARD_STAT_FILE;
    {
        PREPARE_SHARD_REQ_CTX(EGGSFS_STAT_FILE_REQ_SIZE);
        eggsfs_stat_file_req_put_start(&ctx, start);
        eggsfs_stat_file_req_put_id(&ctx, start, file_id, file);
        eggsfs_stat_file_req_put_end(ctx, file_id, end);
        eggsfs_metadata_request_init(&info->sock, metadata_req, &state);
        int err = eggsfs_metadata_send_request(&info->sock, &info->shard_addrs1[shid], &info->shard_addrs2[shid], metadata_req, ctx.start, ctx.cursor-ctx.start, &state);
        if (err) { return err; }
    }

    return 0;
}

int eggsfs_shard_parse_getattr_file(struct sk_buff* skb, u64* mtime, u64* atime, u64* size) {
    u8 kind = EGGSFS_SHARD_STAT_FILE;

    PREPARE_SHARD_RESP_CTX();
    eggsfs_stat_file_resp_get_start(&ctx, start);
    eggsfs_stat_file_resp_get_mtime(&ctx, start, resp_mtime);
    eggsfs_stat_file_resp_get_atime(&ctx, resp_mtime, resp_atime);
    eggsfs_stat_file_resp_get_size(&ctx, resp_atime, resp_size);
    eggsfs_stat_file_resp_get_end(&ctx, resp_size, end);
    eggsfs_stat_file_resp_get_finish(&ctx, end);
    FINISH_RESP();
    *mtime = resp_mtime.x;
    *atime = resp_atime.x;
    *size = resp_size.x;

    return 0;
}

int eggsfs_shard_getattr_file(struct eggsfs_fs_info* info, u64 file, u64* mtime, u64* atime, u64* size) {
    struct sk_buff* skb;
    u32 attempts;

    u64 req_id = alloc_request_id();
    u8 kind = EGGSFS_SHARD_STAT_FILE;
    {
        PREPARE_SHARD_REQ_CTX(EGGSFS_STAT_FILE_REQ_SIZE);
        eggsfs_stat_file_req_put_start(&ctx, start);
        eggsfs_stat_file_req_put_id(&ctx, start, file_id, file);
        eggsfs_stat_file_req_put_end(ctx, file_id, end);
        skb = eggsfs_send_shard_req(info, eggsfs_inode_shard(file), req_id, &ctx, &attempts);
        if (IS_ERR(skb)) { return PTR_ERR(skb); }
    }

    return eggsfs_shard_parse_getattr_file(skb, mtime, atime, size);
}

int eggsfs_shard_create_file(struct eggsfs_fs_info* info, u8 shid, int itype, const char* name, int name_len, u64* ino, u64* cookie) {
    struct sk_buff* skb;
    u32 attempts;

    u64 req_id = alloc_request_id();
    u8 kind = EGGSFS_SHARD_CONSTRUCT_FILE;
    {
        PREPARE_SHARD_REQ_CTX(EGGSFS_CONSTRUCT_FILE_REQ_MAX_SIZE);
        eggsfs_construct_file_req_put_start(&ctx, start);
        eggsfs_construct_file_req_put_type(&ctx, start, type, itype);
        eggsfs_construct_file_req_put_note(&ctx, type, note, name, name_len);
        eggsfs_construct_file_req_put_end(&ctx, note, end);
        skb = eggsfs_send_shard_req(info, shid, req_id, &ctx, &attempts);
        if (IS_ERR(skb)) { return PTR_ERR(skb); }
    }

    {
        PREPARE_SHARD_RESP_CTX();
        eggsfs_construct_file_resp_get_start(&ctx, start);
        eggsfs_construct_file_resp_get_id(&ctx, start, resp_id);
        eggsfs_construct_file_resp_get_cookie(&ctx, resp_id, resp_cookie);
        eggsfs_construct_file_resp_get_end(&ctx, resp_cookie, end);
        eggsfs_construct_file_resp_get_finish(&ctx, end);
        FINISH_RESP();
        *ino = resp_id.x;
        *cookie = resp_cookie.x;
    }

    return 0;
}

int eggsfs_shard_add_inline_span(struct eggsfs_fs_info* info, u64 file, u64 cookie, u64 offset, u32 size, const char* data, u8 len) {
    BUG_ON(size < len); // this never makes sense
    u32 crc = eggsfs_crc32c_simple(0, data, len);
    crc = eggsfs_crc32c_zero_extend(crc, size - len);

    struct sk_buff* skb;
    u32 attempts;
    u64 req_id = alloc_request_id();
    u8 kind = EGGSFS_SHARD_ADD_INLINE_SPAN;
    {
        PREPARE_SHARD_REQ_CTX(EGGSFS_ADD_INLINE_SPAN_REQ_MAX_SIZE);
        eggsfs_add_inline_span_req_put_start(&ctx, start);
        eggsfs_add_inline_span_req_put_file_id(&ctx, start, file_id, file);
        eggsfs_add_inline_span_req_put_cookie(&ctx, file_id, req_cookie, cookie);
        eggsfs_add_inline_span_req_put_storage_class(&ctx, req_cookie, req_storage_class, EGGSFS_INLINE_STORAGE);
        eggsfs_add_inline_span_req_put_byte_offset(&ctx, req_storage_class, req_byte_offset, offset);
        eggsfs_add_inline_span_req_put_size(&ctx, req_byte_offset, req_size, size);
        eggsfs_add_inline_span_req_put_crc(&ctx, req_size, req_crc, crc);
        eggsfs_add_inline_span_req_put_body(&ctx, req_crc, req_body, data, len);
        eggsfs_add_inline_span_req_put_end(&ctx, req_body, end);
        skb = eggsfs_send_shard_req(info, eggsfs_inode_shard(file), req_id, &ctx, &attempts);
        if (IS_ERR(skb)) { return PTR_ERR(skb); }
    }

    {
        PREPARE_SHARD_RESP_CTX();
        eggsfs_add_inline_span_resp_get_start(&ctx, start);
        eggsfs_add_inline_span_resp_get_end(&ctx, start, end);
        eggsfs_add_inline_span_resp_get_finish(&ctx, end);
        FINISH_RESP();
    }

    return 0;
}

int eggsfs_shard_set_time(struct eggsfs_fs_info* info, u64 file, u64 mtime, u64 atime) {
    struct sk_buff* skb;
    u32 attempts;
    u64 req_id = alloc_request_id();
    u8 kind = EGGSFS_SHARD_SET_TIME;
    {
        PREPARE_SHARD_REQ_CTX(EGGSFS_SET_TIME_REQ_SIZE);
        eggsfs_set_time_req_put_start(&ctx, start);
        eggsfs_set_time_req_put_id(&ctx, start, file_id, file);
        eggsfs_set_time_req_put_mtime(&ctx, file_id, mtime_req, mtime);
        eggsfs_set_time_req_put_atime(&ctx, mtime_req, atime_req, atime);
        eggsfs_set_time_req_put_end(&ctx, atime_req, end);
        skb = eggsfs_send_shard_req(info, eggsfs_inode_shard(file), req_id, &ctx, &attempts);
        if (IS_ERR(skb)) { return PTR_ERR(skb); }
    }

    {
        PREPARE_SHARD_RESP_CTX();
        eggsfs_set_time_resp_get_start(&ctx, start);
        eggsfs_set_time_resp_get_end(&ctx, start, end);
        eggsfs_set_time_resp_get_finish(&ctx, end);
        FINISH_RESP();
    }

    return 0;
}

int eggsfs_shard_set_atime_nowait(struct eggsfs_fs_info* info, u64 file, u64 atime) {
    u64 req_id = alloc_request_id();
    u8 kind = EGGSFS_SHARD_SET_TIME;

    PREPARE_SHARD_REQ_CTX(EGGSFS_SET_TIME_REQ_SIZE);
    eggsfs_set_time_req_put_start(&ctx, start);
    eggsfs_set_time_req_put_id(&ctx, start, file_id, file);
    eggsfs_set_time_req_put_mtime(&ctx, file_id, mtime_req, 0);
    eggsfs_set_time_req_put_atime(&ctx, mtime_req, atime_req, atime | (1ull<<63));
    eggsfs_set_time_req_put_end(&ctx, atime_req, end);

    u8 shid = eggsfs_inode_shard(file);
    return eggsfs_metadata_request_nowait(&info->sock, req_id, ctx.start, ctx.cursor-ctx.start, &info->shard_addrs1[shid], &info->shard_addrs2[shid]);

    return 0;
}

int eggsfs_shard_add_span_initiate(
    struct eggsfs_fs_info* info, void* data, u64 file, u64 cookie, u64 offset, u32 size, u32 crc, u8 storage_class, u8 parity, u8 stripes, u32 cell_size, u32* cell_crcs,
    u16 blacklist_length, char (*blacklist)[16],
    struct eggsfs_add_span_initiate_block* blocks_out
) {
    struct sk_buff* skb;
    int i;
    u32 attempts;
    u64 req_id = alloc_request_id();
    u8 kind = EGGSFS_SHARD_ADD_SPAN_INITIATE;

    // Not worth writing a bincodegen stuff for this variable sized request, of which we have
    // only two instance.
    int B = eggsfs_blocks(parity);
    if (B > EGGSFS_MAX_BLOCKS) {
        return -EINVAL;
    }

#define MSG_SIZE(__B, __stripes, __blacklist_length) ( \
        8 + /* FileId */ \
        8 + /* Cookie */ \
        8 + /* ByteOffset */ \
        4 + /* Size */ \
        4 + /* CRC */ \
        1 + /* StorageClass */ \
        2 + /* Blacklist length */ \
        (__blacklist_length * (16 + 8)) + /* blacklist */ \
        1 + /* Parity */ \
        1 + /* Stripes */ \
        4 + /* CellSize */ \
        2 + (4*__stripes*__B) /* CRCs */ \
    )

    size_t msg_size = MSG_SIZE(B, stripes, blacklist_length);
    char req[EGGSFS_SHARD_HEADER_SIZE + MSG_SIZE(EGGSFS_MAX_BLOCKS, EGGSFS_MAX_STRIPES, EGGSFS_MAX_BLACKLIST_LENGTH)];

#undef MSG_SIZE

    {
        PREPARE_SHARD_REQ_CTX_INNER(msg_size);
        eggsfs_add_span_initiate_req_put_start(&ctx, start);
        eggsfs_add_span_initiate_req_put_file_id(&ctx, start, req_fileid, file);
        eggsfs_add_span_initiate_req_put_cookie(&ctx, req_fileid, req_cookie, cookie);
        eggsfs_add_span_initiate_req_put_byte_offset(&ctx, req_cookie, req_byteoffset, offset);
        eggsfs_add_span_initiate_req_put_size(&ctx, req_byteoffset, req_size, size);
        eggsfs_add_span_initiate_req_put_crc(&ctx, req_size, req_crc, crc);
        eggsfs_add_span_initiate_req_put_storage_class(&ctx, req_crc, req_storage_class, storage_class);
        eggsfs_add_span_initiate_req_put_blacklist(&ctx, req_storage_class, req_blacklist, blacklist_length);
        for (i = 0; i < blacklist_length; i++) {
            BUG_ON(ctx.end - ctx.cursor < 16 + 8); // failure domain + block service id
            memcpy(ctx.cursor, blacklist[i], 16);
            ctx.cursor += 16;
            memset(ctx.cursor, 0, 8);
            ctx.cursor += 8;
        }
        eggsfs_add_span_initiate_req_put_parity(&ctx, req_blacklist, req_parity, parity);
        eggsfs_add_span_initiate_req_put_stripes(&ctx, req_parity, req_stripes, stripes);
        eggsfs_add_span_initiate_req_put_cell_size(&ctx, req_stripes, req_cell_size, cell_size);
        eggsfs_add_span_initiate_req_put_crcs(&ctx, req_cell_size, req_crcs, stripes*B);
        int i;
        for (i = 0; i < stripes*B; i++) {
            eggsfs_bincode_put_u32(&ctx, cell_crcs[i]);
        }
        eggsfs_add_span_initiate_req_put_end(&ctx, req_crcs, end);
        skb = eggsfs_send_shard_req(info, eggsfs_inode_shard(file), req_id, &ctx, &attempts);
        if (IS_ERR(skb)) { return PTR_ERR(skb); }
    }


    {
        PREPARE_SHARD_RESP_CTX();
        eggsfs_add_span_initiate_resp_get_start(&ctx, start);
        eggsfs_add_span_initiate_resp_get_blocks(&ctx, start, blocks);
        if (unlikely(ctx.err == 0 && blocks.len != B)) {
            consume_skb(skb);
            eggsfs_warn("expected %d blocks, got %d", B, blocks.len);
            return EGGSFS_ERR_MALFORMED_RESPONSE;
        }
        int i;
        for (i = 0; i < B; i++) {
            eggsfs_add_span_initiate_block_info_get_start(&ctx, start);
            eggsfs_add_span_initiate_block_info_get_block_service_addrs(&ctx, start, addr_start);
            eggsfs_addrs_info_get_addr1(&ctx, addr_start, ipport1_start);
            eggsfs_ip_port_get_addrs(&ctx, ipport1_start, bs_ip1);
            eggsfs_ip_port_get_port(&ctx, bs_ip1, bs_port1);
            eggsfs_ip_port_get_end(&ctx, bs_port1, ipport1_end);
            eggsfs_addrs_info_get_addr2(&ctx, ipport1_end, ipport2_start);
            eggsfs_ip_port_get_addrs(&ctx, ipport2_start, bs_ip2);
            eggsfs_ip_port_get_port(&ctx, bs_ip2, bs_port2);
            eggsfs_ip_port_get_end(&ctx, bs_port2, ipport2_end);
            eggsfs_addrs_info_get_end(&ctx, ipport2_end, addr_end);
            eggsfs_add_span_initiate_block_info_get_block_service_id(&ctx, addr_end, bs_id);
            eggsfs_add_span_initiate_block_info_get_block_service_failure_domain(&ctx, bs_id, failure_domain_start);
            eggsfs_failure_domain_get(&ctx, failure_domain_start, failure_domain_end, failure_domain);
            eggsfs_add_span_initiate_block_info_get_block_id(&ctx, failure_domain_end, block_id);
            eggsfs_add_span_initiate_block_info_get_certificate(&ctx, block_id, certificate);
            if (likely(ctx.err == 0)) {
                blocks_out[i].ip1 = bs_ip1.x;
                blocks_out[i].port1 = bs_port1.x;
                blocks_out[i].ip2 = bs_ip2.x;
                blocks_out[i].port2 = bs_port2.x;
                blocks_out[i].block_id = block_id.x;
                blocks_out[i].certificate = certificate.x;
                blocks_out[i].block_service_id = bs_id.x;
                memcpy(blocks_out[i].failure_domain, failure_domain, sizeof(failure_domain));
            }
        }
        eggsfs_add_span_initiate_resp_get_end(&ctx, blocks, end);
        eggsfs_add_span_initiate_resp_get_finish(&ctx, end);
        consume_skb(skb);
        if (unlikely(ctx.err != 0)) {
            eggsfs_debug("resp of kind %02x failed with err %d", kind, ctx.err);
            return ctx.err;
        }
    }

    return 0;
}

int eggsfs_shard_add_span_certify(
    struct eggsfs_fs_info* info, u64 file, u64 cookie, u64 offset, u8 parity, const u64* block_ids, const u64* block_proofs
) {
    struct sk_buff* skb;
    u32 attempts;

    u64 req_id = alloc_request_id();
    u8 kind = EGGSFS_SHARD_ADD_SPAN_CERTIFY;

    int B = eggsfs_blocks(parity);
    if (unlikely(B > EGGSFS_MAX_BLOCKS)) {
        return -EINVAL;
    }

    // Not worth writing a bincodegen stuff for this variable sized request, of which we have
    // only two instances.

#define MSG_SIZE(__B) ( \
        8 + /* FileId */ \
        8 + /* Cookie */ \
        8 + /* ByteOffset */ \
        2 + (8*2*__B) /* Proofs */ \
    )
    char req[EGGSFS_SHARD_HEADER_SIZE + MSG_SIZE(EGGSFS_MAX_BLOCKS)];
    size_t msg_size = MSG_SIZE(B);
#undef MSG_SIZE

    {
        PREPARE_SHARD_REQ_CTX_INNER(msg_size);
        eggsfs_add_span_certify_req_put_start(&ctx, start);
        eggsfs_add_span_certify_req_put_file_id(&ctx, start, req_fileid, file);
        eggsfs_add_span_certify_req_put_cookie(&ctx, req_fileid, req_cookie, cookie);
        eggsfs_add_span_certify_req_put_byte_offset(&ctx, req_cookie, req_byteoffset, offset);
        eggsfs_add_span_certify_req_put_proofs(&ctx, req_byteoffset, req_proofs, B);
        int i;
        for (i = 0; i < B; i++) {
            eggsfs_block_proof_put_start(&ctx, start);
            eggsfs_block_proof_put_block_id(&ctx, start, req_block_id, block_ids[i]);
            eggsfs_block_proof_put_proof(&ctx, req_block_id, req_proof, block_proofs[i]);
            eggsfs_block_proof_put_end(&ctx, req_proof, end);
        }
        eggsfs_add_span_certify_req_put_end(&ctx, req_proofs, end);
        skb = eggsfs_send_shard_req(info, eggsfs_inode_shard(file), req_id, &ctx, &attempts);
        if (IS_ERR(skb)) { return PTR_ERR(skb); }
    }

    {
        PREPARE_SHARD_RESP_CTX();
        eggsfs_add_span_certify_resp_get_start(&ctx, start);
        eggsfs_add_span_certify_resp_get_end(&ctx, start, end);
        eggsfs_add_span_certify_resp_get_finish(&ctx, end);
        FINISH_RESP();
    }

    return 0;
}

int eggsfs_shard_file_spans(struct eggsfs_fs_info* info, u64 file, u64 offset, u64* next_offset, void* data) {
    struct sk_buff* skb;
    u32 attempts;

    u64 req_id = alloc_request_id();
    u8 kind = EGGSFS_SHARD_FILE_SPANS;
    {
        PREPARE_SHARD_REQ_CTX(EGGSFS_FILE_SPANS_REQ_SIZE);
        eggsfs_file_spans_req_put_start(&ctx, start);
        eggsfs_file_spans_req_put_file_id(&ctx, start, file_id, file);
        eggsfs_file_spans_req_put_byte_offset(&ctx, file_id, byte_offset, offset);
        eggsfs_file_spans_req_put_limit(&ctx, byte_offset, limit, 0);
        eggsfs_file_spans_req_put_mtu(&ctx, limit, mtu, eggsfs_mtu);
        eggsfs_file_spans_req_put_end(&ctx, mtu, end);
        skb = eggsfs_send_shard_req(info, eggsfs_inode_shard(file), req_id, &ctx, &attempts);
        if (IS_ERR(skb)) { return PTR_ERR(skb); }
    }

    {
        PREPARE_SHARD_RESP_CTX();
        eggsfs_file_spans_resp_get_start(&ctx, start);
        eggsfs_file_spans_resp_get_next_offset(&ctx, start, resp_next_offset);
        if (likely(ctx.err == 0)) {
            *next_offset = resp_next_offset.x;
        }
        eggsfs_file_spans_resp_get_block_services(&ctx, resp_next_offset, block_services);
        char* bs_offset = ctx.buf;
        if (likely(ctx.err == 0)) {
            ctx.buf += EGGSFS_BLOCK_SERVICE_SIZE*block_services.len;
        }
        eggsfs_file_spans_resp_get_spans(&ctx, block_services, spans);
        int i;
        for (i = 0; i < spans.len; i++) {
            eggsfs_fetched_span_header_get_start(&ctx, header_start);
            eggsfs_fetched_span_header_get_byte_offset(&ctx, header_start, byte_offset);
            eggsfs_fetched_span_header_get_size(&ctx, byte_offset, size);
            eggsfs_fetched_span_header_get_crc(&ctx, size, crc);
            eggsfs_fetched_span_header_get_storage_class(&ctx, crc, storage_class);
            eggsfs_fetched_span_header_get_end(&ctx, storage_class, header_end);
            eggsfs_bincode_get_finish_list_el(header_end);
            if (ctx.err == 0 && storage_class.x == EGGSFS_EMPTY_STORAGE) {
                ctx.err = EGGSFS_ERR_MALFORMED_RESPONSE;
            }
            if (storage_class.x == EGGSFS_INLINE_STORAGE) {
                eggsfs_fetched_inline_span_get_start(&ctx, start);
                eggsfs_fetched_inline_span_get_body(&ctx, start, body);
                eggsfs_fetched_inline_span_get_end(&ctx, body, end);
                eggsfs_bincode_get_finish_list_el(end);
                if (likely(ctx.err == 0)) {
                    // TODO check CRC?
                    eggsfs_file_spans_cb_inline_span(data, byte_offset.x, size.x, body.str.len, body.str.buf);
                }
            } else {
                eggsfs_fetched_blocks_span_get_start(&ctx, start);
                eggsfs_fetched_blocks_span_get_parity(&ctx, start, parity);
                eggsfs_fetched_blocks_span_get_stripes(&ctx, parity, stripes);
                eggsfs_fetched_blocks_span_get_cell_size(&ctx, stripes, cell_size);
                eggsfs_fetched_blocks_span_get_blocks(&ctx, cell_size, blocks);
                struct eggsfs_bincode_get_ctx blocks_ctx = ctx;
                if (likely(ctx.err == 0)) {
                    ctx.buf += EGGSFS_FETCHED_BLOCK_SIZE*blocks.len;
                }
                blocks_ctx.end = ctx.buf;
                eggsfs_fetched_blocks_span_get_stripes_crc(&ctx, blocks, stripes_crc_resp);
                uint32_t* stripes_crc = (uint32_t*)ctx.buf;
                if (likely(ctx.err == 0)) {
                    ctx.buf += 4*stripes_crc_resp.len;
                }
                eggsfs_fetched_blocks_span_get_end(&ctx, stripes_crc_resp, end);
                eggsfs_bincode_get_finish_list_el(end);
                if (likely(ctx.err == 0)) {
                    eggsfs_file_spans_cb_span(data, byte_offset.x, size.x, crc.x, storage_class.x, parity.x, stripes.x, cell_size.x, stripes_crc);
                    blocks_ctx.err = ctx.err;
                    int j;
                    for (j = 0; j < blocks.len; j++) {
                        eggsfs_fetched_block_get_start(&blocks_ctx, start);
                        eggsfs_fetched_block_get_block_service_ix(&blocks_ctx, start, block_service_ix);
                        eggsfs_fetched_block_get_block_id(&blocks_ctx, block_service_ix, block_id);
                        eggsfs_fetched_block_get_crc(&blocks_ctx, block_id, crc);
                        eggsfs_fetched_block_get_end(&blocks_ctx, crc, end);
                        eggsfs_bincode_get_finish_list_el(end);
                        if (likely(blocks_ctx.err == 0)) {
                            if (block_service_ix.x >= block_services.len) {
                                blocks_ctx.err = EGGSFS_ERR_MALFORMED_RESPONSE;
                            } else {
                                struct eggsfs_bincode_get_ctx bs_ctx = {
                                    .buf = bs_offset + EGGSFS_BLOCK_SERVICE_SIZE*block_service_ix.x,
                                    .end = bs_offset + EGGSFS_BLOCK_SERVICE_SIZE*(block_service_ix.x+1),
                                    .err = 0,
                                };
                                eggsfs_block_service_get_start(&bs_ctx, start);
                                eggsfs_block_service_get_addrs(&bs_ctx, start, addr_start);
                                eggsfs_addrs_info_get_addr1(&bs_ctx, addr_start, ipport1_start);
                                eggsfs_ip_port_get_addrs(&bs_ctx, ipport1_start, ip1);
                                eggsfs_ip_port_get_port(&bs_ctx, ip1, port1);
                                eggsfs_ip_port_get_end(&bs_ctx, port1, ipport1_end);
                                eggsfs_addrs_info_get_addr2(&bs_ctx, ipport1_end, ipport2_start);
                                eggsfs_ip_port_get_addrs(&bs_ctx, ipport2_start, ip2);
                                eggsfs_ip_port_get_port(&bs_ctx, ip2, port2);
                                eggsfs_ip_port_get_end(&bs_ctx, port2, ipport2_end);
                                eggsfs_addrs_info_get_end(&bs_ctx, ipport2_end, addr_end);
                                eggsfs_block_service_get_id(&bs_ctx, addr_end, bs_id);
                                eggsfs_block_service_get_flags(&bs_ctx, bs_id, bs_flags);
                                eggsfs_block_service_get_end(&bs_ctx, bs_flags, end);
                                eggsfs_bincode_get_finish_list_el(end);
                                if (likely(bs_ctx.err == 0)) {
                                    eggsfs_file_spans_cb_block(data, j, bs_id.x, ip1.x, port1.x, ip2.x, port2.x, bs_flags.x, block_id.x, crc.x);
                                }
                                blocks_ctx.err = bs_ctx.err;
                            }
                        }
                    }
                    ctx.err = blocks_ctx.err;
                }
            }
        }
        FINISH_RESP();
    }

    return 0;
}

int eggsfs_shard_move_span(
    struct eggsfs_fs_info* info, u64 file1, u64 offset1, u64 cookie1, u64 file2, u64 offset2, u64 cookie2, u32 span_size
) {
    BUG_ON(eggsfs_inode_shard(file1) != eggsfs_inode_shard(file2));
    u8 shard = eggsfs_inode_shard(file1);

    struct sk_buff* skb;
    u32 attempts;
    u64 req_id = alloc_request_id();
    u8 kind = EGGSFS_SHARD_MOVE_SPAN;
    {
        PREPARE_SHARD_REQ_CTX(EGGSFS_MOVE_SPAN_REQ_SIZE);
        eggsfs_move_span_req_put_start(&ctx, start);
        eggsfs_move_span_req_put_span_size(&ctx, start, span_size_req, span_size);
        eggsfs_move_span_req_put_file_id1(&ctx, span_size_req, file1_req, file1);
        eggsfs_move_span_req_put_byte_offset1(&ctx, file1_req, offset1_req, offset1);
        eggsfs_move_span_req_put_cookie1(&ctx, offset1_req, cookie1_req, cookie1);
        eggsfs_move_span_req_put_file_id2(&ctx, cookie1_req, file2_req, file2);
        eggsfs_move_span_req_put_byte_offset2(&ctx, file2_req, offset2_req, offset2);
        eggsfs_move_span_req_put_cookie2(&ctx, offset2_req, cookie2_req, cookie2);
        eggsfs_move_span_req_put_end(&ctx, cookie2_req, end);
        skb = eggsfs_send_shard_req(info, shard, req_id, &ctx, &attempts);
        if (IS_ERR(skb)) { return PTR_ERR(skb); }
    }

    {
        PREPARE_SHARD_RESP_CTX();
        eggsfs_move_span_resp_get_start(&ctx, start);
        eggsfs_move_span_resp_get_end(&ctx, start, end);
        eggsfs_move_span_resp_get_finish(&ctx, end);
        FINISH_RESP();
    }

    return 0;
}


int eggsfs_cdc_mkdir(struct eggsfs_fs_info* info, u64 dir, const char* name, int name_len, u64* ino, u64* creation_time) {
    struct sk_buff* skb;
    u32 attempts;
    u64 req_id = alloc_request_id();
    u8 kind = EGGSFS_CDC_MAKE_DIRECTORY;
    {
        PREPARE_CDC_REQ_CTX(EGGSFS_MAKE_DIRECTORY_REQ_MAX_SIZE);
        eggsfs_make_directory_req_put_start(&ctx, start);
        eggsfs_make_directory_req_put_owner_id(&ctx, start, owner_id, dir);
        eggsfs_make_directory_req_put_name(&ctx, owner_id, name_put, name, name_len);
        eggsfs_make_directory_req_put_end(ctx, name_put, end);
        skb = eggsfs_send_cdc_req(info, req_id, &ctx, &attempts);
        if (IS_ERR(skb)) { return PTR_ERR(skb); }
    }

    {
        PREPARE_CDC_RESP_CTX();
        eggsfs_make_directory_resp_get_start(&ctx, start);
        eggsfs_make_directory_resp_get_id(&ctx, start, id);
        eggsfs_make_directory_resp_get_creation_time(&ctx, id, resp_creation_time);
        eggsfs_make_directory_resp_get_end(&ctx, resp_creation_time, end);
        eggsfs_make_directory_resp_get_finish(&ctx, end);
        FINISH_RESP();
        *ino = id.x;
        *creation_time = resp_creation_time.x;
    }

    return 0;
}

int eggsfs_cdc_rmdir(struct eggsfs_fs_info* info, u64 owner_dir, u64 target, u64 creation_time, const char* name, int name_len) {
    struct sk_buff* skb;
    u32 attempts;
    u64 req_id = alloc_request_id();
    u8 kind = EGGSFS_CDC_SOFT_UNLINK_DIRECTORY;
    {
        PREPARE_CDC_REQ_CTX(EGGSFS_SOFT_UNLINK_DIRECTORY_REQ_MAX_SIZE);
        eggsfs_soft_unlink_directory_req_put_start(&ctx, start);
        eggsfs_soft_unlink_directory_req_put_owner_id(&ctx, start, owner_id, owner_dir);
        eggsfs_soft_unlink_directory_req_put_target_id(&ctx, owner_id, target_id, target);
        eggsfs_soft_unlink_directory_req_put_creation_time(&ctx, target_id, req_creation_time, creation_time);
        eggsfs_soft_unlink_directory_req_put_name(&ctx, req_creation_time, req_name, name, name_len);
        eggsfs_soft_unlink_directory_req_put_end(ctx, req_name, end);
        skb = eggsfs_send_cdc_req(info, req_id, &ctx, &attempts);
        if (IS_ERR(skb)) { return PTR_ERR(skb); }
    }

    {
        PREPARE_CDC_RESP_CTX();
        eggsfs_soft_unlink_directory_resp_get_start(&ctx, start);
        eggsfs_soft_unlink_directory_resp_get_end(&ctx, start, end);
        eggsfs_soft_unlink_directory_resp_get_finish(&ctx, end);
        if (attempts > 1 && ctx.err == EGGSFS_ERR_EDGE_NOT_FOUND) {
            eggsfs_debug("got edge not found, performing followup checks");
            // See commentary in shardreq.go
            if (check_deleted_edge(info, owner_dir, target, name, name_len, creation_time, true)) {
                ctx.err = 0;
            }
        }
        FINISH_RESP();
    }

    return 0;
}

int eggsfs_cdc_rename_directory(
    struct eggsfs_fs_info* info,
    u64 target,
    u64 old_parent, u64 new_parent,
    const char* old_name, int old_name_len, u64 old_creation_time,
    const char* new_name, int new_name_len,
    u64* new_creation_time
) {
    struct sk_buff* skb;
    u32 attempts;
    u64 req_id = alloc_request_id();
    u8 kind = EGGSFS_CDC_RENAME_DIRECTORY;
    {
        PREPARE_CDC_REQ_CTX(EGGSFS_RENAME_DIRECTORY_REQ_MAX_SIZE);
        eggsfs_rename_directory_req_put_start(&ctx, start);
        eggsfs_rename_directory_req_put_target_id(&ctx, start, target_id, target);
        eggsfs_rename_directory_req_put_old_owner_id(&ctx, target_id, old_owner_id, old_parent);
        eggsfs_rename_directory_req_put_old_name(&ctx, old_owner_id, req_old_name, old_name, old_name_len);
        eggsfs_rename_directory_req_put_old_creation_time(&ctx, req_old_name, req_old_creation_time, old_creation_time);
        eggsfs_rename_directory_req_put_new_owner_id(&ctx, req_old_creation_time, req_new_owner_id, new_parent);
        eggsfs_rename_directory_req_put_new_name(&ctx, req_new_owner_id, req_new_name, new_name, new_name_len);
        skb = eggsfs_send_cdc_req(info, req_id, &ctx, &attempts);
        if (IS_ERR(skb)) { return PTR_ERR(skb); }
    }

    {
        PREPARE_CDC_RESP_CTX();
        eggsfs_rename_directory_resp_get_start(&ctx, start);
        eggsfs_rename_directory_resp_get_creation_time(&ctx, start, resp_new_creation_time);
        eggsfs_rename_directory_resp_get_end(&ctx, resp_new_creation_time, end);
        eggsfs_rename_directory_resp_get_finish(&ctx, end);
        bool recovered = false;
        if (attempts > 1 && ctx.err == EGGSFS_ERR_EDGE_NOT_FOUND) {
            eggsfs_debug("got edge not found, performing followup checks");
            // See commentary in shardreq.go
            if (
                check_deleted_edge(info, old_parent, target, old_name, old_name_len, old_creation_time, false) &&
                check_new_edge_after_rename(info, new_parent, target, new_name, new_name_len, new_creation_time)
            ) {
                recovered = true;
                ctx.err = 0;
            }
        }
        FINISH_RESP();
        if (!recovered) {
            *new_creation_time = resp_new_creation_time.x;
        }
    }

    return 0;
}

int eggsfs_cdc_rename_file(
    struct eggsfs_fs_info* info,
    u64 target, u64 old_parent, u64 new_parent,
    const char* old_name, int old_name_len, u64 old_creation_time,
    const char* new_name, int new_name_len,
    u64* new_creation_time
) {
    struct sk_buff* skb;
    u32 attempts;
    u64 req_id = alloc_request_id();
    u8 kind = EGGSFS_CDC_RENAME_FILE;
    {
        PREPARE_CDC_REQ_CTX(EGGSFS_RENAME_FILE_REQ_MAX_SIZE);
        eggsfs_rename_file_req_put_start(&ctx, start);
        eggsfs_rename_file_req_put_target_id(&ctx, start, target_id, target);
        eggsfs_rename_file_req_put_old_owner_id(&ctx, target_id, old_owner_id, old_parent);
        eggsfs_rename_file_req_put_old_name(&ctx, old_owner_id, req_old_name, old_name, old_name_len);
        eggsfs_rename_file_req_put_old_creation_time(&ctx, req_old_name, req_old_creation_time, old_creation_time);
        eggsfs_rename_file_req_put_new_owner_id(&ctx, req_old_creation_time, req_new_owner_id, new_parent);
        eggsfs_rename_file_req_put_new_name(&ctx, req_new_owner_id, req_new_name, new_name, new_name_len);
        skb = eggsfs_send_cdc_req(info, req_id, &ctx, &attempts);
        if (IS_ERR(skb)) { return PTR_ERR(skb); }
    }

    {
        PREPARE_CDC_RESP_CTX();
        eggsfs_rename_file_resp_get_start(&ctx, start);
        eggsfs_rename_file_resp_get_creation_time(&ctx, start, resp_new_creation_time);
        eggsfs_rename_file_resp_get_end(&ctx, resp_new_creation_time, end);
        eggsfs_rename_file_resp_get_finish(&ctx, end);
        bool recovered = false;
        if (attempts > 1 && ctx.err == EGGSFS_ERR_EDGE_NOT_FOUND) {
            eggsfs_debug("got edge not found, performing followup checks");
            // See commentary in shardreq.go
            if (
                check_deleted_edge(info, old_parent, target, old_name, old_name_len, old_creation_time, false) &&
                check_new_edge_after_rename(info, new_parent, target, new_name, new_name_len, new_creation_time)
            ) {
                recovered = true;
                ctx.err = 0;
            }
        }
        FINISH_RESP();
        if (!recovered) {
            *new_creation_time = resp_new_creation_time.x;
        }
    }

    return 0;
}

void __init eggsfs_shard_init(void) {
    int cpu;
    u64 base = get_random_u64();
    for_each_possible_cpu(cpu) {
        BUG_ON(cpu >= 1024);
        *per_cpu_ptr(&next_request_id, cpu) = cpu + base;
    }
}
