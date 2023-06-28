#ifndef _EGGSFS_BINCODE_H
#define _EGGSFS_BINCODE_H

#ifdef __KERNEL__
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/bug.h>
#include <asm/unaligned.h>
#endif

struct eggsfs_bincode_put_ctx { char* start; char* cursor; char* end; };

struct eggsfs_bincode_get_ctx { char* buf; char* end; u16 err; };

struct eggsfs_bincode_bytes { char* buf; u8 len; };

#define eggsfs_bincode_get_finish_list_el(end) ((void)(end))

struct eggsfs_failure_domain_start;
struct eggsfs_failure_domain_end;

static inline void _eggsfs_failure_domain_get(struct eggsfs_bincode_get_ctx* ctx, struct eggsfs_failure_domain_start* start, uint8_t* name) {
    if (likely(ctx->err == 0)) {
        if (unlikely(ctx->end - ctx->buf < 16)) {
            ctx->err = 14; // 14 == EGGSFS_ERR_MALFORMED_RESPONSE, see static assert below
        } else {
            memcpy(name, ctx->buf, 16);
            ctx->buf += 16;
        }
    }
}
#define eggsfs_failure_domain_get(ctx, prev, next, name) \
    struct eggsfs_failure_domain_end* next; \
    uint8_t name[16]; \
    _eggsfs_failure_domain_get(ctx, prev, name)

#include "bincodegen.h"

#ifdef __KERNEL__
static_assert(14 == EGGSFS_ERR_MALFORMED_RESPONSE);
#endif

inline static void eggsfs_bincode_put_u64(struct eggsfs_bincode_put_ctx* ctx, u64 x) {
    BUG_ON(ctx->end - ctx->cursor < sizeof(x));
    put_unaligned_le64(x, ctx->cursor);
    ctx->cursor += sizeof(x);
}
inline static void eggsfs_bincode_put_u32(struct eggsfs_bincode_put_ctx* ctx, u32 x) {
    BUG_ON(ctx->end - ctx->cursor < sizeof(x));
    put_unaligned_le32(x, ctx->cursor);
    ctx->cursor += sizeof(x);
}
inline static void eggsfs_bincode_put_u16(struct eggsfs_bincode_put_ctx* ctx, u16 x) {
    BUG_ON(ctx->end - ctx->cursor < sizeof(x));
    put_unaligned_le16(x, ctx->cursor);
    ctx->cursor += sizeof(x);
}
inline static void eggsfs_bincode_put_u8(struct eggsfs_bincode_put_ctx* ctx, u8 x) {
    BUG_ON(ctx->end - ctx->cursor < sizeof(x));
    *(u8*)(ctx->cursor) = x;
    ctx->cursor += sizeof(x);
}

inline static u64 eggsfs_bincode_get_u64(struct eggsfs_bincode_get_ctx* ctx) {
    if (likely(ctx->err == 0)) {
        if (unlikely(ctx->end - ctx->buf < sizeof(u64))) {
            ctx->err = EGGSFS_ERR_MALFORMED_RESPONSE;
            return 0;
        } else {
            u64 x = get_unaligned_le64(ctx->buf);
            ctx->buf += sizeof(x);
            return x;
        }
    } else {
        return 0;
    }
}
inline static u32 eggsfs_bincode_get_u32(struct eggsfs_bincode_get_ctx* ctx) {
    if (likely(ctx->err == 0)) {
        if (unlikely(ctx->end - ctx->buf < sizeof(u32))) {
            ctx->err = EGGSFS_ERR_MALFORMED_RESPONSE;
            return 0;
        } else {
            u32 x = get_unaligned_le32(ctx->buf);
            ctx->buf += sizeof(x);
            return x;
        }
    } else {
        return 0;
    }
}
inline static u16 eggsfs_bincode_get_u16(struct eggsfs_bincode_get_ctx* ctx) {
    if (likely(ctx->err == 0)) {
        if (unlikely(ctx->end - ctx->buf < sizeof(u16))) {
            ctx->err = EGGSFS_ERR_MALFORMED_RESPONSE;
            return 0;
        } else {
            u16 x = get_unaligned_le16(ctx->buf);
            ctx->buf += sizeof(x);
            return x;
        }
    } else {
        return 0;
    }
}
inline static u8 eggsfs_bincode_get_u8(struct eggsfs_bincode_get_ctx* ctx) {
    if (likely(ctx->err == 0)) {
        if (unlikely(ctx->end - ctx->buf < sizeof(u8))) {
            ctx->err = EGGSFS_ERR_MALFORMED_RESPONSE;
            return 0;
        } else {
            u8 x = *(u8*)(ctx->buf);
            ctx->buf += sizeof(x);
            return x;
        }
    } else {
        return 0;
    }
}

