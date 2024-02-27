#include <asm/unaligned.h>
#include <linux/version.h>

#include "bincode.h"
#include "shuckle.h"
#include "log.h"
#include "err.h"
#include "super.h"

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 10, 0))
#define kernel_setsockopt(sock, level, optname, optval, optlen) sock_setsockopt(sock, level, optname, KERNEL_SOCKPTR(optval), optlen)
#endif

void eggsfs_write_shuckle_req_header(char* buf, u32 req_len, u8 req_kind) {
    put_unaligned_le32(EGGSFS_SHUCKLE_REQ_PROTOCOL_VERSION, buf); buf += 4;
    // automatically include the kind, much nicer for the caller
    put_unaligned_le32(req_len+1, buf); buf += 4;
    *(u8*)buf = req_kind; buf++;
}

int eggsfs_read_shuckle_resp_header(char* buf, u32* resp_len, u8* resp_kind) {
    u32 protocol = get_unaligned_le32(buf); buf += 4;
    if (protocol != EGGSFS_SHUCKLE_RESP_PROTOCOL_VERSION) {
        eggsfs_warn("bad shuckle protocol, expected 0x%08x, got 0x%08x", EGGSFS_SHUCKLE_RESP_PROTOCOL_VERSION, protocol);
        return -EINVAL;
    }
    *resp_len = get_unaligned_le32(buf); buf += 4;
    if (*resp_len == 0) {
        eggsfs_warn("unexpected zero-length shuckle response (the kind should at least be there)");
        return -EINVAL;
    }
    *resp_kind = *(u8*)buf; buf++;
    eggsfs_debug("resp_len=%d, resp_kind=0x%02x", *resp_len, (int)*resp_kind);
    (*resp_len)--; // exclude the kind, it's much nicer for the caller
    return 0;
}

int eggsfs_create_shuckle_socket(atomic64_t* addr1, atomic64_t* addr2, struct socket** sock) {
    atomic64_t* addrs[2] = {addr1, addr2};
    int err;

    // create socket
    err = sock_create_kern(&init_net, PF_INET, SOCK_STREAM, IPPROTO_TCP, sock);
    if (err < 0) {
        eggsfs_warn("could not create shuckle socket: %d", err);
        return err;
    }

    struct __kernel_sock_timeval tv;
    tv.tv_sec = 10;
    tv.tv_usec = 0;
    err = kernel_setsockopt(*sock, SOL_SOCKET, SO_RCVTIMEO_NEW, (char *)&tv, sizeof(tv));
    if (err < 0) {
        eggsfs_warn("could not set receive timeout on shuckle socket: %d", err);
        goto out_sock;
    }
    err = kernel_setsockopt(*sock, SOL_SOCKET, SO_SNDTIMEO_NEW, (char *)&tv, sizeof(tv));
    if (err < 0) {
        eggsfs_warn("could not set send timeout on shuckle socket: %d", err);
        goto out_sock;
    }

    int syn_count = 3;
    err = kernel_setsockopt(*sock, SOL_TCP, TCP_SYNCNT, (char *)&syn_count, sizeof(syn_count));
    if (err < 0) {
        eggsfs_warn("could not set TCP_SYNCNT=%d on shuckle socket: %d", syn_count, err);
        goto out_sock;
    }

    u64 start = get_jiffies_64();
    u64 i;
    for (i = start; i < start+2; i++) {
        int ix = i%2;
        u64 addr_v = atomic64_read(addrs[ix]);
        struct sockaddr_in addr;
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = eggsfs_get_addr_ip(addr_v);
        addr.sin_port = eggsfs_get_addr_port(addr_v);
        if (addr.sin_port == 0) {
            eggsfs_debug("skipping zero port address val %pI4:%d", &addr.sin_addr, ntohs(addr.sin_port));
            continue;
        }
        eggsfs_debug("connecting to address %pI4:%d", &addr.sin_addr, ntohs(addr.sin_port));
        err = kernel_connect(*sock, (struct sockaddr*)&addr, sizeof(addr), 0);
        if (err < 0) {
            eggsfs_warn("could not connect to shuckle addr %pI4:%d, might retry", &addr.sin_addr, ntohs(addr.sin_port));
            continue;
        }
        eggsfs_debug("connected to shuckle");

        // get the addr
        struct kvec iov;
        struct msghdr msg = {NULL};

        static_assert(EGGSFS_SHARDS_REQ_SIZE == 0);
        char shuckle_req[EGGSFS_SHUCKLE_REQ_HEADER_SIZE];
        eggsfs_write_shuckle_req_header(shuckle_req, 0, EGGSFS_SHUCKLE_SHUCKLE);
        int written_so_far;
        for (written_so_far = 0; written_so_far < sizeof(shuckle_req);) {
            iov.iov_base = shuckle_req + written_so_far;
            iov.iov_len = sizeof(shuckle_req) - written_so_far;
            int written = kernel_sendmsg(*sock, &msg, &iov, 1, iov.iov_len);
            if (written < 0) { err = written; goto out_sock; }
            written_so_far += written;
        }

        char resp_header[EGGSFS_SHUCKLE_RESP_HEADER_SIZE];
        int read_so_far;
        for (read_so_far = 0; read_so_far < sizeof(resp_header);) {
            iov.iov_base = resp_header + read_so_far;
            iov.iov_len = sizeof(resp_header) - read_so_far;
            int read = kernel_recvmsg(*sock, &msg, &iov, 1, iov.iov_len, 0);
            if (read == 0) { err = -ECONNRESET; goto out_sock; }
            if (read < 0) { err = read; goto out_sock; }
            read_so_far += read;
        }
        u32 shuckle_resp_len;
        u8 shuckle_resp_kind;
        err = eggsfs_read_shuckle_resp_header(resp_header, &shuckle_resp_len, &shuckle_resp_kind);
        if (err < 0) { goto out_sock; }
        if (shuckle_resp_len != EGGSFS_SHUCKLE_RESP_SIZE) {
            eggsfs_debug("expected size of %d, got %d", EGGSFS_SHUCKLE_RESP_SIZE, shuckle_resp_len);
            err = -EINVAL; goto out_sock;
        }
        {
            char resp[EGGSFS_SHUCKLE_RESP_SIZE];
            for (read_so_far = 0; read_so_far < sizeof(resp);) {
                iov.iov_base = (char*)&resp + read_so_far;
                iov.iov_len = sizeof(resp) - read_so_far;
                int read = kernel_recvmsg(*sock, &msg, &iov, 1, iov.iov_len, 0);
                if (read == 0) { err = -ECONNRESET; goto out_sock; }
                if (read < 0) { err = read; goto out_sock; }
                read_so_far += read;
            }
            struct eggsfs_bincode_get_ctx ctx = {
                .buf = resp,
                .end = resp + sizeof(resp),
                .err = 0,
            };
            eggsfs_shuckle_resp_get_start(&ctx, start);
            eggsfs_shuckle_resp_get_ip1(&ctx, start, ip1);
            eggsfs_shuckle_resp_get_port1(&ctx, ip1, port1);
            eggsfs_shuckle_resp_get_ip2(&ctx, port1, ip2);
            eggsfs_shuckle_resp_get_port2(&ctx, ip2, port2);
            eggsfs_shuckle_resp_get_end(&ctx, port2, end);
            eggsfs_shuckle_resp_get_finish(&ctx, end);
            if (ctx.err != 0) { err = eggsfs_error_to_linux(ctx.err); goto out_sock; }

            u64 new_addrs[2] = { eggsfs_mk_addr(ip1.x, port1.x), eggsfs_mk_addr(ip2.x, port2.x) };
            bool reconnect = new_addrs[ix] != addr_v;
            atomic64_set(addrs[0], new_addrs[0]);
            atomic64_set(addrs[1], new_addrs[1]);
            if (reconnect) {
                eggsfs_info("we got redirected by shuckle proxy, reconnecting");
                sock_release(*sock);
                return eggsfs_create_shuckle_socket(addr1, addr2, sock);
            }
        }

        return 0;
    }

out_sock:
    // this would only happens with two zero addresses,
    // which we guard against elsewhere in the code.
    BUG_ON(err == 0);
    sock_release(*sock);
    return err;
}

