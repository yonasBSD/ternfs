#ifndef _EGGSFS_SHUCKLE_H
#define _EGGSFS_SHUCKLE_H

#include <linux/kernel.h>
#include <linux/inet.h>

#define EGGSFS_SHUCKLE_REQ_HEADER_SIZE (4 + 4 + 1) // protocol + len + kind
void eggsfs_write_shuckle_req_header(char* buf, u32 req_len, u8 req_kind);

#define EGGSFS_SHUCKLE_RESP_HEADER_SIZE (4 + 4 + 1) // protocol + len + kind
int eggsfs_read_shuckle_resp_header(char* buf, u32* resp_len, u8* resp_kind);

int eggsfs_create_shuckle_socket(const char* shuckle_addr, struct socket** sock);

#endif
