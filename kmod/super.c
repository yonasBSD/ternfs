#include "super.h"

#include <linux/inet.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/statfs.h>

#include "log.h"
#include "inode.h"
#include "export.h"
#include "metadata.h"
#include "dentry.h"
#include "net.h"
#include "shuckle.h"
#include "bincode.h"
#include "sysfs.h"
#include "err.h"
#include "rs.h"

static void eggsfs_free_fs_info(struct eggsfs_fs_info* info) {
    eggsfs_debug("info=%p", info);
    eggsfs_net_shard_free_socket(&info->sock);
    kfree(info);
}

static struct eggsfs_fs_info* eggsfs_init_fs_info(const char* dev_name) {
    int err;

    struct eggsfs_fs_info* info = kmalloc(sizeof(struct eggsfs_fs_info), GFP_KERNEL);
    if (!info) { err = -ENOMEM; goto out; }

    err = eggsfs_parse_shuckle_addr(dev_name, &info->shuckle_addr);
    if (err) { goto out; }

    err = eggsfs_init_shard_socket(&info->sock);
    if (err) { goto out_info; }

    struct socket* shuckle_sock;
    err = eggsfs_create_shuckle_socket(&info->shuckle_addr, &shuckle_sock);
    if (err < 0) { goto out_info; }

    struct kvec iov;
    struct msghdr msg = {NULL};

    static_assert(EGGSFS_SHARDS_REQ_SIZE == 0);
    char shuckle_req[EGGSFS_SHUCKLE_REQ_HEADER_SIZE];
    eggsfs_write_shuckle_req_header(shuckle_req, 0, EGGSFS_SHUCKLE_SHARDS);
    int written_so_far;
    for (written_so_far = 0; written_so_far < sizeof(shuckle_req);) {
        iov.iov_base = shuckle_req + written_so_far;
        iov.iov_len = sizeof(shuckle_req) - written_so_far;
        int written = kernel_sendmsg(shuckle_sock, &msg, &iov, 1, iov.iov_len);
        if (written < 0) { err = written; goto out_sock; }
        written_so_far += written;
    }

    char shards_resp_header[EGGSFS_SHUCKLE_RESP_HEADER_SIZE + 2]; // + 2 = list len
    int read_so_far;
    for (read_so_far = 0; read_so_far < sizeof(shards_resp_header);) {
        iov.iov_base = shards_resp_header + read_so_far;
        iov.iov_len = sizeof(shards_resp_header) - read_so_far;
        int read = kernel_recvmsg(shuckle_sock, &msg, &iov, 1, iov.iov_len, 0);
        if (read < 0) { err = read; goto out_sock; }
        read_so_far += read;
    }
    u32 shuckle_resp_len;
    u8 shuckle_resp_kind;
    err = eggsfs_read_shuckle_resp_header(shards_resp_header, &shuckle_resp_len, &shuckle_resp_kind);
    if (err < 0) { goto out_sock; }
    u16 shard_info_len = get_unaligned_le16(shards_resp_header + sizeof(shards_resp_header) - 2);
    if (shard_info_len != 256) {
        eggsfs_info("expected 256 shard infos, got %d", shard_info_len);
        err = -EIO; goto out_sock;
    }
    if (shuckle_resp_len != 2 + EGGSFS_SHARD_INFO_SIZE*256) {
        eggsfs_info("expected size of %d, got %d", 2 + EGGSFS_SHARD_INFO_SIZE*256, shuckle_resp_len);
        err = -EIO; goto out_sock;
    }

