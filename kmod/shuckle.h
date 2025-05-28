#ifndef _EGGSFS_SHUCKLE_H
#define _EGGSFS_SHUCKLE_H

#include <linux/kernel.h>
#include <linux/inet.h>

#define EGGSFS_SHUCKLE_REQ_HEADER_SIZE (4 + 4 + 1) // protocol + len + kind
void eggsfs_write_shuckle_req_header(char* buf, u32 req_len, u8 req_kind);

#define EGGSFS_SHUCKLE_RESP_HEADER_SIZE (4 + 4 + 1) // protocol + len + kind
int eggsfs_read_shuckle_resp_header(char* buf, u32* resp_len, u8* resp_kind);

struct eggsfs_shuckle_addr {
    struct net* net;
    char* addr;
};

int eggsfs_process_shuckle_addr(struct eggsfs_shuckle_addr* addr, struct sockaddr_in* addr1, struct sockaddr_in* addr2);

int eggsfs_create_shuckle_socket(struct eggsfs_shuckle_addr* addr, struct socket** sock);

#endif
