#include "err.h"
#include "log.h"
#include "bincode.h"

bool eggsfs_unexpected_error(int err) {
    switch (err) {
    case 0: return false;
    case EGGSFS_ERR_INTERNAL_ERROR: return true;
    case EGGSFS_ERR_FATAL_ERROR: return true;
    case EGGSFS_ERR_TIMEOUT: return true;
    case EGGSFS_ERR_MALFORMED_REQUEST: return true;
    case EGGSFS_ERR_MALFORMED_RESPONSE: return true;
    case EGGSFS_ERR_NOT_AUTHORISED: return true;
    case EGGSFS_ERR_UNRECOGNIZED_REQUEST: return true;
    case EGGSFS_ERR_FILE_NOT_FOUND: return false;
    case EGGSFS_ERR_DIRECTORY_NOT_FOUND: return false;
    case EGGSFS_ERR_NAME_NOT_FOUND: return false;
    case EGGSFS_ERR_EDGE_NOT_FOUND: return false;
    case EGGSFS_ERR_EDGE_IS_LOCKED: return true;
    case EGGSFS_ERR_TYPE_IS_DIRECTORY: return true;
    case EGGSFS_ERR_TYPE_IS_NOT_DIRECTORY: return true;
    case EGGSFS_ERR_BAD_COOKIE: return true;
    case EGGSFS_ERR_INCONSISTENT_STORAGE_CLASS_PARITY: return true;
    case EGGSFS_ERR_LAST_SPAN_STATE_NOT_CLEAN: return true;
    case EGGSFS_ERR_COULD_NOT_PICK_BLOCK_SERVICES: return true;
    case EGGSFS_ERR_BAD_SPAN_BODY: return true;
    case EGGSFS_ERR_SPAN_NOT_FOUND: return true;
    case EGGSFS_ERR_BLOCK_SERVICE_NOT_FOUND: return true;
    case EGGSFS_ERR_CANNOT_CERTIFY_BLOCKLESS_SPAN: return true;
    case EGGSFS_ERR_BAD_NUMBER_OF_BLOCKS_PROOFS: return true;
    case EGGSFS_ERR_BAD_BLOCK_PROOF: return true;
    case EGGSFS_ERR_CANNOT_OVERRIDE_NAME: return true;
    case EGGSFS_ERR_NAME_IS_LOCKED: return true;
    case EGGSFS_ERR_MTIME_IS_TOO_RECENT: return true;
    case EGGSFS_ERR_MISMATCHING_TARGET: return true;
    case EGGSFS_ERR_MISMATCHING_OWNER: return true;
    case EGGSFS_ERR_MISMATCHING_CREATION_TIME: return false;
    case EGGSFS_ERR_DIRECTORY_NOT_EMPTY: return true;
    case EGGSFS_ERR_FILE_IS_TRANSIENT: return true;
    case EGGSFS_ERR_OLD_DIRECTORY_NOT_FOUND: return true;
    case EGGSFS_ERR_NEW_DIRECTORY_NOT_FOUND: return true;
    case EGGSFS_ERR_LOOP_IN_DIRECTORY_RENAME: return true;
    case EGGSFS_ERR_DIRECTORY_HAS_OWNER: return true;
    case EGGSFS_ERR_FILE_IS_NOT_TRANSIENT: return true;
    case EGGSFS_ERR_FILE_NOT_EMPTY: return true;
    case EGGSFS_ERR_CANNOT_REMOVE_ROOT_DIRECTORY: return true;
    case EGGSFS_ERR_FILE_EMPTY: return true;
    case EGGSFS_ERR_CANNOT_REMOVE_DIRTY_SPAN: return true;
    case EGGSFS_ERR_BAD_SHARD: return true;
    case EGGSFS_ERR_BAD_NAME: return true;
    case EGGSFS_ERR_MORE_RECENT_SNAPSHOT_EDGE: return true;
    case EGGSFS_ERR_MORE_RECENT_CURRENT_EDGE: return true;
    case EGGSFS_ERR_BAD_DIRECTORY_INFO: return true;
    case EGGSFS_ERR_DEADLINE_NOT_PASSED: return true;
    case EGGSFS_ERR_SAME_SOURCE_AND_DESTINATION: return true;
    case EGGSFS_ERR_SAME_DIRECTORIES: return true;
    case EGGSFS_ERR_SAME_SHARD: return true;
    case EGGSFS_ERR_BAD_PROTOCOL_VERSION: return true;
    case EGGSFS_ERR_BAD_CERTIFICATE: return true;
    case EGGSFS_ERR_BLOCK_TOO_RECENT_FOR_DELETION: return true;
    case EGGSFS_ERR_BLOCK_FETCH_OUT_OF_BOUNDS: return true;
    case EGGSFS_ERR_BAD_BLOCK_CRC: return true;
    case EGGSFS_ERR_BLOCK_TOO_BIG: return true;
    case EGGSFS_ERR_BLOCK_NOT_FOUND: return true;
    case EGGSFS_ERR_BLOCK_IO_ERROR_FILE: return false;
    case EGGSFS_ERR_BLOCK_IO_ERROR_DEVICE: return false;
    case -ERESTARTSYS: return false;
    case -ETIMEDOUT: return false;
    case -ECONNREFUSED: return false;
    case -ECONNABORTED: return false;
    case -ECONNRESET: return false;
    default: return true;
    }
}