    int shid;
    for (shid = 0; shid < 256; shid++) {
        char shard_info_resp[EGGSFS_SHARD_INFO_SIZE];
        for (read_so_far = 0; read_so_far < EGGSFS_SHARD_INFO_SIZE;) {
            iov.iov_base = shard_info_resp + read_so_far;
            iov.iov_len = sizeof(shard_info_resp) - read_so_far;
            int read = kernel_recvmsg(shuckle_sock, &msg, &iov, 1, iov.iov_len, 0);
            if (read < 0) { err = read; goto out_sock; }
            read_so_far += read;
        }
        struct eggsfs_bincode_get_ctx ctx = {
            .buf = shard_info_resp,
            .end = shard_info_resp + sizeof(shard_info_resp),
            .err = 0,
        };
        eggsfs_shard_info_get_start(&ctx, start);
        eggsfs_shard_info_get_ip1(&ctx, start, shard_ip);
        eggsfs_shard_info_get_port1(&ctx, shard_ip, shard_port);
        eggsfs_shard_info_get_ip2(&ctx, shard_port, shard_ip2);
        eggsfs_shard_info_get_port2(&ctx, shard_ip2, shard_port2);
        eggsfs_shard_info_get_last_seen(&ctx, shard_port2, last_seen);
        eggsfs_shard_info_get_end(&ctx, last_seen, end);
        eggsfs_shard_info_get_finish(&ctx, end);
        if (ctx.err != 0) { err = eggsfs_error_to_linux(ctx.err); goto out_sock; }

        struct sockaddr_in* addr = &info->shards_addrs[shid];
        struct msghdr* hdr = &info->shards_msghdrs[shid];

        addr->sin_addr.s_addr = htonl(shard_ip.x);
        addr->sin_family = AF_INET;
        addr->sin_port = htons(shard_port.x);

        eggsfs_debug("shard %d has addr %pI4:%d", shid, &addr->sin_addr, ntohs(addr->sin_port));

        hdr->msg_name = addr;
        hdr->msg_namelen = sizeof(struct sockaddr_in);
        hdr->msg_control = NULL;
        hdr->msg_controllen = 0;
        hdr->msg_flags = 0;
    }

    static_assert(EGGSFS_CDC_REQ_SIZE == 0);
    eggsfs_write_shuckle_req_header(shuckle_req, EGGSFS_SHARDS_REQ_SIZE, EGGSFS_SHUCKLE_CDC);
    for (written_so_far = 0; written_so_far < sizeof(shuckle_req);) {
        iov.iov_base = shuckle_req + written_so_far;
        iov.iov_len = sizeof(shuckle_req) - written_so_far;
        int written = kernel_sendmsg(shuckle_sock, &msg, &iov, 1, iov.iov_len);
        if (written < 0) { err = written; goto out_sock; }
        written_so_far += written;
    }

    char cdc_resp_header[EGGSFS_SHUCKLE_RESP_HEADER_SIZE];
    for (read_so_far = 0; read_so_far < sizeof(cdc_resp_header);) {
        iov.iov_base = cdc_resp_header + read_so_far;
        iov.iov_len = sizeof(cdc_resp_header) - read_so_far;
        int read = kernel_recvmsg(shuckle_sock, &msg, &iov, 1, iov.iov_len, 0);
        if (read < 0) { err = read; goto out_sock; }
        read_so_far += read;
    }
    err = eggsfs_read_shuckle_resp_header(cdc_resp_header, &shuckle_resp_len, &shuckle_resp_kind);
    if (err < 0) { goto out_sock; }
    if (shuckle_resp_len != EGGSFS_CDC_RESP_SIZE) {
        eggsfs_debug("expected size of %d, got %d", EGGSFS_CDC_RESP_SIZE, shuckle_resp_len);
        err = -EINVAL; goto out_sock;
    }
    {
        char cdc_resp[EGGSFS_CDC_RESP_SIZE];
        for (read_so_far = 0; read_so_far < sizeof(cdc_resp);) {
            iov.iov_base = (char*)&cdc_resp + read_so_far;
            iov.iov_len = sizeof(cdc_resp) - read_so_far;
            int read = kernel_recvmsg(shuckle_sock, &msg, &iov, 1, iov.iov_len, 0);
            if (read < 0) { err = read; goto out_sock; }
            read_so_far += read;
        }
        struct eggsfs_bincode_get_ctx ctx = {
            .buf = cdc_resp,
            .end = cdc_resp + sizeof(cdc_resp),
            .err = 0,
        };
        eggsfs_cdc_resp_get_start(&ctx, start);
        eggsfs_cdc_resp_get_ip1(&ctx, start, cdc_ip);
        eggsfs_cdc_resp_get_port1(&ctx, cdc_ip, cdc_port);
        eggsfs_cdc_resp_get_ip2(&ctx, cdc_port, cdc_ip2);
        eggsfs_cdc_resp_get_port2(&ctx, cdc_ip2, cdc_port2);
        eggsfs_cdc_resp_get_last_seen(&ctx, cdc_port2, last_seen);
        eggsfs_cdc_resp_get_end(&ctx, last_seen, end);
        eggsfs_cdc_resp_get_finish(&ctx, end);
        if (ctx.err != 0) { err = eggsfs_error_to_linux(ctx.err); goto out_sock; }

        struct sockaddr_in* addr = &info->cdc_addr;
        struct msghdr* hdr = &info->cdc_msghdr;

        addr->sin_addr.s_addr = htonl(cdc_ip.x);
        addr->sin_family = AF_INET;
        addr->sin_port = htons(cdc_port.x);

        eggsfs_debug("CDC has addr %pI4:%d", &addr->sin_addr, ntohs(addr->sin_port));

        hdr->msg_name = addr;
        hdr->msg_namelen = sizeof(struct sockaddr_in);
        hdr->msg_control = NULL;
        hdr->msg_controllen = 0;
        hdr->msg_flags = 0;
    }

