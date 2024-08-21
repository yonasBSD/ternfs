#include "super.h"

#include <linux/backing-dev.h>
#include <linux/inet.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/statfs.h>
#include <linux/workqueue.h>
#include <linux/parser.h>

#include "block_services.h"
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

#define MSECS_TO_JIFFIES(_ms) ((_ms * HZ) / 1000)
int eggsfs_shuckle_refresh_time_jiffies = MSECS_TO_JIFFIES(60000);
unsigned int eggsfs_readahead_pages = 10000;

static void eggsfs_free_fs_info(struct eggsfs_fs_info* info) {
    eggsfs_debug("info=%p", info);
    cancel_delayed_work_sync(&info->shuckle_refresh_work);
    eggsfs_net_shard_free_socket(&info->sock);
    kfree(info);
}

static int eggsfs_refresh_fs_info(struct eggsfs_fs_info* info) {
    int err;

    struct socket* shuckle_sock;
    err = eggsfs_create_shuckle_socket(&info->shuckle_addr1, &info->shuckle_addr2, &shuckle_sock);
    if (err < 0) { return err; }

    struct kvec iov;
    struct msghdr msg = {NULL};
    {
        static_assert(EGGSFS_SHARDS_REQ_SIZE == 0);
        char shuckle_req[EGGSFS_SHUCKLE_REQ_HEADER_SIZE];
        eggsfs_write_shuckle_req_header(shuckle_req, EGGSFS_SHARDS_REQ_SIZE, EGGSFS_SHUCKLE_SHARDS);
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
            if (read == 0) { err = -ECONNRESET; goto out_sock; }
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
                if (read == 0) { err = -ECONNRESET; goto out_sock; }
                if (read < 0) { err = read; goto out_sock; }
                read_so_far += read;
            }
            struct eggsfs_bincode_get_ctx ctx = {
                .buf = shard_info_resp,
                .end = shard_info_resp + sizeof(shard_info_resp),
                .err = 0,
            };
            eggsfs_shard_info_get_start(&ctx, start);
            eggsfs_shard_info_get_addrs(&ctx, start, addr_start);
            eggsfs_addrs_info_get_addr1(&ctx, addr_start, ipport1_start);
            eggsfs_ip_port_get_addrs(&ctx, ipport1_start, shard_ip1);
            eggsfs_ip_port_get_port(&ctx, shard_ip1, shard_port1);
            eggsfs_ip_port_get_end(&ctx, shard_port1, ipport1_end);
            eggsfs_addrs_info_get_addr2(&ctx, ipport1_end, ipport2_start);
            eggsfs_ip_port_get_addrs(&ctx, ipport2_start, shard_ip2);
            eggsfs_ip_port_get_port(&ctx, shard_ip2, shard_port2);
            eggsfs_ip_port_get_end(&ctx, shard_port2, ipport2_end);
            eggsfs_addrs_info_get_end(&ctx, ipport2_end, addr_end);
            eggsfs_shard_info_get_last_seen(&ctx, addr_end, last_seen);
            eggsfs_shard_info_get_end(&ctx, last_seen, end);
            eggsfs_shard_info_get_finish(&ctx, end);
            if (ctx.err != 0) { err = eggsfs_error_to_linux(ctx.err); goto out_sock; }

            atomic64_set(&info->shard_addrs1[shid], eggsfs_mk_addr(shard_ip1.x, shard_port1.x));
            atomic64_set(&info->shard_addrs2[shid], eggsfs_mk_addr(shard_ip2.x, shard_port2.x));
        }
    }
    {
        static_assert(EGGSFS_CDC_REQ_SIZE == 0);
        char cdc_req[EGGSFS_SHUCKLE_REQ_HEADER_SIZE];
        eggsfs_write_shuckle_req_header(cdc_req, EGGSFS_CDC_REQ_SIZE, EGGSFS_SHUCKLE_CDC);
        int written_so_far;
        for (written_so_far = 0; written_so_far < sizeof(cdc_req);) {
            iov.iov_base = cdc_req + written_so_far;
            iov.iov_len = sizeof(cdc_req) - written_so_far;
            int written = kernel_sendmsg(shuckle_sock, &msg, &iov, 1, iov.iov_len);
            if (written < 0) { err = written; goto out_sock; }
            written_so_far += written;
        }

        char cdc_resp_header[EGGSFS_SHUCKLE_RESP_HEADER_SIZE];
        int read_so_far;
        for (read_so_far = 0; read_so_far < sizeof(cdc_resp_header);) {
            iov.iov_base = cdc_resp_header + read_so_far;
            iov.iov_len = sizeof(cdc_resp_header) - read_so_far;
            int read = kernel_recvmsg(shuckle_sock, &msg, &iov, 1, iov.iov_len, 0);
            if (read == 0) { err = -ECONNRESET; goto out_sock; }
            if (read < 0) { err = read; goto out_sock; }
            read_so_far += read;
        }
        u32 shuckle_resp_len;
        u8 shuckle_resp_kind;
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
                if (read == 0) { err = -ECONNRESET; goto out_sock; }
                if (read < 0) { err = read; goto out_sock; }
                read_so_far += read;
            }
            struct eggsfs_bincode_get_ctx ctx = {
                .buf = cdc_resp,
                .end = cdc_resp + sizeof(cdc_resp),
                .err = 0,
            };
            eggsfs_cdc_resp_get_start(&ctx, start);
            eggsfs_cdc_resp_get_addrs(&ctx, start, addr_start);
            eggsfs_addrs_info_get_addr1(&ctx, addr_start, ipport1_start);
            eggsfs_ip_port_get_addrs(&ctx, ipport1_start, cdc_ip1);
            eggsfs_ip_port_get_port(&ctx, cdc_ip1, cdc_port1);
            eggsfs_ip_port_get_end(&ctx, cdc_port1, ipport1_end);
            eggsfs_addrs_info_get_addr2(&ctx, ipport1_end, ipport2_start);
            eggsfs_ip_port_get_addrs(&ctx, ipport2_start, cdc_ip2);
            eggsfs_ip_port_get_port(&ctx, cdc_ip2, cdc_port2);
            eggsfs_ip_port_get_end(&ctx, cdc_port2, ipport2_end);
            eggsfs_addrs_info_get_end(&ctx, ipport2_end, addr_end);
            eggsfs_cdc_resp_get_last_seen(&ctx, addr_end, last_seen);
            eggsfs_cdc_resp_get_end(&ctx, last_seen, end);
            eggsfs_cdc_resp_get_finish(&ctx, end);
            if (ctx.err != 0) { err = eggsfs_error_to_linux(ctx.err); goto out_sock; }

            atomic64_set(&info->cdc_addr1, eggsfs_mk_addr(cdc_ip1.x, cdc_port1.x));
            atomic64_set(&info->cdc_addr2, eggsfs_mk_addr(cdc_ip2.x, cdc_port2.x));
        }
    }

    {
        {
            char changed_block_services_req[EGGSFS_SHUCKLE_REQ_HEADER_SIZE + EGGSFS_BLOCK_SERVICES_WITH_FLAG_CHANGE_REQ_SIZE];
            struct eggsfs_bincode_put_ctx ctx = {
                .start = changed_block_services_req + EGGSFS_SHUCKLE_REQ_HEADER_SIZE,
                .cursor = changed_block_services_req + EGGSFS_SHUCKLE_REQ_HEADER_SIZE,
                .end = changed_block_services_req + sizeof(changed_block_services_req),
            };
            eggsfs_block_services_with_flag_change_req_put_start(&ctx, start);
            eggsfs_block_services_with_flag_change_req_put_changed_since(&ctx, start, changed_since, info->block_services_last_changed_time);
            eggsfs_block_services_with_flag_change_req_put_end(&ctx, changed_since, end);
            eggsfs_write_shuckle_req_header(changed_block_services_req, EGGSFS_BLOCK_SERVICES_WITH_FLAG_CHANGE_REQ_SIZE, EGGSFS_SHUCKLE_BLOCK_SERVICES_WITH_FLAG_CHANGE);
            int written_so_far;
            for (written_so_far = 0; written_so_far < sizeof(changed_block_services_req);) {
                iov.iov_base = changed_block_services_req + written_so_far;
                iov.iov_len = sizeof(changed_block_services_req) - written_so_far;
                int written = kernel_sendmsg(shuckle_sock, &msg, &iov, 1, iov.iov_len);
                if (written < 0) { err = written; goto out_sock; }
                written_so_far += written;
            }
        }
        u32 shuckle_resp_len;
        u8 shuckle_resp_kind;
        {
            char block_services_resp_header[EGGSFS_SHUCKLE_RESP_HEADER_SIZE];
            int read_so_far;
            for (read_so_far = 0; read_so_far < sizeof(block_services_resp_header);) {
                iov.iov_base = block_services_resp_header + read_so_far;
                iov.iov_len = sizeof(block_services_resp_header) - read_so_far;
                int read = kernel_recvmsg(shuckle_sock, &msg, &iov, 1, iov.iov_len, 0);
                if (read == 0) { err = -ECONNRESET; goto out_sock; }
                if (read < 0) { err = read; goto out_sock; }
                read_so_far += read;
            }
            err = eggsfs_read_shuckle_resp_header(block_services_resp_header, &shuckle_resp_len, &shuckle_resp_kind);
            if (err < 0) { goto out_sock; }
        }
        u64 last_changed;
        u16 block_services_len;
        {
            char last_changed_and_len[sizeof(last_changed) + sizeof(block_services_len)];
            if (shuckle_resp_len < sizeof(last_changed_and_len)) {
                eggsfs_debug("expected size of at least %d for BlockServicesWithFlagChangeResp, got %d", sizeof(last_changed_and_len), shuckle_resp_len);
                err = -EINVAL;
                goto out_sock;
            }
            int read_so_far;
            for (read_so_far = 0; read_so_far < sizeof(last_changed_and_len);) {
                iov.iov_base = last_changed_and_len + read_so_far;
                iov.iov_len = sizeof(last_changed_and_len) - read_so_far;
                int read = kernel_recvmsg(shuckle_sock, &msg, &iov, 1, iov.iov_len, 0);
                if (read == 0) { err = -ECONNRESET; goto out_sock; }
                if (read < 0) { err = read; goto out_sock; }
                read_so_far += read;
            }
            last_changed = get_unaligned_le64(last_changed_and_len);
            block_services_len = get_unaligned_le16(last_changed_and_len + sizeof(last_changed));
            shuckle_resp_len -= sizeof(last_changed_and_len);
        }
        {
            if (shuckle_resp_len != EGGSFS_BLOCK_SERVICE_SIZE * block_services_len) {
                eggsfs_debug("expected size of at least %d for %d BlockServices in BlockServicesWithFlagChangeResp, got %d",
                    EGGSFS_BLOCK_SERVICE_SIZE * block_services_len, block_services_len, shuckle_resp_len);
                err = -EINVAL;
                goto out_sock;
            }
            u16 block_service_idx;
            int read_so_far;
            for (block_service_idx = 0; block_service_idx < block_services_len; block_service_idx++) {
                char block_service_buf[EGGSFS_BLOCK_SERVICE_SIZE];
                for (read_so_far = 0; read_so_far < sizeof(block_service_buf);) {
                    iov.iov_base = block_service_buf + read_so_far;
                    iov.iov_len = sizeof(block_service_buf) - read_so_far;
                    int read = kernel_recvmsg(shuckle_sock, &msg, &iov, 1, iov.iov_len, 0);
                    if (read == 0) { err = -ECONNRESET; goto out_sock; }
                    if (read < 0) { err = read; goto out_sock; }
                    read_so_far += read;
                }
                struct eggsfs_bincode_get_ctx bs_ctx = {
                    .buf = block_service_buf,
                    .end = block_service_buf + sizeof(block_service_buf),
                    .err = 0,
                };
                eggsfs_block_service_get_start(&bs_ctx, start);
                eggsfs_block_service_get_addrs(&bs_ctx, start, addr_start);
                eggsfs_addrs_info_get_addr1(&bs_ctx, addr_start, ipport1_start);
                eggsfs_ip_port_get_addrs(&bs_ctx, ipport1_start, ip1);
                eggsfs_ip_port_get_port(&bs_ctx, ip1, port1);
                eggsfs_ip_port_get_end(&bs_ctx, port1, ipport1_end);
                eggsfs_addrs_info_get_addr2(&bs_ctx, ipport1_end, ipport2_start);
                eggsfs_ip_port_get_addrs(&bs_ctx, ipport2_start, ip2);
                eggsfs_ip_port_get_port(&bs_ctx, ip2, port2);
                eggsfs_ip_port_get_end(&bs_ctx, port2, ipport2_end);
                eggsfs_addrs_info_get_end(&bs_ctx, ipport2_end, addr_end);
                eggsfs_block_service_get_id(&bs_ctx, addr_end, bs_id);
                eggsfs_block_service_get_flags(&bs_ctx, bs_id, bs_flags);
                eggsfs_block_service_get_end(&bs_ctx, bs_flags, end);
                eggsfs_block_service_get_finish(&bs_ctx, end);
                if (bs_ctx.err != 0) { err = eggsfs_error_to_linux(bs_ctx.err); goto out_sock; }

                struct eggsfs_block_service bs;
                bs.id = bs_id.x;
                bs.ip1 = ip1.x;
                bs.port1 = port1.x;
                bs.ip2 = ip2.x;
                bs.port2 = port2.x;
                bs.flags = bs_flags.x;
                struct eggsfs_stored_block_service* sbs = eggsfs_upsert_block_service(&bs);
                if (IS_ERR(sbs)) {
                    err = PTR_ERR(sbs);
                    goto out_sock;
                }
            }
        }
        info->block_services_last_changed_time = last_changed;
    }

    sock_release(shuckle_sock);
    return 0;

