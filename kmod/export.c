#include "export.h"
#include "inode.h"
#include "metadata.h"
#include <linux/exportfs.h>

enum eggsfs_fid_type {
    EGGSFS_FILEID_ROOT = 0,
    EGGSFS_FILEID_GEN = 1,
    EGGSFS_FILEID_GEN_PARENT = 2,
};

struct eggsfs_fid64 {
    u64 ino;
    u64 parent_ino;
} __attribute__((packed));

static int eggsfs_fileid_length(int fileid_type) {
    switch (fileid_type) {
    case EGGSFS_FILEID_GEN:
        return 3;
    case EGGSFS_FILEID_GEN_PARENT:
        return 6;
    }
    return FILEID_INVALID;
}

static int eggsfs_encode_fh(struct inode *inode, u32 *fh, int *max_len, struct inode *parent) {
    struct eggsfs_fid64* fid64 = (struct eggsfs_fid64 *)fh;
    int fileid_type;
    int len;

    if (!parent) {
        fileid_type = EGGSFS_FILEID_GEN;
    } else {
        fileid_type = EGGSFS_FILEID_GEN_PARENT;
    }

    len = eggsfs_fileid_length(fileid_type);
    if (*max_len < len) {
        *max_len = len;
        return FILEID_INVALID;
    }
    *max_len = len;

    switch (fileid_type) {
        case EGGSFS_FILEID_GEN_PARENT:
            fid64->parent_ino = EGGSFS_I(parent)->inode.i_ino;
            /*FALLTHRU*/
        case EGGSFS_FILEID_GEN:
            fid64->ino = EGGSFS_I(inode)->inode.i_ino;
            break;
    }

    return fileid_type;
}

static struct inode * eggsfs_nfs_get_inode(struct super_block *sb, u64 ino) {
    // NFS can sometimes send requests for ino 0.  Fail them gracefully.
    if (ino == 0) {
        return ERR_PTR(-ESTALE);
    }

    struct inode *inode = eggsfs_get_inode(sb, NULL, ino);
    if (IS_ERR(inode)) {
        eggsfs_info("returning error %ld as ESTALE", PTR_ERR(inode));
        return ERR_PTR(-ESTALE);
    }
    return inode;
}

static struct dentry * eggsfs_fh_to_dentry(struct super_block *sb, struct fid *fid, int fh_len, int fileid_type) {
    struct eggsfs_fid64 *fid64 = (struct eggsfs_fid64 *)fid;
    struct inode *inode = NULL;

    if (fh_len < eggsfs_fileid_length(fileid_type)) {
        return NULL;
    }

    switch (fileid_type) {
    case EGGSFS_FILEID_GEN_PARENT:
    case EGGSFS_FILEID_GEN:
        inode = eggsfs_nfs_get_inode(sb, fid64->ino);
        break;
    }

    return d_obtain_alias(inode);
}

static struct dentry * eggsfs_fh_to_parent(struct super_block *sb, struct fid *fid, int fh_len, int fileid_type) {
    struct eggsfs_fid64 *fid64 = (struct eggsfs_fid64 *)fid;
    struct inode *inode = NULL;

    if (fh_len < eggsfs_fileid_length(fileid_type)) {
        return NULL;
    }

    switch (fileid_type) {
    case EGGSFS_FILEID_GEN_PARENT:
        inode = eggsfs_nfs_get_inode(sb, fid64->parent_ino);
        break;
    }

    return d_obtain_alias(inode);
}

static struct dentry * eggsfs_get_parent(struct dentry *child) {
    int err;
    u64 mtime;
    u64 owner;
    struct eggsfs_policy_body block_policy;
    struct eggsfs_policy_body span_policy;
    struct eggsfs_policy_body stripe_policy;
    err = eggsfs_shard_getattr_dir(
        (struct eggsfs_fs_info *)child->d_inode->i_sb->s_fs_info,
        child->d_inode->i_ino,
        &mtime,
        &owner,
        &block_policy,
        &span_policy,
        &stripe_policy
    );
    if (err != 0 ) {
        return ERR_PTR(err);
    }
    return d_obtain_alias(eggsfs_nfs_get_inode(child->d_sb, owner));
}

struct export_operations eggsfs_export_ops = {
    .encode_fh    = eggsfs_encode_fh,
    .fh_to_dentry = eggsfs_fh_to_dentry,
    .fh_to_parent = eggsfs_fh_to_parent,
    .get_parent   = eggsfs_get_parent,
};