    sock_release(shuckle_sock);

    eggsfs_info("mount successful");

    return info;

out_sock:
    sock_release(shuckle_sock);
out_info:
    eggsfs_free_fs_info(info);
out:
    return ERR_PTR(err);
}

static void eggsfs_put_super(struct super_block* sb) {
    eggsfs_debug("sb=%p", sb);
    eggsfs_free_fs_info(sb->s_fs_info);
    sb->s_fs_info = NULL;
}

#define EGGSFS_SUPER_MAGIC 0x45474753 // EGGS

static int eggsfs_statfs(struct dentry* dentry, struct kstatfs* stats) {
    struct eggsfs_fs_info* info = (struct eggsfs_fs_info*)dentry->d_sb->s_fs_info;

    struct socket* shuckle_sock;
    int err = eggsfs_create_shuckle_socket(&info->shuckle_addr, &shuckle_sock);
    if (err < 0) {
        eggsfs_info("could not create socket err=%d", err);
        return err;
    }

    struct kvec iov;
    struct msghdr msg = {NULL};

    static_assert(EGGSFS_INFO_REQ_SIZE == 0);
    char shuckle_req[EGGSFS_SHUCKLE_REQ_HEADER_SIZE];
    eggsfs_write_shuckle_req_header(shuckle_req, 0, EGGSFS_SHUCKLE_INFO);
    int written_so_far;
    for (written_so_far = 0; written_so_far < sizeof(shuckle_req);) {
        iov.iov_base = shuckle_req + written_so_far;
        iov.iov_len = sizeof(shuckle_req) - written_so_far;
        int written = kernel_sendmsg(shuckle_sock, &msg, &iov, 1, iov.iov_len);
        if (written < 0) {
            err = written;
            eggsfs_info("could not send msg err=%d", err);
            goto out_sock;
        }
        written_so_far += written;
    }

    char shuckle_resp_header[EGGSFS_SHUCKLE_RESP_HEADER_SIZE];
    int read_so_far;
    for (read_so_far = 0; read_so_far < sizeof(shuckle_resp_header);) {
        iov.iov_base = shuckle_resp_header + read_so_far;
        iov.iov_len = sizeof(shuckle_resp_header) - read_so_far;
        int read = kernel_recvmsg(shuckle_sock, &msg, &iov, 1, iov.iov_len, 0);
        if (read < 0) {
            err = read;
            eggsfs_info("could not recv msg err=%d", err);
            goto out_sock;
        }
        read_so_far += read;
    }
    u32 shuckle_resp_len;
    u8 shuckle_resp_kind;
    err = eggsfs_read_shuckle_resp_header(shuckle_resp_header, &shuckle_resp_len, &shuckle_resp_kind);
    if (err < 0) {
        eggsfs_info("could not read header err=%d", err);
        goto out_sock;
    }
    if (shuckle_resp_len != EGGSFS_INFO_RESP_SIZE) {
        eggsfs_info("expected size of %d, got %d", EGGSFS_INFO_RESP_SIZE, shuckle_resp_len);
        err = -EIO; goto out_sock;
    }
    char shuckle_resp[EGGSFS_INFO_RESP_SIZE];
    for (read_so_far = 0; read_so_far < sizeof(shuckle_resp);) {
        iov.iov_base = (char*)&shuckle_resp + read_so_far;
        iov.iov_len = sizeof(shuckle_resp) - read_so_far;
        int read = kernel_recvmsg(shuckle_sock, &msg, &iov, 1, iov.iov_len, 0);
        if (read < 0) {
            err = read;
            eggsfs_info("could not recv msg err=%d", err);
            goto out_sock;
        }
        read_so_far += read;
    }
    struct eggsfs_bincode_get_ctx ctx = {
        .buf = shuckle_resp,
        .end = shuckle_resp + sizeof(shuckle_resp),
        .err = 0,
    };
    eggsfs_info_resp_get_start(&ctx, start);
    eggsfs_info_resp_get_num_block_services(&ctx, start, num_block_services);
    eggsfs_info_resp_get_num_failure_domains(&ctx, num_block_services, num_failure_domains);
    eggsfs_info_resp_get_capacity(&ctx, num_failure_domains, capacity);
    eggsfs_info_resp_get_available(&ctx, capacity, available);
    eggsfs_info_resp_get_blocks(&ctx, available, blocks);
    eggsfs_info_resp_get_end(&ctx, blocks, end);
    eggsfs_info_resp_get_finish(&ctx, end);
    if (ctx.err != 0) {
        eggsfs_info("response error %s (%d)", eggsfs_err_str(ctx.err), ctx.err);
        err = eggsfs_error_to_linux(ctx.err);
        goto out_sock;
    }

    stats->f_type = EGGSFS_SUPER_MAGIC;
    stats->f_bsize = PAGE_SIZE;
    stats->f_frsize = PAGE_SIZE;
	stats->f_blocks = capacity.x / PAGE_SIZE;
    stats->f_bfree = available.x / PAGE_SIZE;
    stats->f_bavail = available.x / PAGE_SIZE;
    stats->f_files = -1;
    stats->f_ffree = -1;
    stats->f_namelen = EGGSFS_MAX_FILENAME;

    sock_release(shuckle_sock);
    return 0;

out_sock:
    sock_release(shuckle_sock);
    if (err) {
        eggsfs_info("failed err=%d", err);
    }
    return err;
}