out_sock:
    sock_release(shuckle_sock);
    return err;
}

static void eggsfs_shuckle_refresh_work(struct work_struct* work) {
    struct eggsfs_fs_info* info = container_of(container_of(work, struct delayed_work, work), struct eggsfs_fs_info, shuckle_refresh_work);
    int err = eggsfs_refresh_fs_info(info);
    if (err != 0 && err != -EAGAIN) {
        eggsfs_warn("failed to refresh shuckle data: %d", err);
    }
    eggsfs_debug("scheduling shuckle data refresh after %dms", jiffies_to_msecs(eggsfs_shuckle_refresh_time_jiffies));
    queue_delayed_work(system_long_wq, &info->shuckle_refresh_work, eggsfs_shuckle_refresh_time_jiffies);
}

enum {
    Opt_err, Opt_uid, Opt_gid, Opt_umask, Opt_dmask, Opt_fmask
};

static const match_table_t tokens = {
    {Opt_uid, "uid=%u"},
    {Opt_gid, "gid=%u"},
    {Opt_umask, "umask=%u"},
    {Opt_dmask, "dmask=%u"},
    {Opt_fmask, "fmask=%u"},
    {Opt_err, NULL}
};

static int eggsfs_parse_options(char* options, struct eggsfs_fs_info* eggsfs_info) {
    char *p;
    int option;
    substring_t args[MAX_OPT_ARGS];

    // defaults
    eggsfs_info->uid = make_kuid(&init_user_ns, 1000);
    eggsfs_info->gid = make_kgid(&init_user_ns, 1000);
    eggsfs_info->dmask = 0000;
    eggsfs_info->fmask = 0111;

    if (!options)
        return 0;

    while ((p = strsep(&options, ",")) != NULL) {
        int token;
        if (!*p)
            continue;

        token = match_token(p, tokens, args);
        switch (token) {
        case Opt_uid:
            if (match_int(&args[0], &option))
                return -EINVAL;
            eggsfs_info->uid = make_kuid(&init_user_ns, option);
            if (!uid_valid(eggsfs_info->uid))
                return -EINVAL;
            break;
        case Opt_gid:
            if (match_int(&args[0], &option))
                return -EINVAL;
            eggsfs_info->gid = make_kgid(&init_user_ns, option);
            if (!gid_valid(eggsfs_info->gid))
                return -EINVAL;
            break;
        case Opt_umask:
            if (match_octal(&args[0], &option))
                return -EINVAL;
            eggsfs_info->fmask = eggsfs_info->dmask = option;
            break;
        case Opt_dmask:
            if (match_octal(&args[0], &option))
                return -EINVAL;
            eggsfs_info->dmask = option;
            break;
        case Opt_fmask:
            if (match_octal(&args[0], &option))
                return -EINVAL;
            eggsfs_info->fmask = option;
            break;
        default:
            eggsfs_error("Invalid mount option \"%s\"", p);
            return -EINVAL;
        }
    }

    return 0;
}