static int eggsfs_parse_single_shuckle_addr(const char* str, int len, atomic64_t* addr) {
    int err;
    const char* addr_end;

    eggsfs_debug("parsing shuckle address %*pE", len, str);

    // parse device, which is the shuckle address in 0.0.0.0:0 form (ipv4, port)
    __be32 addrn;
    if (in4_pton(str, len, (u8*)&addrn, ':', &addr_end) != 1) {
        return -EINVAL;
    }
    u16 port;
    {
        char port_str[6]; // max port + terminating byte
        int port_len = len - (addr_end-str) - 1;
        if (port_len <= 0 || port_len >= sizeof(port_str)) {
            eggsfs_warn("bad port in address %*pE (port_len=%d)", len, str, port_len);
            return -EINVAL;
        }
        memcpy(port_str, addr_end+1, port_len);
        port_str[port_len] = '\0';
        err = kstrtou16(port_str, 10, &port);
        if (err < 0) {
            eggsfs_warn("bad port in address %*pE", len, str);
            return err;
        }
        if (port == 0) {
            eggsfs_warn("zero port in address %*pE", len, str);
            return -EINVAL;
        }
    }

    eggsfs_debug("parsed shuckle addr %pI4:%d", &addrn, port);

    atomic64_set(addr, eggsfs_mk_addr(ntohl(addrn), port));

    eggsfs_debug("")

    return 0;
}

int eggsfs_parse_shuckle_addr(const char* str, atomic64_t* addr1, atomic64_t* addr2) {
    size_t str_len = strlen(str);
    const char* addr1_end = strchr(str, ',');
    if (addr1_end == NULL) { // only one address
        atomic64_set(addr2, 0);
        return eggsfs_parse_single_shuckle_addr(str, str_len, addr1);
    } else if (str + str_len == addr1_end) {
        return -EINVAL; // terminated with :
    } else {
        int err = eggsfs_parse_single_shuckle_addr(str, addr1_end-str, addr1);
        if (err) {
            return err;
        }
        return eggsfs_parse_single_shuckle_addr(addr1_end+1, str_len - (addr1_end-str) - 1, addr2);
    }
}
