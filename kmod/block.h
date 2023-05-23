#ifndef _EGGSFS_BLOCKSIMPLE_H
#define _EGGSFS_BLOCKSIMPLE_H

#include <linux/kernel.h>
#include <linux/completion.h>
#include <linux/net.h>
#include <net/tcp.h>

#include "bincode.h"

#define EGGSFS_MAX_BLOCK_SIZE (100 << 20) // 100MiB

#define EGGSFS_BLOCKS_RESP_HEADER_SIZE (4 + 1) // protocol + kind

struct eggsfs_block_service {
    u64 id;
    u32 ip1;
    u32 ip2;
    u16 port1;
    u16 port2;
};

struct eggsfs_block_socket;

struct eggsfs_block_socket* eggsfs_get_fetch_block_socket(struct eggsfs_block_service* block_service);
// Must have waited for all outstanding requests before putting.
void eggsfs_put_fetch_block_socket(struct eggsfs_block_socket* socket);

struct eggsfs_fetch_block_request {
    // Pages where we write the block. When the request is done everything
    // will be available here. Freeing this is the responsibility of the
    // caller, who must wait for the request to be completed before doing so.
    struct list_head pages;

    // Completes when the block is fetched and all pages have been filled
    // in.
    struct completion comp;

    // Stores the error if something went wrong (to be checked by the caller before
    // inspecting pages or crc).
    int err;

    // CRC of the body
    u32 crc;

    // Below fields are private.

    // List node for list of requests enqueued for a given socket.
    struct list_head socket_requests;

    // The caller and blocksimple.c both need to make sure the request is still there
    // when they need it. We need such a thing since we sometimes send fetch requests
    // from contexts we want to be killable (i.e. syscalls). So in those cases we
    // need to drop them on the floor.
    atomic_t refcount;

    // How many bytes we have left to read.
    u32 bytes_left;
    // The cursor inside the current page.
    u16 page_offset;
    // How much we've read of the header.
    u8 header_read;
    static_assert(EGGSFS_FETCH_BLOCK_RESP_SIZE == 0);
    union {
        char buf[EGGSFS_BLOCKS_RESP_HEADER_SIZE + 2];
        struct {
            __le32 protocol;
            u8 kind;
            __le16 error;
        } __attribute__((packed)) data;
    } header;
};

// Might return errors.
struct eggsfs_fetch_block_request* eggsfs_fetch_block(
    struct eggsfs_block_socket* socket,
    // This must match with what you passed into `eggsfs_get_fetch_block_socket`
    u64 block_service_id,
    u64 block_id,
    u32 offset,
    u32 count
);

// Must be called when you're done with the request. It's OK to call
// this before the request has finished. If the caller has successfully waited for
// the request to complete, it's OK to take ownership of the pages by removing them
// from the list.
void eggsfs_put_fetch_block_request(struct eggsfs_fetch_block_request* req);

struct eggsfs_block_socket* eggsfs_get_write_block_socket(struct eggsfs_block_service* block_service);
// Must have waited for all outstanding requests before putting.
void eggsfs_put_write_block_socket(struct eggsfs_block_socket* socket);

struct eggsfs_write_block_request {
    // Called when request is done
    struct work_struct* callback;

    // The current page we're reading from. Singly linked list, next one
    // in ->private. These pages are managed by the issuer of the request.
    struct page* page;

    // List node for list of requests enqueued for a given socket.
    struct list_head socket_requests;

    // Req info
    u64 block_service_id;
    u64 block_id;
    u32 crc;
    u32 size;
    u64 certificate;

    // * 0: not done yet
    // * 1: done
    // * -n: errored
    atomic_t status;

    u32 bytes_left; // how many bytes left to write
    u16 page_offset; // offset into page we're reading from

    // How much we've written of the write request
    u8 write_req_written;
    // How much we're read of the first response (after we ask to write), and of the
    // second (after we've written the block).
    u8 write_resp_read;
    static_assert(EGGSFS_WRITE_BLOCK_RESP_SIZE == 0);
    union {
        char buf[EGGSFS_BLOCKS_RESP_HEADER_SIZE + 2];
        struct {
            __le32 protocol;
            u8 kind;
            __le16 error;
        } __attribute__((packed)) data;
    } write_resp;
    u8 written_resp_read;
    static_assert(EGGSFS_BLOCK_WRITTEN_RESP_SIZE > 2);
    union {
        char buf[EGGSFS_BLOCKS_RESP_HEADER_SIZE+EGGSFS_BLOCK_WRITTEN_RESP_SIZE];
        struct {
            __le32 protocol;
            u8 kind;
            union {
                __le16 error;
                __be64 proof;
            };
        } __attribute__((packed)) data;
    } written_resp;
};

// Unlike fetching, here you _must_ wait for the request to be complete. This is since
// we must know that nothing might use the pages passed in before freeing them.
struct eggsfs_write_block_request* eggsfs_write_block(
    struct eggsfs_block_socket* socket,
    struct work_struct* callback,
    // This must match with what you passed into `eggsfs_get_fetch_block_socket`
    u64 block_service_id,
    u64 block_id,
    u64 certificate,
    u32 size,
    u32 crc,
    // There must be enough pages to write everything. The next page should
    // be in ->private (the reasons for not using ->lru are purely because
    // the code that actually uses this is easier to write this way).
    struct page* page
);

// Must be called when you're done with the request. Can't be called
// before the request is completed.
void eggsfs_put_write_block_request(struct eggsfs_write_block_request* req);

int __init eggsfs_blocksimple_init(void);
void __cold eggsfs_blocksimple_exit(void);

#endif
