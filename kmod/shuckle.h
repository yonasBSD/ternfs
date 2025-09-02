#ifndef _TERNFS_SHUCKLE_H
#define _TERNFS_SHUCKLE_H

#include <linux/kernel.h>
#include <linux/inet.h>

#define TERNFS_SHUCKLE_REQ_HEADER_SIZE (4 + 4 + 1) // protocol + len + kind
void ternfs_write_shuckle_req_header(char* buf, u32 req_len, u8 req_kind);

#define TERNFS_SHUCKLE_RESP_HEADER_SIZE (4 + 4 + 1) // protocol + len + kind
int ternfs_read_shuckle_resp_header(char* buf, u32* resp_len, u8* resp_kind);

struct ternfs_shuckle_addr {
    struct net* net;
    char* addr;
};

int ternfs_process_shuckle_addr(struct ternfs_shuckle_addr* addr, struct sockaddr_in* addr1, struct sockaddr_in* addr2);

int ternfs_create_shuckle_socket(struct ternfs_shuckle_addr* addr, struct socket** sock);

#endif
