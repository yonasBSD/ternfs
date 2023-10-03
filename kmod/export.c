#include "export.h"
#include "inode.h"
#include "metadata.h"
#include "err.h"

#include <linux/exportfs.h>

struct eggsfs_fh {
    u64 ino;
    u64 parent_ino;
} __attribute__((packed));

// -1 if the file id is not an eggsfs file id
static int eggsfs_fh_length(int fileid_type) {
    // for some reason the length here is in 32-bit words.
    switch (fileid_type) {
    case FILEID_INO32_GEN:
        return 8/4;
    case FILEID_INO32_GEN_PARENT:
        return 16/4;
    }
    return -1;
}

static int eggsfs_encode_fh(struct inode* inode, u32* fh, int* max_len, struct inode* parent) {
    struct eggsfs_fh* efh = (struct eggsfs_fh*)fh;

    int fileid_type;
    // We reuse FILEID_INO32_GEN/FILEID_INO32_GEN_PARENT so that existing packet sniffers
    // will be able to display something, although it won't be entirely sensible (since
    // we don't have generation numbers), it'll correctly identify the things with and
    // without parents.
    //
    // Note that even if FILEID_INO32_GEN_PARENT is (32bit, 32bit, 32bit), its size has
    // the 32bit padding at the end, so we're fine.
    if (!parent) {
        fileid_type = FILEID_INO32_GEN;
    } else {
        fileid_type = FILEID_INO32_GEN_PARENT;
    }

    int len = eggsfs_fh_length(fileid_type);
    BUG_ON(len < 0);
    if (*max_len < len) {
        *max_len = len;
        return FILEID_INVALID;
    }
    *max_len = len;

    if (parent) {
        efh->parent_ino = parent->i_ino;
    }
    efh->ino = inode->i_ino;

    return fileid_type;
}

static struct inode* eggsfs_nfs_get_inode(struct super_block *sb, u64 ino) {
    // NFS can sometimes send requests for ino 0.  Fail them gracefully.
    if (ino == 0) {
        return ERR_PTR(-ESTALE);
    }

    struct inode* inode = eggsfs_get_inode_export(sb, NULL, ino);
    if (IS_ERR(inode)) {
        eggsfs_debug("returning error %ld as ESTALE", PTR_ERR(inode));
        return ERR_PTR(-ESTALE);
    }
    return inode;
}

static struct dentry* eggsfs_fh_to_dentry(struct super_block *sb, struct fid *fid, int fh_len, int fileid_type) {
    struct eggsfs_fh *efh = (struct eggsfs_fh *)fid;
    
    int expected_len = eggsfs_fh_length(fileid_type);
    if (expected_len < 0) {
        eggsfs_warn("unexpected fid type %d", fileid_type);
        return NULL;
    }
    if (fh_len < expected_len) {
        eggsfs_warn("unexpected fh len %d, expected at least %d", fh_len, expected_len);
        return NULL;
    }

    struct inode* inode = eggsfs_nfs_get_inode(sb, efh->ino);
    if (unlikely(IS_ERR(inode))) {
        return ERR_CAST(inode);
    }

    return d_obtain_alias(inode);
}

static struct dentry* eggsfs_fh_to_parent(struct super_block *sb, struct fid *fid, int fh_len, int fileid_type) {
    struct eggsfs_fh* efh = (struct eggsfs_fh *)fid;

    int expected_len = eggsfs_fh_length(fileid_type);
    if (expected_len < 0) {
        eggsfs_warn("unexpected fid type %d", fileid_type);
        return NULL;
    }
    if (fh_len < expected_len) {
        eggsfs_warn("unexpected fh len %d, expected at least %d", fh_len, expected_len);
        return NULL;
    }
    if (fileid_type != FILEID_INO32_GEN_PARENT) {
        return NULL;
    }

    // we know it's FILEID_INO32_GEN_PARENT by now
    struct inode* inode = eggsfs_nfs_get_inode(sb, efh->parent_ino);
    if (unlikely(IS_ERR(inode))) {
        return ERR_CAST(inode);
    }

    return d_obtain_alias(inode);
}

static struct dentry* eggsfs_get_parent(struct dentry *child) {
    u64 mtime;
    u64 owner;
    struct eggsfs_policy_body block_policy;
    struct eggsfs_policy_body span_policy;
    struct eggsfs_policy_body stripe_policy;
    int err = eggsfs_error_to_linux(eggsfs_shard_getattr_dir(
        (struct eggsfs_fs_info *)child->d_inode->i_sb->s_fs_info,
        child->d_inode->i_ino,
        &mtime,
        &owner,
        &block_policy,
        &span_policy,
        &stripe_policy
    ));
    if (err != 0) {
        return ERR_PTR(err);
    }

    struct inode* inode = eggsfs_nfs_get_inode(child->d_sb, owner);
    if (unlikely(IS_ERR(inode))) {
        return ERR_CAST(inode);
    }

    return d_obtain_alias(inode);
}

struct export_operations eggsfs_export_ops = {
    .encode_fh = eggsfs_encode_fh,
    .fh_to_dentry = eggsfs_fh_to_dentry,
    .fh_to_parent = eggsfs_fh_to_parent,
    .get_parent = eggsfs_get_parent,
};
