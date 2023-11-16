#ifndef _EGGSFS_SHUCKLE_H
#define _EGGSFS_SHUCKLE_H

#include <linux/kernel.h>
#include <linux/inet.h>

#define EGGSFS_SHUCKLE_REQ_HEADER_SIZE (4 + 4 + 1) // protocol + len + kind
void eggsfs_write_shuckle_req_header(char* buf, u32 req_len, u8 req_kind);

#define EGGSFS_SHUCKLE_RESP_HEADER_SIZE (4 + 4 + 1) // protocol + len + kind
int eggsfs_read_shuckle_resp_header(char* buf, u32* resp_len, u8* resp_kind);

int eggsfs_parse_shuckle_addr(const char* str, atomic64_t* addr1, atomic64_t* addr2);

// Might override addr1/addr2 if we get new stuff from
// a shuckle proxy.
int eggsfs_create_shuckle_socket(atomic64_t* addr1, atomic64_t* addr2, struct socket** sock);

#endif