// >>> format(struct.unpack('<I', b'SHA\0')[0], 'x')
// '414853'
static const u32 EGGSFS_SHARD_REQ_PROTOCOL_VERSION = 0x414853;

// >>> format(struct.unpack('<I', b'SHA\1')[0], 'x')
// '1414853'
static const u32 EGGSFS_SHARD_RESP_PROTOCOL_VERSION = 0x1414853;

// >>> format(struct.unpack('<I', b'CDC\0')[0], 'x')
// '434443'
static const u32 EGGSFS_CDC_REQ_PROTOCOL_VERSION = 0x434443;

// >>> format(struct.unpack('<I', b'CDC\1')[0], 'x')
// '1434443'
static const u32 EGGSFS_CDC_RESP_PROTOCOL_VERSION = 0x1434443;

// >>> format(struct.unpack('<I', b'SHU\0')[0], 'x')
// '554853'
static const u32 EGGSFS_SHUCKLE_REQ_PROTOCOL_VERSION = 0x554853;

// >>> format(struct.unpack('<I', b'SHU\1')[0], 'x')
// '1554853'
static const u32 EGGSFS_SHUCKLE_RESP_PROTOCOL_VERSION = 0x1554853;

// >>> format(struct.unpack('<I', b'BLO\0')[0], 'x')
// '4f4c42'
static const u32 EGGSFS_BLOCKS_REQ_PROTOCOL_VERSION = 0x4f4c42;

// >>> format(struct.unpack('<I', b'BLO\1')[0], 'x')
// '14f4c42'
static const u32 EGGSFS_BLOCKS_RESP_PROTOCOL_VERSION = 0x14f4c42;

static const u8 SPAN_POLICY_TAG = 2;
static const u8 BLOCK_POLICY_TAG = 3;
static const u8 STRIPE_POLICY_TAG = 4;

static const u8 EGGSFS_EMPTY_STORAGE  = 0;
static const u8 EGGSFS_INLINE_STORAGE = 1;
static const u8 EGGSFS_HDD_STORAGE    = 2;
static const u8 EGGSFS_FLASH_STORAGE  = 3;

#define EGGSFS_GET_EXTRA(x) (((x) & (1ull<<63)) >> 63)
#define EGGSFS_GET_EXTRA_ID(x) ((x) & ~(1ull<<63))

#define EGGSFS_MAX_FILENAME 255

#define EGGSFS_BLOCK_SERVICE_STALE           1u
#define EGGSFS_BLOCK_SERVICE_NO_READ        (1u<<1)
#define EGGSFS_BLOCK_SERVICE_NO_WRITE       (1u<<2)
#define EGGSFS_BLOCK_SERVICE_DECOMMISSIONED (1u<<3)

#define EGGSFS_BLOCK_SERVICE_DONT_READ  (EGGSFS_BLOCK_SERVICE_STALE | EGGSFS_BLOCK_SERVICE_NO_READ  | EGGSFS_BLOCK_SERVICE_DECOMMISSIONED)
#define EGGSFS_BLOCK_SERVICE_DONT_WRITE (EGGSFS_BLOCK_SERVICE_STALE | EGGSFS_BLOCK_SERVICE_NO_WRITE | EGGSFS_BLOCK_SERVICE_DECOMMISSIONED)

#define EGGSFS_FULL_READ_DIR_CURRENT   (1u << 0)
#define EGGSFS_FULL_READ_DIR_BACKWARDS (1u << 1)
#define EGGSFS_FULL_READ_DIR_SAME_NAME (1u << 2)

#define EGGSFS_MAX_STRIPES 15

#endif
