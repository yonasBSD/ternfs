#ifndef _EGGSFS_BLOCK_H
#define _EGGSFS_BLOCK_H

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
void eggsfs_put_fetch_block_socket(struct eggsfs_block_socket* socket);

struct eggfs_fetch_block_request;

// If the callback wants to gain ownership of the pages, it can remove them from
// the list.
struct eggsfs_fetch_complete {
    int err;
    u32 crc;
    u64 block_id;
    struct list_head pages;
};

// Might return errors.
struct eggsfs_fetch_block_request* eggsfs_fetch_block(
    struct eggsfs_block_socket* socket,
    void (*callback)(struct eggsfs_fetch_complete* complete, void* data),
    void* data,
    // This must match with what you passed into `eggsfs_get_fetch_block_socket`
    u64 block_service_id,
    u64 block_id,
    u32 offset,
    u32 count
);

struct eggsfs_block_socket* eggsfs_get_write_block_socket(struct eggsfs_block_service* block_service);
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

    u32 bytes_left; // how many bytes left to write (just the block, excluding the request)
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

int __init eggsfs_block_init(void);
void __cold eggsfs_block_exit(void);

#endif