static struct eggsfs_fs_info* eggsfs_init_fs_info(const char* dev_name, char* options) {
    int err;

    struct eggsfs_fs_info* eggsfs_info = kmalloc(sizeof(struct eggsfs_fs_info), GFP_KERNEL);
    if (!eggsfs_info) { err = -ENOMEM; goto out; }

    err = eggsfs_parse_shuckle_addr(dev_name, &eggsfs_info->shuckle_addr1, &eggsfs_info->shuckle_addr2);
    if (err) { goto out_info; }

    err = eggsfs_parse_options(options, eggsfs_info);
    if (err) { goto out_info; }

    err = eggsfs_init_shard_socket(&eggsfs_info->sock);
    if (err) { goto out_info; }

    err = eggsfs_refresh_fs_info(eggsfs_info);
    if (err != 0) { goto out_socket; }

    // for the first update we will ask for everything that changed in last day.
    // this is more than enough time for any older changed to be visible to shards and propagated through block info
    eggsfs_info->block_services_last_changed_time = ktime_get_real_ns() - 86400000000000ull;

    INIT_DELAYED_WORK(&eggsfs_info->shuckle_refresh_work, eggsfs_shuckle_refresh_work);
    queue_delayed_work(system_long_wq, &eggsfs_info->shuckle_refresh_work, eggsfs_shuckle_refresh_time_jiffies);