static const struct super_operations eggsfs_super_ops = {
    .alloc_inode = eggsfs_inode_alloc,
    .evict_inode = eggsfs_inode_evict,
    .free_inode = eggsfs_inode_free,

    .put_super = eggsfs_put_super,

    .statfs = eggsfs_statfs,
};

static struct dentry* eggsfs_mount(struct file_system_type* fs_type, int flags, const char* dev_name, void* data) {    
    int err;

    eggsfs_info("mounting at %s", dev_name);
    eggsfs_debug("fs_type=%p flags=%d dev_name=%s data=%p", fs_type, flags, dev_name, data);

    struct eggsfs_fs_info* info = eggsfs_init_fs_info(dev_name);
    if (IS_ERR(info)) { err = PTR_ERR(info); goto out_err; }
    
    struct super_block* sb = sget(fs_type, NULL, set_anon_super, flags, NULL);
    if (IS_ERR(sb)) { err = PTR_ERR(sb); goto out_info; }

    sb->s_fs_info = info;

    sb->s_flags = SB_NOSUID | SB_NODEV | SB_NOEXEC | SB_NOATIME | SB_NODIRATIME;
    sb->s_iflags = SB_I_NOEXEC | SB_I_NODEV;

    sb->s_op = &eggsfs_super_ops;
    sb->s_d_op = &eggsfs_dentry_ops;
    sb->s_export_op = &eggsfs_export_ops;

    sb->s_time_gran = 1;
    sb->s_time_min = 0;
    sb->s_time_max = U64_MAX/1000000000ull;
    sb->s_maxbytes= MAX_LFS_FILESIZE;

    struct inode* root = eggsfs_get_inode(sb,  NULL, EGGSFS_ROOT_INODE);
    if (IS_ERR(root)) { err = PTR_ERR(root); goto out_sb; }
    
    struct eggsfs_inode* root_enode = EGGSFS_I(root);

    err = eggsfs_do_getattr(root_enode);
    if (err) { goto out_sb; }

    if (!root_enode->block_policy || !root_enode->span_policy || !root_enode->stripe_policy) {
        eggsfs_warn("no policies for root directory!");
        err = -EIO;
        goto out_sb;
    }

    sb->s_root = d_make_root(root);
    if (!sb->s_root) { err = -ENOMEM; goto out_sb; }

    sb->s_flags |= SB_ACTIVE;

    return dget(sb->s_root);

out_sb:
    deactivate_locked_super(sb);
out_info:
    eggsfs_free_fs_info(info);
out_err:
    eggsfs_debug("failed err=%d", err);
    return ERR_PTR(err);
}

static void eggsfs_kill_sb(struct super_block* sb) {
    eggsfs_debug("sb=%p", sb);
    kill_anon_super(sb);
}

static struct file_system_type eggsfs_fs_type = {
    .owner = THIS_MODULE,
    .name = "eggsfs",
    .mount = eggsfs_mount,
    .kill_sb = eggsfs_kill_sb,
    // TODO is FS_RENAME_DOES_D_MOVE needed?
    .fs_flags = FS_RENAME_DOES_D_MOVE,
};
MODULE_ALIAS_FS("eggsfs");

int __init eggsfs_fs_init(void) {
    return register_filesystem(&eggsfs_fs_type);
}

void __cold eggsfs_fs_exit(void) {
    eggsfs_debug("fs exit");
    unregister_filesystem(&eggsfs_fs_type);
}

