#include <asm/unaligned.h>

#include "bincode.h"
#include "shuckle.h"
#include "log.h"

void eggsfs_write_shuckle_req_header(char* buf, u32 req_len, u8 req_kind) {
    put_unaligned_le32(EGGSFS_SHUCKLE_REQ_PROTOCOL_VERSION, buf); buf += 4;
    // automatically include the kind, much nicer for the caller
    put_unaligned_le32(req_len+1, buf); buf += 4;
    *(u8*)buf = req_kind; buf++;
}

int eggsfs_read_shuckle_resp_header(char* buf, u32* resp_len, u8* resp_kind) {
    u32 protocol = get_unaligned_le32(buf); buf += 4;
    if (protocol != EGGSFS_SHUCKLE_RESP_PROTOCOL_VERSION) {
        eggsfs_debug_print("bad shuckle protocol, expected 0x%08x, got 0x%08x", EGGSFS_SHUCKLE_RESP_PROTOCOL_VERSION, protocol);
        return -EINVAL;
    }
    *resp_len = get_unaligned_le32(buf); buf += 4;
    if (*resp_len == 0) {
        eggsfs_debug_print("unexpected zero-length shuckle response (the kind should at least be there)");
        return -EINVAL;
    }
    *resp_kind = *(u8*)buf; buf++;
    eggsfs_debug_print("resp_len=%d, resp_kind=0x%02x", *resp_len, (int)*resp_kind);
    (*resp_len)--; // exclude the kind, it's much nicer for the caller
    return 0;
}

int eggsfs_create_shuckle_socket(const char* shuckle_addr, struct socket** sock) {
    int err;
    const char* addr_end;
    u16 port;

    // parse device, which is the shuckle address in 0.0.0.0:0 form (ipv4, port)
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    if (in4_pton(shuckle_addr, -1, (u8*)&addr.sin_addr, ':', &addr_end) != 1) {
        err = -EINVAL;
        goto out_err;
    }
    err = kstrtou16(addr_end+1, 10, &port);
    if (err < 0) { goto out_err; }
    addr.sin_port = htons(port);
    eggsfs_debug_print("parsed shuckle addr %pI4:%d", &addr.sin_addr, ntohs(addr.sin_port));

    // create socket
    err = sock_create_kern(&init_net, PF_INET, SOCK_STREAM, IPPROTO_TCP, sock);
    if (err < 0) { goto out_err; }

    // connect
    err = kernel_connect(*sock, (struct sockaddr*)&addr, sizeof(addr), 0);
    if (err < 0) { goto out_connect; }
    eggsfs_debug_print("connected to shuckle");

    return 0;

out_connect:
    sock_release(*sock);
out_err:
    eggsfs_debug_print("failed err=%d", err);
    return err;
}