// Safe to use with non-error `err`
int eggsfs_error_to_linux(int err) {
    bool unexpected = eggsfs_unexpected_error(err);
    if (unexpected) {
        eggsfs_warn("unexpected eggsfs error %s (%d)", eggsfs_err_str(err), err);
    }
    if (err <= 0) { return err; }
    switch (err) {
    case EGGSFS_ERR_INTERNAL_ERROR: return -EIO;
    case EGGSFS_ERR_FATAL_ERROR: return -EIO;
    case EGGSFS_ERR_TIMEOUT: return -ETIMEDOUT;
    case EGGSFS_ERR_NOT_AUTHORISED: return -EPERM;
    case EGGSFS_ERR_UNRECOGNIZED_REQUEST: return -EIO;
    case EGGSFS_ERR_FILE_NOT_FOUND: return -ENOENT;
    case EGGSFS_ERR_DIRECTORY_NOT_FOUND: return -ENOENT;
    case EGGSFS_ERR_NAME_NOT_FOUND: return -ENOENT;
    case EGGSFS_ERR_EDGE_NOT_FOUND: return -ENOENT;
    case EGGSFS_ERR_OLD_DIRECTORY_NOT_FOUND: return -ENOENT;
    case EGGSFS_ERR_NEW_DIRECTORY_NOT_FOUND: return -ENOENT;
    case EGGSFS_ERR_TYPE_IS_DIRECTORY: return -EISDIR;
    case EGGSFS_ERR_TYPE_IS_NOT_DIRECTORY: return -ENOTDIR;
    case EGGSFS_ERR_BAD_COOKIE: return -EBADCOOKIE;
    case EGGSFS_ERR_INCONSISTENT_STORAGE_CLASS_PARITY: return -EIO;
    case EGGSFS_ERR_LAST_SPAN_STATE_NOT_CLEAN: return -EIO;
    case EGGSFS_ERR_COULD_NOT_PICK_BLOCK_SERVICES: return -EIO;
    case EGGSFS_ERR_BAD_SPAN_BODY: return -EIO;
    case EGGSFS_ERR_SPAN_NOT_FOUND: return -EIO;
    case EGGSFS_ERR_BLOCK_SERVICE_NOT_FOUND: return -EIO;
    case EGGSFS_ERR_CANNOT_CERTIFY_BLOCKLESS_SPAN: return -EIO;
    case EGGSFS_ERR_BAD_NUMBER_OF_BLOCKS_PROOFS: return -EIO;
    case EGGSFS_ERR_BAD_BLOCK_PROOF: return -EIO;
    case EGGSFS_ERR_CANNOT_OVERRIDE_NAME: return -EEXIST;
    case EGGSFS_ERR_NAME_IS_LOCKED: return -EIO;
    case EGGSFS_ERR_MISMATCHING_TARGET: return -EIO;
    case EGGSFS_ERR_MISMATCHING_OWNER: return -EIO;
    case EGGSFS_ERR_DIRECTORY_NOT_EMPTY: return -ENOTEMPTY;
    case EGGSFS_ERR_FILE_IS_TRANSIENT: return -EIO;
    case EGGSFS_ERR_LOOP_IN_DIRECTORY_RENAME: return -ELOOP;
    case EGGSFS_ERR_MALFORMED_REQUEST: return -EIO;
    case EGGSFS_ERR_MALFORMED_RESPONSE: return -EIO;
    case EGGSFS_ERR_BLOCK_IO_ERROR_FILE: return -EIO;
    case EGGSFS_ERR_BLOCK_IO_ERROR_DEVICE: return -EIO;
    }
    return -EIO;
}