    return eggsfs_info;

out_socket:
    eggsfs_net_shard_free_socket(&eggsfs_info->sock);
out_info:
    kfree(eggsfs_info);
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
    int err = eggsfs_create_shuckle_socket(&info->shuckle_addr1, &info->shuckle_addr2, &shuckle_sock);
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
        if (read == 0) { err = -ECONNRESET; goto out_sock; }
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
        if (read == 0) { err = -ECONNRESET; goto out_sock; }
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
    eggsfs_debug("fs_type=%p flags=%d dev_name=%s data=%s", fs_type, flags, dev_name, data ? (char*)data : "");

    struct eggsfs_fs_info* info = eggsfs_init_fs_info(dev_name, data);
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
    sb->s_maxbytes = MAX_LFS_FILESIZE;
    sb->s_blocksize = PAGE_SIZE;
    sb->s_magic = EGGSFS_SUPER_MAGIC;
    sb->s_blocksize_bits = ffs(sb->s_blocksize) - 1;

    sb->s_bdi = &noop_backing_dev_info;
    sb->s_bdi->ra_pages = eggsfs_readahead_pages;

    struct inode* root = eggsfs_get_inode_normal(sb, NULL, EGGSFS_ROOT_INODE);
    if (IS_ERR(root)) { err = PTR_ERR(root); goto out_sb; }

    struct eggsfs_inode* root_enode = EGGSFS_I(root);

    err = eggsfs_do_getattr(root_enode, ATTR_CACHE_NORM_TIMEOUT);
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

