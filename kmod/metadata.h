#ifndef _EGGSFS_SHARD_H
#define _EGGSFS_SHARD_H

#include <linux/kernel.h>

#include "net.h"
#include "super.h"
#include "inode.h"

int eggsfs_shard_lookup(struct eggsfs_fs_info* info, u64 dir, const char* name, int name_len, u64* ino, u64* creation_time);
int eggsfs_shard_readdir(struct eggsfs_fs_info* info, u64 dir, u64 start_pos, void* data, u64* next_hash);
int eggsfs_shard_unlink_file(struct eggsfs_fs_info* info, u64 dir, u64 file, const char* name, int name_len, u64 creation_time);
int eggsfs_shard_rename(struct eggsfs_fs_info* info, u64 dir, u64 target_id, const char* old_name, int old_name_len, u64 old_creation_time, const char* new_name, int new_name_len, u64* new_creation_time);
int eggsfs_shard_link_file(struct eggsfs_fs_info* info, u64 file, u64 cookie, u64 dir, const char* name, int name_len, u64* creation_time);
int eggsfs_shard_getattr_file(struct eggsfs_fs_info* info, u64 file, u64* mtime, u64* size);
int eggsfs_shard_getattr_dir(
    struct eggsfs_fs_info* info,
    u64 file,
    u64* mtime,
    struct eggsfs_block_policies* block_policies,
    struct eggsfs_span_policies* span_policies,
    u32* target_stripe_size
);
int eggsfs_shard_getattr(struct eggsfs_fs_info* info, u64 id);
int eggsfs_shard_create_file(struct eggsfs_fs_info* info, u8 shid, int itype, const char* name, int name_len, u64* ino, u64* cookie);
int eggsfs_shard_file_spans(struct eggsfs_fs_info* info, u64 file, u64 offset, void* data);
int eggsfs_shard_add_inline_span(struct eggsfs_fs_info* info, u64 file, u64 cookie, u64 offset, u32 size, const char* data, u8 len);
int eggsfs_shard_add_span_initiate(
    struct eggsfs_fs_info* info, void* data, u64 file, u64 cookie, u64 offset, u32 size, u32 crc, u8 storage_class, u8 parity,
    u8 stripes, u32 cell_size, u32* cell_crcs
);
int eggsfs_shard_add_span_certify(
    struct eggsfs_fs_info* info, u64 file, u64 cookie, u64 offset, u8 parity, const u64* block_ids, const u64* block_proofs
);

struct eggsfs_shard_add_span_initiate_new_block {
    u64 size;
    u32 crc32;
};
struct eggsfs_shard_add_span_initiate_async_request {
    struct eggsfs_shard_async_request request;
    char data[EGGSFS_UDP_MTU];
};
void eggsfs_shard_add_span_initiate_async(struct eggsfs_fs_info* info, struct eggsfs_shard_add_span_initiate_async_request* request, u64 file, u64 cookie, u64 byte_offset, u8 storage_class, u8 parity, u32 crc32, u64 size, struct eggsfs_shard_add_span_initiate_new_block* blocks, u16 n_blocks);

struct eggsfs_shard_add_span_certify_block_proof {
    u64 block_id;
    u64 proof;
};
struct eggsfs_shard_add_span_certify_async_request {
    struct eggsfs_shard_async_request request;
    char data[EGGSFS_UDP_MTU];
};
void eggsfs_shard_add_span_certify_async(struct eggsfs_fs_info* info, struct eggsfs_shard_add_span_certify_async_request* request, u64 file, u64 cookie, u64 offset, struct eggsfs_shard_add_span_certify_block_proof* proofs, u16 n_blocks);

int eggsfs_cdc_mkdir(struct eggsfs_fs_info* info, u64 dir, const char* name, int name_len, u64* ino, u64* creation_time);
int eggsfs_cdc_rmdir(struct eggsfs_fs_info* info, u64 owner_dir, u64 target, u64 creation_time, const char* name, int name_len);

int eggsfs_cdc_rename_directory(
    struct eggsfs_fs_info* info,
    u64 target, u64 old_parent, u64 new_parent,
    const char* old_name, int old_name_len, u64 old_creation_time,
    const char* new_name, int new_name_len,
    u64* new_creation_time
);
int eggsfs_cdc_rename_file(
    struct eggsfs_fs_info* info,
    u64 target, u64 old_parent, u64 new_parent,
    const char* old_name, int old_name_len, u64 old_creation_time,
    const char* new_name, int new_name_len,
    u64* new_creation_time
);

void __init eggsfs_shard_init(void);

#endif
