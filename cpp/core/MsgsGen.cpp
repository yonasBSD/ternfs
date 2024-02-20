// Automatically generated with go run bincodegen.
// Run `go generate ./...` from the go/ directory to regenerate it.
#include "Msgs.hpp"

std::ostream& operator<<(std::ostream& out, EggsError err) {
    switch (err) {
    case EggsError::INTERNAL_ERROR:
        out << "INTERNAL_ERROR";
        break;
    case EggsError::FATAL_ERROR:
        out << "FATAL_ERROR";
        break;
    case EggsError::TIMEOUT:
        out << "TIMEOUT";
        break;
    case EggsError::MALFORMED_REQUEST:
        out << "MALFORMED_REQUEST";
        break;
    case EggsError::MALFORMED_RESPONSE:
        out << "MALFORMED_RESPONSE";
        break;
    case EggsError::NOT_AUTHORISED:
        out << "NOT_AUTHORISED";
        break;
    case EggsError::UNRECOGNIZED_REQUEST:
        out << "UNRECOGNIZED_REQUEST";
        break;
    case EggsError::FILE_NOT_FOUND:
        out << "FILE_NOT_FOUND";
        break;
    case EggsError::DIRECTORY_NOT_FOUND:
        out << "DIRECTORY_NOT_FOUND";
        break;
    case EggsError::NAME_NOT_FOUND:
        out << "NAME_NOT_FOUND";
        break;
    case EggsError::EDGE_NOT_FOUND:
        out << "EDGE_NOT_FOUND";
        break;
    case EggsError::EDGE_IS_LOCKED:
        out << "EDGE_IS_LOCKED";
        break;
    case EggsError::TYPE_IS_DIRECTORY:
        out << "TYPE_IS_DIRECTORY";
        break;
    case EggsError::TYPE_IS_NOT_DIRECTORY:
        out << "TYPE_IS_NOT_DIRECTORY";
        break;
    case EggsError::BAD_COOKIE:
        out << "BAD_COOKIE";
        break;
    case EggsError::INCONSISTENT_STORAGE_CLASS_PARITY:
        out << "INCONSISTENT_STORAGE_CLASS_PARITY";
        break;
    case EggsError::LAST_SPAN_STATE_NOT_CLEAN:
        out << "LAST_SPAN_STATE_NOT_CLEAN";
        break;
    case EggsError::COULD_NOT_PICK_BLOCK_SERVICES:
        out << "COULD_NOT_PICK_BLOCK_SERVICES";
        break;
    case EggsError::BAD_SPAN_BODY:
        out << "BAD_SPAN_BODY";
        break;
    case EggsError::SPAN_NOT_FOUND:
        out << "SPAN_NOT_FOUND";
        break;
    case EggsError::BLOCK_SERVICE_NOT_FOUND:
        out << "BLOCK_SERVICE_NOT_FOUND";
        break;
    case EggsError::CANNOT_CERTIFY_BLOCKLESS_SPAN:
        out << "CANNOT_CERTIFY_BLOCKLESS_SPAN";
        break;
    case EggsError::BAD_NUMBER_OF_BLOCKS_PROOFS:
        out << "BAD_NUMBER_OF_BLOCKS_PROOFS";
        break;
    case EggsError::BAD_BLOCK_PROOF:
        out << "BAD_BLOCK_PROOF";
        break;
    case EggsError::CANNOT_OVERRIDE_NAME:
        out << "CANNOT_OVERRIDE_NAME";
        break;
    case EggsError::NAME_IS_LOCKED:
        out << "NAME_IS_LOCKED";
        break;
    case EggsError::MTIME_IS_TOO_RECENT:
        out << "MTIME_IS_TOO_RECENT";
        break;
    case EggsError::MISMATCHING_TARGET:
        out << "MISMATCHING_TARGET";
        break;
    case EggsError::MISMATCHING_OWNER:
        out << "MISMATCHING_OWNER";
        break;
    case EggsError::MISMATCHING_CREATION_TIME:
        out << "MISMATCHING_CREATION_TIME";
        break;
    case EggsError::DIRECTORY_NOT_EMPTY:
        out << "DIRECTORY_NOT_EMPTY";
        break;
    case EggsError::FILE_IS_TRANSIENT:
        out << "FILE_IS_TRANSIENT";
        break;
    case EggsError::OLD_DIRECTORY_NOT_FOUND:
        out << "OLD_DIRECTORY_NOT_FOUND";
        break;
    case EggsError::NEW_DIRECTORY_NOT_FOUND:
        out << "NEW_DIRECTORY_NOT_FOUND";
        break;
    case EggsError::LOOP_IN_DIRECTORY_RENAME:
        out << "LOOP_IN_DIRECTORY_RENAME";
        break;
    case EggsError::DIRECTORY_HAS_OWNER:
        out << "DIRECTORY_HAS_OWNER";
        break;
    case EggsError::FILE_IS_NOT_TRANSIENT:
        out << "FILE_IS_NOT_TRANSIENT";
        break;
    case EggsError::FILE_NOT_EMPTY:
        out << "FILE_NOT_EMPTY";
        break;
    case EggsError::CANNOT_REMOVE_ROOT_DIRECTORY:
        out << "CANNOT_REMOVE_ROOT_DIRECTORY";
        break;
    case EggsError::FILE_EMPTY:
        out << "FILE_EMPTY";
        break;
    case EggsError::CANNOT_REMOVE_DIRTY_SPAN:
        out << "CANNOT_REMOVE_DIRTY_SPAN";
        break;
    case EggsError::BAD_SHARD:
        out << "BAD_SHARD";
        break;
    case EggsError::BAD_NAME:
        out << "BAD_NAME";
        break;
    case EggsError::MORE_RECENT_SNAPSHOT_EDGE:
        out << "MORE_RECENT_SNAPSHOT_EDGE";
        break;
    case EggsError::MORE_RECENT_CURRENT_EDGE:
        out << "MORE_RECENT_CURRENT_EDGE";
        break;
    case EggsError::BAD_DIRECTORY_INFO:
        out << "BAD_DIRECTORY_INFO";
        break;
    case EggsError::DEADLINE_NOT_PASSED:
        out << "DEADLINE_NOT_PASSED";
        break;
    case EggsError::SAME_SOURCE_AND_DESTINATION:
        out << "SAME_SOURCE_AND_DESTINATION";
        break;
    case EggsError::SAME_DIRECTORIES:
        out << "SAME_DIRECTORIES";
        break;
    case EggsError::SAME_SHARD:
        out << "SAME_SHARD";
        break;
    case EggsError::BAD_PROTOCOL_VERSION:
        out << "BAD_PROTOCOL_VERSION";
        break;
    case EggsError::BAD_CERTIFICATE:
        out << "BAD_CERTIFICATE";
        break;
    case EggsError::BLOCK_TOO_RECENT_FOR_DELETION:
        out << "BLOCK_TOO_RECENT_FOR_DELETION";
        break;
    case EggsError::BLOCK_FETCH_OUT_OF_BOUNDS:
        out << "BLOCK_FETCH_OUT_OF_BOUNDS";
        break;
    case EggsError::BAD_BLOCK_CRC:
        out << "BAD_BLOCK_CRC";
        break;
    case EggsError::BLOCK_TOO_BIG:
        out << "BLOCK_TOO_BIG";
        break;
    case EggsError::BLOCK_NOT_FOUND:
        out << "BLOCK_NOT_FOUND";
        break;
    case EggsError::CANNOT_UNSET_DECOMMISSIONED:
        out << "CANNOT_UNSET_DECOMMISSIONED";
        break;
    case EggsError::CANNOT_REGISTER_DECOMMISSIONED:
        out << "CANNOT_REGISTER_DECOMMISSIONED";
        break;
    case EggsError::BLOCK_TOO_OLD_FOR_WRITE:
        out << "BLOCK_TOO_OLD_FOR_WRITE";
        break;
    case EggsError::BLOCK_IO_ERROR_DEVICE:
        out << "BLOCK_IO_ERROR_DEVICE";
        break;
    case EggsError::BLOCK_IO_ERROR_FILE:
        out << "BLOCK_IO_ERROR_FILE";
        break;
    case EggsError::INVALID_REPLICA:
        out << "INVALID_REPLICA";
        break;
    case EggsError::DIFFERENT_ADDRS_INFO:
        out << "DIFFERENT_ADDRS_INFO";
        break;
    default:
        out << "EggsError(" << ((int)err) << ")";
        break;
    }
    return out;
}

std::ostream& operator<<(std::ostream& out, ShardMessageKind kind) {
    switch (kind) {
    case ShardMessageKind::LOOKUP:
        out << "LOOKUP";
        break;
    case ShardMessageKind::STAT_FILE:
        out << "STAT_FILE";
        break;
    case ShardMessageKind::STAT_DIRECTORY:
        out << "STAT_DIRECTORY";
        break;
    case ShardMessageKind::READ_DIR:
        out << "READ_DIR";
        break;
    case ShardMessageKind::CONSTRUCT_FILE:
        out << "CONSTRUCT_FILE";
        break;
    case ShardMessageKind::ADD_SPAN_INITIATE:
        out << "ADD_SPAN_INITIATE";
        break;
    case ShardMessageKind::ADD_SPAN_CERTIFY:
        out << "ADD_SPAN_CERTIFY";
        break;
    case ShardMessageKind::LINK_FILE:
        out << "LINK_FILE";
        break;
    case ShardMessageKind::SOFT_UNLINK_FILE:
        out << "SOFT_UNLINK_FILE";
        break;
    case ShardMessageKind::FILE_SPANS:
        out << "FILE_SPANS";
        break;
    case ShardMessageKind::SAME_DIRECTORY_RENAME:
        out << "SAME_DIRECTORY_RENAME";
        break;
    case ShardMessageKind::ADD_INLINE_SPAN:
        out << "ADD_INLINE_SPAN";
        break;
    case ShardMessageKind::SET_TIME:
        out << "SET_TIME";
        break;
    case ShardMessageKind::FULL_READ_DIR:
        out << "FULL_READ_DIR";
        break;
    case ShardMessageKind::MOVE_SPAN:
        out << "MOVE_SPAN";
        break;
    case ShardMessageKind::REMOVE_NON_OWNED_EDGE:
        out << "REMOVE_NON_OWNED_EDGE";
        break;
    case ShardMessageKind::SAME_SHARD_HARD_FILE_UNLINK:
        out << "SAME_SHARD_HARD_FILE_UNLINK";
        break;
    case ShardMessageKind::STAT_TRANSIENT_FILE:
        out << "STAT_TRANSIENT_FILE";
        break;
    case ShardMessageKind::SET_DIRECTORY_INFO:
        out << "SET_DIRECTORY_INFO";
        break;
    case ShardMessageKind::VISIT_DIRECTORIES:
        out << "VISIT_DIRECTORIES";
        break;
    case ShardMessageKind::VISIT_FILES:
        out << "VISIT_FILES";
        break;
    case ShardMessageKind::VISIT_TRANSIENT_FILES:
        out << "VISIT_TRANSIENT_FILES";
        break;
    case ShardMessageKind::REMOVE_SPAN_INITIATE:
        out << "REMOVE_SPAN_INITIATE";
        break;
    case ShardMessageKind::REMOVE_SPAN_CERTIFY:
        out << "REMOVE_SPAN_CERTIFY";
        break;
    case ShardMessageKind::SWAP_BLOCKS:
        out << "SWAP_BLOCKS";
        break;
    case ShardMessageKind::BLOCK_SERVICE_FILES:
        out << "BLOCK_SERVICE_FILES";
        break;
    case ShardMessageKind::REMOVE_INODE:
        out << "REMOVE_INODE";
        break;
    case ShardMessageKind::ADD_SPAN_INITIATE_WITH_REFERENCE:
        out << "ADD_SPAN_INITIATE_WITH_REFERENCE";
        break;
    case ShardMessageKind::REMOVE_ZERO_BLOCK_SERVICE_FILES:
        out << "REMOVE_ZERO_BLOCK_SERVICE_FILES";
        break;
    case ShardMessageKind::CREATE_DIRECTORY_INODE:
        out << "CREATE_DIRECTORY_INODE";
        break;
    case ShardMessageKind::SET_DIRECTORY_OWNER:
        out << "SET_DIRECTORY_OWNER";
        break;
    case ShardMessageKind::REMOVE_DIRECTORY_OWNER:
        out << "REMOVE_DIRECTORY_OWNER";
        break;
    case ShardMessageKind::CREATE_LOCKED_CURRENT_EDGE:
        out << "CREATE_LOCKED_CURRENT_EDGE";
        break;
    case ShardMessageKind::LOCK_CURRENT_EDGE:
        out << "LOCK_CURRENT_EDGE";
        break;
    case ShardMessageKind::UNLOCK_CURRENT_EDGE:
        out << "UNLOCK_CURRENT_EDGE";
        break;
    case ShardMessageKind::REMOVE_OWNED_SNAPSHOT_FILE_EDGE:
        out << "REMOVE_OWNED_SNAPSHOT_FILE_EDGE";
        break;
    case ShardMessageKind::MAKE_FILE_TRANSIENT:
        out << "MAKE_FILE_TRANSIENT";
        break;
    default:
        out << "ShardMessageKind(" << ((int)kind) << ")";
        break;
    }
    return out;
}

std::ostream& operator<<(std::ostream& out, CDCMessageKind kind) {
    switch (kind) {
    case CDCMessageKind::MAKE_DIRECTORY:
        out << "MAKE_DIRECTORY";
        break;
    case CDCMessageKind::RENAME_FILE:
        out << "RENAME_FILE";
        break;
    case CDCMessageKind::SOFT_UNLINK_DIRECTORY:
        out << "SOFT_UNLINK_DIRECTORY";
        break;
    case CDCMessageKind::RENAME_DIRECTORY:
        out << "RENAME_DIRECTORY";
        break;
    case CDCMessageKind::HARD_UNLINK_DIRECTORY:
        out << "HARD_UNLINK_DIRECTORY";
        break;
    case CDCMessageKind::CROSS_SHARD_HARD_UNLINK_FILE:
        out << "CROSS_SHARD_HARD_UNLINK_FILE";
        break;
    default:
        out << "CDCMessageKind(" << ((int)kind) << ")";
        break;
    }
    return out;
}

std::ostream& operator<<(std::ostream& out, ShuckleMessageKind kind) {
    switch (kind) {
    case ShuckleMessageKind::SHARDS:
        out << "SHARDS";
        break;
    case ShuckleMessageKind::CDC:
        out << "CDC";
        break;
    case ShuckleMessageKind::INFO:
        out << "INFO";
        break;
    case ShuckleMessageKind::SHUCKLE:
        out << "SHUCKLE";
        break;
    case ShuckleMessageKind::REGISTER_BLOCK_SERVICES:
        out << "REGISTER_BLOCK_SERVICES";
        break;
    case ShuckleMessageKind::REGISTER_SHARD:
        out << "REGISTER_SHARD";
        break;
    case ShuckleMessageKind::ALL_BLOCK_SERVICES:
        out << "ALL_BLOCK_SERVICES";
        break;
    case ShuckleMessageKind::REGISTER_CDC:
        out << "REGISTER_CDC";
        break;
    case ShuckleMessageKind::SET_BLOCK_SERVICE_FLAGS:
        out << "SET_BLOCK_SERVICE_FLAGS";
        break;
    case ShuckleMessageKind::BLOCK_SERVICE:
        out << "BLOCK_SERVICE";
        break;
    case ShuckleMessageKind::INSERT_STATS:
        out << "INSERT_STATS";
        break;
    case ShuckleMessageKind::SHARD:
        out << "SHARD";
        break;
    case ShuckleMessageKind::GET_STATS:
        out << "GET_STATS";
        break;
    case ShuckleMessageKind::REGISTER_SHARD_REPLICA:
        out << "REGISTER_SHARD_REPLICA";
        break;
    case ShuckleMessageKind::SHARD_REPLICAS:
        out << "SHARD_REPLICAS";
        break;
    case ShuckleMessageKind::SHARD_BLOCK_SERVICES:
        out << "SHARD_BLOCK_SERVICES";
        break;
    case ShuckleMessageKind::REGISTER_CDC_REPLICA:
        out << "REGISTER_CDC_REPLICA";
        break;
    case ShuckleMessageKind::CDC_REPLICAS:
        out << "CDC_REPLICAS";
        break;
    default:
        out << "ShuckleMessageKind(" << ((int)kind) << ")";
        break;
    }
    return out;
}

std::ostream& operator<<(std::ostream& out, BlocksMessageKind kind) {
    switch (kind) {
    case BlocksMessageKind::FETCH_BLOCK:
        out << "FETCH_BLOCK";
        break;
    case BlocksMessageKind::WRITE_BLOCK:
        out << "WRITE_BLOCK";
        break;
    case BlocksMessageKind::ERASE_BLOCK:
        out << "ERASE_BLOCK";
        break;
    case BlocksMessageKind::TEST_WRITE:
        out << "TEST_WRITE";
        break;
    case BlocksMessageKind::CHECK_BLOCK:
        out << "CHECK_BLOCK";
        break;
    default:
        out << "BlocksMessageKind(" << ((int)kind) << ")";
        break;
    }
    return out;
}

void FailureDomain::pack(BincodeBuf& buf) const {
    buf.packFixedBytes<16>(name);
}
void FailureDomain::unpack(BincodeBuf& buf) {
    buf.unpackFixedBytes<16>(name);
}
void FailureDomain::clear() {
    name.clear();
}
bool FailureDomain::operator==(const FailureDomain& rhs) const {
    if (name != rhs.name) { return false; };
    return true;
}
std::ostream& operator<<(std::ostream& out, const FailureDomain& x) {
    out << "FailureDomain(" << "Name=" << x.name << ")";
    return out;
}

void DirectoryInfoEntry::pack(BincodeBuf& buf) const {
    buf.packScalar<uint8_t>(tag);
    buf.packBytes(body);
}
void DirectoryInfoEntry::unpack(BincodeBuf& buf) {
    tag = buf.unpackScalar<uint8_t>();
    buf.unpackBytes(body);
}
void DirectoryInfoEntry::clear() {
    tag = uint8_t(0);
    body.clear();
}
bool DirectoryInfoEntry::operator==(const DirectoryInfoEntry& rhs) const {
    if ((uint8_t)this->tag != (uint8_t)rhs.tag) { return false; };
    if (body != rhs.body) { return false; };
    return true;
}
std::ostream& operator<<(std::ostream& out, const DirectoryInfoEntry& x) {
    out << "DirectoryInfoEntry(" << "Tag=" << (int)x.tag << ", " << "Body=" << x.body << ")";
    return out;
}

void DirectoryInfo::pack(BincodeBuf& buf) const {
    buf.packList<DirectoryInfoEntry>(entries);
}
void DirectoryInfo::unpack(BincodeBuf& buf) {
    buf.unpackList<DirectoryInfoEntry>(entries);
}
void DirectoryInfo::clear() {
    entries.clear();
}
bool DirectoryInfo::operator==(const DirectoryInfo& rhs) const {
    if (entries != rhs.entries) { return false; };
    return true;
}
std::ostream& operator<<(std::ostream& out, const DirectoryInfo& x) {
    out << "DirectoryInfo(" << "Entries=" << x.entries << ")";
    return out;
}

void CurrentEdge::pack(BincodeBuf& buf) const {
    targetId.pack(buf);
    buf.packScalar<uint64_t>(nameHash);
    buf.packBytes(name);
    creationTime.pack(buf);
}
void CurrentEdge::unpack(BincodeBuf& buf) {
    targetId.unpack(buf);
    nameHash = buf.unpackScalar<uint64_t>();
    buf.unpackBytes(name);
    creationTime.unpack(buf);
}
void CurrentEdge::clear() {
    targetId = InodeId();
    nameHash = uint64_t(0);
    name.clear();
    creationTime = EggsTime();
}
bool CurrentEdge::operator==(const CurrentEdge& rhs) const {
    if ((InodeId)this->targetId != (InodeId)rhs.targetId) { return false; };
    if ((uint64_t)this->nameHash != (uint64_t)rhs.nameHash) { return false; };
    if (name != rhs.name) { return false; };
    if ((EggsTime)this->creationTime != (EggsTime)rhs.creationTime) { return false; };
    return true;
}
std::ostream& operator<<(std::ostream& out, const CurrentEdge& x) {
    out << "CurrentEdge(" << "TargetId=" << x.targetId << ", " << "NameHash=" << x.nameHash << ", " << "Name=" << GoLangQuotedStringFmt(x.name.data(), x.name.size()) << ", " << "CreationTime=" << x.creationTime << ")";
    return out;
}

void AddSpanInitiateBlockInfo::pack(BincodeBuf& buf) const {
    buf.packFixedBytes<4>(blockServiceIp1);
    buf.packScalar<uint16_t>(blockServicePort1);
    buf.packFixedBytes<4>(blockServiceIp2);
    buf.packScalar<uint16_t>(blockServicePort2);
    blockServiceId.pack(buf);
    blockServiceFailureDomain.pack(buf);
    buf.packScalar<uint64_t>(blockId);
    buf.packFixedBytes<8>(certificate);
}
void AddSpanInitiateBlockInfo::unpack(BincodeBuf& buf) {
    buf.unpackFixedBytes<4>(blockServiceIp1);
    blockServicePort1 = buf.unpackScalar<uint16_t>();
    buf.unpackFixedBytes<4>(blockServiceIp2);
    blockServicePort2 = buf.unpackScalar<uint16_t>();
    blockServiceId.unpack(buf);
    blockServiceFailureDomain.unpack(buf);
    blockId = buf.unpackScalar<uint64_t>();
    buf.unpackFixedBytes<8>(certificate);
}
void AddSpanInitiateBlockInfo::clear() {
    blockServiceIp1.clear();
    blockServicePort1 = uint16_t(0);
    blockServiceIp2.clear();
    blockServicePort2 = uint16_t(0);
    blockServiceId = BlockServiceId(0);
    blockServiceFailureDomain.clear();
    blockId = uint64_t(0);
    certificate.clear();
}
bool AddSpanInitiateBlockInfo::operator==(const AddSpanInitiateBlockInfo& rhs) const {
    if (blockServiceIp1 != rhs.blockServiceIp1) { return false; };
    if ((uint16_t)this->blockServicePort1 != (uint16_t)rhs.blockServicePort1) { return false; };
    if (blockServiceIp2 != rhs.blockServiceIp2) { return false; };
    if ((uint16_t)this->blockServicePort2 != (uint16_t)rhs.blockServicePort2) { return false; };
    if ((BlockServiceId)this->blockServiceId != (BlockServiceId)rhs.blockServiceId) { return false; };
    if (blockServiceFailureDomain != rhs.blockServiceFailureDomain) { return false; };
    if ((uint64_t)this->blockId != (uint64_t)rhs.blockId) { return false; };
    if (certificate != rhs.certificate) { return false; };
    return true;
}
std::ostream& operator<<(std::ostream& out, const AddSpanInitiateBlockInfo& x) {
    out << "AddSpanInitiateBlockInfo(" << "BlockServiceIp1=" << x.blockServiceIp1 << ", " << "BlockServicePort1=" << x.blockServicePort1 << ", " << "BlockServiceIp2=" << x.blockServiceIp2 << ", " << "BlockServicePort2=" << x.blockServicePort2 << ", " << "BlockServiceId=" << x.blockServiceId << ", " << "BlockServiceFailureDomain=" << x.blockServiceFailureDomain << ", " << "BlockId=" << x.blockId << ", " << "Certificate=" << x.certificate << ")";
    return out;
}

void RemoveSpanInitiateBlockInfo::pack(BincodeBuf& buf) const {
    buf.packFixedBytes<4>(blockServiceIp1);
    buf.packScalar<uint16_t>(blockServicePort1);
    buf.packFixedBytes<4>(blockServiceIp2);
    buf.packScalar<uint16_t>(blockServicePort2);
    blockServiceId.pack(buf);
    blockServiceFailureDomain.pack(buf);
    buf.packScalar<uint8_t>(blockServiceFlags);
    buf.packScalar<uint64_t>(blockId);
    buf.packFixedBytes<8>(certificate);
}
void RemoveSpanInitiateBlockInfo::unpack(BincodeBuf& buf) {
    buf.unpackFixedBytes<4>(blockServiceIp1);
    blockServicePort1 = buf.unpackScalar<uint16_t>();
    buf.unpackFixedBytes<4>(blockServiceIp2);
    blockServicePort2 = buf.unpackScalar<uint16_t>();
    blockServiceId.unpack(buf);
    blockServiceFailureDomain.unpack(buf);
    blockServiceFlags = buf.unpackScalar<uint8_t>();
    blockId = buf.unpackScalar<uint64_t>();
    buf.unpackFixedBytes<8>(certificate);
}
void RemoveSpanInitiateBlockInfo::clear() {
    blockServiceIp1.clear();
    blockServicePort1 = uint16_t(0);
    blockServiceIp2.clear();
    blockServicePort2 = uint16_t(0);
    blockServiceId = BlockServiceId(0);
    blockServiceFailureDomain.clear();
    blockServiceFlags = uint8_t(0);
    blockId = uint64_t(0);
    certificate.clear();
}
bool RemoveSpanInitiateBlockInfo::operator==(const RemoveSpanInitiateBlockInfo& rhs) const {
    if (blockServiceIp1 != rhs.blockServiceIp1) { return false; };
    if ((uint16_t)this->blockServicePort1 != (uint16_t)rhs.blockServicePort1) { return false; };
    if (blockServiceIp2 != rhs.blockServiceIp2) { return false; };
    if ((uint16_t)this->blockServicePort2 != (uint16_t)rhs.blockServicePort2) { return false; };
    if ((BlockServiceId)this->blockServiceId != (BlockServiceId)rhs.blockServiceId) { return false; };
    if (blockServiceFailureDomain != rhs.blockServiceFailureDomain) { return false; };
    if ((uint8_t)this->blockServiceFlags != (uint8_t)rhs.blockServiceFlags) { return false; };
    if ((uint64_t)this->blockId != (uint64_t)rhs.blockId) { return false; };
    if (certificate != rhs.certificate) { return false; };
    return true;
}
std::ostream& operator<<(std::ostream& out, const RemoveSpanInitiateBlockInfo& x) {
    out << "RemoveSpanInitiateBlockInfo(" << "BlockServiceIp1=" << x.blockServiceIp1 << ", " << "BlockServicePort1=" << x.blockServicePort1 << ", " << "BlockServiceIp2=" << x.blockServiceIp2 << ", " << "BlockServicePort2=" << x.blockServicePort2 << ", " << "BlockServiceId=" << x.blockServiceId << ", " << "BlockServiceFailureDomain=" << x.blockServiceFailureDomain << ", " << "BlockServiceFlags=" << (int)x.blockServiceFlags << ", " << "BlockId=" << x.blockId << ", " << "Certificate=" << x.certificate << ")";
    return out;
}

void BlockProof::pack(BincodeBuf& buf) const {
    buf.packScalar<uint64_t>(blockId);
    buf.packFixedBytes<8>(proof);
}
void BlockProof::unpack(BincodeBuf& buf) {
    blockId = buf.unpackScalar<uint64_t>();
    buf.unpackFixedBytes<8>(proof);
}
void BlockProof::clear() {
    blockId = uint64_t(0);
    proof.clear();
}
bool BlockProof::operator==(const BlockProof& rhs) const {
    if ((uint64_t)this->blockId != (uint64_t)rhs.blockId) { return false; };
    if (proof != rhs.proof) { return false; };
    return true;
}
std::ostream& operator<<(std::ostream& out, const BlockProof& x) {
    out << "BlockProof(" << "BlockId=" << x.blockId << ", " << "Proof=" << x.proof << ")";
    return out;
}

void BlockService::pack(BincodeBuf& buf) const {
    buf.packFixedBytes<4>(ip1);
    buf.packScalar<uint16_t>(port1);
    buf.packFixedBytes<4>(ip2);
    buf.packScalar<uint16_t>(port2);
    id.pack(buf);
    buf.packScalar<uint8_t>(flags);
}
void BlockService::unpack(BincodeBuf& buf) {
    buf.unpackFixedBytes<4>(ip1);
    port1 = buf.unpackScalar<uint16_t>();
    buf.unpackFixedBytes<4>(ip2);
    port2 = buf.unpackScalar<uint16_t>();
    id.unpack(buf);
    flags = buf.unpackScalar<uint8_t>();
}
void BlockService::clear() {
    ip1.clear();
    port1 = uint16_t(0);
    ip2.clear();
    port2 = uint16_t(0);
    id = BlockServiceId(0);
    flags = uint8_t(0);
}
bool BlockService::operator==(const BlockService& rhs) const {
    if (ip1 != rhs.ip1) { return false; };
    if ((uint16_t)this->port1 != (uint16_t)rhs.port1) { return false; };
    if (ip2 != rhs.ip2) { return false; };
    if ((uint16_t)this->port2 != (uint16_t)rhs.port2) { return false; };
    if ((BlockServiceId)this->id != (BlockServiceId)rhs.id) { return false; };
    if ((uint8_t)this->flags != (uint8_t)rhs.flags) { return false; };
    return true;
}
std::ostream& operator<<(std::ostream& out, const BlockService& x) {
    out << "BlockService(" << "Ip1=" << x.ip1 << ", " << "Port1=" << x.port1 << ", " << "Ip2=" << x.ip2 << ", " << "Port2=" << x.port2 << ", " << "Id=" << x.id << ", " << "Flags=" << (int)x.flags << ")";
    return out;
}

void ShardInfo::pack(BincodeBuf& buf) const {
    buf.packFixedBytes<4>(ip1);
    buf.packScalar<uint16_t>(port1);
    buf.packFixedBytes<4>(ip2);
    buf.packScalar<uint16_t>(port2);
    lastSeen.pack(buf);
}
void ShardInfo::unpack(BincodeBuf& buf) {
    buf.unpackFixedBytes<4>(ip1);
    port1 = buf.unpackScalar<uint16_t>();
    buf.unpackFixedBytes<4>(ip2);
    port2 = buf.unpackScalar<uint16_t>();
    lastSeen.unpack(buf);
}
void ShardInfo::clear() {
    ip1.clear();
    port1 = uint16_t(0);
    ip2.clear();
    port2 = uint16_t(0);
    lastSeen = EggsTime();
}
bool ShardInfo::operator==(const ShardInfo& rhs) const {
    if (ip1 != rhs.ip1) { return false; };
    if ((uint16_t)this->port1 != (uint16_t)rhs.port1) { return false; };
    if (ip2 != rhs.ip2) { return false; };
    if ((uint16_t)this->port2 != (uint16_t)rhs.port2) { return false; };
    if ((EggsTime)this->lastSeen != (EggsTime)rhs.lastSeen) { return false; };
    return true;
}
std::ostream& operator<<(std::ostream& out, const ShardInfo& x) {
    out << "ShardInfo(" << "Ip1=" << x.ip1 << ", " << "Port1=" << x.port1 << ", " << "Ip2=" << x.ip2 << ", " << "Port2=" << x.port2 << ", " << "LastSeen=" << x.lastSeen << ")";
    return out;
}

void BlockPolicyEntry::pack(BincodeBuf& buf) const {
    buf.packScalar<uint8_t>(storageClass);
    buf.packScalar<uint32_t>(minSize);
}
void BlockPolicyEntry::unpack(BincodeBuf& buf) {
    storageClass = buf.unpackScalar<uint8_t>();
    minSize = buf.unpackScalar<uint32_t>();
}
void BlockPolicyEntry::clear() {
    storageClass = uint8_t(0);
    minSize = uint32_t(0);
}
bool BlockPolicyEntry::operator==(const BlockPolicyEntry& rhs) const {
    if ((uint8_t)this->storageClass != (uint8_t)rhs.storageClass) { return false; };
    if ((uint32_t)this->minSize != (uint32_t)rhs.minSize) { return false; };
    return true;
}
std::ostream& operator<<(std::ostream& out, const BlockPolicyEntry& x) {
    out << "BlockPolicyEntry(" << "StorageClass=" << (int)x.storageClass << ", " << "MinSize=" << x.minSize << ")";
    return out;
}

void SpanPolicyEntry::pack(BincodeBuf& buf) const {
    buf.packScalar<uint32_t>(maxSize);
    parity.pack(buf);
}
void SpanPolicyEntry::unpack(BincodeBuf& buf) {
    maxSize = buf.unpackScalar<uint32_t>();
    parity.unpack(buf);
}
void SpanPolicyEntry::clear() {
    maxSize = uint32_t(0);
    parity = Parity();
}
bool SpanPolicyEntry::operator==(const SpanPolicyEntry& rhs) const {
    if ((uint32_t)this->maxSize != (uint32_t)rhs.maxSize) { return false; };
    if ((Parity)this->parity != (Parity)rhs.parity) { return false; };
    return true;
}
std::ostream& operator<<(std::ostream& out, const SpanPolicyEntry& x) {
    out << "SpanPolicyEntry(" << "MaxSize=" << x.maxSize << ", " << "Parity=" << x.parity << ")";
    return out;
}

void StripePolicy::pack(BincodeBuf& buf) const {
    buf.packScalar<uint32_t>(targetStripeSize);
}
void StripePolicy::unpack(BincodeBuf& buf) {
    targetStripeSize = buf.unpackScalar<uint32_t>();
}
void StripePolicy::clear() {
    targetStripeSize = uint32_t(0);
}
bool StripePolicy::operator==(const StripePolicy& rhs) const {
    if ((uint32_t)this->targetStripeSize != (uint32_t)rhs.targetStripeSize) { return false; };
    return true;
}
std::ostream& operator<<(std::ostream& out, const StripePolicy& x) {
    out << "StripePolicy(" << "TargetStripeSize=" << x.targetStripeSize << ")";
    return out;
}

void FetchedBlock::pack(BincodeBuf& buf) const {
    buf.packScalar<uint8_t>(blockServiceIx);
    buf.packScalar<uint64_t>(blockId);
    crc.pack(buf);
}
void FetchedBlock::unpack(BincodeBuf& buf) {
    blockServiceIx = buf.unpackScalar<uint8_t>();
    blockId = buf.unpackScalar<uint64_t>();
    crc.unpack(buf);
}
void FetchedBlock::clear() {
    blockServiceIx = uint8_t(0);
    blockId = uint64_t(0);
    crc = Crc(0);
}
bool FetchedBlock::operator==(const FetchedBlock& rhs) const {
    if ((uint8_t)this->blockServiceIx != (uint8_t)rhs.blockServiceIx) { return false; };
    if ((uint64_t)this->blockId != (uint64_t)rhs.blockId) { return false; };
    if ((Crc)this->crc != (Crc)rhs.crc) { return false; };
    return true;
}
std::ostream& operator<<(std::ostream& out, const FetchedBlock& x) {
    out << "FetchedBlock(" << "BlockServiceIx=" << (int)x.blockServiceIx << ", " << "BlockId=" << x.blockId << ", " << "Crc=" << x.crc << ")";
    return out;
}

void FetchedSpanHeader::pack(BincodeBuf& buf) const {
    buf.packScalar<uint64_t>(byteOffset);
    buf.packScalar<uint32_t>(size);
    crc.pack(buf);
    buf.packScalar<uint8_t>(storageClass);
}
void FetchedSpanHeader::unpack(BincodeBuf& buf) {
    byteOffset = buf.unpackScalar<uint64_t>();
    size = buf.unpackScalar<uint32_t>();
    crc.unpack(buf);
    storageClass = buf.unpackScalar<uint8_t>();
}
void FetchedSpanHeader::clear() {
    byteOffset = uint64_t(0);
    size = uint32_t(0);
    crc = Crc(0);
    storageClass = uint8_t(0);
}
bool FetchedSpanHeader::operator==(const FetchedSpanHeader& rhs) const {
    if ((uint64_t)this->byteOffset != (uint64_t)rhs.byteOffset) { return false; };
    if ((uint32_t)this->size != (uint32_t)rhs.size) { return false; };
    if ((Crc)this->crc != (Crc)rhs.crc) { return false; };
    if ((uint8_t)this->storageClass != (uint8_t)rhs.storageClass) { return false; };
    return true;
}
std::ostream& operator<<(std::ostream& out, const FetchedSpanHeader& x) {
    out << "FetchedSpanHeader(" << "ByteOffset=" << x.byteOffset << ", " << "Size=" << x.size << ", " << "Crc=" << x.crc << ", " << "StorageClass=" << (int)x.storageClass << ")";
    return out;
}

void FetchedInlineSpan::pack(BincodeBuf& buf) const {
    buf.packBytes(body);
}
void FetchedInlineSpan::unpack(BincodeBuf& buf) {
    buf.unpackBytes(body);
}
void FetchedInlineSpan::clear() {
    body.clear();
}
bool FetchedInlineSpan::operator==(const FetchedInlineSpan& rhs) const {
    if (body != rhs.body) { return false; };
    return true;
}
std::ostream& operator<<(std::ostream& out, const FetchedInlineSpan& x) {
    out << "FetchedInlineSpan(" << "Body=" << x.body << ")";
    return out;
}

void FetchedBlocksSpan::pack(BincodeBuf& buf) const {
    parity.pack(buf);
    buf.packScalar<uint8_t>(stripes);
    buf.packScalar<uint32_t>(cellSize);
    buf.packList<FetchedBlock>(blocks);
    buf.packList<Crc>(stripesCrc);
}
void FetchedBlocksSpan::unpack(BincodeBuf& buf) {
    parity.unpack(buf);
    stripes = buf.unpackScalar<uint8_t>();
    cellSize = buf.unpackScalar<uint32_t>();
    buf.unpackList<FetchedBlock>(blocks);
    buf.unpackList<Crc>(stripesCrc);
}
void FetchedBlocksSpan::clear() {
    parity = Parity();
    stripes = uint8_t(0);
    cellSize = uint32_t(0);
    blocks.clear();
    stripesCrc.clear();
}
bool FetchedBlocksSpan::operator==(const FetchedBlocksSpan& rhs) const {
    if ((Parity)this->parity != (Parity)rhs.parity) { return false; };
    if ((uint8_t)this->stripes != (uint8_t)rhs.stripes) { return false; };
    if ((uint32_t)this->cellSize != (uint32_t)rhs.cellSize) { return false; };
    if (blocks != rhs.blocks) { return false; };
    if (stripesCrc != rhs.stripesCrc) { return false; };
    return true;
}
std::ostream& operator<<(std::ostream& out, const FetchedBlocksSpan& x) {
    out << "FetchedBlocksSpan(" << "Parity=" << x.parity << ", " << "Stripes=" << (int)x.stripes << ", " << "CellSize=" << x.cellSize << ", " << "Blocks=" << x.blocks << ", " << "StripesCrc=" << x.stripesCrc << ")";
    return out;
}

void BlacklistEntry::pack(BincodeBuf& buf) const {
    failureDomain.pack(buf);
    blockService.pack(buf);
}
void BlacklistEntry::unpack(BincodeBuf& buf) {
    failureDomain.unpack(buf);
    blockService.unpack(buf);
}
void BlacklistEntry::clear() {
    failureDomain.clear();
    blockService = BlockServiceId(0);
}
bool BlacklistEntry::operator==(const BlacklistEntry& rhs) const {
    if (failureDomain != rhs.failureDomain) { return false; };
    if ((BlockServiceId)this->blockService != (BlockServiceId)rhs.blockService) { return false; };
    return true;
}
std::ostream& operator<<(std::ostream& out, const BlacklistEntry& x) {
    out << "BlacklistEntry(" << "FailureDomain=" << x.failureDomain << ", " << "BlockService=" << x.blockService << ")";
    return out;
}

void Edge::pack(BincodeBuf& buf) const {
    buf.packScalar<bool>(current);
    targetId.pack(buf);
    buf.packScalar<uint64_t>(nameHash);
    buf.packBytes(name);
    creationTime.pack(buf);
}
void Edge::unpack(BincodeBuf& buf) {
    current = buf.unpackScalar<bool>();
    targetId.unpack(buf);
    nameHash = buf.unpackScalar<uint64_t>();
    buf.unpackBytes(name);
    creationTime.unpack(buf);
}
void Edge::clear() {
    current = bool(0);
    targetId = InodeIdExtra();
    nameHash = uint64_t(0);
    name.clear();
    creationTime = EggsTime();
}
bool Edge::operator==(const Edge& rhs) const {
    if ((bool)this->current != (bool)rhs.current) { return false; };
    if ((InodeIdExtra)this->targetId != (InodeIdExtra)rhs.targetId) { return false; };
    if ((uint64_t)this->nameHash != (uint64_t)rhs.nameHash) { return false; };
    if (name != rhs.name) { return false; };
    if ((EggsTime)this->creationTime != (EggsTime)rhs.creationTime) { return false; };
    return true;
}
std::ostream& operator<<(std::ostream& out, const Edge& x) {
    out << "Edge(" << "Current=" << x.current << ", " << "TargetId=" << x.targetId << ", " << "NameHash=" << x.nameHash << ", " << "Name=" << GoLangQuotedStringFmt(x.name.data(), x.name.size()) << ", " << "CreationTime=" << x.creationTime << ")";
    return out;
}

void FullReadDirCursor::pack(BincodeBuf& buf) const {
    buf.packScalar<bool>(current);
    buf.packBytes(startName);
    startTime.pack(buf);
}
void FullReadDirCursor::unpack(BincodeBuf& buf) {
    current = buf.unpackScalar<bool>();
    buf.unpackBytes(startName);
    startTime.unpack(buf);
}
void FullReadDirCursor::clear() {
    current = bool(0);
    startName.clear();
    startTime = EggsTime();
}
bool FullReadDirCursor::operator==(const FullReadDirCursor& rhs) const {
    if ((bool)this->current != (bool)rhs.current) { return false; };
    if (startName != rhs.startName) { return false; };
    if ((EggsTime)this->startTime != (EggsTime)rhs.startTime) { return false; };
    return true;
}
std::ostream& operator<<(std::ostream& out, const FullReadDirCursor& x) {
    out << "FullReadDirCursor(" << "Current=" << x.current << ", " << "StartName=" << GoLangQuotedStringFmt(x.startName.data(), x.startName.size()) << ", " << "StartTime=" << x.startTime << ")";
    return out;
}

void TransientFile::pack(BincodeBuf& buf) const {
    id.pack(buf);
    buf.packFixedBytes<8>(cookie);
    deadlineTime.pack(buf);
}
void TransientFile::unpack(BincodeBuf& buf) {
    id.unpack(buf);
    buf.unpackFixedBytes<8>(cookie);
    deadlineTime.unpack(buf);
}
void TransientFile::clear() {
    id = InodeId();
    cookie.clear();
    deadlineTime = EggsTime();
}
bool TransientFile::operator==(const TransientFile& rhs) const {
    if ((InodeId)this->id != (InodeId)rhs.id) { return false; };
    if (cookie != rhs.cookie) { return false; };
    if ((EggsTime)this->deadlineTime != (EggsTime)rhs.deadlineTime) { return false; };
    return true;
}
std::ostream& operator<<(std::ostream& out, const TransientFile& x) {
    out << "TransientFile(" << "Id=" << x.id << ", " << "Cookie=" << x.cookie << ", " << "DeadlineTime=" << x.deadlineTime << ")";
    return out;
}

void EntryNewBlockInfo::pack(BincodeBuf& buf) const {
    blockServiceId.pack(buf);
    crc.pack(buf);
}
void EntryNewBlockInfo::unpack(BincodeBuf& buf) {
    blockServiceId.unpack(buf);
    crc.unpack(buf);
}
void EntryNewBlockInfo::clear() {
    blockServiceId = BlockServiceId(0);
    crc = Crc(0);
}
bool EntryNewBlockInfo::operator==(const EntryNewBlockInfo& rhs) const {
    if ((BlockServiceId)this->blockServiceId != (BlockServiceId)rhs.blockServiceId) { return false; };
    if ((Crc)this->crc != (Crc)rhs.crc) { return false; };
    return true;
}
std::ostream& operator<<(std::ostream& out, const EntryNewBlockInfo& x) {
    out << "EntryNewBlockInfo(" << "BlockServiceId=" << x.blockServiceId << ", " << "Crc=" << x.crc << ")";
    return out;
}

void RegisterBlockServiceInfo::pack(BincodeBuf& buf) const {
    id.pack(buf);
    buf.packFixedBytes<4>(ip1);
    buf.packScalar<uint16_t>(port1);
    buf.packFixedBytes<4>(ip2);
    buf.packScalar<uint16_t>(port2);
    buf.packScalar<uint8_t>(storageClass);
    failureDomain.pack(buf);
    buf.packFixedBytes<16>(secretKey);
    buf.packScalar<uint8_t>(flags);
    buf.packScalar<uint64_t>(capacityBytes);
    buf.packScalar<uint64_t>(availableBytes);
    buf.packScalar<uint64_t>(blocks);
    buf.packBytes(path);
    lastSeen.pack(buf);
}
void RegisterBlockServiceInfo::unpack(BincodeBuf& buf) {
    id.unpack(buf);
    buf.unpackFixedBytes<4>(ip1);
    port1 = buf.unpackScalar<uint16_t>();
    buf.unpackFixedBytes<4>(ip2);
    port2 = buf.unpackScalar<uint16_t>();
    storageClass = buf.unpackScalar<uint8_t>();
    failureDomain.unpack(buf);
    buf.unpackFixedBytes<16>(secretKey);
    flags = buf.unpackScalar<uint8_t>();
    capacityBytes = buf.unpackScalar<uint64_t>();
    availableBytes = buf.unpackScalar<uint64_t>();
    blocks = buf.unpackScalar<uint64_t>();
    buf.unpackBytes(path);
    lastSeen.unpack(buf);
}
void RegisterBlockServiceInfo::clear() {
    id = BlockServiceId(0);
    ip1.clear();
    port1 = uint16_t(0);
    ip2.clear();
    port2 = uint16_t(0);
    storageClass = uint8_t(0);
    failureDomain.clear();
    secretKey.clear();
    flags = uint8_t(0);
    capacityBytes = uint64_t(0);
    availableBytes = uint64_t(0);
    blocks = uint64_t(0);
    path.clear();
    lastSeen = EggsTime();
}
bool RegisterBlockServiceInfo::operator==(const RegisterBlockServiceInfo& rhs) const {
    if ((BlockServiceId)this->id != (BlockServiceId)rhs.id) { return false; };
    if (ip1 != rhs.ip1) { return false; };
    if ((uint16_t)this->port1 != (uint16_t)rhs.port1) { return false; };
    if (ip2 != rhs.ip2) { return false; };
    if ((uint16_t)this->port2 != (uint16_t)rhs.port2) { return false; };
    if ((uint8_t)this->storageClass != (uint8_t)rhs.storageClass) { return false; };
    if (failureDomain != rhs.failureDomain) { return false; };
    if (secretKey != rhs.secretKey) { return false; };
    if ((uint8_t)this->flags != (uint8_t)rhs.flags) { return false; };
    if ((uint64_t)this->capacityBytes != (uint64_t)rhs.capacityBytes) { return false; };
    if ((uint64_t)this->availableBytes != (uint64_t)rhs.availableBytes) { return false; };
    if ((uint64_t)this->blocks != (uint64_t)rhs.blocks) { return false; };
    if (path != rhs.path) { return false; };
    if ((EggsTime)this->lastSeen != (EggsTime)rhs.lastSeen) { return false; };
    return true;
}
std::ostream& operator<<(std::ostream& out, const RegisterBlockServiceInfo& x) {
    out << "RegisterBlockServiceInfo(" << "Id=" << x.id << ", " << "Ip1=" << x.ip1 << ", " << "Port1=" << x.port1 << ", " << "Ip2=" << x.ip2 << ", " << "Port2=" << x.port2 << ", " << "StorageClass=" << (int)x.storageClass << ", " << "FailureDomain=" << x.failureDomain << ", " << "SecretKey=" << x.secretKey << ", " << "Flags=" << (int)x.flags << ", " << "CapacityBytes=" << x.capacityBytes << ", " << "AvailableBytes=" << x.availableBytes << ", " << "Blocks=" << x.blocks << ", " << "Path=" << GoLangQuotedStringFmt(x.path.data(), x.path.size()) << ", " << "LastSeen=" << x.lastSeen << ")";
    return out;
}

void BlockServiceInfo::pack(BincodeBuf& buf) const {
    info.pack(buf);
    buf.packScalar<bool>(hasFiles);
}
void BlockServiceInfo::unpack(BincodeBuf& buf) {
    info.unpack(buf);
    hasFiles = buf.unpackScalar<bool>();
}
void BlockServiceInfo::clear() {
    info.clear();
    hasFiles = bool(0);
}
bool BlockServiceInfo::operator==(const BlockServiceInfo& rhs) const {
    if (info != rhs.info) { return false; };
    if ((bool)this->hasFiles != (bool)rhs.hasFiles) { return false; };
    return true;
}
std::ostream& operator<<(std::ostream& out, const BlockServiceInfo& x) {
    out << "BlockServiceInfo(" << "Info=" << x.info << ", " << "HasFiles=" << x.hasFiles << ")";
    return out;
}

void AddrsInfo::pack(BincodeBuf& buf) const {
    buf.packFixedBytes<4>(ip1);
    buf.packScalar<uint16_t>(port1);
    buf.packFixedBytes<4>(ip2);
    buf.packScalar<uint16_t>(port2);
}
void AddrsInfo::unpack(BincodeBuf& buf) {
    buf.unpackFixedBytes<4>(ip1);
    port1 = buf.unpackScalar<uint16_t>();
    buf.unpackFixedBytes<4>(ip2);
    port2 = buf.unpackScalar<uint16_t>();
}
void AddrsInfo::clear() {
    ip1.clear();
    port1 = uint16_t(0);
    ip2.clear();
    port2 = uint16_t(0);
}
bool AddrsInfo::operator==(const AddrsInfo& rhs) const {
    if (ip1 != rhs.ip1) { return false; };
    if ((uint16_t)this->port1 != (uint16_t)rhs.port1) { return false; };
    if (ip2 != rhs.ip2) { return false; };
    if ((uint16_t)this->port2 != (uint16_t)rhs.port2) { return false; };
    return true;
}
std::ostream& operator<<(std::ostream& out, const AddrsInfo& x) {
    out << "AddrsInfo(" << "Ip1=" << x.ip1 << ", " << "Port1=" << x.port1 << ", " << "Ip2=" << x.ip2 << ", " << "Port2=" << x.port2 << ")";
    return out;
}

void SpanPolicy::pack(BincodeBuf& buf) const {
    buf.packList<SpanPolicyEntry>(entries);
}
void SpanPolicy::unpack(BincodeBuf& buf) {
    buf.unpackList<SpanPolicyEntry>(entries);
}
void SpanPolicy::clear() {
    entries.clear();
}
bool SpanPolicy::operator==(const SpanPolicy& rhs) const {
    if (entries != rhs.entries) { return false; };
    return true;
}
std::ostream& operator<<(std::ostream& out, const SpanPolicy& x) {
    out << "SpanPolicy(" << "Entries=" << x.entries << ")";
    return out;
}

void BlockPolicy::pack(BincodeBuf& buf) const {
    buf.packList<BlockPolicyEntry>(entries);
}
void BlockPolicy::unpack(BincodeBuf& buf) {
    buf.unpackList<BlockPolicyEntry>(entries);
}
void BlockPolicy::clear() {
    entries.clear();
}
bool BlockPolicy::operator==(const BlockPolicy& rhs) const {
    if (entries != rhs.entries) { return false; };
    return true;
}
std::ostream& operator<<(std::ostream& out, const BlockPolicy& x) {
    out << "BlockPolicy(" << "Entries=" << x.entries << ")";
    return out;
}

void SnapshotPolicy::pack(BincodeBuf& buf) const {
    buf.packScalar<uint64_t>(deleteAfterTime);
    buf.packScalar<uint16_t>(deleteAfterVersions);
}
void SnapshotPolicy::unpack(BincodeBuf& buf) {
    deleteAfterTime = buf.unpackScalar<uint64_t>();
    deleteAfterVersions = buf.unpackScalar<uint16_t>();
}
void SnapshotPolicy::clear() {
    deleteAfterTime = uint64_t(0);
    deleteAfterVersions = uint16_t(0);
}
bool SnapshotPolicy::operator==(const SnapshotPolicy& rhs) const {
    if ((uint64_t)this->deleteAfterTime != (uint64_t)rhs.deleteAfterTime) { return false; };
    if ((uint16_t)this->deleteAfterVersions != (uint16_t)rhs.deleteAfterVersions) { return false; };
    return true;
}
std::ostream& operator<<(std::ostream& out, const SnapshotPolicy& x) {
    out << "SnapshotPolicy(" << "DeleteAfterTime=" << x.deleteAfterTime << ", " << "DeleteAfterVersions=" << x.deleteAfterVersions << ")";
    return out;
}

void Stat::pack(BincodeBuf& buf) const {
    buf.packBytes(name);
    time.pack(buf);
    buf.packList<uint8_t>(value);
}
void Stat::unpack(BincodeBuf& buf) {
    buf.unpackBytes(name);
    time.unpack(buf);
    buf.unpackList<uint8_t>(value);
}
void Stat::clear() {
    name.clear();
    time = EggsTime();
    value.clear();
}
bool Stat::operator==(const Stat& rhs) const {
    if (name != rhs.name) { return false; };
    if ((EggsTime)this->time != (EggsTime)rhs.time) { return false; };
    if (value != rhs.value) { return false; };
    return true;
}
std::ostream& operator<<(std::ostream& out, const Stat& x) {
    out << "Stat(" << "Name=" << GoLangQuotedStringFmt(x.name.data(), x.name.size()) << ", " << "Time=" << x.time << ", " << "Value=" << x.value << ")";
    return out;
}

void LookupReq::pack(BincodeBuf& buf) const {
    dirId.pack(buf);
    buf.packBytes(name);
}
void LookupReq::unpack(BincodeBuf& buf) {
    dirId.unpack(buf);
    buf.unpackBytes(name);
}
void LookupReq::clear() {
    dirId = InodeId();
    name.clear();
}
bool LookupReq::operator==(const LookupReq& rhs) const {
    if ((InodeId)this->dirId != (InodeId)rhs.dirId) { return false; };
    if (name != rhs.name) { return false; };
    return true;
}
std::ostream& operator<<(std::ostream& out, const LookupReq& x) {
    out << "LookupReq(" << "DirId=" << x.dirId << ", " << "Name=" << GoLangQuotedStringFmt(x.name.data(), x.name.size()) << ")";
    return out;
}

void LookupResp::pack(BincodeBuf& buf) const {
    targetId.pack(buf);
    creationTime.pack(buf);
}
void LookupResp::unpack(BincodeBuf& buf) {
    targetId.unpack(buf);
    creationTime.unpack(buf);
}
void LookupResp::clear() {
    targetId = InodeId();
    creationTime = EggsTime();
}
bool LookupResp::operator==(const LookupResp& rhs) const {
    if ((InodeId)this->targetId != (InodeId)rhs.targetId) { return false; };
    if ((EggsTime)this->creationTime != (EggsTime)rhs.creationTime) { return false; };
    return true;
}
std::ostream& operator<<(std::ostream& out, const LookupResp& x) {
    out << "LookupResp(" << "TargetId=" << x.targetId << ", " << "CreationTime=" << x.creationTime << ")";
    return out;
}

void StatFileReq::pack(BincodeBuf& buf) const {
    id.pack(buf);
}
void StatFileReq::unpack(BincodeBuf& buf) {
    id.unpack(buf);
}
void StatFileReq::clear() {
    id = InodeId();
}
bool StatFileReq::operator==(const StatFileReq& rhs) const {
    if ((InodeId)this->id != (InodeId)rhs.id) { return false; };
    return true;
}
std::ostream& operator<<(std::ostream& out, const StatFileReq& x) {
    out << "StatFileReq(" << "Id=" << x.id << ")";
    return out;
}

void StatFileResp::pack(BincodeBuf& buf) const {
    mtime.pack(buf);
    atime.pack(buf);
    buf.packScalar<uint64_t>(size);
}
void StatFileResp::unpack(BincodeBuf& buf) {
    mtime.unpack(buf);
    atime.unpack(buf);
    size = buf.unpackScalar<uint64_t>();
}
void StatFileResp::clear() {
    mtime = EggsTime();
    atime = EggsTime();
    size = uint64_t(0);
}
bool StatFileResp::operator==(const StatFileResp& rhs) const {
    if ((EggsTime)this->mtime != (EggsTime)rhs.mtime) { return false; };
    if ((EggsTime)this->atime != (EggsTime)rhs.atime) { return false; };
    if ((uint64_t)this->size != (uint64_t)rhs.size) { return false; };
    return true;
}
std::ostream& operator<<(std::ostream& out, const StatFileResp& x) {
    out << "StatFileResp(" << "Mtime=" << x.mtime << ", " << "Atime=" << x.atime << ", " << "Size=" << x.size << ")";
    return out;
}

void StatDirectoryReq::pack(BincodeBuf& buf) const {
    id.pack(buf);
}
void StatDirectoryReq::unpack(BincodeBuf& buf) {
    id.unpack(buf);
}
void StatDirectoryReq::clear() {
    id = InodeId();
}
bool StatDirectoryReq::operator==(const StatDirectoryReq& rhs) const {
    if ((InodeId)this->id != (InodeId)rhs.id) { return false; };
    return true;
}
std::ostream& operator<<(std::ostream& out, const StatDirectoryReq& x) {
    out << "StatDirectoryReq(" << "Id=" << x.id << ")";
    return out;
}

void StatDirectoryResp::pack(BincodeBuf& buf) const {
    mtime.pack(buf);
    owner.pack(buf);
    info.pack(buf);
}
void StatDirectoryResp::unpack(BincodeBuf& buf) {
    mtime.unpack(buf);
    owner.unpack(buf);
    info.unpack(buf);
}
void StatDirectoryResp::clear() {
    mtime = EggsTime();
    owner = InodeId();
    info.clear();
}
bool StatDirectoryResp::operator==(const StatDirectoryResp& rhs) const {
    if ((EggsTime)this->mtime != (EggsTime)rhs.mtime) { return false; };
    if ((InodeId)this->owner != (InodeId)rhs.owner) { return false; };
    if (info != rhs.info) { return false; };
    return true;
}
std::ostream& operator<<(std::ostream& out, const StatDirectoryResp& x) {
    out << "StatDirectoryResp(" << "Mtime=" << x.mtime << ", " << "Owner=" << x.owner << ", " << "Info=" << x.info << ")";
    return out;
}

void ReadDirReq::pack(BincodeBuf& buf) const {
    dirId.pack(buf);
    buf.packScalar<uint64_t>(startHash);
    buf.packScalar<uint16_t>(mtu);
}
void ReadDirReq::unpack(BincodeBuf& buf) {
    dirId.unpack(buf);
    startHash = buf.unpackScalar<uint64_t>();
    mtu = buf.unpackScalar<uint16_t>();
}
void ReadDirReq::clear() {
    dirId = InodeId();
    startHash = uint64_t(0);
    mtu = uint16_t(0);
}
bool ReadDirReq::operator==(const ReadDirReq& rhs) const {
    if ((InodeId)this->dirId != (InodeId)rhs.dirId) { return false; };
    if ((uint64_t)this->startHash != (uint64_t)rhs.startHash) { return false; };
    if ((uint16_t)this->mtu != (uint16_t)rhs.mtu) { return false; };
    return true;
}
std::ostream& operator<<(std::ostream& out, const ReadDirReq& x) {
    out << "ReadDirReq(" << "DirId=" << x.dirId << ", " << "StartHash=" << x.startHash << ", " << "Mtu=" << x.mtu << ")";
    return out;
}

void ReadDirResp::pack(BincodeBuf& buf) const {
    buf.packScalar<uint64_t>(nextHash);
    buf.packList<CurrentEdge>(results);
}
void ReadDirResp::unpack(BincodeBuf& buf) {
    nextHash = buf.unpackScalar<uint64_t>();
    buf.unpackList<CurrentEdge>(results);
}
void ReadDirResp::clear() {
    nextHash = uint64_t(0);
    results.clear();
}
bool ReadDirResp::operator==(const ReadDirResp& rhs) const {
    if ((uint64_t)this->nextHash != (uint64_t)rhs.nextHash) { return false; };
    if (results != rhs.results) { return false; };
    return true;
}
std::ostream& operator<<(std::ostream& out, const ReadDirResp& x) {
    out << "ReadDirResp(" << "NextHash=" << x.nextHash << ", " << "Results=" << x.results << ")";
    return out;
}

void ConstructFileReq::pack(BincodeBuf& buf) const {
    buf.packScalar<uint8_t>(type);
    buf.packBytes(note);
}
void ConstructFileReq::unpack(BincodeBuf& buf) {
    type = buf.unpackScalar<uint8_t>();
    buf.unpackBytes(note);
}
void ConstructFileReq::clear() {
    type = uint8_t(0);
    note.clear();
}
bool ConstructFileReq::operator==(const ConstructFileReq& rhs) const {
    if ((uint8_t)this->type != (uint8_t)rhs.type) { return false; };
    if (note != rhs.note) { return false; };
    return true;
}
std::ostream& operator<<(std::ostream& out, const ConstructFileReq& x) {
    out << "ConstructFileReq(" << "Type=" << (int)x.type << ", " << "Note=" << GoLangQuotedStringFmt(x.note.data(), x.note.size()) << ")";
    return out;
}

void ConstructFileResp::pack(BincodeBuf& buf) const {
    id.pack(buf);
    buf.packFixedBytes<8>(cookie);
}
void ConstructFileResp::unpack(BincodeBuf& buf) {
    id.unpack(buf);
    buf.unpackFixedBytes<8>(cookie);
}
void ConstructFileResp::clear() {
    id = InodeId();
    cookie.clear();
}
bool ConstructFileResp::operator==(const ConstructFileResp& rhs) const {
    if ((InodeId)this->id != (InodeId)rhs.id) { return false; };
    if (cookie != rhs.cookie) { return false; };
    return true;
}
std::ostream& operator<<(std::ostream& out, const ConstructFileResp& x) {
    out << "ConstructFileResp(" << "Id=" << x.id << ", " << "Cookie=" << x.cookie << ")";
    return out;
}

void AddSpanInitiateReq::pack(BincodeBuf& buf) const {
    fileId.pack(buf);
    buf.packFixedBytes<8>(cookie);
    buf.packScalar<uint64_t>(byteOffset);
    buf.packScalar<uint32_t>(size);
    crc.pack(buf);
    buf.packScalar<uint8_t>(storageClass);
    buf.packList<BlacklistEntry>(blacklist);
    parity.pack(buf);
    buf.packScalar<uint8_t>(stripes);
    buf.packScalar<uint32_t>(cellSize);
    buf.packList<Crc>(crcs);
}
void AddSpanInitiateReq::unpack(BincodeBuf& buf) {
    fileId.unpack(buf);
    buf.unpackFixedBytes<8>(cookie);
    byteOffset = buf.unpackScalar<uint64_t>();
    size = buf.unpackScalar<uint32_t>();
    crc.unpack(buf);
    storageClass = buf.unpackScalar<uint8_t>();
    buf.unpackList<BlacklistEntry>(blacklist);
    parity.unpack(buf);
    stripes = buf.unpackScalar<uint8_t>();
    cellSize = buf.unpackScalar<uint32_t>();
    buf.unpackList<Crc>(crcs);
}
void AddSpanInitiateReq::clear() {
    fileId = InodeId();
    cookie.clear();
    byteOffset = uint64_t(0);
    size = uint32_t(0);
    crc = Crc(0);
    storageClass = uint8_t(0);
    blacklist.clear();
    parity = Parity();
    stripes = uint8_t(0);
    cellSize = uint32_t(0);
    crcs.clear();
}
bool AddSpanInitiateReq::operator==(const AddSpanInitiateReq& rhs) const {
    if ((InodeId)this->fileId != (InodeId)rhs.fileId) { return false; };
    if (cookie != rhs.cookie) { return false; };
    if ((uint64_t)this->byteOffset != (uint64_t)rhs.byteOffset) { return false; };
    if ((uint32_t)this->size != (uint32_t)rhs.size) { return false; };
    if ((Crc)this->crc != (Crc)rhs.crc) { return false; };
    if ((uint8_t)this->storageClass != (uint8_t)rhs.storageClass) { return false; };
    if (blacklist != rhs.blacklist) { return false; };
    if ((Parity)this->parity != (Parity)rhs.parity) { return false; };
    if ((uint8_t)this->stripes != (uint8_t)rhs.stripes) { return false; };
    if ((uint32_t)this->cellSize != (uint32_t)rhs.cellSize) { return false; };
    if (crcs != rhs.crcs) { return false; };
    return true;
}
std::ostream& operator<<(std::ostream& out, const AddSpanInitiateReq& x) {
    out << "AddSpanInitiateReq(" << "FileId=" << x.fileId << ", " << "Cookie=" << x.cookie << ", " << "ByteOffset=" << x.byteOffset << ", " << "Size=" << x.size << ", " << "Crc=" << x.crc << ", " << "StorageClass=" << (int)x.storageClass << ", " << "Blacklist=" << x.blacklist << ", " << "Parity=" << x.parity << ", " << "Stripes=" << (int)x.stripes << ", " << "CellSize=" << x.cellSize << ", " << "Crcs=" << x.crcs << ")";
    return out;
}

void AddSpanInitiateResp::pack(BincodeBuf& buf) const {
    buf.packList<AddSpanInitiateBlockInfo>(blocks);
}
void AddSpanInitiateResp::unpack(BincodeBuf& buf) {
    buf.unpackList<AddSpanInitiateBlockInfo>(blocks);
}
void AddSpanInitiateResp::clear() {
    blocks.clear();
}
bool AddSpanInitiateResp::operator==(const AddSpanInitiateResp& rhs) const {
    if (blocks != rhs.blocks) { return false; };
    return true;
}
std::ostream& operator<<(std::ostream& out, const AddSpanInitiateResp& x) {
    out << "AddSpanInitiateResp(" << "Blocks=" << x.blocks << ")";
    return out;
}

void AddSpanCertifyReq::pack(BincodeBuf& buf) const {
    fileId.pack(buf);
    buf.packFixedBytes<8>(cookie);
    buf.packScalar<uint64_t>(byteOffset);
    buf.packList<BlockProof>(proofs);
}
void AddSpanCertifyReq::unpack(BincodeBuf& buf) {
    fileId.unpack(buf);
    buf.unpackFixedBytes<8>(cookie);
    byteOffset = buf.unpackScalar<uint64_t>();
    buf.unpackList<BlockProof>(proofs);
}
void AddSpanCertifyReq::clear() {
    fileId = InodeId();
    cookie.clear();
    byteOffset = uint64_t(0);
    proofs.clear();
}
bool AddSpanCertifyReq::operator==(const AddSpanCertifyReq& rhs) const {
    if ((InodeId)this->fileId != (InodeId)rhs.fileId) { return false; };
    if (cookie != rhs.cookie) { return false; };
    if ((uint64_t)this->byteOffset != (uint64_t)rhs.byteOffset) { return false; };
    if (proofs != rhs.proofs) { return false; };
    return true;
}
std::ostream& operator<<(std::ostream& out, const AddSpanCertifyReq& x) {
    out << "AddSpanCertifyReq(" << "FileId=" << x.fileId << ", " << "Cookie=" << x.cookie << ", " << "ByteOffset=" << x.byteOffset << ", " << "Proofs=" << x.proofs << ")";
    return out;
}

void AddSpanCertifyResp::pack(BincodeBuf& buf) const {
}
void AddSpanCertifyResp::unpack(BincodeBuf& buf) {
}
void AddSpanCertifyResp::clear() {
}
bool AddSpanCertifyResp::operator==(const AddSpanCertifyResp& rhs) const {
    return true;
}
std::ostream& operator<<(std::ostream& out, const AddSpanCertifyResp& x) {
    out << "AddSpanCertifyResp(" << ")";
    return out;
}

void LinkFileReq::pack(BincodeBuf& buf) const {
    fileId.pack(buf);
    buf.packFixedBytes<8>(cookie);
    ownerId.pack(buf);
    buf.packBytes(name);
}
void LinkFileReq::unpack(BincodeBuf& buf) {
    fileId.unpack(buf);
    buf.unpackFixedBytes<8>(cookie);
    ownerId.unpack(buf);
    buf.unpackBytes(name);
}
void LinkFileReq::clear() {
    fileId = InodeId();
    cookie.clear();
    ownerId = InodeId();
    name.clear();
}
bool LinkFileReq::operator==(const LinkFileReq& rhs) const {
    if ((InodeId)this->fileId != (InodeId)rhs.fileId) { return false; };
    if (cookie != rhs.cookie) { return false; };
    if ((InodeId)this->ownerId != (InodeId)rhs.ownerId) { return false; };
    if (name != rhs.name) { return false; };
    return true;
}
std::ostream& operator<<(std::ostream& out, const LinkFileReq& x) {
    out << "LinkFileReq(" << "FileId=" << x.fileId << ", " << "Cookie=" << x.cookie << ", " << "OwnerId=" << x.ownerId << ", " << "Name=" << GoLangQuotedStringFmt(x.name.data(), x.name.size()) << ")";
    return out;
}

void LinkFileResp::pack(BincodeBuf& buf) const {
    creationTime.pack(buf);
}
void LinkFileResp::unpack(BincodeBuf& buf) {
    creationTime.unpack(buf);
}
void LinkFileResp::clear() {
    creationTime = EggsTime();
}
bool LinkFileResp::operator==(const LinkFileResp& rhs) const {
    if ((EggsTime)this->creationTime != (EggsTime)rhs.creationTime) { return false; };
    return true;
}
std::ostream& operator<<(std::ostream& out, const LinkFileResp& x) {
    out << "LinkFileResp(" << "CreationTime=" << x.creationTime << ")";
    return out;
}

void SoftUnlinkFileReq::pack(BincodeBuf& buf) const {
    ownerId.pack(buf);
    fileId.pack(buf);
    buf.packBytes(name);
    creationTime.pack(buf);
}
void SoftUnlinkFileReq::unpack(BincodeBuf& buf) {
    ownerId.unpack(buf);
    fileId.unpack(buf);
    buf.unpackBytes(name);
    creationTime.unpack(buf);
}
void SoftUnlinkFileReq::clear() {
    ownerId = InodeId();
    fileId = InodeId();
    name.clear();
    creationTime = EggsTime();
}
bool SoftUnlinkFileReq::operator==(const SoftUnlinkFileReq& rhs) const {
    if ((InodeId)this->ownerId != (InodeId)rhs.ownerId) { return false; };
    if ((InodeId)this->fileId != (InodeId)rhs.fileId) { return false; };
    if (name != rhs.name) { return false; };
    if ((EggsTime)this->creationTime != (EggsTime)rhs.creationTime) { return false; };
    return true;
}
std::ostream& operator<<(std::ostream& out, const SoftUnlinkFileReq& x) {
    out << "SoftUnlinkFileReq(" << "OwnerId=" << x.ownerId << ", " << "FileId=" << x.fileId << ", " << "Name=" << GoLangQuotedStringFmt(x.name.data(), x.name.size()) << ", " << "CreationTime=" << x.creationTime << ")";
    return out;
}

void SoftUnlinkFileResp::pack(BincodeBuf& buf) const {
    deleteCreationTime.pack(buf);
}
void SoftUnlinkFileResp::unpack(BincodeBuf& buf) {
    deleteCreationTime.unpack(buf);
}
void SoftUnlinkFileResp::clear() {
    deleteCreationTime = EggsTime();
}
bool SoftUnlinkFileResp::operator==(const SoftUnlinkFileResp& rhs) const {
    if ((EggsTime)this->deleteCreationTime != (EggsTime)rhs.deleteCreationTime) { return false; };
    return true;
}
std::ostream& operator<<(std::ostream& out, const SoftUnlinkFileResp& x) {
    out << "SoftUnlinkFileResp(" << "DeleteCreationTime=" << x.deleteCreationTime << ")";
    return out;
}

void FileSpansReq::pack(BincodeBuf& buf) const {
    fileId.pack(buf);
    buf.packScalar<uint64_t>(byteOffset);
    buf.packScalar<uint32_t>(limit);
    buf.packScalar<uint16_t>(mtu);
}
void FileSpansReq::unpack(BincodeBuf& buf) {
    fileId.unpack(buf);
    byteOffset = buf.unpackScalar<uint64_t>();
    limit = buf.unpackScalar<uint32_t>();
    mtu = buf.unpackScalar<uint16_t>();
}
void FileSpansReq::clear() {
    fileId = InodeId();
    byteOffset = uint64_t(0);
    limit = uint32_t(0);
    mtu = uint16_t(0);
}
bool FileSpansReq::operator==(const FileSpansReq& rhs) const {
    if ((InodeId)this->fileId != (InodeId)rhs.fileId) { return false; };
    if ((uint64_t)this->byteOffset != (uint64_t)rhs.byteOffset) { return false; };
    if ((uint32_t)this->limit != (uint32_t)rhs.limit) { return false; };
    if ((uint16_t)this->mtu != (uint16_t)rhs.mtu) { return false; };
    return true;
}
std::ostream& operator<<(std::ostream& out, const FileSpansReq& x) {
    out << "FileSpansReq(" << "FileId=" << x.fileId << ", " << "ByteOffset=" << x.byteOffset << ", " << "Limit=" << x.limit << ", " << "Mtu=" << x.mtu << ")";
    return out;
}

void FileSpansResp::pack(BincodeBuf& buf) const {
    buf.packScalar<uint64_t>(nextOffset);
    buf.packList<BlockService>(blockServices);
    buf.packList<FetchedSpan>(spans);
}
void FileSpansResp::unpack(BincodeBuf& buf) {
    nextOffset = buf.unpackScalar<uint64_t>();
    buf.unpackList<BlockService>(blockServices);
    buf.unpackList<FetchedSpan>(spans);
}
void FileSpansResp::clear() {
    nextOffset = uint64_t(0);
    blockServices.clear();
    spans.clear();
}
bool FileSpansResp::operator==(const FileSpansResp& rhs) const {
    if ((uint64_t)this->nextOffset != (uint64_t)rhs.nextOffset) { return false; };
    if (blockServices != rhs.blockServices) { return false; };
    if (spans != rhs.spans) { return false; };
    return true;
}
std::ostream& operator<<(std::ostream& out, const FileSpansResp& x) {
    out << "FileSpansResp(" << "NextOffset=" << x.nextOffset << ", " << "BlockServices=" << x.blockServices << ", " << "Spans=" << x.spans << ")";
    return out;
}

void SameDirectoryRenameReq::pack(BincodeBuf& buf) const {
    targetId.pack(buf);
    dirId.pack(buf);
    buf.packBytes(oldName);
    oldCreationTime.pack(buf);
    buf.packBytes(newName);
}
void SameDirectoryRenameReq::unpack(BincodeBuf& buf) {
    targetId.unpack(buf);
    dirId.unpack(buf);
    buf.unpackBytes(oldName);
    oldCreationTime.unpack(buf);
    buf.unpackBytes(newName);
}
void SameDirectoryRenameReq::clear() {
    targetId = InodeId();
    dirId = InodeId();
    oldName.clear();
    oldCreationTime = EggsTime();
    newName.clear();
}
bool SameDirectoryRenameReq::operator==(const SameDirectoryRenameReq& rhs) const {
    if ((InodeId)this->targetId != (InodeId)rhs.targetId) { return false; };
    if ((InodeId)this->dirId != (InodeId)rhs.dirId) { return false; };
    if (oldName != rhs.oldName) { return false; };
    if ((EggsTime)this->oldCreationTime != (EggsTime)rhs.oldCreationTime) { return false; };
    if (newName != rhs.newName) { return false; };
    return true;
}
std::ostream& operator<<(std::ostream& out, const SameDirectoryRenameReq& x) {
    out << "SameDirectoryRenameReq(" << "TargetId=" << x.targetId << ", " << "DirId=" << x.dirId << ", " << "OldName=" << GoLangQuotedStringFmt(x.oldName.data(), x.oldName.size()) << ", " << "OldCreationTime=" << x.oldCreationTime << ", " << "NewName=" << GoLangQuotedStringFmt(x.newName.data(), x.newName.size()) << ")";
    return out;
}

void SameDirectoryRenameResp::pack(BincodeBuf& buf) const {
    newCreationTime.pack(buf);
}
void SameDirectoryRenameResp::unpack(BincodeBuf& buf) {
    newCreationTime.unpack(buf);
}
void SameDirectoryRenameResp::clear() {
    newCreationTime = EggsTime();
}
bool SameDirectoryRenameResp::operator==(const SameDirectoryRenameResp& rhs) const {
    if ((EggsTime)this->newCreationTime != (EggsTime)rhs.newCreationTime) { return false; };
    return true;
}
std::ostream& operator<<(std::ostream& out, const SameDirectoryRenameResp& x) {
    out << "SameDirectoryRenameResp(" << "NewCreationTime=" << x.newCreationTime << ")";
    return out;
}

void AddInlineSpanReq::pack(BincodeBuf& buf) const {
    fileId.pack(buf);
    buf.packFixedBytes<8>(cookie);
    buf.packScalar<uint8_t>(storageClass);
    buf.packScalar<uint64_t>(byteOffset);
    buf.packScalar<uint32_t>(size);
    crc.pack(buf);
    buf.packBytes(body);
}
void AddInlineSpanReq::unpack(BincodeBuf& buf) {
    fileId.unpack(buf);
    buf.unpackFixedBytes<8>(cookie);
    storageClass = buf.unpackScalar<uint8_t>();
    byteOffset = buf.unpackScalar<uint64_t>();
    size = buf.unpackScalar<uint32_t>();
    crc.unpack(buf);
    buf.unpackBytes(body);
}
void AddInlineSpanReq::clear() {
    fileId = InodeId();
    cookie.clear();
    storageClass = uint8_t(0);
    byteOffset = uint64_t(0);
    size = uint32_t(0);
    crc = Crc(0);
    body.clear();
}
bool AddInlineSpanReq::operator==(const AddInlineSpanReq& rhs) const {
    if ((InodeId)this->fileId != (InodeId)rhs.fileId) { return false; };
    if (cookie != rhs.cookie) { return false; };
    if ((uint8_t)this->storageClass != (uint8_t)rhs.storageClass) { return false; };
    if ((uint64_t)this->byteOffset != (uint64_t)rhs.byteOffset) { return false; };
    if ((uint32_t)this->size != (uint32_t)rhs.size) { return false; };
    if ((Crc)this->crc != (Crc)rhs.crc) { return false; };
    if (body != rhs.body) { return false; };
    return true;
}
std::ostream& operator<<(std::ostream& out, const AddInlineSpanReq& x) {
    out << "AddInlineSpanReq(" << "FileId=" << x.fileId << ", " << "Cookie=" << x.cookie << ", " << "StorageClass=" << (int)x.storageClass << ", " << "ByteOffset=" << x.byteOffset << ", " << "Size=" << x.size << ", " << "Crc=" << x.crc << ", " << "Body=" << x.body << ")";
    return out;
}

void AddInlineSpanResp::pack(BincodeBuf& buf) const {
}
void AddInlineSpanResp::unpack(BincodeBuf& buf) {
}
void AddInlineSpanResp::clear() {
}
bool AddInlineSpanResp::operator==(const AddInlineSpanResp& rhs) const {
    return true;
}
std::ostream& operator<<(std::ostream& out, const AddInlineSpanResp& x) {
    out << "AddInlineSpanResp(" << ")";
    return out;
}

void SetTimeReq::pack(BincodeBuf& buf) const {
    id.pack(buf);
    buf.packScalar<uint64_t>(mtime);
    buf.packScalar<uint64_t>(atime);
}
void SetTimeReq::unpack(BincodeBuf& buf) {
    id.unpack(buf);
    mtime = buf.unpackScalar<uint64_t>();
    atime = buf.unpackScalar<uint64_t>();
}
void SetTimeReq::clear() {
    id = InodeId();
    mtime = uint64_t(0);
    atime = uint64_t(0);
}
bool SetTimeReq::operator==(const SetTimeReq& rhs) const {
    if ((InodeId)this->id != (InodeId)rhs.id) { return false; };
    if ((uint64_t)this->mtime != (uint64_t)rhs.mtime) { return false; };
    if ((uint64_t)this->atime != (uint64_t)rhs.atime) { return false; };
    return true;
}
std::ostream& operator<<(std::ostream& out, const SetTimeReq& x) {
    out << "SetTimeReq(" << "Id=" << x.id << ", " << "Mtime=" << x.mtime << ", " << "Atime=" << x.atime << ")";
    return out;
}

void SetTimeResp::pack(BincodeBuf& buf) const {
}
void SetTimeResp::unpack(BincodeBuf& buf) {
}
void SetTimeResp::clear() {
}
bool SetTimeResp::operator==(const SetTimeResp& rhs) const {
    return true;
}
std::ostream& operator<<(std::ostream& out, const SetTimeResp& x) {
    out << "SetTimeResp(" << ")";
    return out;
}

void FullReadDirReq::pack(BincodeBuf& buf) const {
    dirId.pack(buf);
    buf.packScalar<uint8_t>(flags);
    buf.packBytes(startName);
    startTime.pack(buf);
    buf.packScalar<uint16_t>(limit);
    buf.packScalar<uint16_t>(mtu);
}
void FullReadDirReq::unpack(BincodeBuf& buf) {
    dirId.unpack(buf);
    flags = buf.unpackScalar<uint8_t>();
    buf.unpackBytes(startName);
    startTime.unpack(buf);
    limit = buf.unpackScalar<uint16_t>();
    mtu = buf.unpackScalar<uint16_t>();
}
void FullReadDirReq::clear() {
    dirId = InodeId();
    flags = uint8_t(0);
    startName.clear();
    startTime = EggsTime();
    limit = uint16_t(0);
    mtu = uint16_t(0);
}
bool FullReadDirReq::operator==(const FullReadDirReq& rhs) const {
    if ((InodeId)this->dirId != (InodeId)rhs.dirId) { return false; };
    if ((uint8_t)this->flags != (uint8_t)rhs.flags) { return false; };
    if (startName != rhs.startName) { return false; };
    if ((EggsTime)this->startTime != (EggsTime)rhs.startTime) { return false; };
    if ((uint16_t)this->limit != (uint16_t)rhs.limit) { return false; };
    if ((uint16_t)this->mtu != (uint16_t)rhs.mtu) { return false; };
    return true;
}
std::ostream& operator<<(std::ostream& out, const FullReadDirReq& x) {
    out << "FullReadDirReq(" << "DirId=" << x.dirId << ", " << "Flags=" << (int)x.flags << ", " << "StartName=" << GoLangQuotedStringFmt(x.startName.data(), x.startName.size()) << ", " << "StartTime=" << x.startTime << ", " << "Limit=" << x.limit << ", " << "Mtu=" << x.mtu << ")";
    return out;
}

void FullReadDirResp::pack(BincodeBuf& buf) const {
    next.pack(buf);
    buf.packList<Edge>(results);
}
void FullReadDirResp::unpack(BincodeBuf& buf) {
    next.unpack(buf);
    buf.unpackList<Edge>(results);
}
void FullReadDirResp::clear() {
    next.clear();
    results.clear();
}
bool FullReadDirResp::operator==(const FullReadDirResp& rhs) const {
    if (next != rhs.next) { return false; };
    if (results != rhs.results) { return false; };
    return true;
}
std::ostream& operator<<(std::ostream& out, const FullReadDirResp& x) {
    out << "FullReadDirResp(" << "Next=" << x.next << ", " << "Results=" << x.results << ")";
    return out;
}

void MoveSpanReq::pack(BincodeBuf& buf) const {
    buf.packScalar<uint32_t>(spanSize);
    fileId1.pack(buf);
    buf.packScalar<uint64_t>(byteOffset1);
    buf.packFixedBytes<8>(cookie1);
    fileId2.pack(buf);
    buf.packScalar<uint64_t>(byteOffset2);
    buf.packFixedBytes<8>(cookie2);
}
void MoveSpanReq::unpack(BincodeBuf& buf) {
    spanSize = buf.unpackScalar<uint32_t>();
    fileId1.unpack(buf);
    byteOffset1 = buf.unpackScalar<uint64_t>();
    buf.unpackFixedBytes<8>(cookie1);
    fileId2.unpack(buf);
    byteOffset2 = buf.unpackScalar<uint64_t>();
    buf.unpackFixedBytes<8>(cookie2);
}
void MoveSpanReq::clear() {
    spanSize = uint32_t(0);
    fileId1 = InodeId();
    byteOffset1 = uint64_t(0);
    cookie1.clear();
    fileId2 = InodeId();
    byteOffset2 = uint64_t(0);
    cookie2.clear();
}
bool MoveSpanReq::operator==(const MoveSpanReq& rhs) const {
    if ((uint32_t)this->spanSize != (uint32_t)rhs.spanSize) { return false; };
    if ((InodeId)this->fileId1 != (InodeId)rhs.fileId1) { return false; };
    if ((uint64_t)this->byteOffset1 != (uint64_t)rhs.byteOffset1) { return false; };
    if (cookie1 != rhs.cookie1) { return false; };
    if ((InodeId)this->fileId2 != (InodeId)rhs.fileId2) { return false; };
    if ((uint64_t)this->byteOffset2 != (uint64_t)rhs.byteOffset2) { return false; };
    if (cookie2 != rhs.cookie2) { return false; };
    return true;
}
std::ostream& operator<<(std::ostream& out, const MoveSpanReq& x) {
    out << "MoveSpanReq(" << "SpanSize=" << x.spanSize << ", " << "FileId1=" << x.fileId1 << ", " << "ByteOffset1=" << x.byteOffset1 << ", " << "Cookie1=" << x.cookie1 << ", " << "FileId2=" << x.fileId2 << ", " << "ByteOffset2=" << x.byteOffset2 << ", " << "Cookie2=" << x.cookie2 << ")";
    return out;
}

void MoveSpanResp::pack(BincodeBuf& buf) const {
}
void MoveSpanResp::unpack(BincodeBuf& buf) {
}
void MoveSpanResp::clear() {
}
bool MoveSpanResp::operator==(const MoveSpanResp& rhs) const {
    return true;
}
std::ostream& operator<<(std::ostream& out, const MoveSpanResp& x) {
    out << "MoveSpanResp(" << ")";
    return out;
}

void RemoveNonOwnedEdgeReq::pack(BincodeBuf& buf) const {
    dirId.pack(buf);
    targetId.pack(buf);
    buf.packBytes(name);
    creationTime.pack(buf);
}
void RemoveNonOwnedEdgeReq::unpack(BincodeBuf& buf) {
    dirId.unpack(buf);
    targetId.unpack(buf);
    buf.unpackBytes(name);
    creationTime.unpack(buf);
}
void RemoveNonOwnedEdgeReq::clear() {
    dirId = InodeId();
    targetId = InodeId();
    name.clear();
    creationTime = EggsTime();
}
bool RemoveNonOwnedEdgeReq::operator==(const RemoveNonOwnedEdgeReq& rhs) const {
    if ((InodeId)this->dirId != (InodeId)rhs.dirId) { return false; };
    if ((InodeId)this->targetId != (InodeId)rhs.targetId) { return false; };
    if (name != rhs.name) { return false; };
    if ((EggsTime)this->creationTime != (EggsTime)rhs.creationTime) { return false; };
    return true;
}
std::ostream& operator<<(std::ostream& out, const RemoveNonOwnedEdgeReq& x) {
    out << "RemoveNonOwnedEdgeReq(" << "DirId=" << x.dirId << ", " << "TargetId=" << x.targetId << ", " << "Name=" << GoLangQuotedStringFmt(x.name.data(), x.name.size()) << ", " << "CreationTime=" << x.creationTime << ")";
    return out;
}

void RemoveNonOwnedEdgeResp::pack(BincodeBuf& buf) const {
}
void RemoveNonOwnedEdgeResp::unpack(BincodeBuf& buf) {
}
void RemoveNonOwnedEdgeResp::clear() {
}
bool RemoveNonOwnedEdgeResp::operator==(const RemoveNonOwnedEdgeResp& rhs) const {
    return true;
}
std::ostream& operator<<(std::ostream& out, const RemoveNonOwnedEdgeResp& x) {
    out << "RemoveNonOwnedEdgeResp(" << ")";
    return out;
}

void SameShardHardFileUnlinkReq::pack(BincodeBuf& buf) const {
    ownerId.pack(buf);
    targetId.pack(buf);
    buf.packBytes(name);
    creationTime.pack(buf);
}
void SameShardHardFileUnlinkReq::unpack(BincodeBuf& buf) {
    ownerId.unpack(buf);
    targetId.unpack(buf);
    buf.unpackBytes(name);
    creationTime.unpack(buf);
}
void SameShardHardFileUnlinkReq::clear() {
    ownerId = InodeId();
    targetId = InodeId();
    name.clear();
    creationTime = EggsTime();
}
bool SameShardHardFileUnlinkReq::operator==(const SameShardHardFileUnlinkReq& rhs) const {
    if ((InodeId)this->ownerId != (InodeId)rhs.ownerId) { return false; };
    if ((InodeId)this->targetId != (InodeId)rhs.targetId) { return false; };
    if (name != rhs.name) { return false; };
    if ((EggsTime)this->creationTime != (EggsTime)rhs.creationTime) { return false; };
    return true;
}
std::ostream& operator<<(std::ostream& out, const SameShardHardFileUnlinkReq& x) {
    out << "SameShardHardFileUnlinkReq(" << "OwnerId=" << x.ownerId << ", " << "TargetId=" << x.targetId << ", " << "Name=" << GoLangQuotedStringFmt(x.name.data(), x.name.size()) << ", " << "CreationTime=" << x.creationTime << ")";
    return out;
}

void SameShardHardFileUnlinkResp::pack(BincodeBuf& buf) const {
}
void SameShardHardFileUnlinkResp::unpack(BincodeBuf& buf) {
}
void SameShardHardFileUnlinkResp::clear() {
}
bool SameShardHardFileUnlinkResp::operator==(const SameShardHardFileUnlinkResp& rhs) const {
    return true;
}
std::ostream& operator<<(std::ostream& out, const SameShardHardFileUnlinkResp& x) {
    out << "SameShardHardFileUnlinkResp(" << ")";
    return out;
}

void StatTransientFileReq::pack(BincodeBuf& buf) const {
    id.pack(buf);
}
void StatTransientFileReq::unpack(BincodeBuf& buf) {
    id.unpack(buf);
}
void StatTransientFileReq::clear() {
    id = InodeId();
}
bool StatTransientFileReq::operator==(const StatTransientFileReq& rhs) const {
    if ((InodeId)this->id != (InodeId)rhs.id) { return false; };
    return true;
}
std::ostream& operator<<(std::ostream& out, const StatTransientFileReq& x) {
    out << "StatTransientFileReq(" << "Id=" << x.id << ")";
    return out;
}

void StatTransientFileResp::pack(BincodeBuf& buf) const {
    mtime.pack(buf);
    buf.packScalar<uint64_t>(size);
    buf.packBytes(note);
}
void StatTransientFileResp::unpack(BincodeBuf& buf) {
    mtime.unpack(buf);
    size = buf.unpackScalar<uint64_t>();
    buf.unpackBytes(note);
}
void StatTransientFileResp::clear() {
    mtime = EggsTime();
    size = uint64_t(0);
    note.clear();
}
bool StatTransientFileResp::operator==(const StatTransientFileResp& rhs) const {
    if ((EggsTime)this->mtime != (EggsTime)rhs.mtime) { return false; };
    if ((uint64_t)this->size != (uint64_t)rhs.size) { return false; };
    if (note != rhs.note) { return false; };
    return true;
}
std::ostream& operator<<(std::ostream& out, const StatTransientFileResp& x) {
    out << "StatTransientFileResp(" << "Mtime=" << x.mtime << ", " << "Size=" << x.size << ", " << "Note=" << GoLangQuotedStringFmt(x.note.data(), x.note.size()) << ")";
    return out;
}

void SetDirectoryInfoReq::pack(BincodeBuf& buf) const {
    id.pack(buf);
    info.pack(buf);
}
void SetDirectoryInfoReq::unpack(BincodeBuf& buf) {
    id.unpack(buf);
    info.unpack(buf);
}
void SetDirectoryInfoReq::clear() {
    id = InodeId();
    info.clear();
}
bool SetDirectoryInfoReq::operator==(const SetDirectoryInfoReq& rhs) const {
    if ((InodeId)this->id != (InodeId)rhs.id) { return false; };
    if (info != rhs.info) { return false; };
    return true;
}
std::ostream& operator<<(std::ostream& out, const SetDirectoryInfoReq& x) {
    out << "SetDirectoryInfoReq(" << "Id=" << x.id << ", " << "Info=" << x.info << ")";
    return out;
}

void SetDirectoryInfoResp::pack(BincodeBuf& buf) const {
}
void SetDirectoryInfoResp::unpack(BincodeBuf& buf) {
}
void SetDirectoryInfoResp::clear() {
}
bool SetDirectoryInfoResp::operator==(const SetDirectoryInfoResp& rhs) const {
    return true;
}
std::ostream& operator<<(std::ostream& out, const SetDirectoryInfoResp& x) {
    out << "SetDirectoryInfoResp(" << ")";
    return out;
}

void VisitDirectoriesReq::pack(BincodeBuf& buf) const {
    beginId.pack(buf);
    buf.packScalar<uint16_t>(mtu);
}
void VisitDirectoriesReq::unpack(BincodeBuf& buf) {
    beginId.unpack(buf);
    mtu = buf.unpackScalar<uint16_t>();
}
void VisitDirectoriesReq::clear() {
    beginId = InodeId();
    mtu = uint16_t(0);
}
bool VisitDirectoriesReq::operator==(const VisitDirectoriesReq& rhs) const {
    if ((InodeId)this->beginId != (InodeId)rhs.beginId) { return false; };
    if ((uint16_t)this->mtu != (uint16_t)rhs.mtu) { return false; };
    return true;
}
std::ostream& operator<<(std::ostream& out, const VisitDirectoriesReq& x) {
    out << "VisitDirectoriesReq(" << "BeginId=" << x.beginId << ", " << "Mtu=" << x.mtu << ")";
    return out;
}

void VisitDirectoriesResp::pack(BincodeBuf& buf) const {
    nextId.pack(buf);
    buf.packList<InodeId>(ids);
}
void VisitDirectoriesResp::unpack(BincodeBuf& buf) {
    nextId.unpack(buf);
    buf.unpackList<InodeId>(ids);
}
void VisitDirectoriesResp::clear() {
    nextId = InodeId();
    ids.clear();
}
bool VisitDirectoriesResp::operator==(const VisitDirectoriesResp& rhs) const {
    if ((InodeId)this->nextId != (InodeId)rhs.nextId) { return false; };
    if (ids != rhs.ids) { return false; };
    return true;
}
std::ostream& operator<<(std::ostream& out, const VisitDirectoriesResp& x) {
    out << "VisitDirectoriesResp(" << "NextId=" << x.nextId << ", " << "Ids=" << x.ids << ")";
    return out;
}

void VisitFilesReq::pack(BincodeBuf& buf) const {
    beginId.pack(buf);
    buf.packScalar<uint16_t>(mtu);
}
void VisitFilesReq::unpack(BincodeBuf& buf) {
    beginId.unpack(buf);
    mtu = buf.unpackScalar<uint16_t>();
}
void VisitFilesReq::clear() {
    beginId = InodeId();
    mtu = uint16_t(0);
}
bool VisitFilesReq::operator==(const VisitFilesReq& rhs) const {
    if ((InodeId)this->beginId != (InodeId)rhs.beginId) { return false; };
    if ((uint16_t)this->mtu != (uint16_t)rhs.mtu) { return false; };
    return true;
}
std::ostream& operator<<(std::ostream& out, const VisitFilesReq& x) {
    out << "VisitFilesReq(" << "BeginId=" << x.beginId << ", " << "Mtu=" << x.mtu << ")";
    return out;
}

void VisitFilesResp::pack(BincodeBuf& buf) const {
    nextId.pack(buf);
    buf.packList<InodeId>(ids);
}
void VisitFilesResp::unpack(BincodeBuf& buf) {
    nextId.unpack(buf);
    buf.unpackList<InodeId>(ids);
}
void VisitFilesResp::clear() {
    nextId = InodeId();
    ids.clear();
}
bool VisitFilesResp::operator==(const VisitFilesResp& rhs) const {
    if ((InodeId)this->nextId != (InodeId)rhs.nextId) { return false; };
    if (ids != rhs.ids) { return false; };
    return true;
}
std::ostream& operator<<(std::ostream& out, const VisitFilesResp& x) {
    out << "VisitFilesResp(" << "NextId=" << x.nextId << ", " << "Ids=" << x.ids << ")";
    return out;
}

void VisitTransientFilesReq::pack(BincodeBuf& buf) const {
    beginId.pack(buf);
    buf.packScalar<uint16_t>(mtu);
}
void VisitTransientFilesReq::unpack(BincodeBuf& buf) {
    beginId.unpack(buf);
    mtu = buf.unpackScalar<uint16_t>();
}
void VisitTransientFilesReq::clear() {
    beginId = InodeId();
    mtu = uint16_t(0);
}
bool VisitTransientFilesReq::operator==(const VisitTransientFilesReq& rhs) const {
    if ((InodeId)this->beginId != (InodeId)rhs.beginId) { return false; };
    if ((uint16_t)this->mtu != (uint16_t)rhs.mtu) { return false; };
    return true;
}
std::ostream& operator<<(std::ostream& out, const VisitTransientFilesReq& x) {
    out << "VisitTransientFilesReq(" << "BeginId=" << x.beginId << ", " << "Mtu=" << x.mtu << ")";
    return out;
}

void VisitTransientFilesResp::pack(BincodeBuf& buf) const {
    nextId.pack(buf);
    buf.packList<TransientFile>(files);
}
void VisitTransientFilesResp::unpack(BincodeBuf& buf) {
    nextId.unpack(buf);
    buf.unpackList<TransientFile>(files);
}
void VisitTransientFilesResp::clear() {
    nextId = InodeId();
    files.clear();
}
bool VisitTransientFilesResp::operator==(const VisitTransientFilesResp& rhs) const {
    if ((InodeId)this->nextId != (InodeId)rhs.nextId) { return false; };
    if (files != rhs.files) { return false; };
    return true;
}
std::ostream& operator<<(std::ostream& out, const VisitTransientFilesResp& x) {
    out << "VisitTransientFilesResp(" << "NextId=" << x.nextId << ", " << "Files=" << x.files << ")";
    return out;
}

void RemoveSpanInitiateReq::pack(BincodeBuf& buf) const {
    fileId.pack(buf);
    buf.packFixedBytes<8>(cookie);
}
void RemoveSpanInitiateReq::unpack(BincodeBuf& buf) {
    fileId.unpack(buf);
    buf.unpackFixedBytes<8>(cookie);
}
void RemoveSpanInitiateReq::clear() {
    fileId = InodeId();
    cookie.clear();
}
bool RemoveSpanInitiateReq::operator==(const RemoveSpanInitiateReq& rhs) const {
    if ((InodeId)this->fileId != (InodeId)rhs.fileId) { return false; };
    if (cookie != rhs.cookie) { return false; };
    return true;
}
std::ostream& operator<<(std::ostream& out, const RemoveSpanInitiateReq& x) {
    out << "RemoveSpanInitiateReq(" << "FileId=" << x.fileId << ", " << "Cookie=" << x.cookie << ")";
    return out;
}

void RemoveSpanInitiateResp::pack(BincodeBuf& buf) const {
    buf.packScalar<uint64_t>(byteOffset);
    buf.packList<RemoveSpanInitiateBlockInfo>(blocks);
}
void RemoveSpanInitiateResp::unpack(BincodeBuf& buf) {
    byteOffset = buf.unpackScalar<uint64_t>();
    buf.unpackList<RemoveSpanInitiateBlockInfo>(blocks);
}
void RemoveSpanInitiateResp::clear() {
    byteOffset = uint64_t(0);
    blocks.clear();
}
bool RemoveSpanInitiateResp::operator==(const RemoveSpanInitiateResp& rhs) const {
    if ((uint64_t)this->byteOffset != (uint64_t)rhs.byteOffset) { return false; };
    if (blocks != rhs.blocks) { return false; };
    return true;
}
std::ostream& operator<<(std::ostream& out, const RemoveSpanInitiateResp& x) {
    out << "RemoveSpanInitiateResp(" << "ByteOffset=" << x.byteOffset << ", " << "Blocks=" << x.blocks << ")";
    return out;
}

void RemoveSpanCertifyReq::pack(BincodeBuf& buf) const {
    fileId.pack(buf);
    buf.packFixedBytes<8>(cookie);
    buf.packScalar<uint64_t>(byteOffset);
    buf.packList<BlockProof>(proofs);
}
void RemoveSpanCertifyReq::unpack(BincodeBuf& buf) {
    fileId.unpack(buf);
    buf.unpackFixedBytes<8>(cookie);
    byteOffset = buf.unpackScalar<uint64_t>();
    buf.unpackList<BlockProof>(proofs);
}
void RemoveSpanCertifyReq::clear() {
    fileId = InodeId();
    cookie.clear();
    byteOffset = uint64_t(0);
    proofs.clear();
}
bool RemoveSpanCertifyReq::operator==(const RemoveSpanCertifyReq& rhs) const {
    if ((InodeId)this->fileId != (InodeId)rhs.fileId) { return false; };
    if (cookie != rhs.cookie) { return false; };
    if ((uint64_t)this->byteOffset != (uint64_t)rhs.byteOffset) { return false; };
    if (proofs != rhs.proofs) { return false; };
    return true;
}
std::ostream& operator<<(std::ostream& out, const RemoveSpanCertifyReq& x) {
    out << "RemoveSpanCertifyReq(" << "FileId=" << x.fileId << ", " << "Cookie=" << x.cookie << ", " << "ByteOffset=" << x.byteOffset << ", " << "Proofs=" << x.proofs << ")";
    return out;
}

void RemoveSpanCertifyResp::pack(BincodeBuf& buf) const {
}
void RemoveSpanCertifyResp::unpack(BincodeBuf& buf) {
}
void RemoveSpanCertifyResp::clear() {
}
bool RemoveSpanCertifyResp::operator==(const RemoveSpanCertifyResp& rhs) const {
    return true;
}
std::ostream& operator<<(std::ostream& out, const RemoveSpanCertifyResp& x) {
    out << "RemoveSpanCertifyResp(" << ")";
    return out;
}

void SwapBlocksReq::pack(BincodeBuf& buf) const {
    fileId1.pack(buf);
    buf.packScalar<uint64_t>(byteOffset1);
    buf.packScalar<uint64_t>(blockId1);
    fileId2.pack(buf);
    buf.packScalar<uint64_t>(byteOffset2);
    buf.packScalar<uint64_t>(blockId2);
}
void SwapBlocksReq::unpack(BincodeBuf& buf) {
    fileId1.unpack(buf);
    byteOffset1 = buf.unpackScalar<uint64_t>();
    blockId1 = buf.unpackScalar<uint64_t>();
    fileId2.unpack(buf);
    byteOffset2 = buf.unpackScalar<uint64_t>();
    blockId2 = buf.unpackScalar<uint64_t>();
}
void SwapBlocksReq::clear() {
    fileId1 = InodeId();
    byteOffset1 = uint64_t(0);
    blockId1 = uint64_t(0);
    fileId2 = InodeId();
    byteOffset2 = uint64_t(0);
    blockId2 = uint64_t(0);
}
bool SwapBlocksReq::operator==(const SwapBlocksReq& rhs) const {
    if ((InodeId)this->fileId1 != (InodeId)rhs.fileId1) { return false; };
    if ((uint64_t)this->byteOffset1 != (uint64_t)rhs.byteOffset1) { return false; };
    if ((uint64_t)this->blockId1 != (uint64_t)rhs.blockId1) { return false; };
    if ((InodeId)this->fileId2 != (InodeId)rhs.fileId2) { return false; };
    if ((uint64_t)this->byteOffset2 != (uint64_t)rhs.byteOffset2) { return false; };
    if ((uint64_t)this->blockId2 != (uint64_t)rhs.blockId2) { return false; };
    return true;
}
std::ostream& operator<<(std::ostream& out, const SwapBlocksReq& x) {
    out << "SwapBlocksReq(" << "FileId1=" << x.fileId1 << ", " << "ByteOffset1=" << x.byteOffset1 << ", " << "BlockId1=" << x.blockId1 << ", " << "FileId2=" << x.fileId2 << ", " << "ByteOffset2=" << x.byteOffset2 << ", " << "BlockId2=" << x.blockId2 << ")";
    return out;
}

void SwapBlocksResp::pack(BincodeBuf& buf) const {
}
void SwapBlocksResp::unpack(BincodeBuf& buf) {
}
void SwapBlocksResp::clear() {
}
bool SwapBlocksResp::operator==(const SwapBlocksResp& rhs) const {
    return true;
}
std::ostream& operator<<(std::ostream& out, const SwapBlocksResp& x) {
    out << "SwapBlocksResp(" << ")";
    return out;
}

void BlockServiceFilesReq::pack(BincodeBuf& buf) const {
    blockServiceId.pack(buf);
    startFrom.pack(buf);
}
void BlockServiceFilesReq::unpack(BincodeBuf& buf) {
    blockServiceId.unpack(buf);
    startFrom.unpack(buf);
}
void BlockServiceFilesReq::clear() {
    blockServiceId = BlockServiceId(0);
    startFrom = InodeId();
}
bool BlockServiceFilesReq::operator==(const BlockServiceFilesReq& rhs) const {
    if ((BlockServiceId)this->blockServiceId != (BlockServiceId)rhs.blockServiceId) { return false; };
    if ((InodeId)this->startFrom != (InodeId)rhs.startFrom) { return false; };
    return true;
}
std::ostream& operator<<(std::ostream& out, const BlockServiceFilesReq& x) {
    out << "BlockServiceFilesReq(" << "BlockServiceId=" << x.blockServiceId << ", " << "StartFrom=" << x.startFrom << ")";
    return out;
}

void BlockServiceFilesResp::pack(BincodeBuf& buf) const {
    buf.packList<InodeId>(fileIds);
}
void BlockServiceFilesResp::unpack(BincodeBuf& buf) {
    buf.unpackList<InodeId>(fileIds);
}
void BlockServiceFilesResp::clear() {
    fileIds.clear();
}
bool BlockServiceFilesResp::operator==(const BlockServiceFilesResp& rhs) const {
    if (fileIds != rhs.fileIds) { return false; };
    return true;
}
std::ostream& operator<<(std::ostream& out, const BlockServiceFilesResp& x) {
    out << "BlockServiceFilesResp(" << "FileIds=" << x.fileIds << ")";
    return out;
}

void RemoveInodeReq::pack(BincodeBuf& buf) const {
    id.pack(buf);
}
void RemoveInodeReq::unpack(BincodeBuf& buf) {
    id.unpack(buf);
}
void RemoveInodeReq::clear() {
    id = InodeId();
}
bool RemoveInodeReq::operator==(const RemoveInodeReq& rhs) const {
    if ((InodeId)this->id != (InodeId)rhs.id) { return false; };
    return true;
}
std::ostream& operator<<(std::ostream& out, const RemoveInodeReq& x) {
    out << "RemoveInodeReq(" << "Id=" << x.id << ")";
    return out;
}

void RemoveInodeResp::pack(BincodeBuf& buf) const {
}
void RemoveInodeResp::unpack(BincodeBuf& buf) {
}
void RemoveInodeResp::clear() {
}
bool RemoveInodeResp::operator==(const RemoveInodeResp& rhs) const {
    return true;
}
std::ostream& operator<<(std::ostream& out, const RemoveInodeResp& x) {
    out << "RemoveInodeResp(" << ")";
    return out;
}

void AddSpanInitiateWithReferenceReq::pack(BincodeBuf& buf) const {
    req.pack(buf);
    reference.pack(buf);
}
void AddSpanInitiateWithReferenceReq::unpack(BincodeBuf& buf) {
    req.unpack(buf);
    reference.unpack(buf);
}
void AddSpanInitiateWithReferenceReq::clear() {
    req.clear();
    reference = InodeId();
}
bool AddSpanInitiateWithReferenceReq::operator==(const AddSpanInitiateWithReferenceReq& rhs) const {
    if (req != rhs.req) { return false; };
    if ((InodeId)this->reference != (InodeId)rhs.reference) { return false; };
    return true;
}
std::ostream& operator<<(std::ostream& out, const AddSpanInitiateWithReferenceReq& x) {
    out << "AddSpanInitiateWithReferenceReq(" << "Req=" << x.req << ", " << "Reference=" << x.reference << ")";
    return out;
}

void AddSpanInitiateWithReferenceResp::pack(BincodeBuf& buf) const {
    resp.pack(buf);
}
void AddSpanInitiateWithReferenceResp::unpack(BincodeBuf& buf) {
    resp.unpack(buf);
}
void AddSpanInitiateWithReferenceResp::clear() {
    resp.clear();
}
bool AddSpanInitiateWithReferenceResp::operator==(const AddSpanInitiateWithReferenceResp& rhs) const {
    if (resp != rhs.resp) { return false; };
    return true;
}
std::ostream& operator<<(std::ostream& out, const AddSpanInitiateWithReferenceResp& x) {
    out << "AddSpanInitiateWithReferenceResp(" << "Resp=" << x.resp << ")";
    return out;
}

void RemoveZeroBlockServiceFilesReq::pack(BincodeBuf& buf) const {
    startBlockService.pack(buf);
    startFile.pack(buf);
}
void RemoveZeroBlockServiceFilesReq::unpack(BincodeBuf& buf) {
    startBlockService.unpack(buf);
    startFile.unpack(buf);
}
void RemoveZeroBlockServiceFilesReq::clear() {
    startBlockService = BlockServiceId(0);
    startFile = InodeId();
}
bool RemoveZeroBlockServiceFilesReq::operator==(const RemoveZeroBlockServiceFilesReq& rhs) const {
    if ((BlockServiceId)this->startBlockService != (BlockServiceId)rhs.startBlockService) { return false; };
    if ((InodeId)this->startFile != (InodeId)rhs.startFile) { return false; };
    return true;
}
std::ostream& operator<<(std::ostream& out, const RemoveZeroBlockServiceFilesReq& x) {
    out << "RemoveZeroBlockServiceFilesReq(" << "StartBlockService=" << x.startBlockService << ", " << "StartFile=" << x.startFile << ")";
    return out;
}

void RemoveZeroBlockServiceFilesResp::pack(BincodeBuf& buf) const {
    buf.packScalar<uint64_t>(removed);
    nextBlockService.pack(buf);
    nextFile.pack(buf);
}
void RemoveZeroBlockServiceFilesResp::unpack(BincodeBuf& buf) {
    removed = buf.unpackScalar<uint64_t>();
    nextBlockService.unpack(buf);
    nextFile.unpack(buf);
}
void RemoveZeroBlockServiceFilesResp::clear() {
    removed = uint64_t(0);
    nextBlockService = BlockServiceId(0);
    nextFile = InodeId();
}
bool RemoveZeroBlockServiceFilesResp::operator==(const RemoveZeroBlockServiceFilesResp& rhs) const {
    if ((uint64_t)this->removed != (uint64_t)rhs.removed) { return false; };
    if ((BlockServiceId)this->nextBlockService != (BlockServiceId)rhs.nextBlockService) { return false; };
    if ((InodeId)this->nextFile != (InodeId)rhs.nextFile) { return false; };
    return true;
}
std::ostream& operator<<(std::ostream& out, const RemoveZeroBlockServiceFilesResp& x) {
    out << "RemoveZeroBlockServiceFilesResp(" << "Removed=" << x.removed << ", " << "NextBlockService=" << x.nextBlockService << ", " << "NextFile=" << x.nextFile << ")";
    return out;
}

void CreateDirectoryInodeReq::pack(BincodeBuf& buf) const {
    id.pack(buf);
    ownerId.pack(buf);
    info.pack(buf);
}
void CreateDirectoryInodeReq::unpack(BincodeBuf& buf) {
    id.unpack(buf);
    ownerId.unpack(buf);
    info.unpack(buf);
}
void CreateDirectoryInodeReq::clear() {
    id = InodeId();
    ownerId = InodeId();
    info.clear();
}
bool CreateDirectoryInodeReq::operator==(const CreateDirectoryInodeReq& rhs) const {
    if ((InodeId)this->id != (InodeId)rhs.id) { return false; };
    if ((InodeId)this->ownerId != (InodeId)rhs.ownerId) { return false; };
    if (info != rhs.info) { return false; };
    return true;
}
std::ostream& operator<<(std::ostream& out, const CreateDirectoryInodeReq& x) {
    out << "CreateDirectoryInodeReq(" << "Id=" << x.id << ", " << "OwnerId=" << x.ownerId << ", " << "Info=" << x.info << ")";
    return out;
}

void CreateDirectoryInodeResp::pack(BincodeBuf& buf) const {
    mtime.pack(buf);
}
void CreateDirectoryInodeResp::unpack(BincodeBuf& buf) {
    mtime.unpack(buf);
}
void CreateDirectoryInodeResp::clear() {
    mtime = EggsTime();
}
bool CreateDirectoryInodeResp::operator==(const CreateDirectoryInodeResp& rhs) const {
    if ((EggsTime)this->mtime != (EggsTime)rhs.mtime) { return false; };
    return true;
}
std::ostream& operator<<(std::ostream& out, const CreateDirectoryInodeResp& x) {
    out << "CreateDirectoryInodeResp(" << "Mtime=" << x.mtime << ")";
    return out;
}

void SetDirectoryOwnerReq::pack(BincodeBuf& buf) const {
    dirId.pack(buf);
    ownerId.pack(buf);
}
void SetDirectoryOwnerReq::unpack(BincodeBuf& buf) {
    dirId.unpack(buf);
    ownerId.unpack(buf);
}
void SetDirectoryOwnerReq::clear() {
    dirId = InodeId();
    ownerId = InodeId();
}
bool SetDirectoryOwnerReq::operator==(const SetDirectoryOwnerReq& rhs) const {
    if ((InodeId)this->dirId != (InodeId)rhs.dirId) { return false; };
    if ((InodeId)this->ownerId != (InodeId)rhs.ownerId) { return false; };
    return true;
}
std::ostream& operator<<(std::ostream& out, const SetDirectoryOwnerReq& x) {
    out << "SetDirectoryOwnerReq(" << "DirId=" << x.dirId << ", " << "OwnerId=" << x.ownerId << ")";
    return out;
}

void SetDirectoryOwnerResp::pack(BincodeBuf& buf) const {
}
void SetDirectoryOwnerResp::unpack(BincodeBuf& buf) {
}
void SetDirectoryOwnerResp::clear() {
}
bool SetDirectoryOwnerResp::operator==(const SetDirectoryOwnerResp& rhs) const {
    return true;
}
std::ostream& operator<<(std::ostream& out, const SetDirectoryOwnerResp& x) {
    out << "SetDirectoryOwnerResp(" << ")";
    return out;
}

void RemoveDirectoryOwnerReq::pack(BincodeBuf& buf) const {
    dirId.pack(buf);
    info.pack(buf);
}
void RemoveDirectoryOwnerReq::unpack(BincodeBuf& buf) {
    dirId.unpack(buf);
    info.unpack(buf);
}
void RemoveDirectoryOwnerReq::clear() {
    dirId = InodeId();
    info.clear();
}
bool RemoveDirectoryOwnerReq::operator==(const RemoveDirectoryOwnerReq& rhs) const {
    if ((InodeId)this->dirId != (InodeId)rhs.dirId) { return false; };
    if (info != rhs.info) { return false; };
    return true;
}
std::ostream& operator<<(std::ostream& out, const RemoveDirectoryOwnerReq& x) {
    out << "RemoveDirectoryOwnerReq(" << "DirId=" << x.dirId << ", " << "Info=" << x.info << ")";
    return out;
}

void RemoveDirectoryOwnerResp::pack(BincodeBuf& buf) const {
}
void RemoveDirectoryOwnerResp::unpack(BincodeBuf& buf) {
}
void RemoveDirectoryOwnerResp::clear() {
}
bool RemoveDirectoryOwnerResp::operator==(const RemoveDirectoryOwnerResp& rhs) const {
    return true;
}
std::ostream& operator<<(std::ostream& out, const RemoveDirectoryOwnerResp& x) {
    out << "RemoveDirectoryOwnerResp(" << ")";
    return out;
}

void CreateLockedCurrentEdgeReq::pack(BincodeBuf& buf) const {
    dirId.pack(buf);
    buf.packBytes(name);
    targetId.pack(buf);
    oldCreationTime.pack(buf);
}
void CreateLockedCurrentEdgeReq::unpack(BincodeBuf& buf) {
    dirId.unpack(buf);
    buf.unpackBytes(name);
    targetId.unpack(buf);
    oldCreationTime.unpack(buf);
}
void CreateLockedCurrentEdgeReq::clear() {
    dirId = InodeId();
    name.clear();
    targetId = InodeId();
    oldCreationTime = EggsTime();
}
bool CreateLockedCurrentEdgeReq::operator==(const CreateLockedCurrentEdgeReq& rhs) const {
    if ((InodeId)this->dirId != (InodeId)rhs.dirId) { return false; };
    if (name != rhs.name) { return false; };
    if ((InodeId)this->targetId != (InodeId)rhs.targetId) { return false; };
    if ((EggsTime)this->oldCreationTime != (EggsTime)rhs.oldCreationTime) { return false; };
    return true;
}
std::ostream& operator<<(std::ostream& out, const CreateLockedCurrentEdgeReq& x) {
    out << "CreateLockedCurrentEdgeReq(" << "DirId=" << x.dirId << ", " << "Name=" << GoLangQuotedStringFmt(x.name.data(), x.name.size()) << ", " << "TargetId=" << x.targetId << ", " << "OldCreationTime=" << x.oldCreationTime << ")";
    return out;
}

void CreateLockedCurrentEdgeResp::pack(BincodeBuf& buf) const {
    creationTime.pack(buf);
}
void CreateLockedCurrentEdgeResp::unpack(BincodeBuf& buf) {
    creationTime.unpack(buf);
}
void CreateLockedCurrentEdgeResp::clear() {
    creationTime = EggsTime();
}
bool CreateLockedCurrentEdgeResp::operator==(const CreateLockedCurrentEdgeResp& rhs) const {
    if ((EggsTime)this->creationTime != (EggsTime)rhs.creationTime) { return false; };
    return true;
}
std::ostream& operator<<(std::ostream& out, const CreateLockedCurrentEdgeResp& x) {
    out << "CreateLockedCurrentEdgeResp(" << "CreationTime=" << x.creationTime << ")";
    return out;
}

void LockCurrentEdgeReq::pack(BincodeBuf& buf) const {
    dirId.pack(buf);
    targetId.pack(buf);
    creationTime.pack(buf);
    buf.packBytes(name);
}
void LockCurrentEdgeReq::unpack(BincodeBuf& buf) {
    dirId.unpack(buf);
    targetId.unpack(buf);
    creationTime.unpack(buf);
    buf.unpackBytes(name);
}
void LockCurrentEdgeReq::clear() {
    dirId = InodeId();
    targetId = InodeId();
    creationTime = EggsTime();
    name.clear();
}
bool LockCurrentEdgeReq::operator==(const LockCurrentEdgeReq& rhs) const {
    if ((InodeId)this->dirId != (InodeId)rhs.dirId) { return false; };
    if ((InodeId)this->targetId != (InodeId)rhs.targetId) { return false; };
    if ((EggsTime)this->creationTime != (EggsTime)rhs.creationTime) { return false; };
    if (name != rhs.name) { return false; };
    return true;
}
std::ostream& operator<<(std::ostream& out, const LockCurrentEdgeReq& x) {
    out << "LockCurrentEdgeReq(" << "DirId=" << x.dirId << ", " << "TargetId=" << x.targetId << ", " << "CreationTime=" << x.creationTime << ", " << "Name=" << GoLangQuotedStringFmt(x.name.data(), x.name.size()) << ")";
    return out;
}

void LockCurrentEdgeResp::pack(BincodeBuf& buf) const {
}
void LockCurrentEdgeResp::unpack(BincodeBuf& buf) {
}
void LockCurrentEdgeResp::clear() {
}
bool LockCurrentEdgeResp::operator==(const LockCurrentEdgeResp& rhs) const {
    return true;
}
std::ostream& operator<<(std::ostream& out, const LockCurrentEdgeResp& x) {
    out << "LockCurrentEdgeResp(" << ")";
    return out;
}

void UnlockCurrentEdgeReq::pack(BincodeBuf& buf) const {
    dirId.pack(buf);
    buf.packBytes(name);
    creationTime.pack(buf);
    targetId.pack(buf);
    buf.packScalar<bool>(wasMoved);
}
void UnlockCurrentEdgeReq::unpack(BincodeBuf& buf) {
    dirId.unpack(buf);
    buf.unpackBytes(name);
    creationTime.unpack(buf);
    targetId.unpack(buf);
    wasMoved = buf.unpackScalar<bool>();
}
void UnlockCurrentEdgeReq::clear() {
    dirId = InodeId();
    name.clear();
    creationTime = EggsTime();
    targetId = InodeId();
    wasMoved = bool(0);
}
bool UnlockCurrentEdgeReq::operator==(const UnlockCurrentEdgeReq& rhs) const {
    if ((InodeId)this->dirId != (InodeId)rhs.dirId) { return false; };
    if (name != rhs.name) { return false; };
    if ((EggsTime)this->creationTime != (EggsTime)rhs.creationTime) { return false; };
    if ((InodeId)this->targetId != (InodeId)rhs.targetId) { return false; };
    if ((bool)this->wasMoved != (bool)rhs.wasMoved) { return false; };
    return true;
}
std::ostream& operator<<(std::ostream& out, const UnlockCurrentEdgeReq& x) {
    out << "UnlockCurrentEdgeReq(" << "DirId=" << x.dirId << ", " << "Name=" << GoLangQuotedStringFmt(x.name.data(), x.name.size()) << ", " << "CreationTime=" << x.creationTime << ", " << "TargetId=" << x.targetId << ", " << "WasMoved=" << x.wasMoved << ")";
    return out;
}

void UnlockCurrentEdgeResp::pack(BincodeBuf& buf) const {
}
void UnlockCurrentEdgeResp::unpack(BincodeBuf& buf) {
}
void UnlockCurrentEdgeResp::clear() {
}
bool UnlockCurrentEdgeResp::operator==(const UnlockCurrentEdgeResp& rhs) const {
    return true;
}
std::ostream& operator<<(std::ostream& out, const UnlockCurrentEdgeResp& x) {
    out << "UnlockCurrentEdgeResp(" << ")";
    return out;
}

void RemoveOwnedSnapshotFileEdgeReq::pack(BincodeBuf& buf) const {
    ownerId.pack(buf);
    targetId.pack(buf);
    buf.packBytes(name);
    creationTime.pack(buf);
}
void RemoveOwnedSnapshotFileEdgeReq::unpack(BincodeBuf& buf) {
    ownerId.unpack(buf);
    targetId.unpack(buf);
    buf.unpackBytes(name);
    creationTime.unpack(buf);
}
void RemoveOwnedSnapshotFileEdgeReq::clear() {
    ownerId = InodeId();
    targetId = InodeId();
    name.clear();
    creationTime = EggsTime();
}
bool RemoveOwnedSnapshotFileEdgeReq::operator==(const RemoveOwnedSnapshotFileEdgeReq& rhs) const {
    if ((InodeId)this->ownerId != (InodeId)rhs.ownerId) { return false; };
    if ((InodeId)this->targetId != (InodeId)rhs.targetId) { return false; };
    if (name != rhs.name) { return false; };
    if ((EggsTime)this->creationTime != (EggsTime)rhs.creationTime) { return false; };
    return true;
}
std::ostream& operator<<(std::ostream& out, const RemoveOwnedSnapshotFileEdgeReq& x) {
    out << "RemoveOwnedSnapshotFileEdgeReq(" << "OwnerId=" << x.ownerId << ", " << "TargetId=" << x.targetId << ", " << "Name=" << GoLangQuotedStringFmt(x.name.data(), x.name.size()) << ", " << "CreationTime=" << x.creationTime << ")";
    return out;
}

void RemoveOwnedSnapshotFileEdgeResp::pack(BincodeBuf& buf) const {
}
void RemoveOwnedSnapshotFileEdgeResp::unpack(BincodeBuf& buf) {
}
void RemoveOwnedSnapshotFileEdgeResp::clear() {
}
bool RemoveOwnedSnapshotFileEdgeResp::operator==(const RemoveOwnedSnapshotFileEdgeResp& rhs) const {
    return true;
}
std::ostream& operator<<(std::ostream& out, const RemoveOwnedSnapshotFileEdgeResp& x) {
    out << "RemoveOwnedSnapshotFileEdgeResp(" << ")";
    return out;
}

void MakeFileTransientReq::pack(BincodeBuf& buf) const {
    id.pack(buf);
    buf.packBytes(note);
}
void MakeFileTransientReq::unpack(BincodeBuf& buf) {
    id.unpack(buf);
    buf.unpackBytes(note);
}
void MakeFileTransientReq::clear() {
    id = InodeId();
    note.clear();
}
bool MakeFileTransientReq::operator==(const MakeFileTransientReq& rhs) const {
    if ((InodeId)this->id != (InodeId)rhs.id) { return false; };
    if (note != rhs.note) { return false; };
    return true;
}
std::ostream& operator<<(std::ostream& out, const MakeFileTransientReq& x) {
    out << "MakeFileTransientReq(" << "Id=" << x.id << ", " << "Note=" << GoLangQuotedStringFmt(x.note.data(), x.note.size()) << ")";
    return out;
}

void MakeFileTransientResp::pack(BincodeBuf& buf) const {
}
void MakeFileTransientResp::unpack(BincodeBuf& buf) {
}
void MakeFileTransientResp::clear() {
}
bool MakeFileTransientResp::operator==(const MakeFileTransientResp& rhs) const {
    return true;
}
std::ostream& operator<<(std::ostream& out, const MakeFileTransientResp& x) {
    out << "MakeFileTransientResp(" << ")";
    return out;
}

void MakeDirectoryReq::pack(BincodeBuf& buf) const {
    ownerId.pack(buf);
    buf.packBytes(name);
}
void MakeDirectoryReq::unpack(BincodeBuf& buf) {
    ownerId.unpack(buf);
    buf.unpackBytes(name);
}
void MakeDirectoryReq::clear() {
    ownerId = InodeId();
    name.clear();
}
bool MakeDirectoryReq::operator==(const MakeDirectoryReq& rhs) const {
    if ((InodeId)this->ownerId != (InodeId)rhs.ownerId) { return false; };
    if (name != rhs.name) { return false; };
    return true;
}
std::ostream& operator<<(std::ostream& out, const MakeDirectoryReq& x) {
    out << "MakeDirectoryReq(" << "OwnerId=" << x.ownerId << ", " << "Name=" << GoLangQuotedStringFmt(x.name.data(), x.name.size()) << ")";
    return out;
}

void MakeDirectoryResp::pack(BincodeBuf& buf) const {
    id.pack(buf);
    creationTime.pack(buf);
}
void MakeDirectoryResp::unpack(BincodeBuf& buf) {
    id.unpack(buf);
    creationTime.unpack(buf);
}
void MakeDirectoryResp::clear() {
    id = InodeId();
    creationTime = EggsTime();
}
bool MakeDirectoryResp::operator==(const MakeDirectoryResp& rhs) const {
    if ((InodeId)this->id != (InodeId)rhs.id) { return false; };
    if ((EggsTime)this->creationTime != (EggsTime)rhs.creationTime) { return false; };
    return true;
}
std::ostream& operator<<(std::ostream& out, const MakeDirectoryResp& x) {
    out << "MakeDirectoryResp(" << "Id=" << x.id << ", " << "CreationTime=" << x.creationTime << ")";
    return out;
}

void RenameFileReq::pack(BincodeBuf& buf) const {
    targetId.pack(buf);
    oldOwnerId.pack(buf);
    buf.packBytes(oldName);
    oldCreationTime.pack(buf);
    newOwnerId.pack(buf);
    buf.packBytes(newName);
}
void RenameFileReq::unpack(BincodeBuf& buf) {
    targetId.unpack(buf);
    oldOwnerId.unpack(buf);
    buf.unpackBytes(oldName);
    oldCreationTime.unpack(buf);
    newOwnerId.unpack(buf);
    buf.unpackBytes(newName);
}
void RenameFileReq::clear() {
    targetId = InodeId();
    oldOwnerId = InodeId();
    oldName.clear();
    oldCreationTime = EggsTime();
    newOwnerId = InodeId();
    newName.clear();
}
bool RenameFileReq::operator==(const RenameFileReq& rhs) const {
    if ((InodeId)this->targetId != (InodeId)rhs.targetId) { return false; };
    if ((InodeId)this->oldOwnerId != (InodeId)rhs.oldOwnerId) { return false; };
    if (oldName != rhs.oldName) { return false; };
    if ((EggsTime)this->oldCreationTime != (EggsTime)rhs.oldCreationTime) { return false; };
    if ((InodeId)this->newOwnerId != (InodeId)rhs.newOwnerId) { return false; };
    if (newName != rhs.newName) { return false; };
    return true;
}
std::ostream& operator<<(std::ostream& out, const RenameFileReq& x) {
    out << "RenameFileReq(" << "TargetId=" << x.targetId << ", " << "OldOwnerId=" << x.oldOwnerId << ", " << "OldName=" << GoLangQuotedStringFmt(x.oldName.data(), x.oldName.size()) << ", " << "OldCreationTime=" << x.oldCreationTime << ", " << "NewOwnerId=" << x.newOwnerId << ", " << "NewName=" << GoLangQuotedStringFmt(x.newName.data(), x.newName.size()) << ")";
    return out;
}

void RenameFileResp::pack(BincodeBuf& buf) const {
    creationTime.pack(buf);
}
void RenameFileResp::unpack(BincodeBuf& buf) {
    creationTime.unpack(buf);
}
void RenameFileResp::clear() {
    creationTime = EggsTime();
}
bool RenameFileResp::operator==(const RenameFileResp& rhs) const {
    if ((EggsTime)this->creationTime != (EggsTime)rhs.creationTime) { return false; };
    return true;
}
std::ostream& operator<<(std::ostream& out, const RenameFileResp& x) {
    out << "RenameFileResp(" << "CreationTime=" << x.creationTime << ")";
    return out;
}

void SoftUnlinkDirectoryReq::pack(BincodeBuf& buf) const {
    ownerId.pack(buf);
    targetId.pack(buf);
    creationTime.pack(buf);
    buf.packBytes(name);
}
void SoftUnlinkDirectoryReq::unpack(BincodeBuf& buf) {
    ownerId.unpack(buf);
    targetId.unpack(buf);
    creationTime.unpack(buf);
    buf.unpackBytes(name);
}
void SoftUnlinkDirectoryReq::clear() {
    ownerId = InodeId();
    targetId = InodeId();
    creationTime = EggsTime();
    name.clear();
}
bool SoftUnlinkDirectoryReq::operator==(const SoftUnlinkDirectoryReq& rhs) const {
    if ((InodeId)this->ownerId != (InodeId)rhs.ownerId) { return false; };
    if ((InodeId)this->targetId != (InodeId)rhs.targetId) { return false; };
    if ((EggsTime)this->creationTime != (EggsTime)rhs.creationTime) { return false; };
    if (name != rhs.name) { return false; };
    return true;
}
std::ostream& operator<<(std::ostream& out, const SoftUnlinkDirectoryReq& x) {
    out << "SoftUnlinkDirectoryReq(" << "OwnerId=" << x.ownerId << ", " << "TargetId=" << x.targetId << ", " << "CreationTime=" << x.creationTime << ", " << "Name=" << GoLangQuotedStringFmt(x.name.data(), x.name.size()) << ")";
    return out;
}

void SoftUnlinkDirectoryResp::pack(BincodeBuf& buf) const {
}
void SoftUnlinkDirectoryResp::unpack(BincodeBuf& buf) {
}
void SoftUnlinkDirectoryResp::clear() {
}
bool SoftUnlinkDirectoryResp::operator==(const SoftUnlinkDirectoryResp& rhs) const {
    return true;
}
std::ostream& operator<<(std::ostream& out, const SoftUnlinkDirectoryResp& x) {
    out << "SoftUnlinkDirectoryResp(" << ")";
    return out;
}

void RenameDirectoryReq::pack(BincodeBuf& buf) const {
    targetId.pack(buf);
    oldOwnerId.pack(buf);
    buf.packBytes(oldName);
    oldCreationTime.pack(buf);
    newOwnerId.pack(buf);
    buf.packBytes(newName);
}
void RenameDirectoryReq::unpack(BincodeBuf& buf) {
    targetId.unpack(buf);
    oldOwnerId.unpack(buf);
    buf.unpackBytes(oldName);
    oldCreationTime.unpack(buf);
    newOwnerId.unpack(buf);
    buf.unpackBytes(newName);
}
void RenameDirectoryReq::clear() {
    targetId = InodeId();
    oldOwnerId = InodeId();
    oldName.clear();
    oldCreationTime = EggsTime();
    newOwnerId = InodeId();
    newName.clear();
}
bool RenameDirectoryReq::operator==(const RenameDirectoryReq& rhs) const {
    if ((InodeId)this->targetId != (InodeId)rhs.targetId) { return false; };
    if ((InodeId)this->oldOwnerId != (InodeId)rhs.oldOwnerId) { return false; };
    if (oldName != rhs.oldName) { return false; };
    if ((EggsTime)this->oldCreationTime != (EggsTime)rhs.oldCreationTime) { return false; };
    if ((InodeId)this->newOwnerId != (InodeId)rhs.newOwnerId) { return false; };
    if (newName != rhs.newName) { return false; };
    return true;
}
std::ostream& operator<<(std::ostream& out, const RenameDirectoryReq& x) {
    out << "RenameDirectoryReq(" << "TargetId=" << x.targetId << ", " << "OldOwnerId=" << x.oldOwnerId << ", " << "OldName=" << GoLangQuotedStringFmt(x.oldName.data(), x.oldName.size()) << ", " << "OldCreationTime=" << x.oldCreationTime << ", " << "NewOwnerId=" << x.newOwnerId << ", " << "NewName=" << GoLangQuotedStringFmt(x.newName.data(), x.newName.size()) << ")";
    return out;
}

void RenameDirectoryResp::pack(BincodeBuf& buf) const {
    creationTime.pack(buf);
}
void RenameDirectoryResp::unpack(BincodeBuf& buf) {
    creationTime.unpack(buf);
}
void RenameDirectoryResp::clear() {
    creationTime = EggsTime();
}
bool RenameDirectoryResp::operator==(const RenameDirectoryResp& rhs) const {
    if ((EggsTime)this->creationTime != (EggsTime)rhs.creationTime) { return false; };
    return true;
}
std::ostream& operator<<(std::ostream& out, const RenameDirectoryResp& x) {
    out << "RenameDirectoryResp(" << "CreationTime=" << x.creationTime << ")";
    return out;
}

void HardUnlinkDirectoryReq::pack(BincodeBuf& buf) const {
    dirId.pack(buf);
}
void HardUnlinkDirectoryReq::unpack(BincodeBuf& buf) {
    dirId.unpack(buf);
}
void HardUnlinkDirectoryReq::clear() {
    dirId = InodeId();
}
bool HardUnlinkDirectoryReq::operator==(const HardUnlinkDirectoryReq& rhs) const {
    if ((InodeId)this->dirId != (InodeId)rhs.dirId) { return false; };
    return true;
}
std::ostream& operator<<(std::ostream& out, const HardUnlinkDirectoryReq& x) {
    out << "HardUnlinkDirectoryReq(" << "DirId=" << x.dirId << ")";
    return out;
}

void HardUnlinkDirectoryResp::pack(BincodeBuf& buf) const {
}
void HardUnlinkDirectoryResp::unpack(BincodeBuf& buf) {
}
void HardUnlinkDirectoryResp::clear() {
}
bool HardUnlinkDirectoryResp::operator==(const HardUnlinkDirectoryResp& rhs) const {
    return true;
}
std::ostream& operator<<(std::ostream& out, const HardUnlinkDirectoryResp& x) {
    out << "HardUnlinkDirectoryResp(" << ")";
    return out;
}

void CrossShardHardUnlinkFileReq::pack(BincodeBuf& buf) const {
    ownerId.pack(buf);
    targetId.pack(buf);
    buf.packBytes(name);
    creationTime.pack(buf);
}
void CrossShardHardUnlinkFileReq::unpack(BincodeBuf& buf) {
    ownerId.unpack(buf);
    targetId.unpack(buf);
    buf.unpackBytes(name);
    creationTime.unpack(buf);
}
void CrossShardHardUnlinkFileReq::clear() {
    ownerId = InodeId();
    targetId = InodeId();
    name.clear();
    creationTime = EggsTime();
}
bool CrossShardHardUnlinkFileReq::operator==(const CrossShardHardUnlinkFileReq& rhs) const {
    if ((InodeId)this->ownerId != (InodeId)rhs.ownerId) { return false; };
    if ((InodeId)this->targetId != (InodeId)rhs.targetId) { return false; };
    if (name != rhs.name) { return false; };
    if ((EggsTime)this->creationTime != (EggsTime)rhs.creationTime) { return false; };
    return true;
}
std::ostream& operator<<(std::ostream& out, const CrossShardHardUnlinkFileReq& x) {
    out << "CrossShardHardUnlinkFileReq(" << "OwnerId=" << x.ownerId << ", " << "TargetId=" << x.targetId << ", " << "Name=" << GoLangQuotedStringFmt(x.name.data(), x.name.size()) << ", " << "CreationTime=" << x.creationTime << ")";
    return out;
}

void CrossShardHardUnlinkFileResp::pack(BincodeBuf& buf) const {
}
void CrossShardHardUnlinkFileResp::unpack(BincodeBuf& buf) {
}
void CrossShardHardUnlinkFileResp::clear() {
}
bool CrossShardHardUnlinkFileResp::operator==(const CrossShardHardUnlinkFileResp& rhs) const {
    return true;
}
std::ostream& operator<<(std::ostream& out, const CrossShardHardUnlinkFileResp& x) {
    out << "CrossShardHardUnlinkFileResp(" << ")";
    return out;
}

void ShardsReq::pack(BincodeBuf& buf) const {
}
void ShardsReq::unpack(BincodeBuf& buf) {
}
void ShardsReq::clear() {
}
bool ShardsReq::operator==(const ShardsReq& rhs) const {
    return true;
}
std::ostream& operator<<(std::ostream& out, const ShardsReq& x) {
    out << "ShardsReq(" << ")";
    return out;
}

void ShardsResp::pack(BincodeBuf& buf) const {
    buf.packList<ShardInfo>(shards);
}
void ShardsResp::unpack(BincodeBuf& buf) {
    buf.unpackList<ShardInfo>(shards);
}
void ShardsResp::clear() {
    shards.clear();
}
bool ShardsResp::operator==(const ShardsResp& rhs) const {
    if (shards != rhs.shards) { return false; };
    return true;
}
std::ostream& operator<<(std::ostream& out, const ShardsResp& x) {
    out << "ShardsResp(" << "Shards=" << x.shards << ")";
    return out;
}

void CdcReq::pack(BincodeBuf& buf) const {
}
void CdcReq::unpack(BincodeBuf& buf) {
}
void CdcReq::clear() {
}
bool CdcReq::operator==(const CdcReq& rhs) const {
    return true;
}
std::ostream& operator<<(std::ostream& out, const CdcReq& x) {
    out << "CdcReq(" << ")";
    return out;
}

void CdcResp::pack(BincodeBuf& buf) const {
    buf.packFixedBytes<4>(ip1);
    buf.packScalar<uint16_t>(port1);
    buf.packFixedBytes<4>(ip2);
    buf.packScalar<uint16_t>(port2);
    lastSeen.pack(buf);
}
void CdcResp::unpack(BincodeBuf& buf) {
    buf.unpackFixedBytes<4>(ip1);
    port1 = buf.unpackScalar<uint16_t>();
    buf.unpackFixedBytes<4>(ip2);
    port2 = buf.unpackScalar<uint16_t>();
    lastSeen.unpack(buf);
}
void CdcResp::clear() {
    ip1.clear();
    port1 = uint16_t(0);
    ip2.clear();
    port2 = uint16_t(0);
    lastSeen = EggsTime();
}
bool CdcResp::operator==(const CdcResp& rhs) const {
    if (ip1 != rhs.ip1) { return false; };
    if ((uint16_t)this->port1 != (uint16_t)rhs.port1) { return false; };
    if (ip2 != rhs.ip2) { return false; };
    if ((uint16_t)this->port2 != (uint16_t)rhs.port2) { return false; };
    if ((EggsTime)this->lastSeen != (EggsTime)rhs.lastSeen) { return false; };
    return true;
}
std::ostream& operator<<(std::ostream& out, const CdcResp& x) {
    out << "CdcResp(" << "Ip1=" << x.ip1 << ", " << "Port1=" << x.port1 << ", " << "Ip2=" << x.ip2 << ", " << "Port2=" << x.port2 << ", " << "LastSeen=" << x.lastSeen << ")";
    return out;
}

void InfoReq::pack(BincodeBuf& buf) const {
}
void InfoReq::unpack(BincodeBuf& buf) {
}
void InfoReq::clear() {
}
bool InfoReq::operator==(const InfoReq& rhs) const {
    return true;
}
std::ostream& operator<<(std::ostream& out, const InfoReq& x) {
    out << "InfoReq(" << ")";
    return out;
}

void InfoResp::pack(BincodeBuf& buf) const {
    buf.packScalar<uint32_t>(numBlockServices);
    buf.packScalar<uint32_t>(numFailureDomains);
    buf.packScalar<uint64_t>(capacity);
    buf.packScalar<uint64_t>(available);
    buf.packScalar<uint64_t>(blocks);
}
void InfoResp::unpack(BincodeBuf& buf) {
    numBlockServices = buf.unpackScalar<uint32_t>();
    numFailureDomains = buf.unpackScalar<uint32_t>();
    capacity = buf.unpackScalar<uint64_t>();
    available = buf.unpackScalar<uint64_t>();
    blocks = buf.unpackScalar<uint64_t>();
}
void InfoResp::clear() {
    numBlockServices = uint32_t(0);
    numFailureDomains = uint32_t(0);
    capacity = uint64_t(0);
    available = uint64_t(0);
    blocks = uint64_t(0);
}
bool InfoResp::operator==(const InfoResp& rhs) const {
    if ((uint32_t)this->numBlockServices != (uint32_t)rhs.numBlockServices) { return false; };
    if ((uint32_t)this->numFailureDomains != (uint32_t)rhs.numFailureDomains) { return false; };
    if ((uint64_t)this->capacity != (uint64_t)rhs.capacity) { return false; };
    if ((uint64_t)this->available != (uint64_t)rhs.available) { return false; };
    if ((uint64_t)this->blocks != (uint64_t)rhs.blocks) { return false; };
    return true;
}
std::ostream& operator<<(std::ostream& out, const InfoResp& x) {
    out << "InfoResp(" << "NumBlockServices=" << x.numBlockServices << ", " << "NumFailureDomains=" << x.numFailureDomains << ", " << "Capacity=" << x.capacity << ", " << "Available=" << x.available << ", " << "Blocks=" << x.blocks << ")";
    return out;
}

void ShuckleReq::pack(BincodeBuf& buf) const {
}
void ShuckleReq::unpack(BincodeBuf& buf) {
}
void ShuckleReq::clear() {
}
bool ShuckleReq::operator==(const ShuckleReq& rhs) const {
    return true;
}
std::ostream& operator<<(std::ostream& out, const ShuckleReq& x) {
    out << "ShuckleReq(" << ")";
    return out;
}

void ShuckleResp::pack(BincodeBuf& buf) const {
    buf.packFixedBytes<4>(ip1);
    buf.packScalar<uint16_t>(port1);
    buf.packFixedBytes<4>(ip2);
    buf.packScalar<uint16_t>(port2);
}
void ShuckleResp::unpack(BincodeBuf& buf) {
    buf.unpackFixedBytes<4>(ip1);
    port1 = buf.unpackScalar<uint16_t>();
    buf.unpackFixedBytes<4>(ip2);
    port2 = buf.unpackScalar<uint16_t>();
}
void ShuckleResp::clear() {
    ip1.clear();
    port1 = uint16_t(0);
    ip2.clear();
    port2 = uint16_t(0);
}
bool ShuckleResp::operator==(const ShuckleResp& rhs) const {
    if (ip1 != rhs.ip1) { return false; };
    if ((uint16_t)this->port1 != (uint16_t)rhs.port1) { return false; };
    if (ip2 != rhs.ip2) { return false; };
    if ((uint16_t)this->port2 != (uint16_t)rhs.port2) { return false; };
    return true;
}
std::ostream& operator<<(std::ostream& out, const ShuckleResp& x) {
    out << "ShuckleResp(" << "Ip1=" << x.ip1 << ", " << "Port1=" << x.port1 << ", " << "Ip2=" << x.ip2 << ", " << "Port2=" << x.port2 << ")";
    return out;
}

void RegisterBlockServicesReq::pack(BincodeBuf& buf) const {
    buf.packList<RegisterBlockServiceInfo>(blockServices);
}
void RegisterBlockServicesReq::unpack(BincodeBuf& buf) {
    buf.unpackList<RegisterBlockServiceInfo>(blockServices);
}
void RegisterBlockServicesReq::clear() {
    blockServices.clear();
}
bool RegisterBlockServicesReq::operator==(const RegisterBlockServicesReq& rhs) const {
    if (blockServices != rhs.blockServices) { return false; };
    return true;
}
std::ostream& operator<<(std::ostream& out, const RegisterBlockServicesReq& x) {
    out << "RegisterBlockServicesReq(" << "BlockServices=" << x.blockServices << ")";
    return out;
}

void RegisterBlockServicesResp::pack(BincodeBuf& buf) const {
}
void RegisterBlockServicesResp::unpack(BincodeBuf& buf) {
}
void RegisterBlockServicesResp::clear() {
}
bool RegisterBlockServicesResp::operator==(const RegisterBlockServicesResp& rhs) const {
    return true;
}
std::ostream& operator<<(std::ostream& out, const RegisterBlockServicesResp& x) {
    out << "RegisterBlockServicesResp(" << ")";
    return out;
}

void RegisterShardReq::pack(BincodeBuf& buf) const {
    id.pack(buf);
    info.pack(buf);
}
void RegisterShardReq::unpack(BincodeBuf& buf) {
    id.unpack(buf);
    info.unpack(buf);
}
void RegisterShardReq::clear() {
    id = ShardId();
    info.clear();
}
bool RegisterShardReq::operator==(const RegisterShardReq& rhs) const {
    if ((ShardId)this->id != (ShardId)rhs.id) { return false; };
    if (info != rhs.info) { return false; };
    return true;
}
std::ostream& operator<<(std::ostream& out, const RegisterShardReq& x) {
    out << "RegisterShardReq(" << "Id=" << x.id << ", " << "Info=" << x.info << ")";
    return out;
}

void RegisterShardResp::pack(BincodeBuf& buf) const {
}
void RegisterShardResp::unpack(BincodeBuf& buf) {
}
void RegisterShardResp::clear() {
}
bool RegisterShardResp::operator==(const RegisterShardResp& rhs) const {
    return true;
}
std::ostream& operator<<(std::ostream& out, const RegisterShardResp& x) {
    out << "RegisterShardResp(" << ")";
    return out;
}

void AllBlockServicesReq::pack(BincodeBuf& buf) const {
}
void AllBlockServicesReq::unpack(BincodeBuf& buf) {
}
void AllBlockServicesReq::clear() {
}
bool AllBlockServicesReq::operator==(const AllBlockServicesReq& rhs) const {
    return true;
}
std::ostream& operator<<(std::ostream& out, const AllBlockServicesReq& x) {
    out << "AllBlockServicesReq(" << ")";
    return out;
}

void AllBlockServicesResp::pack(BincodeBuf& buf) const {
    buf.packList<BlockServiceInfo>(blockServices);
}
void AllBlockServicesResp::unpack(BincodeBuf& buf) {
    buf.unpackList<BlockServiceInfo>(blockServices);
}
void AllBlockServicesResp::clear() {
    blockServices.clear();
}
bool AllBlockServicesResp::operator==(const AllBlockServicesResp& rhs) const {
    if (blockServices != rhs.blockServices) { return false; };
    return true;
}
std::ostream& operator<<(std::ostream& out, const AllBlockServicesResp& x) {
    out << "AllBlockServicesResp(" << "BlockServices=" << x.blockServices << ")";
    return out;
}

void RegisterCdcReq::pack(BincodeBuf& buf) const {
    buf.packFixedBytes<4>(ip1);
    buf.packScalar<uint16_t>(port1);
    buf.packFixedBytes<4>(ip2);
    buf.packScalar<uint16_t>(port2);
}
void RegisterCdcReq::unpack(BincodeBuf& buf) {
    buf.unpackFixedBytes<4>(ip1);
    port1 = buf.unpackScalar<uint16_t>();
    buf.unpackFixedBytes<4>(ip2);
    port2 = buf.unpackScalar<uint16_t>();
}
void RegisterCdcReq::clear() {
    ip1.clear();
    port1 = uint16_t(0);
    ip2.clear();
    port2 = uint16_t(0);
}
bool RegisterCdcReq::operator==(const RegisterCdcReq& rhs) const {
    if (ip1 != rhs.ip1) { return false; };
    if ((uint16_t)this->port1 != (uint16_t)rhs.port1) { return false; };
    if (ip2 != rhs.ip2) { return false; };
    if ((uint16_t)this->port2 != (uint16_t)rhs.port2) { return false; };
    return true;
}
std::ostream& operator<<(std::ostream& out, const RegisterCdcReq& x) {
    out << "RegisterCdcReq(" << "Ip1=" << x.ip1 << ", " << "Port1=" << x.port1 << ", " << "Ip2=" << x.ip2 << ", " << "Port2=" << x.port2 << ")";
    return out;
}

void RegisterCdcResp::pack(BincodeBuf& buf) const {
}
void RegisterCdcResp::unpack(BincodeBuf& buf) {
}
void RegisterCdcResp::clear() {
}
bool RegisterCdcResp::operator==(const RegisterCdcResp& rhs) const {
    return true;
}
std::ostream& operator<<(std::ostream& out, const RegisterCdcResp& x) {
    out << "RegisterCdcResp(" << ")";
    return out;
}

void SetBlockServiceFlagsReq::pack(BincodeBuf& buf) const {
    id.pack(buf);
    buf.packScalar<uint8_t>(flags);
    buf.packScalar<uint8_t>(flagsMask);
}
void SetBlockServiceFlagsReq::unpack(BincodeBuf& buf) {
    id.unpack(buf);
    flags = buf.unpackScalar<uint8_t>();
    flagsMask = buf.unpackScalar<uint8_t>();
}
void SetBlockServiceFlagsReq::clear() {
    id = BlockServiceId(0);
    flags = uint8_t(0);
    flagsMask = uint8_t(0);
}
bool SetBlockServiceFlagsReq::operator==(const SetBlockServiceFlagsReq& rhs) const {
    if ((BlockServiceId)this->id != (BlockServiceId)rhs.id) { return false; };
    if ((uint8_t)this->flags != (uint8_t)rhs.flags) { return false; };
    if ((uint8_t)this->flagsMask != (uint8_t)rhs.flagsMask) { return false; };
    return true;
}
std::ostream& operator<<(std::ostream& out, const SetBlockServiceFlagsReq& x) {
    out << "SetBlockServiceFlagsReq(" << "Id=" << x.id << ", " << "Flags=" << (int)x.flags << ", " << "FlagsMask=" << (int)x.flagsMask << ")";
    return out;
}

void SetBlockServiceFlagsResp::pack(BincodeBuf& buf) const {
}
void SetBlockServiceFlagsResp::unpack(BincodeBuf& buf) {
}
void SetBlockServiceFlagsResp::clear() {
}
bool SetBlockServiceFlagsResp::operator==(const SetBlockServiceFlagsResp& rhs) const {
    return true;
}
std::ostream& operator<<(std::ostream& out, const SetBlockServiceFlagsResp& x) {
    out << "SetBlockServiceFlagsResp(" << ")";
    return out;
}

void BlockServiceReq::pack(BincodeBuf& buf) const {
    id.pack(buf);
}
void BlockServiceReq::unpack(BincodeBuf& buf) {
    id.unpack(buf);
}
void BlockServiceReq::clear() {
    id = BlockServiceId(0);
}
bool BlockServiceReq::operator==(const BlockServiceReq& rhs) const {
    if ((BlockServiceId)this->id != (BlockServiceId)rhs.id) { return false; };
    return true;
}
std::ostream& operator<<(std::ostream& out, const BlockServiceReq& x) {
    out << "BlockServiceReq(" << "Id=" << x.id << ")";
    return out;
}

void BlockServiceResp::pack(BincodeBuf& buf) const {
    info.pack(buf);
}
void BlockServiceResp::unpack(BincodeBuf& buf) {
    info.unpack(buf);
}
void BlockServiceResp::clear() {
    info.clear();
}
bool BlockServiceResp::operator==(const BlockServiceResp& rhs) const {
    if (info != rhs.info) { return false; };
    return true;
}
std::ostream& operator<<(std::ostream& out, const BlockServiceResp& x) {
    out << "BlockServiceResp(" << "Info=" << x.info << ")";
    return out;
}

void InsertStatsReq::pack(BincodeBuf& buf) const {
    buf.packList<Stat>(stats);
}
void InsertStatsReq::unpack(BincodeBuf& buf) {
    buf.unpackList<Stat>(stats);
}
void InsertStatsReq::clear() {
    stats.clear();
}
bool InsertStatsReq::operator==(const InsertStatsReq& rhs) const {
    if (stats != rhs.stats) { return false; };
    return true;
}
std::ostream& operator<<(std::ostream& out, const InsertStatsReq& x) {
    out << "InsertStatsReq(" << "Stats=" << x.stats << ")";
    return out;
}

void InsertStatsResp::pack(BincodeBuf& buf) const {
}
void InsertStatsResp::unpack(BincodeBuf& buf) {
}
void InsertStatsResp::clear() {
}
bool InsertStatsResp::operator==(const InsertStatsResp& rhs) const {
    return true;
}
std::ostream& operator<<(std::ostream& out, const InsertStatsResp& x) {
    out << "InsertStatsResp(" << ")";
    return out;
}

void ShardReq::pack(BincodeBuf& buf) const {
    id.pack(buf);
}
void ShardReq::unpack(BincodeBuf& buf) {
    id.unpack(buf);
}
void ShardReq::clear() {
    id = ShardId();
}
bool ShardReq::operator==(const ShardReq& rhs) const {
    if ((ShardId)this->id != (ShardId)rhs.id) { return false; };
    return true;
}
std::ostream& operator<<(std::ostream& out, const ShardReq& x) {
    out << "ShardReq(" << "Id=" << x.id << ")";
    return out;
}

void ShardResp::pack(BincodeBuf& buf) const {
    info.pack(buf);
}
void ShardResp::unpack(BincodeBuf& buf) {
    info.unpack(buf);
}
void ShardResp::clear() {
    info.clear();
}
bool ShardResp::operator==(const ShardResp& rhs) const {
    if (info != rhs.info) { return false; };
    return true;
}
std::ostream& operator<<(std::ostream& out, const ShardResp& x) {
    out << "ShardResp(" << "Info=" << x.info << ")";
    return out;
}

void GetStatsReq::pack(BincodeBuf& buf) const {
    startTime.pack(buf);
    buf.packBytes(startName);
    endTime.pack(buf);
}
void GetStatsReq::unpack(BincodeBuf& buf) {
    startTime.unpack(buf);
    buf.unpackBytes(startName);
    endTime.unpack(buf);
}
void GetStatsReq::clear() {
    startTime = EggsTime();
    startName.clear();
    endTime = EggsTime();
}
bool GetStatsReq::operator==(const GetStatsReq& rhs) const {
    if ((EggsTime)this->startTime != (EggsTime)rhs.startTime) { return false; };
    if (startName != rhs.startName) { return false; };
    if ((EggsTime)this->endTime != (EggsTime)rhs.endTime) { return false; };
    return true;
}
std::ostream& operator<<(std::ostream& out, const GetStatsReq& x) {
    out << "GetStatsReq(" << "StartTime=" << x.startTime << ", " << "StartName=" << GoLangQuotedStringFmt(x.startName.data(), x.startName.size()) << ", " << "EndTime=" << x.endTime << ")";
    return out;
}

void GetStatsResp::pack(BincodeBuf& buf) const {
    nextTime.pack(buf);
    buf.packBytes(nextName);
    buf.packList<Stat>(stats);
}
void GetStatsResp::unpack(BincodeBuf& buf) {
    nextTime.unpack(buf);
    buf.unpackBytes(nextName);
    buf.unpackList<Stat>(stats);
}
void GetStatsResp::clear() {
    nextTime = EggsTime();
    nextName.clear();
    stats.clear();
}
bool GetStatsResp::operator==(const GetStatsResp& rhs) const {
    if ((EggsTime)this->nextTime != (EggsTime)rhs.nextTime) { return false; };
    if (nextName != rhs.nextName) { return false; };
    if (stats != rhs.stats) { return false; };
    return true;
}
std::ostream& operator<<(std::ostream& out, const GetStatsResp& x) {
    out << "GetStatsResp(" << "NextTime=" << x.nextTime << ", " << "NextName=" << GoLangQuotedStringFmt(x.nextName.data(), x.nextName.size()) << ", " << "Stats=" << x.stats << ")";
    return out;
}

void RegisterShardReplicaReq::pack(BincodeBuf& buf) const {
    shrid.pack(buf);
    buf.packScalar<bool>(isLeader);
    info.pack(buf);
}
void RegisterShardReplicaReq::unpack(BincodeBuf& buf) {
    shrid.unpack(buf);
    isLeader = buf.unpackScalar<bool>();
    info.unpack(buf);
}
void RegisterShardReplicaReq::clear() {
    shrid = ShardReplicaId();
    isLeader = bool(0);
    info.clear();
}
bool RegisterShardReplicaReq::operator==(const RegisterShardReplicaReq& rhs) const {
    if ((ShardReplicaId)this->shrid != (ShardReplicaId)rhs.shrid) { return false; };
    if ((bool)this->isLeader != (bool)rhs.isLeader) { return false; };
    if (info != rhs.info) { return false; };
    return true;
}
std::ostream& operator<<(std::ostream& out, const RegisterShardReplicaReq& x) {
    out << "RegisterShardReplicaReq(" << "Shrid=" << x.shrid << ", " << "IsLeader=" << x.isLeader << ", " << "Info=" << x.info << ")";
    return out;
}

void RegisterShardReplicaResp::pack(BincodeBuf& buf) const {
}
void RegisterShardReplicaResp::unpack(BincodeBuf& buf) {
}
void RegisterShardReplicaResp::clear() {
}
bool RegisterShardReplicaResp::operator==(const RegisterShardReplicaResp& rhs) const {
    return true;
}
std::ostream& operator<<(std::ostream& out, const RegisterShardReplicaResp& x) {
    out << "RegisterShardReplicaResp(" << ")";
    return out;
}

void ShardReplicasReq::pack(BincodeBuf& buf) const {
    id.pack(buf);
}
void ShardReplicasReq::unpack(BincodeBuf& buf) {
    id.unpack(buf);
}
void ShardReplicasReq::clear() {
    id = ShardId();
}
bool ShardReplicasReq::operator==(const ShardReplicasReq& rhs) const {
    if ((ShardId)this->id != (ShardId)rhs.id) { return false; };
    return true;
}
std::ostream& operator<<(std::ostream& out, const ShardReplicasReq& x) {
    out << "ShardReplicasReq(" << "Id=" << x.id << ")";
    return out;
}

void ShardReplicasResp::pack(BincodeBuf& buf) const {
    buf.packList<AddrsInfo>(replicas);
}
void ShardReplicasResp::unpack(BincodeBuf& buf) {
    buf.unpackList<AddrsInfo>(replicas);
}
void ShardReplicasResp::clear() {
    replicas.clear();
}
bool ShardReplicasResp::operator==(const ShardReplicasResp& rhs) const {
    if (replicas != rhs.replicas) { return false; };
    return true;
}
std::ostream& operator<<(std::ostream& out, const ShardReplicasResp& x) {
    out << "ShardReplicasResp(" << "Replicas=" << x.replicas << ")";
    return out;
}

void ShardBlockServicesReq::pack(BincodeBuf& buf) const {
    shardId.pack(buf);
}
void ShardBlockServicesReq::unpack(BincodeBuf& buf) {
    shardId.unpack(buf);
}
void ShardBlockServicesReq::clear() {
    shardId = ShardId();
}
bool ShardBlockServicesReq::operator==(const ShardBlockServicesReq& rhs) const {
    if ((ShardId)this->shardId != (ShardId)rhs.shardId) { return false; };
    return true;
}
std::ostream& operator<<(std::ostream& out, const ShardBlockServicesReq& x) {
    out << "ShardBlockServicesReq(" << "ShardId=" << x.shardId << ")";
    return out;
}

void ShardBlockServicesResp::pack(BincodeBuf& buf) const {
    buf.packList<BlockServiceId>(blockServices);
}
void ShardBlockServicesResp::unpack(BincodeBuf& buf) {
    buf.unpackList<BlockServiceId>(blockServices);
}
void ShardBlockServicesResp::clear() {
    blockServices.clear();
}
bool ShardBlockServicesResp::operator==(const ShardBlockServicesResp& rhs) const {
    if (blockServices != rhs.blockServices) { return false; };
    return true;
}
std::ostream& operator<<(std::ostream& out, const ShardBlockServicesResp& x) {
    out << "ShardBlockServicesResp(" << "BlockServices=" << x.blockServices << ")";
    return out;
}

void RegisterCdcReplicaReq::pack(BincodeBuf& buf) const {
    replica.pack(buf);
    buf.packScalar<bool>(isLeader);
    info.pack(buf);
}
void RegisterCdcReplicaReq::unpack(BincodeBuf& buf) {
    replica.unpack(buf);
    isLeader = buf.unpackScalar<bool>();
    info.unpack(buf);
}
void RegisterCdcReplicaReq::clear() {
    replica = ReplicaId();
    isLeader = bool(0);
    info.clear();
}
bool RegisterCdcReplicaReq::operator==(const RegisterCdcReplicaReq& rhs) const {
    if ((ReplicaId)this->replica != (ReplicaId)rhs.replica) { return false; };
    if ((bool)this->isLeader != (bool)rhs.isLeader) { return false; };
    if (info != rhs.info) { return false; };
    return true;
}
std::ostream& operator<<(std::ostream& out, const RegisterCdcReplicaReq& x) {
    out << "RegisterCdcReplicaReq(" << "Replica=" << x.replica << ", " << "IsLeader=" << x.isLeader << ", " << "Info=" << x.info << ")";
    return out;
}

void RegisterCdcReplicaResp::pack(BincodeBuf& buf) const {
}
void RegisterCdcReplicaResp::unpack(BincodeBuf& buf) {
}
void RegisterCdcReplicaResp::clear() {
}
bool RegisterCdcReplicaResp::operator==(const RegisterCdcReplicaResp& rhs) const {
    return true;
}
std::ostream& operator<<(std::ostream& out, const RegisterCdcReplicaResp& x) {
    out << "RegisterCdcReplicaResp(" << ")";
    return out;
}

void CdcReplicasReq::pack(BincodeBuf& buf) const {
}
void CdcReplicasReq::unpack(BincodeBuf& buf) {
}
void CdcReplicasReq::clear() {
}
bool CdcReplicasReq::operator==(const CdcReplicasReq& rhs) const {
    return true;
}
std::ostream& operator<<(std::ostream& out, const CdcReplicasReq& x) {
    out << "CdcReplicasReq(" << ")";
    return out;
}

void CdcReplicasResp::pack(BincodeBuf& buf) const {
    buf.packList<AddrsInfo>(replicas);
}
void CdcReplicasResp::unpack(BincodeBuf& buf) {
    buf.unpackList<AddrsInfo>(replicas);
}
void CdcReplicasResp::clear() {
    replicas.clear();
}
bool CdcReplicasResp::operator==(const CdcReplicasResp& rhs) const {
    if (replicas != rhs.replicas) { return false; };
    return true;
}
std::ostream& operator<<(std::ostream& out, const CdcReplicasResp& x) {
    out << "CdcReplicasResp(" << "Replicas=" << x.replicas << ")";
    return out;
}

void FetchBlockReq::pack(BincodeBuf& buf) const {
    buf.packScalar<uint64_t>(blockId);
    buf.packScalar<uint32_t>(offset);
    buf.packScalar<uint32_t>(count);
}
void FetchBlockReq::unpack(BincodeBuf& buf) {
    blockId = buf.unpackScalar<uint64_t>();
    offset = buf.unpackScalar<uint32_t>();
    count = buf.unpackScalar<uint32_t>();
}
void FetchBlockReq::clear() {
    blockId = uint64_t(0);
    offset = uint32_t(0);
    count = uint32_t(0);
}
bool FetchBlockReq::operator==(const FetchBlockReq& rhs) const {
    if ((uint64_t)this->blockId != (uint64_t)rhs.blockId) { return false; };
    if ((uint32_t)this->offset != (uint32_t)rhs.offset) { return false; };
    if ((uint32_t)this->count != (uint32_t)rhs.count) { return false; };
    return true;
}
std::ostream& operator<<(std::ostream& out, const FetchBlockReq& x) {
    out << "FetchBlockReq(" << "BlockId=" << x.blockId << ", " << "Offset=" << x.offset << ", " << "Count=" << x.count << ")";
    return out;
}

void FetchBlockResp::pack(BincodeBuf& buf) const {
}
void FetchBlockResp::unpack(BincodeBuf& buf) {
}
void FetchBlockResp::clear() {
}
bool FetchBlockResp::operator==(const FetchBlockResp& rhs) const {
    return true;
}
std::ostream& operator<<(std::ostream& out, const FetchBlockResp& x) {
    out << "FetchBlockResp(" << ")";
    return out;
}

void WriteBlockReq::pack(BincodeBuf& buf) const {
    buf.packScalar<uint64_t>(blockId);
    crc.pack(buf);
    buf.packScalar<uint32_t>(size);
    buf.packFixedBytes<8>(certificate);
}
void WriteBlockReq::unpack(BincodeBuf& buf) {
    blockId = buf.unpackScalar<uint64_t>();
    crc.unpack(buf);
    size = buf.unpackScalar<uint32_t>();
    buf.unpackFixedBytes<8>(certificate);
}
void WriteBlockReq::clear() {
    blockId = uint64_t(0);
    crc = Crc(0);
    size = uint32_t(0);
    certificate.clear();
}
bool WriteBlockReq::operator==(const WriteBlockReq& rhs) const {
    if ((uint64_t)this->blockId != (uint64_t)rhs.blockId) { return false; };
    if ((Crc)this->crc != (Crc)rhs.crc) { return false; };
    if ((uint32_t)this->size != (uint32_t)rhs.size) { return false; };
    if (certificate != rhs.certificate) { return false; };
    return true;
}
std::ostream& operator<<(std::ostream& out, const WriteBlockReq& x) {
    out << "WriteBlockReq(" << "BlockId=" << x.blockId << ", " << "Crc=" << x.crc << ", " << "Size=" << x.size << ", " << "Certificate=" << x.certificate << ")";
    return out;
}

void WriteBlockResp::pack(BincodeBuf& buf) const {
    buf.packFixedBytes<8>(proof);
}
void WriteBlockResp::unpack(BincodeBuf& buf) {
    buf.unpackFixedBytes<8>(proof);
}
void WriteBlockResp::clear() {
    proof.clear();
}
bool WriteBlockResp::operator==(const WriteBlockResp& rhs) const {
    if (proof != rhs.proof) { return false; };
    return true;
}
std::ostream& operator<<(std::ostream& out, const WriteBlockResp& x) {
    out << "WriteBlockResp(" << "Proof=" << x.proof << ")";
    return out;
}

void EraseBlockReq::pack(BincodeBuf& buf) const {
    buf.packScalar<uint64_t>(blockId);
    buf.packFixedBytes<8>(certificate);
}
void EraseBlockReq::unpack(BincodeBuf& buf) {
    blockId = buf.unpackScalar<uint64_t>();
    buf.unpackFixedBytes<8>(certificate);
}
void EraseBlockReq::clear() {
    blockId = uint64_t(0);
    certificate.clear();
}
bool EraseBlockReq::operator==(const EraseBlockReq& rhs) const {
    if ((uint64_t)this->blockId != (uint64_t)rhs.blockId) { return false; };
    if (certificate != rhs.certificate) { return false; };
    return true;
}
std::ostream& operator<<(std::ostream& out, const EraseBlockReq& x) {
    out << "EraseBlockReq(" << "BlockId=" << x.blockId << ", " << "Certificate=" << x.certificate << ")";
    return out;
}

void EraseBlockResp::pack(BincodeBuf& buf) const {
    buf.packFixedBytes<8>(proof);
}
void EraseBlockResp::unpack(BincodeBuf& buf) {
    buf.unpackFixedBytes<8>(proof);
}
void EraseBlockResp::clear() {
    proof.clear();
}
bool EraseBlockResp::operator==(const EraseBlockResp& rhs) const {
    if (proof != rhs.proof) { return false; };
    return true;
}
std::ostream& operator<<(std::ostream& out, const EraseBlockResp& x) {
    out << "EraseBlockResp(" << "Proof=" << x.proof << ")";
    return out;
}

void TestWriteReq::pack(BincodeBuf& buf) const {
    buf.packScalar<uint64_t>(size);
}
void TestWriteReq::unpack(BincodeBuf& buf) {
    size = buf.unpackScalar<uint64_t>();
}
void TestWriteReq::clear() {
    size = uint64_t(0);
}
bool TestWriteReq::operator==(const TestWriteReq& rhs) const {
    if ((uint64_t)this->size != (uint64_t)rhs.size) { return false; };
    return true;
}
std::ostream& operator<<(std::ostream& out, const TestWriteReq& x) {
    out << "TestWriteReq(" << "Size=" << x.size << ")";
    return out;
}

void TestWriteResp::pack(BincodeBuf& buf) const {
}
void TestWriteResp::unpack(BincodeBuf& buf) {
}
void TestWriteResp::clear() {
}
bool TestWriteResp::operator==(const TestWriteResp& rhs) const {
    return true;
}
std::ostream& operator<<(std::ostream& out, const TestWriteResp& x) {
    out << "TestWriteResp(" << ")";
    return out;
}

void CheckBlockReq::pack(BincodeBuf& buf) const {
    buf.packScalar<uint64_t>(blockId);
    buf.packScalar<uint32_t>(size);
    crc.pack(buf);
}
void CheckBlockReq::unpack(BincodeBuf& buf) {
    blockId = buf.unpackScalar<uint64_t>();
    size = buf.unpackScalar<uint32_t>();
    crc.unpack(buf);
}
void CheckBlockReq::clear() {
    blockId = uint64_t(0);
    size = uint32_t(0);
    crc = Crc(0);
}
bool CheckBlockReq::operator==(const CheckBlockReq& rhs) const {
    if ((uint64_t)this->blockId != (uint64_t)rhs.blockId) { return false; };
    if ((uint32_t)this->size != (uint32_t)rhs.size) { return false; };
    if ((Crc)this->crc != (Crc)rhs.crc) { return false; };
    return true;
}
std::ostream& operator<<(std::ostream& out, const CheckBlockReq& x) {
    out << "CheckBlockReq(" << "BlockId=" << x.blockId << ", " << "Size=" << x.size << ", " << "Crc=" << x.crc << ")";
    return out;
}

void CheckBlockResp::pack(BincodeBuf& buf) const {
}
void CheckBlockResp::unpack(BincodeBuf& buf) {
}
void CheckBlockResp::clear() {
}
bool CheckBlockResp::operator==(const CheckBlockResp& rhs) const {
    return true;
}
std::ostream& operator<<(std::ostream& out, const CheckBlockResp& x) {
    out << "CheckBlockResp(" << ")";
    return out;
}

const LookupReq& ShardReqContainer::getLookup() const {
    ALWAYS_ASSERT(_kind == ShardMessageKind::LOOKUP, "%s != %s", _kind, ShardMessageKind::LOOKUP);
    return std::get<0>(_data);
}
LookupReq& ShardReqContainer::setLookup() {
    _kind = ShardMessageKind::LOOKUP;
    auto& x = _data.emplace<0>();
    return x;
}
const StatFileReq& ShardReqContainer::getStatFile() const {
    ALWAYS_ASSERT(_kind == ShardMessageKind::STAT_FILE, "%s != %s", _kind, ShardMessageKind::STAT_FILE);
    return std::get<1>(_data);
}
StatFileReq& ShardReqContainer::setStatFile() {
    _kind = ShardMessageKind::STAT_FILE;
    auto& x = _data.emplace<1>();
    return x;
}
const StatDirectoryReq& ShardReqContainer::getStatDirectory() const {
    ALWAYS_ASSERT(_kind == ShardMessageKind::STAT_DIRECTORY, "%s != %s", _kind, ShardMessageKind::STAT_DIRECTORY);
    return std::get<2>(_data);
}
StatDirectoryReq& ShardReqContainer::setStatDirectory() {
    _kind = ShardMessageKind::STAT_DIRECTORY;
    auto& x = _data.emplace<2>();
    return x;
}
const ReadDirReq& ShardReqContainer::getReadDir() const {
    ALWAYS_ASSERT(_kind == ShardMessageKind::READ_DIR, "%s != %s", _kind, ShardMessageKind::READ_DIR);
    return std::get<3>(_data);
}
ReadDirReq& ShardReqContainer::setReadDir() {
    _kind = ShardMessageKind::READ_DIR;
    auto& x = _data.emplace<3>();
    return x;
}
const ConstructFileReq& ShardReqContainer::getConstructFile() const {
    ALWAYS_ASSERT(_kind == ShardMessageKind::CONSTRUCT_FILE, "%s != %s", _kind, ShardMessageKind::CONSTRUCT_FILE);
    return std::get<4>(_data);
}
ConstructFileReq& ShardReqContainer::setConstructFile() {
    _kind = ShardMessageKind::CONSTRUCT_FILE;
    auto& x = _data.emplace<4>();
    return x;
}
const AddSpanInitiateReq& ShardReqContainer::getAddSpanInitiate() const {
    ALWAYS_ASSERT(_kind == ShardMessageKind::ADD_SPAN_INITIATE, "%s != %s", _kind, ShardMessageKind::ADD_SPAN_INITIATE);
    return std::get<5>(_data);
}
AddSpanInitiateReq& ShardReqContainer::setAddSpanInitiate() {
    _kind = ShardMessageKind::ADD_SPAN_INITIATE;
    auto& x = _data.emplace<5>();
    return x;
}
const AddSpanCertifyReq& ShardReqContainer::getAddSpanCertify() const {
    ALWAYS_ASSERT(_kind == ShardMessageKind::ADD_SPAN_CERTIFY, "%s != %s", _kind, ShardMessageKind::ADD_SPAN_CERTIFY);
    return std::get<6>(_data);
}
AddSpanCertifyReq& ShardReqContainer::setAddSpanCertify() {
    _kind = ShardMessageKind::ADD_SPAN_CERTIFY;
    auto& x = _data.emplace<6>();
    return x;
}
const LinkFileReq& ShardReqContainer::getLinkFile() const {
    ALWAYS_ASSERT(_kind == ShardMessageKind::LINK_FILE, "%s != %s", _kind, ShardMessageKind::LINK_FILE);
    return std::get<7>(_data);
}
LinkFileReq& ShardReqContainer::setLinkFile() {
    _kind = ShardMessageKind::LINK_FILE;
    auto& x = _data.emplace<7>();
    return x;
}
const SoftUnlinkFileReq& ShardReqContainer::getSoftUnlinkFile() const {
    ALWAYS_ASSERT(_kind == ShardMessageKind::SOFT_UNLINK_FILE, "%s != %s", _kind, ShardMessageKind::SOFT_UNLINK_FILE);
    return std::get<8>(_data);
}
SoftUnlinkFileReq& ShardReqContainer::setSoftUnlinkFile() {
    _kind = ShardMessageKind::SOFT_UNLINK_FILE;
    auto& x = _data.emplace<8>();
    return x;
}
const FileSpansReq& ShardReqContainer::getFileSpans() const {
    ALWAYS_ASSERT(_kind == ShardMessageKind::FILE_SPANS, "%s != %s", _kind, ShardMessageKind::FILE_SPANS);
    return std::get<9>(_data);
}
FileSpansReq& ShardReqContainer::setFileSpans() {
    _kind = ShardMessageKind::FILE_SPANS;
    auto& x = _data.emplace<9>();
    return x;
}
const SameDirectoryRenameReq& ShardReqContainer::getSameDirectoryRename() const {
    ALWAYS_ASSERT(_kind == ShardMessageKind::SAME_DIRECTORY_RENAME, "%s != %s", _kind, ShardMessageKind::SAME_DIRECTORY_RENAME);
    return std::get<10>(_data);
}
SameDirectoryRenameReq& ShardReqContainer::setSameDirectoryRename() {
    _kind = ShardMessageKind::SAME_DIRECTORY_RENAME;
    auto& x = _data.emplace<10>();
    return x;
}
const AddInlineSpanReq& ShardReqContainer::getAddInlineSpan() const {
    ALWAYS_ASSERT(_kind == ShardMessageKind::ADD_INLINE_SPAN, "%s != %s", _kind, ShardMessageKind::ADD_INLINE_SPAN);
    return std::get<11>(_data);
}
AddInlineSpanReq& ShardReqContainer::setAddInlineSpan() {
    _kind = ShardMessageKind::ADD_INLINE_SPAN;
    auto& x = _data.emplace<11>();
    return x;
}
const SetTimeReq& ShardReqContainer::getSetTime() const {
    ALWAYS_ASSERT(_kind == ShardMessageKind::SET_TIME, "%s != %s", _kind, ShardMessageKind::SET_TIME);
    return std::get<12>(_data);
}
SetTimeReq& ShardReqContainer::setSetTime() {
    _kind = ShardMessageKind::SET_TIME;
    auto& x = _data.emplace<12>();
    return x;
}
const FullReadDirReq& ShardReqContainer::getFullReadDir() const {
    ALWAYS_ASSERT(_kind == ShardMessageKind::FULL_READ_DIR, "%s != %s", _kind, ShardMessageKind::FULL_READ_DIR);
    return std::get<13>(_data);
}
FullReadDirReq& ShardReqContainer::setFullReadDir() {
    _kind = ShardMessageKind::FULL_READ_DIR;
    auto& x = _data.emplace<13>();
    return x;
}
const MoveSpanReq& ShardReqContainer::getMoveSpan() const {
    ALWAYS_ASSERT(_kind == ShardMessageKind::MOVE_SPAN, "%s != %s", _kind, ShardMessageKind::MOVE_SPAN);
    return std::get<14>(_data);
}
MoveSpanReq& ShardReqContainer::setMoveSpan() {
    _kind = ShardMessageKind::MOVE_SPAN;
    auto& x = _data.emplace<14>();
    return x;
}
const RemoveNonOwnedEdgeReq& ShardReqContainer::getRemoveNonOwnedEdge() const {
    ALWAYS_ASSERT(_kind == ShardMessageKind::REMOVE_NON_OWNED_EDGE, "%s != %s", _kind, ShardMessageKind::REMOVE_NON_OWNED_EDGE);
    return std::get<15>(_data);
}
RemoveNonOwnedEdgeReq& ShardReqContainer::setRemoveNonOwnedEdge() {
    _kind = ShardMessageKind::REMOVE_NON_OWNED_EDGE;
    auto& x = _data.emplace<15>();
    return x;
}
const SameShardHardFileUnlinkReq& ShardReqContainer::getSameShardHardFileUnlink() const {
    ALWAYS_ASSERT(_kind == ShardMessageKind::SAME_SHARD_HARD_FILE_UNLINK, "%s != %s", _kind, ShardMessageKind::SAME_SHARD_HARD_FILE_UNLINK);
    return std::get<16>(_data);
}
SameShardHardFileUnlinkReq& ShardReqContainer::setSameShardHardFileUnlink() {
    _kind = ShardMessageKind::SAME_SHARD_HARD_FILE_UNLINK;
    auto& x = _data.emplace<16>();
    return x;
}
const StatTransientFileReq& ShardReqContainer::getStatTransientFile() const {
    ALWAYS_ASSERT(_kind == ShardMessageKind::STAT_TRANSIENT_FILE, "%s != %s", _kind, ShardMessageKind::STAT_TRANSIENT_FILE);
    return std::get<17>(_data);
}
StatTransientFileReq& ShardReqContainer::setStatTransientFile() {
    _kind = ShardMessageKind::STAT_TRANSIENT_FILE;
    auto& x = _data.emplace<17>();
    return x;
}
const SetDirectoryInfoReq& ShardReqContainer::getSetDirectoryInfo() const {
    ALWAYS_ASSERT(_kind == ShardMessageKind::SET_DIRECTORY_INFO, "%s != %s", _kind, ShardMessageKind::SET_DIRECTORY_INFO);
    return std::get<18>(_data);
}
SetDirectoryInfoReq& ShardReqContainer::setSetDirectoryInfo() {
    _kind = ShardMessageKind::SET_DIRECTORY_INFO;
    auto& x = _data.emplace<18>();
    return x;
}
const VisitDirectoriesReq& ShardReqContainer::getVisitDirectories() const {
    ALWAYS_ASSERT(_kind == ShardMessageKind::VISIT_DIRECTORIES, "%s != %s", _kind, ShardMessageKind::VISIT_DIRECTORIES);
    return std::get<19>(_data);
}
VisitDirectoriesReq& ShardReqContainer::setVisitDirectories() {
    _kind = ShardMessageKind::VISIT_DIRECTORIES;
    auto& x = _data.emplace<19>();
    return x;
}
const VisitFilesReq& ShardReqContainer::getVisitFiles() const {
    ALWAYS_ASSERT(_kind == ShardMessageKind::VISIT_FILES, "%s != %s", _kind, ShardMessageKind::VISIT_FILES);
    return std::get<20>(_data);
}
VisitFilesReq& ShardReqContainer::setVisitFiles() {
    _kind = ShardMessageKind::VISIT_FILES;
    auto& x = _data.emplace<20>();
    return x;
}
const VisitTransientFilesReq& ShardReqContainer::getVisitTransientFiles() const {
    ALWAYS_ASSERT(_kind == ShardMessageKind::VISIT_TRANSIENT_FILES, "%s != %s", _kind, ShardMessageKind::VISIT_TRANSIENT_FILES);
    return std::get<21>(_data);
}
VisitTransientFilesReq& ShardReqContainer::setVisitTransientFiles() {
    _kind = ShardMessageKind::VISIT_TRANSIENT_FILES;
    auto& x = _data.emplace<21>();
    return x;
}
const RemoveSpanInitiateReq& ShardReqContainer::getRemoveSpanInitiate() const {
    ALWAYS_ASSERT(_kind == ShardMessageKind::REMOVE_SPAN_INITIATE, "%s != %s", _kind, ShardMessageKind::REMOVE_SPAN_INITIATE);
    return std::get<22>(_data);
}
RemoveSpanInitiateReq& ShardReqContainer::setRemoveSpanInitiate() {
    _kind = ShardMessageKind::REMOVE_SPAN_INITIATE;
    auto& x = _data.emplace<22>();
    return x;
}
const RemoveSpanCertifyReq& ShardReqContainer::getRemoveSpanCertify() const {
    ALWAYS_ASSERT(_kind == ShardMessageKind::REMOVE_SPAN_CERTIFY, "%s != %s", _kind, ShardMessageKind::REMOVE_SPAN_CERTIFY);
    return std::get<23>(_data);
}
RemoveSpanCertifyReq& ShardReqContainer::setRemoveSpanCertify() {
    _kind = ShardMessageKind::REMOVE_SPAN_CERTIFY;
    auto& x = _data.emplace<23>();
    return x;
}
const SwapBlocksReq& ShardReqContainer::getSwapBlocks() const {
    ALWAYS_ASSERT(_kind == ShardMessageKind::SWAP_BLOCKS, "%s != %s", _kind, ShardMessageKind::SWAP_BLOCKS);
    return std::get<24>(_data);
}
SwapBlocksReq& ShardReqContainer::setSwapBlocks() {
    _kind = ShardMessageKind::SWAP_BLOCKS;
    auto& x = _data.emplace<24>();
    return x;
}
const BlockServiceFilesReq& ShardReqContainer::getBlockServiceFiles() const {
    ALWAYS_ASSERT(_kind == ShardMessageKind::BLOCK_SERVICE_FILES, "%s != %s", _kind, ShardMessageKind::BLOCK_SERVICE_FILES);
    return std::get<25>(_data);
}
BlockServiceFilesReq& ShardReqContainer::setBlockServiceFiles() {
    _kind = ShardMessageKind::BLOCK_SERVICE_FILES;
    auto& x = _data.emplace<25>();
    return x;
}
const RemoveInodeReq& ShardReqContainer::getRemoveInode() const {
    ALWAYS_ASSERT(_kind == ShardMessageKind::REMOVE_INODE, "%s != %s", _kind, ShardMessageKind::REMOVE_INODE);
    return std::get<26>(_data);
}
RemoveInodeReq& ShardReqContainer::setRemoveInode() {
    _kind = ShardMessageKind::REMOVE_INODE;
    auto& x = _data.emplace<26>();
    return x;
}
const AddSpanInitiateWithReferenceReq& ShardReqContainer::getAddSpanInitiateWithReference() const {
    ALWAYS_ASSERT(_kind == ShardMessageKind::ADD_SPAN_INITIATE_WITH_REFERENCE, "%s != %s", _kind, ShardMessageKind::ADD_SPAN_INITIATE_WITH_REFERENCE);
    return std::get<27>(_data);
}
AddSpanInitiateWithReferenceReq& ShardReqContainer::setAddSpanInitiateWithReference() {
    _kind = ShardMessageKind::ADD_SPAN_INITIATE_WITH_REFERENCE;
    auto& x = _data.emplace<27>();
    return x;
}
const RemoveZeroBlockServiceFilesReq& ShardReqContainer::getRemoveZeroBlockServiceFiles() const {
    ALWAYS_ASSERT(_kind == ShardMessageKind::REMOVE_ZERO_BLOCK_SERVICE_FILES, "%s != %s", _kind, ShardMessageKind::REMOVE_ZERO_BLOCK_SERVICE_FILES);
    return std::get<28>(_data);
}
RemoveZeroBlockServiceFilesReq& ShardReqContainer::setRemoveZeroBlockServiceFiles() {
    _kind = ShardMessageKind::REMOVE_ZERO_BLOCK_SERVICE_FILES;
    auto& x = _data.emplace<28>();
    return x;
}
const CreateDirectoryInodeReq& ShardReqContainer::getCreateDirectoryInode() const {
    ALWAYS_ASSERT(_kind == ShardMessageKind::CREATE_DIRECTORY_INODE, "%s != %s", _kind, ShardMessageKind::CREATE_DIRECTORY_INODE);
    return std::get<29>(_data);
}
CreateDirectoryInodeReq& ShardReqContainer::setCreateDirectoryInode() {
    _kind = ShardMessageKind::CREATE_DIRECTORY_INODE;
    auto& x = _data.emplace<29>();
    return x;
}
const SetDirectoryOwnerReq& ShardReqContainer::getSetDirectoryOwner() const {
    ALWAYS_ASSERT(_kind == ShardMessageKind::SET_DIRECTORY_OWNER, "%s != %s", _kind, ShardMessageKind::SET_DIRECTORY_OWNER);
    return std::get<30>(_data);
}
SetDirectoryOwnerReq& ShardReqContainer::setSetDirectoryOwner() {
    _kind = ShardMessageKind::SET_DIRECTORY_OWNER;
    auto& x = _data.emplace<30>();
    return x;
}
const RemoveDirectoryOwnerReq& ShardReqContainer::getRemoveDirectoryOwner() const {
    ALWAYS_ASSERT(_kind == ShardMessageKind::REMOVE_DIRECTORY_OWNER, "%s != %s", _kind, ShardMessageKind::REMOVE_DIRECTORY_OWNER);
    return std::get<31>(_data);
}
RemoveDirectoryOwnerReq& ShardReqContainer::setRemoveDirectoryOwner() {
    _kind = ShardMessageKind::REMOVE_DIRECTORY_OWNER;
    auto& x = _data.emplace<31>();
    return x;
}
const CreateLockedCurrentEdgeReq& ShardReqContainer::getCreateLockedCurrentEdge() const {
    ALWAYS_ASSERT(_kind == ShardMessageKind::CREATE_LOCKED_CURRENT_EDGE, "%s != %s", _kind, ShardMessageKind::CREATE_LOCKED_CURRENT_EDGE);
    return std::get<32>(_data);
}
CreateLockedCurrentEdgeReq& ShardReqContainer::setCreateLockedCurrentEdge() {
    _kind = ShardMessageKind::CREATE_LOCKED_CURRENT_EDGE;
    auto& x = _data.emplace<32>();
    return x;
}
const LockCurrentEdgeReq& ShardReqContainer::getLockCurrentEdge() const {
    ALWAYS_ASSERT(_kind == ShardMessageKind::LOCK_CURRENT_EDGE, "%s != %s", _kind, ShardMessageKind::LOCK_CURRENT_EDGE);
    return std::get<33>(_data);
}
LockCurrentEdgeReq& ShardReqContainer::setLockCurrentEdge() {
    _kind = ShardMessageKind::LOCK_CURRENT_EDGE;
    auto& x = _data.emplace<33>();
    return x;
}
const UnlockCurrentEdgeReq& ShardReqContainer::getUnlockCurrentEdge() const {
    ALWAYS_ASSERT(_kind == ShardMessageKind::UNLOCK_CURRENT_EDGE, "%s != %s", _kind, ShardMessageKind::UNLOCK_CURRENT_EDGE);
    return std::get<34>(_data);
}
UnlockCurrentEdgeReq& ShardReqContainer::setUnlockCurrentEdge() {
    _kind = ShardMessageKind::UNLOCK_CURRENT_EDGE;
    auto& x = _data.emplace<34>();
    return x;
}
const RemoveOwnedSnapshotFileEdgeReq& ShardReqContainer::getRemoveOwnedSnapshotFileEdge() const {
    ALWAYS_ASSERT(_kind == ShardMessageKind::REMOVE_OWNED_SNAPSHOT_FILE_EDGE, "%s != %s", _kind, ShardMessageKind::REMOVE_OWNED_SNAPSHOT_FILE_EDGE);
    return std::get<35>(_data);
}
RemoveOwnedSnapshotFileEdgeReq& ShardReqContainer::setRemoveOwnedSnapshotFileEdge() {
    _kind = ShardMessageKind::REMOVE_OWNED_SNAPSHOT_FILE_EDGE;
    auto& x = _data.emplace<35>();
    return x;
}
const MakeFileTransientReq& ShardReqContainer::getMakeFileTransient() const {
    ALWAYS_ASSERT(_kind == ShardMessageKind::MAKE_FILE_TRANSIENT, "%s != %s", _kind, ShardMessageKind::MAKE_FILE_TRANSIENT);
    return std::get<36>(_data);
}
MakeFileTransientReq& ShardReqContainer::setMakeFileTransient() {
    _kind = ShardMessageKind::MAKE_FILE_TRANSIENT;
    auto& x = _data.emplace<36>();
    return x;
}
ShardReqContainer::ShardReqContainer() {
    clear();
}

ShardReqContainer::ShardReqContainer(const ShardReqContainer& other) {
    *this = other;
}

ShardReqContainer::ShardReqContainer(ShardReqContainer&& other) {
    _data = std::move(other._data);
    _kind = other._kind;
    other._kind = (ShardMessageKind)0;
}

void ShardReqContainer::operator=(const ShardReqContainer& other) {
    if (other.kind() == (ShardMessageKind)0) { clear(); return; }
    switch (other.kind()) {
    case ShardMessageKind::LOOKUP:
        setLookup() = other.getLookup();
        break;
    case ShardMessageKind::STAT_FILE:
        setStatFile() = other.getStatFile();
        break;
    case ShardMessageKind::STAT_DIRECTORY:
        setStatDirectory() = other.getStatDirectory();
        break;
    case ShardMessageKind::READ_DIR:
        setReadDir() = other.getReadDir();
        break;
    case ShardMessageKind::CONSTRUCT_FILE:
        setConstructFile() = other.getConstructFile();
        break;
    case ShardMessageKind::ADD_SPAN_INITIATE:
        setAddSpanInitiate() = other.getAddSpanInitiate();
        break;
    case ShardMessageKind::ADD_SPAN_CERTIFY:
        setAddSpanCertify() = other.getAddSpanCertify();
        break;
    case ShardMessageKind::LINK_FILE:
        setLinkFile() = other.getLinkFile();
        break;
    case ShardMessageKind::SOFT_UNLINK_FILE:
        setSoftUnlinkFile() = other.getSoftUnlinkFile();
        break;
    case ShardMessageKind::FILE_SPANS:
        setFileSpans() = other.getFileSpans();
        break;
    case ShardMessageKind::SAME_DIRECTORY_RENAME:
        setSameDirectoryRename() = other.getSameDirectoryRename();
        break;
    case ShardMessageKind::ADD_INLINE_SPAN:
        setAddInlineSpan() = other.getAddInlineSpan();
        break;
    case ShardMessageKind::SET_TIME:
        setSetTime() = other.getSetTime();
        break;
    case ShardMessageKind::FULL_READ_DIR:
        setFullReadDir() = other.getFullReadDir();
        break;
    case ShardMessageKind::MOVE_SPAN:
        setMoveSpan() = other.getMoveSpan();
        break;
    case ShardMessageKind::REMOVE_NON_OWNED_EDGE:
        setRemoveNonOwnedEdge() = other.getRemoveNonOwnedEdge();
        break;
    case ShardMessageKind::SAME_SHARD_HARD_FILE_UNLINK:
        setSameShardHardFileUnlink() = other.getSameShardHardFileUnlink();
        break;
    case ShardMessageKind::STAT_TRANSIENT_FILE:
        setStatTransientFile() = other.getStatTransientFile();
        break;
    case ShardMessageKind::SET_DIRECTORY_INFO:
        setSetDirectoryInfo() = other.getSetDirectoryInfo();
        break;
    case ShardMessageKind::VISIT_DIRECTORIES:
        setVisitDirectories() = other.getVisitDirectories();
        break;
    case ShardMessageKind::VISIT_FILES:
        setVisitFiles() = other.getVisitFiles();
        break;
    case ShardMessageKind::VISIT_TRANSIENT_FILES:
        setVisitTransientFiles() = other.getVisitTransientFiles();
        break;
    case ShardMessageKind::REMOVE_SPAN_INITIATE:
        setRemoveSpanInitiate() = other.getRemoveSpanInitiate();
        break;
    case ShardMessageKind::REMOVE_SPAN_CERTIFY:
        setRemoveSpanCertify() = other.getRemoveSpanCertify();
        break;
    case ShardMessageKind::SWAP_BLOCKS:
        setSwapBlocks() = other.getSwapBlocks();
        break;
    case ShardMessageKind::BLOCK_SERVICE_FILES:
        setBlockServiceFiles() = other.getBlockServiceFiles();
        break;
    case ShardMessageKind::REMOVE_INODE:
        setRemoveInode() = other.getRemoveInode();
        break;
    case ShardMessageKind::ADD_SPAN_INITIATE_WITH_REFERENCE:
        setAddSpanInitiateWithReference() = other.getAddSpanInitiateWithReference();
        break;
    case ShardMessageKind::REMOVE_ZERO_BLOCK_SERVICE_FILES:
        setRemoveZeroBlockServiceFiles() = other.getRemoveZeroBlockServiceFiles();
        break;
    case ShardMessageKind::CREATE_DIRECTORY_INODE:
        setCreateDirectoryInode() = other.getCreateDirectoryInode();
        break;
    case ShardMessageKind::SET_DIRECTORY_OWNER:
        setSetDirectoryOwner() = other.getSetDirectoryOwner();
        break;
    case ShardMessageKind::REMOVE_DIRECTORY_OWNER:
        setRemoveDirectoryOwner() = other.getRemoveDirectoryOwner();
        break;
    case ShardMessageKind::CREATE_LOCKED_CURRENT_EDGE:
        setCreateLockedCurrentEdge() = other.getCreateLockedCurrentEdge();
        break;
    case ShardMessageKind::LOCK_CURRENT_EDGE:
        setLockCurrentEdge() = other.getLockCurrentEdge();
        break;
    case ShardMessageKind::UNLOCK_CURRENT_EDGE:
        setUnlockCurrentEdge() = other.getUnlockCurrentEdge();
        break;
    case ShardMessageKind::REMOVE_OWNED_SNAPSHOT_FILE_EDGE:
        setRemoveOwnedSnapshotFileEdge() = other.getRemoveOwnedSnapshotFileEdge();
        break;
    case ShardMessageKind::MAKE_FILE_TRANSIENT:
        setMakeFileTransient() = other.getMakeFileTransient();
        break;
    default:
        throw EGGS_EXCEPTION("bad ShardMessageKind kind %s", other.kind());
    }
}

void ShardReqContainer::operator=(ShardReqContainer&& other) {
    _data = std::move(other._data);
    _kind = other._kind;
    other._kind = (ShardMessageKind)0;
}

size_t ShardReqContainer::packedSize() const {
    switch (_kind) {
    case ShardMessageKind::LOOKUP:
        return std::get<0>(_data).packedSize();
    case ShardMessageKind::STAT_FILE:
        return std::get<1>(_data).packedSize();
    case ShardMessageKind::STAT_DIRECTORY:
        return std::get<2>(_data).packedSize();
    case ShardMessageKind::READ_DIR:
        return std::get<3>(_data).packedSize();
    case ShardMessageKind::CONSTRUCT_FILE:
        return std::get<4>(_data).packedSize();
    case ShardMessageKind::ADD_SPAN_INITIATE:
        return std::get<5>(_data).packedSize();
    case ShardMessageKind::ADD_SPAN_CERTIFY:
        return std::get<6>(_data).packedSize();
    case ShardMessageKind::LINK_FILE:
        return std::get<7>(_data).packedSize();
    case ShardMessageKind::SOFT_UNLINK_FILE:
        return std::get<8>(_data).packedSize();
    case ShardMessageKind::FILE_SPANS:
        return std::get<9>(_data).packedSize();
    case ShardMessageKind::SAME_DIRECTORY_RENAME:
        return std::get<10>(_data).packedSize();
    case ShardMessageKind::ADD_INLINE_SPAN:
        return std::get<11>(_data).packedSize();
    case ShardMessageKind::SET_TIME:
        return std::get<12>(_data).packedSize();
    case ShardMessageKind::FULL_READ_DIR:
        return std::get<13>(_data).packedSize();
    case ShardMessageKind::MOVE_SPAN:
        return std::get<14>(_data).packedSize();
    case ShardMessageKind::REMOVE_NON_OWNED_EDGE:
        return std::get<15>(_data).packedSize();
    case ShardMessageKind::SAME_SHARD_HARD_FILE_UNLINK:
        return std::get<16>(_data).packedSize();
    case ShardMessageKind::STAT_TRANSIENT_FILE:
        return std::get<17>(_data).packedSize();
    case ShardMessageKind::SET_DIRECTORY_INFO:
        return std::get<18>(_data).packedSize();
    case ShardMessageKind::VISIT_DIRECTORIES:
        return std::get<19>(_data).packedSize();
    case ShardMessageKind::VISIT_FILES:
        return std::get<20>(_data).packedSize();
    case ShardMessageKind::VISIT_TRANSIENT_FILES:
        return std::get<21>(_data).packedSize();
    case ShardMessageKind::REMOVE_SPAN_INITIATE:
        return std::get<22>(_data).packedSize();
    case ShardMessageKind::REMOVE_SPAN_CERTIFY:
        return std::get<23>(_data).packedSize();
    case ShardMessageKind::SWAP_BLOCKS:
        return std::get<24>(_data).packedSize();
    case ShardMessageKind::BLOCK_SERVICE_FILES:
        return std::get<25>(_data).packedSize();
    case ShardMessageKind::REMOVE_INODE:
        return std::get<26>(_data).packedSize();
    case ShardMessageKind::ADD_SPAN_INITIATE_WITH_REFERENCE:
        return std::get<27>(_data).packedSize();
    case ShardMessageKind::REMOVE_ZERO_BLOCK_SERVICE_FILES:
        return std::get<28>(_data).packedSize();
    case ShardMessageKind::CREATE_DIRECTORY_INODE:
        return std::get<29>(_data).packedSize();
    case ShardMessageKind::SET_DIRECTORY_OWNER:
        return std::get<30>(_data).packedSize();
    case ShardMessageKind::REMOVE_DIRECTORY_OWNER:
        return std::get<31>(_data).packedSize();
    case ShardMessageKind::CREATE_LOCKED_CURRENT_EDGE:
        return std::get<32>(_data).packedSize();
    case ShardMessageKind::LOCK_CURRENT_EDGE:
        return std::get<33>(_data).packedSize();
    case ShardMessageKind::UNLOCK_CURRENT_EDGE:
        return std::get<34>(_data).packedSize();
    case ShardMessageKind::REMOVE_OWNED_SNAPSHOT_FILE_EDGE:
        return std::get<35>(_data).packedSize();
    case ShardMessageKind::MAKE_FILE_TRANSIENT:
        return std::get<36>(_data).packedSize();
    default:
        throw EGGS_EXCEPTION("bad ShardMessageKind kind %s", _kind);
    }
}

void ShardReqContainer::pack(BincodeBuf& buf) const {
    switch (_kind) {
    case ShardMessageKind::LOOKUP:
        std::get<0>(_data).pack(buf);
        break;
    case ShardMessageKind::STAT_FILE:
        std::get<1>(_data).pack(buf);
        break;
    case ShardMessageKind::STAT_DIRECTORY:
        std::get<2>(_data).pack(buf);
        break;
    case ShardMessageKind::READ_DIR:
        std::get<3>(_data).pack(buf);
        break;
    case ShardMessageKind::CONSTRUCT_FILE:
        std::get<4>(_data).pack(buf);
        break;
    case ShardMessageKind::ADD_SPAN_INITIATE:
        std::get<5>(_data).pack(buf);
        break;
    case ShardMessageKind::ADD_SPAN_CERTIFY:
        std::get<6>(_data).pack(buf);
        break;
    case ShardMessageKind::LINK_FILE:
        std::get<7>(_data).pack(buf);
        break;
    case ShardMessageKind::SOFT_UNLINK_FILE:
        std::get<8>(_data).pack(buf);
        break;
    case ShardMessageKind::FILE_SPANS:
        std::get<9>(_data).pack(buf);
        break;
    case ShardMessageKind::SAME_DIRECTORY_RENAME:
        std::get<10>(_data).pack(buf);
        break;
    case ShardMessageKind::ADD_INLINE_SPAN:
        std::get<11>(_data).pack(buf);
        break;
    case ShardMessageKind::SET_TIME:
        std::get<12>(_data).pack(buf);
        break;
    case ShardMessageKind::FULL_READ_DIR:
        std::get<13>(_data).pack(buf);
        break;
    case ShardMessageKind::MOVE_SPAN:
        std::get<14>(_data).pack(buf);
        break;
    case ShardMessageKind::REMOVE_NON_OWNED_EDGE:
        std::get<15>(_data).pack(buf);
        break;
    case ShardMessageKind::SAME_SHARD_HARD_FILE_UNLINK:
        std::get<16>(_data).pack(buf);
        break;
    case ShardMessageKind::STAT_TRANSIENT_FILE:
        std::get<17>(_data).pack(buf);
        break;
    case ShardMessageKind::SET_DIRECTORY_INFO:
        std::get<18>(_data).pack(buf);
        break;
    case ShardMessageKind::VISIT_DIRECTORIES:
        std::get<19>(_data).pack(buf);
        break;
    case ShardMessageKind::VISIT_FILES:
        std::get<20>(_data).pack(buf);
        break;
    case ShardMessageKind::VISIT_TRANSIENT_FILES:
        std::get<21>(_data).pack(buf);
        break;
    case ShardMessageKind::REMOVE_SPAN_INITIATE:
        std::get<22>(_data).pack(buf);
        break;
    case ShardMessageKind::REMOVE_SPAN_CERTIFY:
        std::get<23>(_data).pack(buf);
        break;
    case ShardMessageKind::SWAP_BLOCKS:
        std::get<24>(_data).pack(buf);
        break;
    case ShardMessageKind::BLOCK_SERVICE_FILES:
        std::get<25>(_data).pack(buf);
        break;
    case ShardMessageKind::REMOVE_INODE:
        std::get<26>(_data).pack(buf);
        break;
    case ShardMessageKind::ADD_SPAN_INITIATE_WITH_REFERENCE:
        std::get<27>(_data).pack(buf);
        break;
    case ShardMessageKind::REMOVE_ZERO_BLOCK_SERVICE_FILES:
        std::get<28>(_data).pack(buf);
        break;
    case ShardMessageKind::CREATE_DIRECTORY_INODE:
        std::get<29>(_data).pack(buf);
        break;
    case ShardMessageKind::SET_DIRECTORY_OWNER:
        std::get<30>(_data).pack(buf);
        break;
    case ShardMessageKind::REMOVE_DIRECTORY_OWNER:
        std::get<31>(_data).pack(buf);
        break;
    case ShardMessageKind::CREATE_LOCKED_CURRENT_EDGE:
        std::get<32>(_data).pack(buf);
        break;
    case ShardMessageKind::LOCK_CURRENT_EDGE:
        std::get<33>(_data).pack(buf);
        break;
    case ShardMessageKind::UNLOCK_CURRENT_EDGE:
        std::get<34>(_data).pack(buf);
        break;
    case ShardMessageKind::REMOVE_OWNED_SNAPSHOT_FILE_EDGE:
        std::get<35>(_data).pack(buf);
        break;
    case ShardMessageKind::MAKE_FILE_TRANSIENT:
        std::get<36>(_data).pack(buf);
        break;
    default:
        throw EGGS_EXCEPTION("bad ShardMessageKind kind %s", _kind);
    }
}

void ShardReqContainer::unpack(BincodeBuf& buf, ShardMessageKind kind) {
    _kind = kind;
    switch (kind) {
    case ShardMessageKind::LOOKUP:
        _data.emplace<0>().unpack(buf);
        break;
    case ShardMessageKind::STAT_FILE:
        _data.emplace<1>().unpack(buf);
        break;
    case ShardMessageKind::STAT_DIRECTORY:
        _data.emplace<2>().unpack(buf);
        break;
    case ShardMessageKind::READ_DIR:
        _data.emplace<3>().unpack(buf);
        break;
    case ShardMessageKind::CONSTRUCT_FILE:
        _data.emplace<4>().unpack(buf);
        break;
    case ShardMessageKind::ADD_SPAN_INITIATE:
        _data.emplace<5>().unpack(buf);
        break;
    case ShardMessageKind::ADD_SPAN_CERTIFY:
        _data.emplace<6>().unpack(buf);
        break;
    case ShardMessageKind::LINK_FILE:
        _data.emplace<7>().unpack(buf);
        break;
    case ShardMessageKind::SOFT_UNLINK_FILE:
        _data.emplace<8>().unpack(buf);
        break;
    case ShardMessageKind::FILE_SPANS:
        _data.emplace<9>().unpack(buf);
        break;
    case ShardMessageKind::SAME_DIRECTORY_RENAME:
        _data.emplace<10>().unpack(buf);
        break;
    case ShardMessageKind::ADD_INLINE_SPAN:
        _data.emplace<11>().unpack(buf);
        break;
    case ShardMessageKind::SET_TIME:
        _data.emplace<12>().unpack(buf);
        break;
    case ShardMessageKind::FULL_READ_DIR:
        _data.emplace<13>().unpack(buf);
        break;
    case ShardMessageKind::MOVE_SPAN:
        _data.emplace<14>().unpack(buf);
        break;
    case ShardMessageKind::REMOVE_NON_OWNED_EDGE:
        _data.emplace<15>().unpack(buf);
        break;
    case ShardMessageKind::SAME_SHARD_HARD_FILE_UNLINK:
        _data.emplace<16>().unpack(buf);
        break;
    case ShardMessageKind::STAT_TRANSIENT_FILE:
        _data.emplace<17>().unpack(buf);
        break;
    case ShardMessageKind::SET_DIRECTORY_INFO:
        _data.emplace<18>().unpack(buf);
        break;
    case ShardMessageKind::VISIT_DIRECTORIES:
        _data.emplace<19>().unpack(buf);
        break;
    case ShardMessageKind::VISIT_FILES:
        _data.emplace<20>().unpack(buf);
        break;
    case ShardMessageKind::VISIT_TRANSIENT_FILES:
        _data.emplace<21>().unpack(buf);
        break;
    case ShardMessageKind::REMOVE_SPAN_INITIATE:
        _data.emplace<22>().unpack(buf);
        break;
    case ShardMessageKind::REMOVE_SPAN_CERTIFY:
        _data.emplace<23>().unpack(buf);
        break;
    case ShardMessageKind::SWAP_BLOCKS:
        _data.emplace<24>().unpack(buf);
        break;
    case ShardMessageKind::BLOCK_SERVICE_FILES:
        _data.emplace<25>().unpack(buf);
        break;
    case ShardMessageKind::REMOVE_INODE:
        _data.emplace<26>().unpack(buf);
        break;
    case ShardMessageKind::ADD_SPAN_INITIATE_WITH_REFERENCE:
        _data.emplace<27>().unpack(buf);
        break;
    case ShardMessageKind::REMOVE_ZERO_BLOCK_SERVICE_FILES:
        _data.emplace<28>().unpack(buf);
        break;
    case ShardMessageKind::CREATE_DIRECTORY_INODE:
        _data.emplace<29>().unpack(buf);
        break;
    case ShardMessageKind::SET_DIRECTORY_OWNER:
        _data.emplace<30>().unpack(buf);
        break;
    case ShardMessageKind::REMOVE_DIRECTORY_OWNER:
        _data.emplace<31>().unpack(buf);
        break;
    case ShardMessageKind::CREATE_LOCKED_CURRENT_EDGE:
        _data.emplace<32>().unpack(buf);
        break;
    case ShardMessageKind::LOCK_CURRENT_EDGE:
        _data.emplace<33>().unpack(buf);
        break;
    case ShardMessageKind::UNLOCK_CURRENT_EDGE:
        _data.emplace<34>().unpack(buf);
        break;
    case ShardMessageKind::REMOVE_OWNED_SNAPSHOT_FILE_EDGE:
        _data.emplace<35>().unpack(buf);
        break;
    case ShardMessageKind::MAKE_FILE_TRANSIENT:
        _data.emplace<36>().unpack(buf);
        break;
    default:
        throw BINCODE_EXCEPTION("bad ShardMessageKind kind %s", kind);
    }
}

bool ShardReqContainer::operator==(const ShardReqContainer& other) const {
    if (_kind != other.kind()) { return false; }
    if (_kind == (ShardMessageKind)0) { return true; }
    switch (_kind) {
    case ShardMessageKind::LOOKUP:
        return getLookup() == other.getLookup();
    case ShardMessageKind::STAT_FILE:
        return getStatFile() == other.getStatFile();
    case ShardMessageKind::STAT_DIRECTORY:
        return getStatDirectory() == other.getStatDirectory();
    case ShardMessageKind::READ_DIR:
        return getReadDir() == other.getReadDir();
    case ShardMessageKind::CONSTRUCT_FILE:
        return getConstructFile() == other.getConstructFile();
    case ShardMessageKind::ADD_SPAN_INITIATE:
        return getAddSpanInitiate() == other.getAddSpanInitiate();
    case ShardMessageKind::ADD_SPAN_CERTIFY:
        return getAddSpanCertify() == other.getAddSpanCertify();
    case ShardMessageKind::LINK_FILE:
        return getLinkFile() == other.getLinkFile();
    case ShardMessageKind::SOFT_UNLINK_FILE:
        return getSoftUnlinkFile() == other.getSoftUnlinkFile();
    case ShardMessageKind::FILE_SPANS:
        return getFileSpans() == other.getFileSpans();
    case ShardMessageKind::SAME_DIRECTORY_RENAME:
        return getSameDirectoryRename() == other.getSameDirectoryRename();
    case ShardMessageKind::ADD_INLINE_SPAN:
        return getAddInlineSpan() == other.getAddInlineSpan();
    case ShardMessageKind::SET_TIME:
        return getSetTime() == other.getSetTime();
    case ShardMessageKind::FULL_READ_DIR:
        return getFullReadDir() == other.getFullReadDir();
    case ShardMessageKind::MOVE_SPAN:
        return getMoveSpan() == other.getMoveSpan();
    case ShardMessageKind::REMOVE_NON_OWNED_EDGE:
        return getRemoveNonOwnedEdge() == other.getRemoveNonOwnedEdge();
    case ShardMessageKind::SAME_SHARD_HARD_FILE_UNLINK:
        return getSameShardHardFileUnlink() == other.getSameShardHardFileUnlink();
    case ShardMessageKind::STAT_TRANSIENT_FILE:
        return getStatTransientFile() == other.getStatTransientFile();
    case ShardMessageKind::SET_DIRECTORY_INFO:
        return getSetDirectoryInfo() == other.getSetDirectoryInfo();
    case ShardMessageKind::VISIT_DIRECTORIES:
        return getVisitDirectories() == other.getVisitDirectories();
    case ShardMessageKind::VISIT_FILES:
        return getVisitFiles() == other.getVisitFiles();
    case ShardMessageKind::VISIT_TRANSIENT_FILES:
        return getVisitTransientFiles() == other.getVisitTransientFiles();
    case ShardMessageKind::REMOVE_SPAN_INITIATE:
        return getRemoveSpanInitiate() == other.getRemoveSpanInitiate();
    case ShardMessageKind::REMOVE_SPAN_CERTIFY:
        return getRemoveSpanCertify() == other.getRemoveSpanCertify();
    case ShardMessageKind::SWAP_BLOCKS:
        return getSwapBlocks() == other.getSwapBlocks();
    case ShardMessageKind::BLOCK_SERVICE_FILES:
        return getBlockServiceFiles() == other.getBlockServiceFiles();
    case ShardMessageKind::REMOVE_INODE:
        return getRemoveInode() == other.getRemoveInode();
    case ShardMessageKind::ADD_SPAN_INITIATE_WITH_REFERENCE:
        return getAddSpanInitiateWithReference() == other.getAddSpanInitiateWithReference();
    case ShardMessageKind::REMOVE_ZERO_BLOCK_SERVICE_FILES:
        return getRemoveZeroBlockServiceFiles() == other.getRemoveZeroBlockServiceFiles();
    case ShardMessageKind::CREATE_DIRECTORY_INODE:
        return getCreateDirectoryInode() == other.getCreateDirectoryInode();
    case ShardMessageKind::SET_DIRECTORY_OWNER:
        return getSetDirectoryOwner() == other.getSetDirectoryOwner();
    case ShardMessageKind::REMOVE_DIRECTORY_OWNER:
        return getRemoveDirectoryOwner() == other.getRemoveDirectoryOwner();
    case ShardMessageKind::CREATE_LOCKED_CURRENT_EDGE:
        return getCreateLockedCurrentEdge() == other.getCreateLockedCurrentEdge();
    case ShardMessageKind::LOCK_CURRENT_EDGE:
        return getLockCurrentEdge() == other.getLockCurrentEdge();
    case ShardMessageKind::UNLOCK_CURRENT_EDGE:
        return getUnlockCurrentEdge() == other.getUnlockCurrentEdge();
    case ShardMessageKind::REMOVE_OWNED_SNAPSHOT_FILE_EDGE:
        return getRemoveOwnedSnapshotFileEdge() == other.getRemoveOwnedSnapshotFileEdge();
    case ShardMessageKind::MAKE_FILE_TRANSIENT:
        return getMakeFileTransient() == other.getMakeFileTransient();
    default:
        throw BINCODE_EXCEPTION("bad ShardMessageKind kind %s", _kind);
    }
}

std::ostream& operator<<(std::ostream& out, const ShardReqContainer& x) {
    switch (x.kind()) {
    case ShardMessageKind::LOOKUP:
        out << x.getLookup();
        break;
    case ShardMessageKind::STAT_FILE:
        out << x.getStatFile();
        break;
    case ShardMessageKind::STAT_DIRECTORY:
        out << x.getStatDirectory();
        break;
    case ShardMessageKind::READ_DIR:
        out << x.getReadDir();
        break;
    case ShardMessageKind::CONSTRUCT_FILE:
        out << x.getConstructFile();
        break;
    case ShardMessageKind::ADD_SPAN_INITIATE:
        out << x.getAddSpanInitiate();
        break;
    case ShardMessageKind::ADD_SPAN_CERTIFY:
        out << x.getAddSpanCertify();
        break;
    case ShardMessageKind::LINK_FILE:
        out << x.getLinkFile();
        break;
    case ShardMessageKind::SOFT_UNLINK_FILE:
        out << x.getSoftUnlinkFile();
        break;
    case ShardMessageKind::FILE_SPANS:
        out << x.getFileSpans();
        break;
    case ShardMessageKind::SAME_DIRECTORY_RENAME:
        out << x.getSameDirectoryRename();
        break;
    case ShardMessageKind::ADD_INLINE_SPAN:
        out << x.getAddInlineSpan();
        break;
    case ShardMessageKind::SET_TIME:
        out << x.getSetTime();
        break;
    case ShardMessageKind::FULL_READ_DIR:
        out << x.getFullReadDir();
        break;
    case ShardMessageKind::MOVE_SPAN:
        out << x.getMoveSpan();
        break;
    case ShardMessageKind::REMOVE_NON_OWNED_EDGE:
        out << x.getRemoveNonOwnedEdge();
        break;
    case ShardMessageKind::SAME_SHARD_HARD_FILE_UNLINK:
        out << x.getSameShardHardFileUnlink();
        break;
    case ShardMessageKind::STAT_TRANSIENT_FILE:
        out << x.getStatTransientFile();
        break;
    case ShardMessageKind::SET_DIRECTORY_INFO:
        out << x.getSetDirectoryInfo();
        break;
    case ShardMessageKind::VISIT_DIRECTORIES:
        out << x.getVisitDirectories();
        break;
    case ShardMessageKind::VISIT_FILES:
        out << x.getVisitFiles();
        break;
    case ShardMessageKind::VISIT_TRANSIENT_FILES:
        out << x.getVisitTransientFiles();
        break;
    case ShardMessageKind::REMOVE_SPAN_INITIATE:
        out << x.getRemoveSpanInitiate();
        break;
    case ShardMessageKind::REMOVE_SPAN_CERTIFY:
        out << x.getRemoveSpanCertify();
        break;
    case ShardMessageKind::SWAP_BLOCKS:
        out << x.getSwapBlocks();
        break;
    case ShardMessageKind::BLOCK_SERVICE_FILES:
        out << x.getBlockServiceFiles();
        break;
    case ShardMessageKind::REMOVE_INODE:
        out << x.getRemoveInode();
        break;
    case ShardMessageKind::ADD_SPAN_INITIATE_WITH_REFERENCE:
        out << x.getAddSpanInitiateWithReference();
        break;
    case ShardMessageKind::REMOVE_ZERO_BLOCK_SERVICE_FILES:
        out << x.getRemoveZeroBlockServiceFiles();
        break;
    case ShardMessageKind::CREATE_DIRECTORY_INODE:
        out << x.getCreateDirectoryInode();
        break;
    case ShardMessageKind::SET_DIRECTORY_OWNER:
        out << x.getSetDirectoryOwner();
        break;
    case ShardMessageKind::REMOVE_DIRECTORY_OWNER:
        out << x.getRemoveDirectoryOwner();
        break;
    case ShardMessageKind::CREATE_LOCKED_CURRENT_EDGE:
        out << x.getCreateLockedCurrentEdge();
        break;
    case ShardMessageKind::LOCK_CURRENT_EDGE:
        out << x.getLockCurrentEdge();
        break;
    case ShardMessageKind::UNLOCK_CURRENT_EDGE:
        out << x.getUnlockCurrentEdge();
        break;
    case ShardMessageKind::REMOVE_OWNED_SNAPSHOT_FILE_EDGE:
        out << x.getRemoveOwnedSnapshotFileEdge();
        break;
    case ShardMessageKind::MAKE_FILE_TRANSIENT:
        out << x.getMakeFileTransient();
        break;
    default:
        throw EGGS_EXCEPTION("bad ShardMessageKind kind %s", x.kind());
    }
    return out;
}

const LookupResp& ShardRespContainer::getLookup() const {
    ALWAYS_ASSERT(_kind == ShardMessageKind::LOOKUP, "%s != %s", _kind, ShardMessageKind::LOOKUP);
    return std::get<0>(_data);
}
LookupResp& ShardRespContainer::setLookup() {
    _kind = ShardMessageKind::LOOKUP;
    auto& x = _data.emplace<0>();
    return x;
}
const StatFileResp& ShardRespContainer::getStatFile() const {
    ALWAYS_ASSERT(_kind == ShardMessageKind::STAT_FILE, "%s != %s", _kind, ShardMessageKind::STAT_FILE);
    return std::get<1>(_data);
}
StatFileResp& ShardRespContainer::setStatFile() {
    _kind = ShardMessageKind::STAT_FILE;
    auto& x = _data.emplace<1>();
    return x;
}
const StatDirectoryResp& ShardRespContainer::getStatDirectory() const {
    ALWAYS_ASSERT(_kind == ShardMessageKind::STAT_DIRECTORY, "%s != %s", _kind, ShardMessageKind::STAT_DIRECTORY);
    return std::get<2>(_data);
}
StatDirectoryResp& ShardRespContainer::setStatDirectory() {
    _kind = ShardMessageKind::STAT_DIRECTORY;
    auto& x = _data.emplace<2>();
    return x;
}
const ReadDirResp& ShardRespContainer::getReadDir() const {
    ALWAYS_ASSERT(_kind == ShardMessageKind::READ_DIR, "%s != %s", _kind, ShardMessageKind::READ_DIR);
    return std::get<3>(_data);
}
ReadDirResp& ShardRespContainer::setReadDir() {
    _kind = ShardMessageKind::READ_DIR;
    auto& x = _data.emplace<3>();
    return x;
}
const ConstructFileResp& ShardRespContainer::getConstructFile() const {
    ALWAYS_ASSERT(_kind == ShardMessageKind::CONSTRUCT_FILE, "%s != %s", _kind, ShardMessageKind::CONSTRUCT_FILE);
    return std::get<4>(_data);
}
ConstructFileResp& ShardRespContainer::setConstructFile() {
    _kind = ShardMessageKind::CONSTRUCT_FILE;
    auto& x = _data.emplace<4>();
    return x;
}
const AddSpanInitiateResp& ShardRespContainer::getAddSpanInitiate() const {
    ALWAYS_ASSERT(_kind == ShardMessageKind::ADD_SPAN_INITIATE, "%s != %s", _kind, ShardMessageKind::ADD_SPAN_INITIATE);
    return std::get<5>(_data);
}
AddSpanInitiateResp& ShardRespContainer::setAddSpanInitiate() {
    _kind = ShardMessageKind::ADD_SPAN_INITIATE;
    auto& x = _data.emplace<5>();
    return x;
}
const AddSpanCertifyResp& ShardRespContainer::getAddSpanCertify() const {
    ALWAYS_ASSERT(_kind == ShardMessageKind::ADD_SPAN_CERTIFY, "%s != %s", _kind, ShardMessageKind::ADD_SPAN_CERTIFY);
    return std::get<6>(_data);
}
AddSpanCertifyResp& ShardRespContainer::setAddSpanCertify() {
    _kind = ShardMessageKind::ADD_SPAN_CERTIFY;
    auto& x = _data.emplace<6>();
    return x;
}
const LinkFileResp& ShardRespContainer::getLinkFile() const {
    ALWAYS_ASSERT(_kind == ShardMessageKind::LINK_FILE, "%s != %s", _kind, ShardMessageKind::LINK_FILE);
    return std::get<7>(_data);
}
LinkFileResp& ShardRespContainer::setLinkFile() {
    _kind = ShardMessageKind::LINK_FILE;
    auto& x = _data.emplace<7>();
    return x;
}
const SoftUnlinkFileResp& ShardRespContainer::getSoftUnlinkFile() const {
    ALWAYS_ASSERT(_kind == ShardMessageKind::SOFT_UNLINK_FILE, "%s != %s", _kind, ShardMessageKind::SOFT_UNLINK_FILE);
    return std::get<8>(_data);
}
SoftUnlinkFileResp& ShardRespContainer::setSoftUnlinkFile() {
    _kind = ShardMessageKind::SOFT_UNLINK_FILE;
    auto& x = _data.emplace<8>();
    return x;
}
const FileSpansResp& ShardRespContainer::getFileSpans() const {
    ALWAYS_ASSERT(_kind == ShardMessageKind::FILE_SPANS, "%s != %s", _kind, ShardMessageKind::FILE_SPANS);
    return std::get<9>(_data);
}
FileSpansResp& ShardRespContainer::setFileSpans() {
    _kind = ShardMessageKind::FILE_SPANS;
    auto& x = _data.emplace<9>();
    return x;
}
const SameDirectoryRenameResp& ShardRespContainer::getSameDirectoryRename() const {
    ALWAYS_ASSERT(_kind == ShardMessageKind::SAME_DIRECTORY_RENAME, "%s != %s", _kind, ShardMessageKind::SAME_DIRECTORY_RENAME);
    return std::get<10>(_data);
}
SameDirectoryRenameResp& ShardRespContainer::setSameDirectoryRename() {
    _kind = ShardMessageKind::SAME_DIRECTORY_RENAME;
    auto& x = _data.emplace<10>();
    return x;
}
const AddInlineSpanResp& ShardRespContainer::getAddInlineSpan() const {
    ALWAYS_ASSERT(_kind == ShardMessageKind::ADD_INLINE_SPAN, "%s != %s", _kind, ShardMessageKind::ADD_INLINE_SPAN);
    return std::get<11>(_data);
}
AddInlineSpanResp& ShardRespContainer::setAddInlineSpan() {
    _kind = ShardMessageKind::ADD_INLINE_SPAN;
    auto& x = _data.emplace<11>();
    return x;
}
const SetTimeResp& ShardRespContainer::getSetTime() const {
    ALWAYS_ASSERT(_kind == ShardMessageKind::SET_TIME, "%s != %s", _kind, ShardMessageKind::SET_TIME);
    return std::get<12>(_data);
}
SetTimeResp& ShardRespContainer::setSetTime() {
    _kind = ShardMessageKind::SET_TIME;
    auto& x = _data.emplace<12>();
    return x;
}
const FullReadDirResp& ShardRespContainer::getFullReadDir() const {
    ALWAYS_ASSERT(_kind == ShardMessageKind::FULL_READ_DIR, "%s != %s", _kind, ShardMessageKind::FULL_READ_DIR);
    return std::get<13>(_data);
}
FullReadDirResp& ShardRespContainer::setFullReadDir() {
    _kind = ShardMessageKind::FULL_READ_DIR;
    auto& x = _data.emplace<13>();
    return x;
}
const MoveSpanResp& ShardRespContainer::getMoveSpan() const {
    ALWAYS_ASSERT(_kind == ShardMessageKind::MOVE_SPAN, "%s != %s", _kind, ShardMessageKind::MOVE_SPAN);
    return std::get<14>(_data);
}
MoveSpanResp& ShardRespContainer::setMoveSpan() {
    _kind = ShardMessageKind::MOVE_SPAN;
    auto& x = _data.emplace<14>();
    return x;
}
const RemoveNonOwnedEdgeResp& ShardRespContainer::getRemoveNonOwnedEdge() const {
    ALWAYS_ASSERT(_kind == ShardMessageKind::REMOVE_NON_OWNED_EDGE, "%s != %s", _kind, ShardMessageKind::REMOVE_NON_OWNED_EDGE);
    return std::get<15>(_data);
}
RemoveNonOwnedEdgeResp& ShardRespContainer::setRemoveNonOwnedEdge() {
    _kind = ShardMessageKind::REMOVE_NON_OWNED_EDGE;
    auto& x = _data.emplace<15>();
    return x;
}
const SameShardHardFileUnlinkResp& ShardRespContainer::getSameShardHardFileUnlink() const {
    ALWAYS_ASSERT(_kind == ShardMessageKind::SAME_SHARD_HARD_FILE_UNLINK, "%s != %s", _kind, ShardMessageKind::SAME_SHARD_HARD_FILE_UNLINK);
    return std::get<16>(_data);
}
SameShardHardFileUnlinkResp& ShardRespContainer::setSameShardHardFileUnlink() {
    _kind = ShardMessageKind::SAME_SHARD_HARD_FILE_UNLINK;
    auto& x = _data.emplace<16>();
    return x;
}
const StatTransientFileResp& ShardRespContainer::getStatTransientFile() const {
    ALWAYS_ASSERT(_kind == ShardMessageKind::STAT_TRANSIENT_FILE, "%s != %s", _kind, ShardMessageKind::STAT_TRANSIENT_FILE);
    return std::get<17>(_data);
}
StatTransientFileResp& ShardRespContainer::setStatTransientFile() {
    _kind = ShardMessageKind::STAT_TRANSIENT_FILE;
    auto& x = _data.emplace<17>();
    return x;
}
const SetDirectoryInfoResp& ShardRespContainer::getSetDirectoryInfo() const {
    ALWAYS_ASSERT(_kind == ShardMessageKind::SET_DIRECTORY_INFO, "%s != %s", _kind, ShardMessageKind::SET_DIRECTORY_INFO);
    return std::get<18>(_data);
}
SetDirectoryInfoResp& ShardRespContainer::setSetDirectoryInfo() {
    _kind = ShardMessageKind::SET_DIRECTORY_INFO;
    auto& x = _data.emplace<18>();
    return x;
}
const VisitDirectoriesResp& ShardRespContainer::getVisitDirectories() const {
    ALWAYS_ASSERT(_kind == ShardMessageKind::VISIT_DIRECTORIES, "%s != %s", _kind, ShardMessageKind::VISIT_DIRECTORIES);
    return std::get<19>(_data);
}
VisitDirectoriesResp& ShardRespContainer::setVisitDirectories() {
    _kind = ShardMessageKind::VISIT_DIRECTORIES;
    auto& x = _data.emplace<19>();
    return x;
}
const VisitFilesResp& ShardRespContainer::getVisitFiles() const {
    ALWAYS_ASSERT(_kind == ShardMessageKind::VISIT_FILES, "%s != %s", _kind, ShardMessageKind::VISIT_FILES);
    return std::get<20>(_data);
}
VisitFilesResp& ShardRespContainer::setVisitFiles() {
    _kind = ShardMessageKind::VISIT_FILES;
    auto& x = _data.emplace<20>();
    return x;
}
const VisitTransientFilesResp& ShardRespContainer::getVisitTransientFiles() const {
    ALWAYS_ASSERT(_kind == ShardMessageKind::VISIT_TRANSIENT_FILES, "%s != %s", _kind, ShardMessageKind::VISIT_TRANSIENT_FILES);
    return std::get<21>(_data);
}
VisitTransientFilesResp& ShardRespContainer::setVisitTransientFiles() {
    _kind = ShardMessageKind::VISIT_TRANSIENT_FILES;
    auto& x = _data.emplace<21>();
    return x;
}
const RemoveSpanInitiateResp& ShardRespContainer::getRemoveSpanInitiate() const {
    ALWAYS_ASSERT(_kind == ShardMessageKind::REMOVE_SPAN_INITIATE, "%s != %s", _kind, ShardMessageKind::REMOVE_SPAN_INITIATE);
    return std::get<22>(_data);
}
RemoveSpanInitiateResp& ShardRespContainer::setRemoveSpanInitiate() {
    _kind = ShardMessageKind::REMOVE_SPAN_INITIATE;
    auto& x = _data.emplace<22>();
    return x;
}
const RemoveSpanCertifyResp& ShardRespContainer::getRemoveSpanCertify() const {
    ALWAYS_ASSERT(_kind == ShardMessageKind::REMOVE_SPAN_CERTIFY, "%s != %s", _kind, ShardMessageKind::REMOVE_SPAN_CERTIFY);
    return std::get<23>(_data);
}
RemoveSpanCertifyResp& ShardRespContainer::setRemoveSpanCertify() {
    _kind = ShardMessageKind::REMOVE_SPAN_CERTIFY;
    auto& x = _data.emplace<23>();
    return x;
}
const SwapBlocksResp& ShardRespContainer::getSwapBlocks() const {
    ALWAYS_ASSERT(_kind == ShardMessageKind::SWAP_BLOCKS, "%s != %s", _kind, ShardMessageKind::SWAP_BLOCKS);
    return std::get<24>(_data);
}
SwapBlocksResp& ShardRespContainer::setSwapBlocks() {
    _kind = ShardMessageKind::SWAP_BLOCKS;
    auto& x = _data.emplace<24>();
    return x;
}
const BlockServiceFilesResp& ShardRespContainer::getBlockServiceFiles() const {
    ALWAYS_ASSERT(_kind == ShardMessageKind::BLOCK_SERVICE_FILES, "%s != %s", _kind, ShardMessageKind::BLOCK_SERVICE_FILES);
    return std::get<25>(_data);
}
BlockServiceFilesResp& ShardRespContainer::setBlockServiceFiles() {
    _kind = ShardMessageKind::BLOCK_SERVICE_FILES;
    auto& x = _data.emplace<25>();
    return x;
}
const RemoveInodeResp& ShardRespContainer::getRemoveInode() const {
    ALWAYS_ASSERT(_kind == ShardMessageKind::REMOVE_INODE, "%s != %s", _kind, ShardMessageKind::REMOVE_INODE);
    return std::get<26>(_data);
}
RemoveInodeResp& ShardRespContainer::setRemoveInode() {
    _kind = ShardMessageKind::REMOVE_INODE;
    auto& x = _data.emplace<26>();
    return x;
}
const AddSpanInitiateWithReferenceResp& ShardRespContainer::getAddSpanInitiateWithReference() const {
    ALWAYS_ASSERT(_kind == ShardMessageKind::ADD_SPAN_INITIATE_WITH_REFERENCE, "%s != %s", _kind, ShardMessageKind::ADD_SPAN_INITIATE_WITH_REFERENCE);
    return std::get<27>(_data);
}
AddSpanInitiateWithReferenceResp& ShardRespContainer::setAddSpanInitiateWithReference() {
    _kind = ShardMessageKind::ADD_SPAN_INITIATE_WITH_REFERENCE;
    auto& x = _data.emplace<27>();
    return x;
}
const RemoveZeroBlockServiceFilesResp& ShardRespContainer::getRemoveZeroBlockServiceFiles() const {
    ALWAYS_ASSERT(_kind == ShardMessageKind::REMOVE_ZERO_BLOCK_SERVICE_FILES, "%s != %s", _kind, ShardMessageKind::REMOVE_ZERO_BLOCK_SERVICE_FILES);
    return std::get<28>(_data);
}
RemoveZeroBlockServiceFilesResp& ShardRespContainer::setRemoveZeroBlockServiceFiles() {
    _kind = ShardMessageKind::REMOVE_ZERO_BLOCK_SERVICE_FILES;
    auto& x = _data.emplace<28>();
    return x;
}
const CreateDirectoryInodeResp& ShardRespContainer::getCreateDirectoryInode() const {
    ALWAYS_ASSERT(_kind == ShardMessageKind::CREATE_DIRECTORY_INODE, "%s != %s", _kind, ShardMessageKind::CREATE_DIRECTORY_INODE);
    return std::get<29>(_data);
}
CreateDirectoryInodeResp& ShardRespContainer::setCreateDirectoryInode() {
    _kind = ShardMessageKind::CREATE_DIRECTORY_INODE;
    auto& x = _data.emplace<29>();
    return x;
}
const SetDirectoryOwnerResp& ShardRespContainer::getSetDirectoryOwner() const {
    ALWAYS_ASSERT(_kind == ShardMessageKind::SET_DIRECTORY_OWNER, "%s != %s", _kind, ShardMessageKind::SET_DIRECTORY_OWNER);
    return std::get<30>(_data);
}
SetDirectoryOwnerResp& ShardRespContainer::setSetDirectoryOwner() {
    _kind = ShardMessageKind::SET_DIRECTORY_OWNER;
    auto& x = _data.emplace<30>();
    return x;
}
const RemoveDirectoryOwnerResp& ShardRespContainer::getRemoveDirectoryOwner() const {
    ALWAYS_ASSERT(_kind == ShardMessageKind::REMOVE_DIRECTORY_OWNER, "%s != %s", _kind, ShardMessageKind::REMOVE_DIRECTORY_OWNER);
    return std::get<31>(_data);
}
RemoveDirectoryOwnerResp& ShardRespContainer::setRemoveDirectoryOwner() {
    _kind = ShardMessageKind::REMOVE_DIRECTORY_OWNER;
    auto& x = _data.emplace<31>();
    return x;
}
const CreateLockedCurrentEdgeResp& ShardRespContainer::getCreateLockedCurrentEdge() const {
    ALWAYS_ASSERT(_kind == ShardMessageKind::CREATE_LOCKED_CURRENT_EDGE, "%s != %s", _kind, ShardMessageKind::CREATE_LOCKED_CURRENT_EDGE);
    return std::get<32>(_data);
}
CreateLockedCurrentEdgeResp& ShardRespContainer::setCreateLockedCurrentEdge() {
    _kind = ShardMessageKind::CREATE_LOCKED_CURRENT_EDGE;
    auto& x = _data.emplace<32>();
    return x;
}
const LockCurrentEdgeResp& ShardRespContainer::getLockCurrentEdge() const {
    ALWAYS_ASSERT(_kind == ShardMessageKind::LOCK_CURRENT_EDGE, "%s != %s", _kind, ShardMessageKind::LOCK_CURRENT_EDGE);
    return std::get<33>(_data);
}
LockCurrentEdgeResp& ShardRespContainer::setLockCurrentEdge() {
    _kind = ShardMessageKind::LOCK_CURRENT_EDGE;
    auto& x = _data.emplace<33>();
    return x;
}
const UnlockCurrentEdgeResp& ShardRespContainer::getUnlockCurrentEdge() const {
    ALWAYS_ASSERT(_kind == ShardMessageKind::UNLOCK_CURRENT_EDGE, "%s != %s", _kind, ShardMessageKind::UNLOCK_CURRENT_EDGE);
    return std::get<34>(_data);
}
UnlockCurrentEdgeResp& ShardRespContainer::setUnlockCurrentEdge() {
    _kind = ShardMessageKind::UNLOCK_CURRENT_EDGE;
    auto& x = _data.emplace<34>();
    return x;
}
const RemoveOwnedSnapshotFileEdgeResp& ShardRespContainer::getRemoveOwnedSnapshotFileEdge() const {
    ALWAYS_ASSERT(_kind == ShardMessageKind::REMOVE_OWNED_SNAPSHOT_FILE_EDGE, "%s != %s", _kind, ShardMessageKind::REMOVE_OWNED_SNAPSHOT_FILE_EDGE);
    return std::get<35>(_data);
}
RemoveOwnedSnapshotFileEdgeResp& ShardRespContainer::setRemoveOwnedSnapshotFileEdge() {
    _kind = ShardMessageKind::REMOVE_OWNED_SNAPSHOT_FILE_EDGE;
    auto& x = _data.emplace<35>();
    return x;
}
const MakeFileTransientResp& ShardRespContainer::getMakeFileTransient() const {
    ALWAYS_ASSERT(_kind == ShardMessageKind::MAKE_FILE_TRANSIENT, "%s != %s", _kind, ShardMessageKind::MAKE_FILE_TRANSIENT);
    return std::get<36>(_data);
}
MakeFileTransientResp& ShardRespContainer::setMakeFileTransient() {
    _kind = ShardMessageKind::MAKE_FILE_TRANSIENT;
    auto& x = _data.emplace<36>();
    return x;
}
ShardRespContainer::ShardRespContainer() {
    clear();
}

ShardRespContainer::ShardRespContainer(const ShardRespContainer& other) {
    *this = other;
}

ShardRespContainer::ShardRespContainer(ShardRespContainer&& other) {
    _data = std::move(other._data);
    _kind = other._kind;
    other._kind = (ShardMessageKind)0;
}

void ShardRespContainer::operator=(const ShardRespContainer& other) {
    if (other.kind() == (ShardMessageKind)0) { clear(); return; }
    switch (other.kind()) {
    case ShardMessageKind::LOOKUP:
        setLookup() = other.getLookup();
        break;
    case ShardMessageKind::STAT_FILE:
        setStatFile() = other.getStatFile();
        break;
    case ShardMessageKind::STAT_DIRECTORY:
        setStatDirectory() = other.getStatDirectory();
        break;
    case ShardMessageKind::READ_DIR:
        setReadDir() = other.getReadDir();
        break;
    case ShardMessageKind::CONSTRUCT_FILE:
        setConstructFile() = other.getConstructFile();
        break;
    case ShardMessageKind::ADD_SPAN_INITIATE:
        setAddSpanInitiate() = other.getAddSpanInitiate();
        break;
    case ShardMessageKind::ADD_SPAN_CERTIFY:
        setAddSpanCertify() = other.getAddSpanCertify();
        break;
    case ShardMessageKind::LINK_FILE:
        setLinkFile() = other.getLinkFile();
        break;
    case ShardMessageKind::SOFT_UNLINK_FILE:
        setSoftUnlinkFile() = other.getSoftUnlinkFile();
        break;
    case ShardMessageKind::FILE_SPANS:
        setFileSpans() = other.getFileSpans();
        break;
    case ShardMessageKind::SAME_DIRECTORY_RENAME:
        setSameDirectoryRename() = other.getSameDirectoryRename();
        break;
    case ShardMessageKind::ADD_INLINE_SPAN:
        setAddInlineSpan() = other.getAddInlineSpan();
        break;
    case ShardMessageKind::SET_TIME:
        setSetTime() = other.getSetTime();
        break;
    case ShardMessageKind::FULL_READ_DIR:
        setFullReadDir() = other.getFullReadDir();
        break;
    case ShardMessageKind::MOVE_SPAN:
        setMoveSpan() = other.getMoveSpan();
        break;
    case ShardMessageKind::REMOVE_NON_OWNED_EDGE:
        setRemoveNonOwnedEdge() = other.getRemoveNonOwnedEdge();
        break;
    case ShardMessageKind::SAME_SHARD_HARD_FILE_UNLINK:
        setSameShardHardFileUnlink() = other.getSameShardHardFileUnlink();
        break;
    case ShardMessageKind::STAT_TRANSIENT_FILE:
        setStatTransientFile() = other.getStatTransientFile();
        break;
    case ShardMessageKind::SET_DIRECTORY_INFO:
        setSetDirectoryInfo() = other.getSetDirectoryInfo();
        break;
    case ShardMessageKind::VISIT_DIRECTORIES:
        setVisitDirectories() = other.getVisitDirectories();
        break;
    case ShardMessageKind::VISIT_FILES:
        setVisitFiles() = other.getVisitFiles();
        break;
    case ShardMessageKind::VISIT_TRANSIENT_FILES:
        setVisitTransientFiles() = other.getVisitTransientFiles();
        break;
    case ShardMessageKind::REMOVE_SPAN_INITIATE:
        setRemoveSpanInitiate() = other.getRemoveSpanInitiate();
        break;
    case ShardMessageKind::REMOVE_SPAN_CERTIFY:
        setRemoveSpanCertify() = other.getRemoveSpanCertify();
        break;
    case ShardMessageKind::SWAP_BLOCKS:
        setSwapBlocks() = other.getSwapBlocks();
        break;
    case ShardMessageKind::BLOCK_SERVICE_FILES:
        setBlockServiceFiles() = other.getBlockServiceFiles();
        break;
    case ShardMessageKind::REMOVE_INODE:
        setRemoveInode() = other.getRemoveInode();
        break;
    case ShardMessageKind::ADD_SPAN_INITIATE_WITH_REFERENCE:
        setAddSpanInitiateWithReference() = other.getAddSpanInitiateWithReference();
        break;
    case ShardMessageKind::REMOVE_ZERO_BLOCK_SERVICE_FILES:
        setRemoveZeroBlockServiceFiles() = other.getRemoveZeroBlockServiceFiles();
        break;
    case ShardMessageKind::CREATE_DIRECTORY_INODE:
        setCreateDirectoryInode() = other.getCreateDirectoryInode();
        break;
    case ShardMessageKind::SET_DIRECTORY_OWNER:
        setSetDirectoryOwner() = other.getSetDirectoryOwner();
        break;
    case ShardMessageKind::REMOVE_DIRECTORY_OWNER:
        setRemoveDirectoryOwner() = other.getRemoveDirectoryOwner();
        break;
    case ShardMessageKind::CREATE_LOCKED_CURRENT_EDGE:
        setCreateLockedCurrentEdge() = other.getCreateLockedCurrentEdge();
        break;
    case ShardMessageKind::LOCK_CURRENT_EDGE:
        setLockCurrentEdge() = other.getLockCurrentEdge();
        break;
    case ShardMessageKind::UNLOCK_CURRENT_EDGE:
        setUnlockCurrentEdge() = other.getUnlockCurrentEdge();
        break;
    case ShardMessageKind::REMOVE_OWNED_SNAPSHOT_FILE_EDGE:
        setRemoveOwnedSnapshotFileEdge() = other.getRemoveOwnedSnapshotFileEdge();
        break;
    case ShardMessageKind::MAKE_FILE_TRANSIENT:
        setMakeFileTransient() = other.getMakeFileTransient();
        break;
    default:
        throw EGGS_EXCEPTION("bad ShardMessageKind kind %s", other.kind());
    }
}

void ShardRespContainer::operator=(ShardRespContainer&& other) {
    _data = std::move(other._data);
    _kind = other._kind;
    other._kind = (ShardMessageKind)0;
}

size_t ShardRespContainer::packedSize() const {
    switch (_kind) {
    case ShardMessageKind::LOOKUP:
        return std::get<0>(_data).packedSize();
    case ShardMessageKind::STAT_FILE:
        return std::get<1>(_data).packedSize();
    case ShardMessageKind::STAT_DIRECTORY:
        return std::get<2>(_data).packedSize();
    case ShardMessageKind::READ_DIR:
        return std::get<3>(_data).packedSize();
    case ShardMessageKind::CONSTRUCT_FILE:
        return std::get<4>(_data).packedSize();
    case ShardMessageKind::ADD_SPAN_INITIATE:
        return std::get<5>(_data).packedSize();
    case ShardMessageKind::ADD_SPAN_CERTIFY:
        return std::get<6>(_data).packedSize();
    case ShardMessageKind::LINK_FILE:
        return std::get<7>(_data).packedSize();
    case ShardMessageKind::SOFT_UNLINK_FILE:
        return std::get<8>(_data).packedSize();
    case ShardMessageKind::FILE_SPANS:
        return std::get<9>(_data).packedSize();
    case ShardMessageKind::SAME_DIRECTORY_RENAME:
        return std::get<10>(_data).packedSize();
    case ShardMessageKind::ADD_INLINE_SPAN:
        return std::get<11>(_data).packedSize();
    case ShardMessageKind::SET_TIME:
        return std::get<12>(_data).packedSize();
    case ShardMessageKind::FULL_READ_DIR:
        return std::get<13>(_data).packedSize();
    case ShardMessageKind::MOVE_SPAN:
        return std::get<14>(_data).packedSize();
    case ShardMessageKind::REMOVE_NON_OWNED_EDGE:
        return std::get<15>(_data).packedSize();
    case ShardMessageKind::SAME_SHARD_HARD_FILE_UNLINK:
        return std::get<16>(_data).packedSize();
    case ShardMessageKind::STAT_TRANSIENT_FILE:
        return std::get<17>(_data).packedSize();
    case ShardMessageKind::SET_DIRECTORY_INFO:
        return std::get<18>(_data).packedSize();
    case ShardMessageKind::VISIT_DIRECTORIES:
        return std::get<19>(_data).packedSize();
    case ShardMessageKind::VISIT_FILES:
        return std::get<20>(_data).packedSize();
    case ShardMessageKind::VISIT_TRANSIENT_FILES:
        return std::get<21>(_data).packedSize();
    case ShardMessageKind::REMOVE_SPAN_INITIATE:
        return std::get<22>(_data).packedSize();
    case ShardMessageKind::REMOVE_SPAN_CERTIFY:
        return std::get<23>(_data).packedSize();
    case ShardMessageKind::SWAP_BLOCKS:
        return std::get<24>(_data).packedSize();
    case ShardMessageKind::BLOCK_SERVICE_FILES:
        return std::get<25>(_data).packedSize();
    case ShardMessageKind::REMOVE_INODE:
        return std::get<26>(_data).packedSize();
    case ShardMessageKind::ADD_SPAN_INITIATE_WITH_REFERENCE:
        return std::get<27>(_data).packedSize();
    case ShardMessageKind::REMOVE_ZERO_BLOCK_SERVICE_FILES:
        return std::get<28>(_data).packedSize();
    case ShardMessageKind::CREATE_DIRECTORY_INODE:
        return std::get<29>(_data).packedSize();
    case ShardMessageKind::SET_DIRECTORY_OWNER:
        return std::get<30>(_data).packedSize();
    case ShardMessageKind::REMOVE_DIRECTORY_OWNER:
        return std::get<31>(_data).packedSize();
    case ShardMessageKind::CREATE_LOCKED_CURRENT_EDGE:
        return std::get<32>(_data).packedSize();
    case ShardMessageKind::LOCK_CURRENT_EDGE:
        return std::get<33>(_data).packedSize();
    case ShardMessageKind::UNLOCK_CURRENT_EDGE:
        return std::get<34>(_data).packedSize();
    case ShardMessageKind::REMOVE_OWNED_SNAPSHOT_FILE_EDGE:
        return std::get<35>(_data).packedSize();
    case ShardMessageKind::MAKE_FILE_TRANSIENT:
        return std::get<36>(_data).packedSize();
    default:
        throw EGGS_EXCEPTION("bad ShardMessageKind kind %s", _kind);
    }
}

void ShardRespContainer::pack(BincodeBuf& buf) const {
    switch (_kind) {
    case ShardMessageKind::LOOKUP:
        std::get<0>(_data).pack(buf);
        break;
    case ShardMessageKind::STAT_FILE:
        std::get<1>(_data).pack(buf);
        break;
    case ShardMessageKind::STAT_DIRECTORY:
        std::get<2>(_data).pack(buf);
        break;
    case ShardMessageKind::READ_DIR:
        std::get<3>(_data).pack(buf);
        break;
    case ShardMessageKind::CONSTRUCT_FILE:
        std::get<4>(_data).pack(buf);
        break;
    case ShardMessageKind::ADD_SPAN_INITIATE:
        std::get<5>(_data).pack(buf);
        break;
    case ShardMessageKind::ADD_SPAN_CERTIFY:
        std::get<6>(_data).pack(buf);
        break;
    case ShardMessageKind::LINK_FILE:
        std::get<7>(_data).pack(buf);
        break;
    case ShardMessageKind::SOFT_UNLINK_FILE:
        std::get<8>(_data).pack(buf);
        break;
    case ShardMessageKind::FILE_SPANS:
        std::get<9>(_data).pack(buf);
        break;
    case ShardMessageKind::SAME_DIRECTORY_RENAME:
        std::get<10>(_data).pack(buf);
        break;
    case ShardMessageKind::ADD_INLINE_SPAN:
        std::get<11>(_data).pack(buf);
        break;
    case ShardMessageKind::SET_TIME:
        std::get<12>(_data).pack(buf);
        break;
    case ShardMessageKind::FULL_READ_DIR:
        std::get<13>(_data).pack(buf);
        break;
    case ShardMessageKind::MOVE_SPAN:
        std::get<14>(_data).pack(buf);
        break;
    case ShardMessageKind::REMOVE_NON_OWNED_EDGE:
        std::get<15>(_data).pack(buf);
        break;
    case ShardMessageKind::SAME_SHARD_HARD_FILE_UNLINK:
        std::get<16>(_data).pack(buf);
        break;
    case ShardMessageKind::STAT_TRANSIENT_FILE:
        std::get<17>(_data).pack(buf);
        break;
    case ShardMessageKind::SET_DIRECTORY_INFO:
        std::get<18>(_data).pack(buf);
        break;
    case ShardMessageKind::VISIT_DIRECTORIES:
        std::get<19>(_data).pack(buf);
        break;
    case ShardMessageKind::VISIT_FILES:
        std::get<20>(_data).pack(buf);
        break;
    case ShardMessageKind::VISIT_TRANSIENT_FILES:
        std::get<21>(_data).pack(buf);
        break;
    case ShardMessageKind::REMOVE_SPAN_INITIATE:
        std::get<22>(_data).pack(buf);
        break;
    case ShardMessageKind::REMOVE_SPAN_CERTIFY:
        std::get<23>(_data).pack(buf);
        break;
    case ShardMessageKind::SWAP_BLOCKS:
        std::get<24>(_data).pack(buf);
        break;
    case ShardMessageKind::BLOCK_SERVICE_FILES:
        std::get<25>(_data).pack(buf);
        break;
    case ShardMessageKind::REMOVE_INODE:
        std::get<26>(_data).pack(buf);
        break;
    case ShardMessageKind::ADD_SPAN_INITIATE_WITH_REFERENCE:
        std::get<27>(_data).pack(buf);
        break;
    case ShardMessageKind::REMOVE_ZERO_BLOCK_SERVICE_FILES:
        std::get<28>(_data).pack(buf);
        break;
    case ShardMessageKind::CREATE_DIRECTORY_INODE:
        std::get<29>(_data).pack(buf);
        break;
    case ShardMessageKind::SET_DIRECTORY_OWNER:
        std::get<30>(_data).pack(buf);
        break;
    case ShardMessageKind::REMOVE_DIRECTORY_OWNER:
        std::get<31>(_data).pack(buf);
        break;
    case ShardMessageKind::CREATE_LOCKED_CURRENT_EDGE:
        std::get<32>(_data).pack(buf);
        break;
    case ShardMessageKind::LOCK_CURRENT_EDGE:
        std::get<33>(_data).pack(buf);
        break;
    case ShardMessageKind::UNLOCK_CURRENT_EDGE:
        std::get<34>(_data).pack(buf);
        break;
    case ShardMessageKind::REMOVE_OWNED_SNAPSHOT_FILE_EDGE:
        std::get<35>(_data).pack(buf);
        break;
    case ShardMessageKind::MAKE_FILE_TRANSIENT:
        std::get<36>(_data).pack(buf);
        break;
    default:
        throw EGGS_EXCEPTION("bad ShardMessageKind kind %s", _kind);
    }
}

void ShardRespContainer::unpack(BincodeBuf& buf, ShardMessageKind kind) {
    _kind = kind;
    switch (kind) {
    case ShardMessageKind::LOOKUP:
        _data.emplace<0>().unpack(buf);
        break;
    case ShardMessageKind::STAT_FILE:
        _data.emplace<1>().unpack(buf);
        break;
    case ShardMessageKind::STAT_DIRECTORY:
        _data.emplace<2>().unpack(buf);
        break;
    case ShardMessageKind::READ_DIR:
        _data.emplace<3>().unpack(buf);
        break;
    case ShardMessageKind::CONSTRUCT_FILE:
        _data.emplace<4>().unpack(buf);
        break;
    case ShardMessageKind::ADD_SPAN_INITIATE:
        _data.emplace<5>().unpack(buf);
        break;
    case ShardMessageKind::ADD_SPAN_CERTIFY:
        _data.emplace<6>().unpack(buf);
        break;
    case ShardMessageKind::LINK_FILE:
        _data.emplace<7>().unpack(buf);
        break;
    case ShardMessageKind::SOFT_UNLINK_FILE:
        _data.emplace<8>().unpack(buf);
        break;
    case ShardMessageKind::FILE_SPANS:
        _data.emplace<9>().unpack(buf);
        break;
    case ShardMessageKind::SAME_DIRECTORY_RENAME:
        _data.emplace<10>().unpack(buf);
        break;
    case ShardMessageKind::ADD_INLINE_SPAN:
        _data.emplace<11>().unpack(buf);
        break;
    case ShardMessageKind::SET_TIME:
        _data.emplace<12>().unpack(buf);
        break;
    case ShardMessageKind::FULL_READ_DIR:
        _data.emplace<13>().unpack(buf);
        break;
    case ShardMessageKind::MOVE_SPAN:
        _data.emplace<14>().unpack(buf);
        break;
    case ShardMessageKind::REMOVE_NON_OWNED_EDGE:
        _data.emplace<15>().unpack(buf);
        break;
    case ShardMessageKind::SAME_SHARD_HARD_FILE_UNLINK:
        _data.emplace<16>().unpack(buf);
        break;
    case ShardMessageKind::STAT_TRANSIENT_FILE:
        _data.emplace<17>().unpack(buf);
        break;
    case ShardMessageKind::SET_DIRECTORY_INFO:
        _data.emplace<18>().unpack(buf);
        break;
    case ShardMessageKind::VISIT_DIRECTORIES:
        _data.emplace<19>().unpack(buf);
        break;
    case ShardMessageKind::VISIT_FILES:
        _data.emplace<20>().unpack(buf);
        break;
    case ShardMessageKind::VISIT_TRANSIENT_FILES:
        _data.emplace<21>().unpack(buf);
        break;
    case ShardMessageKind::REMOVE_SPAN_INITIATE:
        _data.emplace<22>().unpack(buf);
        break;
    case ShardMessageKind::REMOVE_SPAN_CERTIFY:
        _data.emplace<23>().unpack(buf);
        break;
    case ShardMessageKind::SWAP_BLOCKS:
        _data.emplace<24>().unpack(buf);
        break;
    case ShardMessageKind::BLOCK_SERVICE_FILES:
        _data.emplace<25>().unpack(buf);
        break;
    case ShardMessageKind::REMOVE_INODE:
        _data.emplace<26>().unpack(buf);
        break;
    case ShardMessageKind::ADD_SPAN_INITIATE_WITH_REFERENCE:
        _data.emplace<27>().unpack(buf);
        break;
    case ShardMessageKind::REMOVE_ZERO_BLOCK_SERVICE_FILES:
        _data.emplace<28>().unpack(buf);
        break;
    case ShardMessageKind::CREATE_DIRECTORY_INODE:
        _data.emplace<29>().unpack(buf);
        break;
    case ShardMessageKind::SET_DIRECTORY_OWNER:
        _data.emplace<30>().unpack(buf);
        break;
    case ShardMessageKind::REMOVE_DIRECTORY_OWNER:
        _data.emplace<31>().unpack(buf);
        break;
    case ShardMessageKind::CREATE_LOCKED_CURRENT_EDGE:
        _data.emplace<32>().unpack(buf);
        break;
    case ShardMessageKind::LOCK_CURRENT_EDGE:
        _data.emplace<33>().unpack(buf);
        break;
    case ShardMessageKind::UNLOCK_CURRENT_EDGE:
        _data.emplace<34>().unpack(buf);
        break;
    case ShardMessageKind::REMOVE_OWNED_SNAPSHOT_FILE_EDGE:
        _data.emplace<35>().unpack(buf);
        break;
    case ShardMessageKind::MAKE_FILE_TRANSIENT:
        _data.emplace<36>().unpack(buf);
        break;
    default:
        throw BINCODE_EXCEPTION("bad ShardMessageKind kind %s", kind);
    }
}

bool ShardRespContainer::operator==(const ShardRespContainer& other) const {
    if (_kind != other.kind()) { return false; }
    if (_kind == (ShardMessageKind)0) { return true; }
    switch (_kind) {
    case ShardMessageKind::LOOKUP:
        return getLookup() == other.getLookup();
    case ShardMessageKind::STAT_FILE:
        return getStatFile() == other.getStatFile();
    case ShardMessageKind::STAT_DIRECTORY:
        return getStatDirectory() == other.getStatDirectory();
    case ShardMessageKind::READ_DIR:
        return getReadDir() == other.getReadDir();
    case ShardMessageKind::CONSTRUCT_FILE:
        return getConstructFile() == other.getConstructFile();
    case ShardMessageKind::ADD_SPAN_INITIATE:
        return getAddSpanInitiate() == other.getAddSpanInitiate();
    case ShardMessageKind::ADD_SPAN_CERTIFY:
        return getAddSpanCertify() == other.getAddSpanCertify();
    case ShardMessageKind::LINK_FILE:
        return getLinkFile() == other.getLinkFile();
    case ShardMessageKind::SOFT_UNLINK_FILE:
        return getSoftUnlinkFile() == other.getSoftUnlinkFile();
    case ShardMessageKind::FILE_SPANS:
        return getFileSpans() == other.getFileSpans();
    case ShardMessageKind::SAME_DIRECTORY_RENAME:
        return getSameDirectoryRename() == other.getSameDirectoryRename();
    case ShardMessageKind::ADD_INLINE_SPAN:
        return getAddInlineSpan() == other.getAddInlineSpan();
    case ShardMessageKind::SET_TIME:
        return getSetTime() == other.getSetTime();
    case ShardMessageKind::FULL_READ_DIR:
        return getFullReadDir() == other.getFullReadDir();
    case ShardMessageKind::MOVE_SPAN:
        return getMoveSpan() == other.getMoveSpan();
    case ShardMessageKind::REMOVE_NON_OWNED_EDGE:
        return getRemoveNonOwnedEdge() == other.getRemoveNonOwnedEdge();
    case ShardMessageKind::SAME_SHARD_HARD_FILE_UNLINK:
        return getSameShardHardFileUnlink() == other.getSameShardHardFileUnlink();
    case ShardMessageKind::STAT_TRANSIENT_FILE:
        return getStatTransientFile() == other.getStatTransientFile();
    case ShardMessageKind::SET_DIRECTORY_INFO:
        return getSetDirectoryInfo() == other.getSetDirectoryInfo();
    case ShardMessageKind::VISIT_DIRECTORIES:
        return getVisitDirectories() == other.getVisitDirectories();
    case ShardMessageKind::VISIT_FILES:
        return getVisitFiles() == other.getVisitFiles();
    case ShardMessageKind::VISIT_TRANSIENT_FILES:
        return getVisitTransientFiles() == other.getVisitTransientFiles();
    case ShardMessageKind::REMOVE_SPAN_INITIATE:
        return getRemoveSpanInitiate() == other.getRemoveSpanInitiate();
    case ShardMessageKind::REMOVE_SPAN_CERTIFY:
        return getRemoveSpanCertify() == other.getRemoveSpanCertify();
    case ShardMessageKind::SWAP_BLOCKS:
        return getSwapBlocks() == other.getSwapBlocks();
    case ShardMessageKind::BLOCK_SERVICE_FILES:
        return getBlockServiceFiles() == other.getBlockServiceFiles();
    case ShardMessageKind::REMOVE_INODE:
        return getRemoveInode() == other.getRemoveInode();
    case ShardMessageKind::ADD_SPAN_INITIATE_WITH_REFERENCE:
        return getAddSpanInitiateWithReference() == other.getAddSpanInitiateWithReference();
    case ShardMessageKind::REMOVE_ZERO_BLOCK_SERVICE_FILES:
        return getRemoveZeroBlockServiceFiles() == other.getRemoveZeroBlockServiceFiles();
    case ShardMessageKind::CREATE_DIRECTORY_INODE:
        return getCreateDirectoryInode() == other.getCreateDirectoryInode();
    case ShardMessageKind::SET_DIRECTORY_OWNER:
        return getSetDirectoryOwner() == other.getSetDirectoryOwner();
    case ShardMessageKind::REMOVE_DIRECTORY_OWNER:
        return getRemoveDirectoryOwner() == other.getRemoveDirectoryOwner();
    case ShardMessageKind::CREATE_LOCKED_CURRENT_EDGE:
        return getCreateLockedCurrentEdge() == other.getCreateLockedCurrentEdge();
    case ShardMessageKind::LOCK_CURRENT_EDGE:
        return getLockCurrentEdge() == other.getLockCurrentEdge();
    case ShardMessageKind::UNLOCK_CURRENT_EDGE:
        return getUnlockCurrentEdge() == other.getUnlockCurrentEdge();
    case ShardMessageKind::REMOVE_OWNED_SNAPSHOT_FILE_EDGE:
        return getRemoveOwnedSnapshotFileEdge() == other.getRemoveOwnedSnapshotFileEdge();
    case ShardMessageKind::MAKE_FILE_TRANSIENT:
        return getMakeFileTransient() == other.getMakeFileTransient();
    default:
        throw BINCODE_EXCEPTION("bad ShardMessageKind kind %s", _kind);
    }
}

std::ostream& operator<<(std::ostream& out, const ShardRespContainer& x) {
    switch (x.kind()) {
    case ShardMessageKind::LOOKUP:
        out << x.getLookup();
        break;
    case ShardMessageKind::STAT_FILE:
        out << x.getStatFile();
        break;
    case ShardMessageKind::STAT_DIRECTORY:
        out << x.getStatDirectory();
        break;
    case ShardMessageKind::READ_DIR:
        out << x.getReadDir();
        break;
    case ShardMessageKind::CONSTRUCT_FILE:
        out << x.getConstructFile();
        break;
    case ShardMessageKind::ADD_SPAN_INITIATE:
        out << x.getAddSpanInitiate();
        break;
    case ShardMessageKind::ADD_SPAN_CERTIFY:
        out << x.getAddSpanCertify();
        break;
    case ShardMessageKind::LINK_FILE:
        out << x.getLinkFile();
        break;
    case ShardMessageKind::SOFT_UNLINK_FILE:
        out << x.getSoftUnlinkFile();
        break;
    case ShardMessageKind::FILE_SPANS:
        out << x.getFileSpans();
        break;
    case ShardMessageKind::SAME_DIRECTORY_RENAME:
        out << x.getSameDirectoryRename();
        break;
    case ShardMessageKind::ADD_INLINE_SPAN:
        out << x.getAddInlineSpan();
        break;
    case ShardMessageKind::SET_TIME:
        out << x.getSetTime();
        break;
    case ShardMessageKind::FULL_READ_DIR:
        out << x.getFullReadDir();
        break;
    case ShardMessageKind::MOVE_SPAN:
        out << x.getMoveSpan();
        break;
    case ShardMessageKind::REMOVE_NON_OWNED_EDGE:
        out << x.getRemoveNonOwnedEdge();
        break;
    case ShardMessageKind::SAME_SHARD_HARD_FILE_UNLINK:
        out << x.getSameShardHardFileUnlink();
        break;
    case ShardMessageKind::STAT_TRANSIENT_FILE:
        out << x.getStatTransientFile();
        break;
    case ShardMessageKind::SET_DIRECTORY_INFO:
        out << x.getSetDirectoryInfo();
        break;
    case ShardMessageKind::VISIT_DIRECTORIES:
        out << x.getVisitDirectories();
        break;
    case ShardMessageKind::VISIT_FILES:
        out << x.getVisitFiles();
        break;
    case ShardMessageKind::VISIT_TRANSIENT_FILES:
        out << x.getVisitTransientFiles();
        break;
    case ShardMessageKind::REMOVE_SPAN_INITIATE:
        out << x.getRemoveSpanInitiate();
        break;
    case ShardMessageKind::REMOVE_SPAN_CERTIFY:
        out << x.getRemoveSpanCertify();
        break;
    case ShardMessageKind::SWAP_BLOCKS:
        out << x.getSwapBlocks();
        break;
    case ShardMessageKind::BLOCK_SERVICE_FILES:
        out << x.getBlockServiceFiles();
        break;
    case ShardMessageKind::REMOVE_INODE:
        out << x.getRemoveInode();
        break;
    case ShardMessageKind::ADD_SPAN_INITIATE_WITH_REFERENCE:
        out << x.getAddSpanInitiateWithReference();
        break;
    case ShardMessageKind::REMOVE_ZERO_BLOCK_SERVICE_FILES:
        out << x.getRemoveZeroBlockServiceFiles();
        break;
    case ShardMessageKind::CREATE_DIRECTORY_INODE:
        out << x.getCreateDirectoryInode();
        break;
    case ShardMessageKind::SET_DIRECTORY_OWNER:
        out << x.getSetDirectoryOwner();
        break;
    case ShardMessageKind::REMOVE_DIRECTORY_OWNER:
        out << x.getRemoveDirectoryOwner();
        break;
    case ShardMessageKind::CREATE_LOCKED_CURRENT_EDGE:
        out << x.getCreateLockedCurrentEdge();
        break;
    case ShardMessageKind::LOCK_CURRENT_EDGE:
        out << x.getLockCurrentEdge();
        break;
    case ShardMessageKind::UNLOCK_CURRENT_EDGE:
        out << x.getUnlockCurrentEdge();
        break;
    case ShardMessageKind::REMOVE_OWNED_SNAPSHOT_FILE_EDGE:
        out << x.getRemoveOwnedSnapshotFileEdge();
        break;
    case ShardMessageKind::MAKE_FILE_TRANSIENT:
        out << x.getMakeFileTransient();
        break;
    default:
        throw EGGS_EXCEPTION("bad ShardMessageKind kind %s", x.kind());
    }
    return out;
}

const MakeDirectoryReq& CDCReqContainer::getMakeDirectory() const {
    ALWAYS_ASSERT(_kind == CDCMessageKind::MAKE_DIRECTORY, "%s != %s", _kind, CDCMessageKind::MAKE_DIRECTORY);
    return std::get<0>(_data);
}
MakeDirectoryReq& CDCReqContainer::setMakeDirectory() {
    _kind = CDCMessageKind::MAKE_DIRECTORY;
    auto& x = _data.emplace<0>();
    return x;
}
const RenameFileReq& CDCReqContainer::getRenameFile() const {
    ALWAYS_ASSERT(_kind == CDCMessageKind::RENAME_FILE, "%s != %s", _kind, CDCMessageKind::RENAME_FILE);
    return std::get<1>(_data);
}
RenameFileReq& CDCReqContainer::setRenameFile() {
    _kind = CDCMessageKind::RENAME_FILE;
    auto& x = _data.emplace<1>();
    return x;
}
const SoftUnlinkDirectoryReq& CDCReqContainer::getSoftUnlinkDirectory() const {
    ALWAYS_ASSERT(_kind == CDCMessageKind::SOFT_UNLINK_DIRECTORY, "%s != %s", _kind, CDCMessageKind::SOFT_UNLINK_DIRECTORY);
    return std::get<2>(_data);
}
SoftUnlinkDirectoryReq& CDCReqContainer::setSoftUnlinkDirectory() {
    _kind = CDCMessageKind::SOFT_UNLINK_DIRECTORY;
    auto& x = _data.emplace<2>();
    return x;
}
const RenameDirectoryReq& CDCReqContainer::getRenameDirectory() const {
    ALWAYS_ASSERT(_kind == CDCMessageKind::RENAME_DIRECTORY, "%s != %s", _kind, CDCMessageKind::RENAME_DIRECTORY);
    return std::get<3>(_data);
}
RenameDirectoryReq& CDCReqContainer::setRenameDirectory() {
    _kind = CDCMessageKind::RENAME_DIRECTORY;
    auto& x = _data.emplace<3>();
    return x;
}
const HardUnlinkDirectoryReq& CDCReqContainer::getHardUnlinkDirectory() const {
    ALWAYS_ASSERT(_kind == CDCMessageKind::HARD_UNLINK_DIRECTORY, "%s != %s", _kind, CDCMessageKind::HARD_UNLINK_DIRECTORY);
    return std::get<4>(_data);
}
HardUnlinkDirectoryReq& CDCReqContainer::setHardUnlinkDirectory() {
    _kind = CDCMessageKind::HARD_UNLINK_DIRECTORY;
    auto& x = _data.emplace<4>();
    return x;
}
const CrossShardHardUnlinkFileReq& CDCReqContainer::getCrossShardHardUnlinkFile() const {
    ALWAYS_ASSERT(_kind == CDCMessageKind::CROSS_SHARD_HARD_UNLINK_FILE, "%s != %s", _kind, CDCMessageKind::CROSS_SHARD_HARD_UNLINK_FILE);
    return std::get<5>(_data);
}
CrossShardHardUnlinkFileReq& CDCReqContainer::setCrossShardHardUnlinkFile() {
    _kind = CDCMessageKind::CROSS_SHARD_HARD_UNLINK_FILE;
    auto& x = _data.emplace<5>();
    return x;
}
CDCReqContainer::CDCReqContainer() {
    clear();
}

CDCReqContainer::CDCReqContainer(const CDCReqContainer& other) {
    *this = other;
}

CDCReqContainer::CDCReqContainer(CDCReqContainer&& other) {
    _data = std::move(other._data);
    _kind = other._kind;
    other._kind = (CDCMessageKind)0;
}

void CDCReqContainer::operator=(const CDCReqContainer& other) {
    if (other.kind() == (CDCMessageKind)0) { clear(); return; }
    switch (other.kind()) {
    case CDCMessageKind::MAKE_DIRECTORY:
        setMakeDirectory() = other.getMakeDirectory();
        break;
    case CDCMessageKind::RENAME_FILE:
        setRenameFile() = other.getRenameFile();
        break;
    case CDCMessageKind::SOFT_UNLINK_DIRECTORY:
        setSoftUnlinkDirectory() = other.getSoftUnlinkDirectory();
        break;
    case CDCMessageKind::RENAME_DIRECTORY:
        setRenameDirectory() = other.getRenameDirectory();
        break;
    case CDCMessageKind::HARD_UNLINK_DIRECTORY:
        setHardUnlinkDirectory() = other.getHardUnlinkDirectory();
        break;
    case CDCMessageKind::CROSS_SHARD_HARD_UNLINK_FILE:
        setCrossShardHardUnlinkFile() = other.getCrossShardHardUnlinkFile();
        break;
    default:
        throw EGGS_EXCEPTION("bad CDCMessageKind kind %s", other.kind());
    }
}

void CDCReqContainer::operator=(CDCReqContainer&& other) {
    _data = std::move(other._data);
    _kind = other._kind;
    other._kind = (CDCMessageKind)0;
}

size_t CDCReqContainer::packedSize() const {
    switch (_kind) {
    case CDCMessageKind::MAKE_DIRECTORY:
        return std::get<0>(_data).packedSize();
    case CDCMessageKind::RENAME_FILE:
        return std::get<1>(_data).packedSize();
    case CDCMessageKind::SOFT_UNLINK_DIRECTORY:
        return std::get<2>(_data).packedSize();
    case CDCMessageKind::RENAME_DIRECTORY:
        return std::get<3>(_data).packedSize();
    case CDCMessageKind::HARD_UNLINK_DIRECTORY:
        return std::get<4>(_data).packedSize();
    case CDCMessageKind::CROSS_SHARD_HARD_UNLINK_FILE:
        return std::get<5>(_data).packedSize();
    default:
        throw EGGS_EXCEPTION("bad CDCMessageKind kind %s", _kind);
    }
}

void CDCReqContainer::pack(BincodeBuf& buf) const {
    switch (_kind) {
    case CDCMessageKind::MAKE_DIRECTORY:
        std::get<0>(_data).pack(buf);
        break;
    case CDCMessageKind::RENAME_FILE:
        std::get<1>(_data).pack(buf);
        break;
    case CDCMessageKind::SOFT_UNLINK_DIRECTORY:
        std::get<2>(_data).pack(buf);
        break;
    case CDCMessageKind::RENAME_DIRECTORY:
        std::get<3>(_data).pack(buf);
        break;
    case CDCMessageKind::HARD_UNLINK_DIRECTORY:
        std::get<4>(_data).pack(buf);
        break;
    case CDCMessageKind::CROSS_SHARD_HARD_UNLINK_FILE:
        std::get<5>(_data).pack(buf);
        break;
    default:
        throw EGGS_EXCEPTION("bad CDCMessageKind kind %s", _kind);
    }
}

void CDCReqContainer::unpack(BincodeBuf& buf, CDCMessageKind kind) {
    _kind = kind;
    switch (kind) {
    case CDCMessageKind::MAKE_DIRECTORY:
        _data.emplace<0>().unpack(buf);
        break;
    case CDCMessageKind::RENAME_FILE:
        _data.emplace<1>().unpack(buf);
        break;
    case CDCMessageKind::SOFT_UNLINK_DIRECTORY:
        _data.emplace<2>().unpack(buf);
        break;
    case CDCMessageKind::RENAME_DIRECTORY:
        _data.emplace<3>().unpack(buf);
        break;
    case CDCMessageKind::HARD_UNLINK_DIRECTORY:
        _data.emplace<4>().unpack(buf);
        break;
    case CDCMessageKind::CROSS_SHARD_HARD_UNLINK_FILE:
        _data.emplace<5>().unpack(buf);
        break;
    default:
        throw BINCODE_EXCEPTION("bad CDCMessageKind kind %s", kind);
    }
}

bool CDCReqContainer::operator==(const CDCReqContainer& other) const {
    if (_kind != other.kind()) { return false; }
    if (_kind == (CDCMessageKind)0) { return true; }
    switch (_kind) {
    case CDCMessageKind::MAKE_DIRECTORY:
        return getMakeDirectory() == other.getMakeDirectory();
    case CDCMessageKind::RENAME_FILE:
        return getRenameFile() == other.getRenameFile();
    case CDCMessageKind::SOFT_UNLINK_DIRECTORY:
        return getSoftUnlinkDirectory() == other.getSoftUnlinkDirectory();
    case CDCMessageKind::RENAME_DIRECTORY:
        return getRenameDirectory() == other.getRenameDirectory();
    case CDCMessageKind::HARD_UNLINK_DIRECTORY:
        return getHardUnlinkDirectory() == other.getHardUnlinkDirectory();
    case CDCMessageKind::CROSS_SHARD_HARD_UNLINK_FILE:
        return getCrossShardHardUnlinkFile() == other.getCrossShardHardUnlinkFile();
    default:
        throw BINCODE_EXCEPTION("bad CDCMessageKind kind %s", _kind);
    }
}

std::ostream& operator<<(std::ostream& out, const CDCReqContainer& x) {
    switch (x.kind()) {
    case CDCMessageKind::MAKE_DIRECTORY:
        out << x.getMakeDirectory();
        break;
    case CDCMessageKind::RENAME_FILE:
        out << x.getRenameFile();
        break;
    case CDCMessageKind::SOFT_UNLINK_DIRECTORY:
        out << x.getSoftUnlinkDirectory();
        break;
    case CDCMessageKind::RENAME_DIRECTORY:
        out << x.getRenameDirectory();
        break;
    case CDCMessageKind::HARD_UNLINK_DIRECTORY:
        out << x.getHardUnlinkDirectory();
        break;
    case CDCMessageKind::CROSS_SHARD_HARD_UNLINK_FILE:
        out << x.getCrossShardHardUnlinkFile();
        break;
    default:
        throw EGGS_EXCEPTION("bad CDCMessageKind kind %s", x.kind());
    }
    return out;
}

const MakeDirectoryResp& CDCRespContainer::getMakeDirectory() const {
    ALWAYS_ASSERT(_kind == CDCMessageKind::MAKE_DIRECTORY, "%s != %s", _kind, CDCMessageKind::MAKE_DIRECTORY);
    return std::get<0>(_data);
}
MakeDirectoryResp& CDCRespContainer::setMakeDirectory() {
    _kind = CDCMessageKind::MAKE_DIRECTORY;
    auto& x = _data.emplace<0>();
    return x;
}
const RenameFileResp& CDCRespContainer::getRenameFile() const {
    ALWAYS_ASSERT(_kind == CDCMessageKind::RENAME_FILE, "%s != %s", _kind, CDCMessageKind::RENAME_FILE);
    return std::get<1>(_data);
}
RenameFileResp& CDCRespContainer::setRenameFile() {
    _kind = CDCMessageKind::RENAME_FILE;
    auto& x = _data.emplace<1>();
    return x;
}
const SoftUnlinkDirectoryResp& CDCRespContainer::getSoftUnlinkDirectory() const {
    ALWAYS_ASSERT(_kind == CDCMessageKind::SOFT_UNLINK_DIRECTORY, "%s != %s", _kind, CDCMessageKind::SOFT_UNLINK_DIRECTORY);
    return std::get<2>(_data);
}
SoftUnlinkDirectoryResp& CDCRespContainer::setSoftUnlinkDirectory() {
    _kind = CDCMessageKind::SOFT_UNLINK_DIRECTORY;
    auto& x = _data.emplace<2>();
    return x;
}
const RenameDirectoryResp& CDCRespContainer::getRenameDirectory() const {
    ALWAYS_ASSERT(_kind == CDCMessageKind::RENAME_DIRECTORY, "%s != %s", _kind, CDCMessageKind::RENAME_DIRECTORY);
    return std::get<3>(_data);
}
RenameDirectoryResp& CDCRespContainer::setRenameDirectory() {
    _kind = CDCMessageKind::RENAME_DIRECTORY;
    auto& x = _data.emplace<3>();
    return x;
}
const HardUnlinkDirectoryResp& CDCRespContainer::getHardUnlinkDirectory() const {
    ALWAYS_ASSERT(_kind == CDCMessageKind::HARD_UNLINK_DIRECTORY, "%s != %s", _kind, CDCMessageKind::HARD_UNLINK_DIRECTORY);
    return std::get<4>(_data);
}
HardUnlinkDirectoryResp& CDCRespContainer::setHardUnlinkDirectory() {
    _kind = CDCMessageKind::HARD_UNLINK_DIRECTORY;
    auto& x = _data.emplace<4>();
    return x;
}
const CrossShardHardUnlinkFileResp& CDCRespContainer::getCrossShardHardUnlinkFile() const {
    ALWAYS_ASSERT(_kind == CDCMessageKind::CROSS_SHARD_HARD_UNLINK_FILE, "%s != %s", _kind, CDCMessageKind::CROSS_SHARD_HARD_UNLINK_FILE);
    return std::get<5>(_data);
}
CrossShardHardUnlinkFileResp& CDCRespContainer::setCrossShardHardUnlinkFile() {
    _kind = CDCMessageKind::CROSS_SHARD_HARD_UNLINK_FILE;
    auto& x = _data.emplace<5>();
    return x;
}
CDCRespContainer::CDCRespContainer() {
    clear();
}

CDCRespContainer::CDCRespContainer(const CDCRespContainer& other) {
    *this = other;
}

CDCRespContainer::CDCRespContainer(CDCRespContainer&& other) {
    _data = std::move(other._data);
    _kind = other._kind;
    other._kind = (CDCMessageKind)0;
}

void CDCRespContainer::operator=(const CDCRespContainer& other) {
    if (other.kind() == (CDCMessageKind)0) { clear(); return; }
    switch (other.kind()) {
    case CDCMessageKind::MAKE_DIRECTORY:
        setMakeDirectory() = other.getMakeDirectory();
        break;
    case CDCMessageKind::RENAME_FILE:
        setRenameFile() = other.getRenameFile();
        break;
    case CDCMessageKind::SOFT_UNLINK_DIRECTORY:
        setSoftUnlinkDirectory() = other.getSoftUnlinkDirectory();
        break;
    case CDCMessageKind::RENAME_DIRECTORY:
        setRenameDirectory() = other.getRenameDirectory();
        break;
    case CDCMessageKind::HARD_UNLINK_DIRECTORY:
        setHardUnlinkDirectory() = other.getHardUnlinkDirectory();
        break;
    case CDCMessageKind::CROSS_SHARD_HARD_UNLINK_FILE:
        setCrossShardHardUnlinkFile() = other.getCrossShardHardUnlinkFile();
        break;
    default:
        throw EGGS_EXCEPTION("bad CDCMessageKind kind %s", other.kind());
    }
}

void CDCRespContainer::operator=(CDCRespContainer&& other) {
    _data = std::move(other._data);
    _kind = other._kind;
    other._kind = (CDCMessageKind)0;
}

size_t CDCRespContainer::packedSize() const {
    switch (_kind) {
    case CDCMessageKind::MAKE_DIRECTORY:
        return std::get<0>(_data).packedSize();
    case CDCMessageKind::RENAME_FILE:
        return std::get<1>(_data).packedSize();
    case CDCMessageKind::SOFT_UNLINK_DIRECTORY:
        return std::get<2>(_data).packedSize();
    case CDCMessageKind::RENAME_DIRECTORY:
        return std::get<3>(_data).packedSize();
    case CDCMessageKind::HARD_UNLINK_DIRECTORY:
        return std::get<4>(_data).packedSize();
    case CDCMessageKind::CROSS_SHARD_HARD_UNLINK_FILE:
        return std::get<5>(_data).packedSize();
    default:
        throw EGGS_EXCEPTION("bad CDCMessageKind kind %s", _kind);
    }
}

void CDCRespContainer::pack(BincodeBuf& buf) const {
    switch (_kind) {
    case CDCMessageKind::MAKE_DIRECTORY:
        std::get<0>(_data).pack(buf);
        break;
    case CDCMessageKind::RENAME_FILE:
        std::get<1>(_data).pack(buf);
        break;
    case CDCMessageKind::SOFT_UNLINK_DIRECTORY:
        std::get<2>(_data).pack(buf);
        break;
    case CDCMessageKind::RENAME_DIRECTORY:
        std::get<3>(_data).pack(buf);
        break;
    case CDCMessageKind::HARD_UNLINK_DIRECTORY:
        std::get<4>(_data).pack(buf);
        break;
    case CDCMessageKind::CROSS_SHARD_HARD_UNLINK_FILE:
        std::get<5>(_data).pack(buf);
        break;
    default:
        throw EGGS_EXCEPTION("bad CDCMessageKind kind %s", _kind);
    }
}

void CDCRespContainer::unpack(BincodeBuf& buf, CDCMessageKind kind) {
    _kind = kind;
    switch (kind) {
    case CDCMessageKind::MAKE_DIRECTORY:
        _data.emplace<0>().unpack(buf);
        break;
    case CDCMessageKind::RENAME_FILE:
        _data.emplace<1>().unpack(buf);
        break;
    case CDCMessageKind::SOFT_UNLINK_DIRECTORY:
        _data.emplace<2>().unpack(buf);
        break;
    case CDCMessageKind::RENAME_DIRECTORY:
        _data.emplace<3>().unpack(buf);
        break;
    case CDCMessageKind::HARD_UNLINK_DIRECTORY:
        _data.emplace<4>().unpack(buf);
        break;
    case CDCMessageKind::CROSS_SHARD_HARD_UNLINK_FILE:
        _data.emplace<5>().unpack(buf);
        break;
    default:
        throw BINCODE_EXCEPTION("bad CDCMessageKind kind %s", kind);
    }
}

bool CDCRespContainer::operator==(const CDCRespContainer& other) const {
    if (_kind != other.kind()) { return false; }
    if (_kind == (CDCMessageKind)0) { return true; }
    switch (_kind) {
    case CDCMessageKind::MAKE_DIRECTORY:
        return getMakeDirectory() == other.getMakeDirectory();
    case CDCMessageKind::RENAME_FILE:
        return getRenameFile() == other.getRenameFile();
    case CDCMessageKind::SOFT_UNLINK_DIRECTORY:
        return getSoftUnlinkDirectory() == other.getSoftUnlinkDirectory();
    case CDCMessageKind::RENAME_DIRECTORY:
        return getRenameDirectory() == other.getRenameDirectory();
    case CDCMessageKind::HARD_UNLINK_DIRECTORY:
        return getHardUnlinkDirectory() == other.getHardUnlinkDirectory();
    case CDCMessageKind::CROSS_SHARD_HARD_UNLINK_FILE:
        return getCrossShardHardUnlinkFile() == other.getCrossShardHardUnlinkFile();
    default:
        throw BINCODE_EXCEPTION("bad CDCMessageKind kind %s", _kind);
    }
}

std::ostream& operator<<(std::ostream& out, const CDCRespContainer& x) {
    switch (x.kind()) {
    case CDCMessageKind::MAKE_DIRECTORY:
        out << x.getMakeDirectory();
        break;
    case CDCMessageKind::RENAME_FILE:
        out << x.getRenameFile();
        break;
    case CDCMessageKind::SOFT_UNLINK_DIRECTORY:
        out << x.getSoftUnlinkDirectory();
        break;
    case CDCMessageKind::RENAME_DIRECTORY:
        out << x.getRenameDirectory();
        break;
    case CDCMessageKind::HARD_UNLINK_DIRECTORY:
        out << x.getHardUnlinkDirectory();
        break;
    case CDCMessageKind::CROSS_SHARD_HARD_UNLINK_FILE:
        out << x.getCrossShardHardUnlinkFile();
        break;
    default:
        throw EGGS_EXCEPTION("bad CDCMessageKind kind %s", x.kind());
    }
    return out;
}

const ShardsReq& ShuckleReqContainer::getShards() const {
    ALWAYS_ASSERT(_kind == ShuckleMessageKind::SHARDS, "%s != %s", _kind, ShuckleMessageKind::SHARDS);
    return std::get<0>(_data);
}
ShardsReq& ShuckleReqContainer::setShards() {
    _kind = ShuckleMessageKind::SHARDS;
    auto& x = _data.emplace<0>();
    return x;
}
const CdcReq& ShuckleReqContainer::getCdc() const {
    ALWAYS_ASSERT(_kind == ShuckleMessageKind::CDC, "%s != %s", _kind, ShuckleMessageKind::CDC);
    return std::get<1>(_data);
}
CdcReq& ShuckleReqContainer::setCdc() {
    _kind = ShuckleMessageKind::CDC;
    auto& x = _data.emplace<1>();
    return x;
}
const InfoReq& ShuckleReqContainer::getInfo() const {
    ALWAYS_ASSERT(_kind == ShuckleMessageKind::INFO, "%s != %s", _kind, ShuckleMessageKind::INFO);
    return std::get<2>(_data);
}
InfoReq& ShuckleReqContainer::setInfo() {
    _kind = ShuckleMessageKind::INFO;
    auto& x = _data.emplace<2>();
    return x;
}
const ShuckleReq& ShuckleReqContainer::getShuckle() const {
    ALWAYS_ASSERT(_kind == ShuckleMessageKind::SHUCKLE, "%s != %s", _kind, ShuckleMessageKind::SHUCKLE);
    return std::get<3>(_data);
}
ShuckleReq& ShuckleReqContainer::setShuckle() {
    _kind = ShuckleMessageKind::SHUCKLE;
    auto& x = _data.emplace<3>();
    return x;
}
const RegisterBlockServicesReq& ShuckleReqContainer::getRegisterBlockServices() const {
    ALWAYS_ASSERT(_kind == ShuckleMessageKind::REGISTER_BLOCK_SERVICES, "%s != %s", _kind, ShuckleMessageKind::REGISTER_BLOCK_SERVICES);
    return std::get<4>(_data);
}
RegisterBlockServicesReq& ShuckleReqContainer::setRegisterBlockServices() {
    _kind = ShuckleMessageKind::REGISTER_BLOCK_SERVICES;
    auto& x = _data.emplace<4>();
    return x;
}
const RegisterShardReq& ShuckleReqContainer::getRegisterShard() const {
    ALWAYS_ASSERT(_kind == ShuckleMessageKind::REGISTER_SHARD, "%s != %s", _kind, ShuckleMessageKind::REGISTER_SHARD);
    return std::get<5>(_data);
}
RegisterShardReq& ShuckleReqContainer::setRegisterShard() {
    _kind = ShuckleMessageKind::REGISTER_SHARD;
    auto& x = _data.emplace<5>();
    return x;
}
const AllBlockServicesReq& ShuckleReqContainer::getAllBlockServices() const {
    ALWAYS_ASSERT(_kind == ShuckleMessageKind::ALL_BLOCK_SERVICES, "%s != %s", _kind, ShuckleMessageKind::ALL_BLOCK_SERVICES);
    return std::get<6>(_data);
}
AllBlockServicesReq& ShuckleReqContainer::setAllBlockServices() {
    _kind = ShuckleMessageKind::ALL_BLOCK_SERVICES;
    auto& x = _data.emplace<6>();
    return x;
}
const RegisterCdcReq& ShuckleReqContainer::getRegisterCdc() const {
    ALWAYS_ASSERT(_kind == ShuckleMessageKind::REGISTER_CDC, "%s != %s", _kind, ShuckleMessageKind::REGISTER_CDC);
    return std::get<7>(_data);
}
RegisterCdcReq& ShuckleReqContainer::setRegisterCdc() {
    _kind = ShuckleMessageKind::REGISTER_CDC;
    auto& x = _data.emplace<7>();
    return x;
}
const SetBlockServiceFlagsReq& ShuckleReqContainer::getSetBlockServiceFlags() const {
    ALWAYS_ASSERT(_kind == ShuckleMessageKind::SET_BLOCK_SERVICE_FLAGS, "%s != %s", _kind, ShuckleMessageKind::SET_BLOCK_SERVICE_FLAGS);
    return std::get<8>(_data);
}
SetBlockServiceFlagsReq& ShuckleReqContainer::setSetBlockServiceFlags() {
    _kind = ShuckleMessageKind::SET_BLOCK_SERVICE_FLAGS;
    auto& x = _data.emplace<8>();
    return x;
}
const BlockServiceReq& ShuckleReqContainer::getBlockService() const {
    ALWAYS_ASSERT(_kind == ShuckleMessageKind::BLOCK_SERVICE, "%s != %s", _kind, ShuckleMessageKind::BLOCK_SERVICE);
    return std::get<9>(_data);
}
BlockServiceReq& ShuckleReqContainer::setBlockService() {
    _kind = ShuckleMessageKind::BLOCK_SERVICE;
    auto& x = _data.emplace<9>();
    return x;
}
const InsertStatsReq& ShuckleReqContainer::getInsertStats() const {
    ALWAYS_ASSERT(_kind == ShuckleMessageKind::INSERT_STATS, "%s != %s", _kind, ShuckleMessageKind::INSERT_STATS);
    return std::get<10>(_data);
}
InsertStatsReq& ShuckleReqContainer::setInsertStats() {
    _kind = ShuckleMessageKind::INSERT_STATS;
    auto& x = _data.emplace<10>();
    return x;
}
const ShardReq& ShuckleReqContainer::getShard() const {
    ALWAYS_ASSERT(_kind == ShuckleMessageKind::SHARD, "%s != %s", _kind, ShuckleMessageKind::SHARD);
    return std::get<11>(_data);
}
ShardReq& ShuckleReqContainer::setShard() {
    _kind = ShuckleMessageKind::SHARD;
    auto& x = _data.emplace<11>();
    return x;
}
const GetStatsReq& ShuckleReqContainer::getGetStats() const {
    ALWAYS_ASSERT(_kind == ShuckleMessageKind::GET_STATS, "%s != %s", _kind, ShuckleMessageKind::GET_STATS);
    return std::get<12>(_data);
}
GetStatsReq& ShuckleReqContainer::setGetStats() {
    _kind = ShuckleMessageKind::GET_STATS;
    auto& x = _data.emplace<12>();
    return x;
}
const RegisterShardReplicaReq& ShuckleReqContainer::getRegisterShardReplica() const {
    ALWAYS_ASSERT(_kind == ShuckleMessageKind::REGISTER_SHARD_REPLICA, "%s != %s", _kind, ShuckleMessageKind::REGISTER_SHARD_REPLICA);
    return std::get<13>(_data);
}
RegisterShardReplicaReq& ShuckleReqContainer::setRegisterShardReplica() {
    _kind = ShuckleMessageKind::REGISTER_SHARD_REPLICA;
    auto& x = _data.emplace<13>();
    return x;
}
const ShardReplicasReq& ShuckleReqContainer::getShardReplicas() const {
    ALWAYS_ASSERT(_kind == ShuckleMessageKind::SHARD_REPLICAS, "%s != %s", _kind, ShuckleMessageKind::SHARD_REPLICAS);
    return std::get<14>(_data);
}
ShardReplicasReq& ShuckleReqContainer::setShardReplicas() {
    _kind = ShuckleMessageKind::SHARD_REPLICAS;
    auto& x = _data.emplace<14>();
    return x;
}
const ShardBlockServicesReq& ShuckleReqContainer::getShardBlockServices() const {
    ALWAYS_ASSERT(_kind == ShuckleMessageKind::SHARD_BLOCK_SERVICES, "%s != %s", _kind, ShuckleMessageKind::SHARD_BLOCK_SERVICES);
    return std::get<15>(_data);
}
ShardBlockServicesReq& ShuckleReqContainer::setShardBlockServices() {
    _kind = ShuckleMessageKind::SHARD_BLOCK_SERVICES;
    auto& x = _data.emplace<15>();
    return x;
}
const RegisterCdcReplicaReq& ShuckleReqContainer::getRegisterCdcReplica() const {
    ALWAYS_ASSERT(_kind == ShuckleMessageKind::REGISTER_CDC_REPLICA, "%s != %s", _kind, ShuckleMessageKind::REGISTER_CDC_REPLICA);
    return std::get<16>(_data);
}
RegisterCdcReplicaReq& ShuckleReqContainer::setRegisterCdcReplica() {
    _kind = ShuckleMessageKind::REGISTER_CDC_REPLICA;
    auto& x = _data.emplace<16>();
    return x;
}
const CdcReplicasReq& ShuckleReqContainer::getCdcReplicas() const {
    ALWAYS_ASSERT(_kind == ShuckleMessageKind::CDC_REPLICAS, "%s != %s", _kind, ShuckleMessageKind::CDC_REPLICAS);
    return std::get<17>(_data);
}
CdcReplicasReq& ShuckleReqContainer::setCdcReplicas() {
    _kind = ShuckleMessageKind::CDC_REPLICAS;
    auto& x = _data.emplace<17>();
    return x;
}
ShuckleReqContainer::ShuckleReqContainer() {
    clear();
}

ShuckleReqContainer::ShuckleReqContainer(const ShuckleReqContainer& other) {
    *this = other;
}

ShuckleReqContainer::ShuckleReqContainer(ShuckleReqContainer&& other) {
    _data = std::move(other._data);
    _kind = other._kind;
    other._kind = (ShuckleMessageKind)0;
}

void ShuckleReqContainer::operator=(const ShuckleReqContainer& other) {
    if (other.kind() == (ShuckleMessageKind)0) { clear(); return; }
    switch (other.kind()) {
    case ShuckleMessageKind::SHARDS:
        setShards() = other.getShards();
        break;
    case ShuckleMessageKind::CDC:
        setCdc() = other.getCdc();
        break;
    case ShuckleMessageKind::INFO:
        setInfo() = other.getInfo();
        break;
    case ShuckleMessageKind::SHUCKLE:
        setShuckle() = other.getShuckle();
        break;
    case ShuckleMessageKind::REGISTER_BLOCK_SERVICES:
        setRegisterBlockServices() = other.getRegisterBlockServices();
        break;
    case ShuckleMessageKind::REGISTER_SHARD:
        setRegisterShard() = other.getRegisterShard();
        break;
    case ShuckleMessageKind::ALL_BLOCK_SERVICES:
        setAllBlockServices() = other.getAllBlockServices();
        break;
    case ShuckleMessageKind::REGISTER_CDC:
        setRegisterCdc() = other.getRegisterCdc();
        break;
    case ShuckleMessageKind::SET_BLOCK_SERVICE_FLAGS:
        setSetBlockServiceFlags() = other.getSetBlockServiceFlags();
        break;
    case ShuckleMessageKind::BLOCK_SERVICE:
        setBlockService() = other.getBlockService();
        break;
    case ShuckleMessageKind::INSERT_STATS:
        setInsertStats() = other.getInsertStats();
        break;
    case ShuckleMessageKind::SHARD:
        setShard() = other.getShard();
        break;
    case ShuckleMessageKind::GET_STATS:
        setGetStats() = other.getGetStats();
        break;
    case ShuckleMessageKind::REGISTER_SHARD_REPLICA:
        setRegisterShardReplica() = other.getRegisterShardReplica();
        break;
    case ShuckleMessageKind::SHARD_REPLICAS:
        setShardReplicas() = other.getShardReplicas();
        break;
    case ShuckleMessageKind::SHARD_BLOCK_SERVICES:
        setShardBlockServices() = other.getShardBlockServices();
        break;
    case ShuckleMessageKind::REGISTER_CDC_REPLICA:
        setRegisterCdcReplica() = other.getRegisterCdcReplica();
        break;
    case ShuckleMessageKind::CDC_REPLICAS:
        setCdcReplicas() = other.getCdcReplicas();
        break;
    default:
        throw EGGS_EXCEPTION("bad ShuckleMessageKind kind %s", other.kind());
    }
}

void ShuckleReqContainer::operator=(ShuckleReqContainer&& other) {
    _data = std::move(other._data);
    _kind = other._kind;
    other._kind = (ShuckleMessageKind)0;
}

size_t ShuckleReqContainer::packedSize() const {
    switch (_kind) {
    case ShuckleMessageKind::SHARDS:
        return std::get<0>(_data).packedSize();
    case ShuckleMessageKind::CDC:
        return std::get<1>(_data).packedSize();
    case ShuckleMessageKind::INFO:
        return std::get<2>(_data).packedSize();
    case ShuckleMessageKind::SHUCKLE:
        return std::get<3>(_data).packedSize();
    case ShuckleMessageKind::REGISTER_BLOCK_SERVICES:
        return std::get<4>(_data).packedSize();
    case ShuckleMessageKind::REGISTER_SHARD:
        return std::get<5>(_data).packedSize();
    case ShuckleMessageKind::ALL_BLOCK_SERVICES:
        return std::get<6>(_data).packedSize();
    case ShuckleMessageKind::REGISTER_CDC:
        return std::get<7>(_data).packedSize();
    case ShuckleMessageKind::SET_BLOCK_SERVICE_FLAGS:
        return std::get<8>(_data).packedSize();
    case ShuckleMessageKind::BLOCK_SERVICE:
        return std::get<9>(_data).packedSize();
    case ShuckleMessageKind::INSERT_STATS:
        return std::get<10>(_data).packedSize();
    case ShuckleMessageKind::SHARD:
        return std::get<11>(_data).packedSize();
    case ShuckleMessageKind::GET_STATS:
        return std::get<12>(_data).packedSize();
    case ShuckleMessageKind::REGISTER_SHARD_REPLICA:
        return std::get<13>(_data).packedSize();
    case ShuckleMessageKind::SHARD_REPLICAS:
        return std::get<14>(_data).packedSize();
    case ShuckleMessageKind::SHARD_BLOCK_SERVICES:
        return std::get<15>(_data).packedSize();
    case ShuckleMessageKind::REGISTER_CDC_REPLICA:
        return std::get<16>(_data).packedSize();
    case ShuckleMessageKind::CDC_REPLICAS:
        return std::get<17>(_data).packedSize();
    default:
        throw EGGS_EXCEPTION("bad ShuckleMessageKind kind %s", _kind);
    }
}

void ShuckleReqContainer::pack(BincodeBuf& buf) const {
    switch (_kind) {
    case ShuckleMessageKind::SHARDS:
        std::get<0>(_data).pack(buf);
        break;
    case ShuckleMessageKind::CDC:
        std::get<1>(_data).pack(buf);
        break;
    case ShuckleMessageKind::INFO:
        std::get<2>(_data).pack(buf);
        break;
    case ShuckleMessageKind::SHUCKLE:
        std::get<3>(_data).pack(buf);
        break;
    case ShuckleMessageKind::REGISTER_BLOCK_SERVICES:
        std::get<4>(_data).pack(buf);
        break;
    case ShuckleMessageKind::REGISTER_SHARD:
        std::get<5>(_data).pack(buf);
        break;
    case ShuckleMessageKind::ALL_BLOCK_SERVICES:
        std::get<6>(_data).pack(buf);
        break;
    case ShuckleMessageKind::REGISTER_CDC:
        std::get<7>(_data).pack(buf);
        break;
    case ShuckleMessageKind::SET_BLOCK_SERVICE_FLAGS:
        std::get<8>(_data).pack(buf);
        break;
    case ShuckleMessageKind::BLOCK_SERVICE:
        std::get<9>(_data).pack(buf);
        break;
    case ShuckleMessageKind::INSERT_STATS:
        std::get<10>(_data).pack(buf);
        break;
    case ShuckleMessageKind::SHARD:
        std::get<11>(_data).pack(buf);
        break;
    case ShuckleMessageKind::GET_STATS:
        std::get<12>(_data).pack(buf);
        break;
    case ShuckleMessageKind::REGISTER_SHARD_REPLICA:
        std::get<13>(_data).pack(buf);
        break;
    case ShuckleMessageKind::SHARD_REPLICAS:
        std::get<14>(_data).pack(buf);
        break;
    case ShuckleMessageKind::SHARD_BLOCK_SERVICES:
        std::get<15>(_data).pack(buf);
        break;
    case ShuckleMessageKind::REGISTER_CDC_REPLICA:
        std::get<16>(_data).pack(buf);
        break;
    case ShuckleMessageKind::CDC_REPLICAS:
        std::get<17>(_data).pack(buf);
        break;
    default:
        throw EGGS_EXCEPTION("bad ShuckleMessageKind kind %s", _kind);
    }
}

void ShuckleReqContainer::unpack(BincodeBuf& buf, ShuckleMessageKind kind) {
    _kind = kind;
    switch (kind) {
    case ShuckleMessageKind::SHARDS:
        _data.emplace<0>().unpack(buf);
        break;
    case ShuckleMessageKind::CDC:
        _data.emplace<1>().unpack(buf);
        break;
    case ShuckleMessageKind::INFO:
        _data.emplace<2>().unpack(buf);
        break;
    case ShuckleMessageKind::SHUCKLE:
        _data.emplace<3>().unpack(buf);
        break;
    case ShuckleMessageKind::REGISTER_BLOCK_SERVICES:
        _data.emplace<4>().unpack(buf);
        break;
    case ShuckleMessageKind::REGISTER_SHARD:
        _data.emplace<5>().unpack(buf);
        break;
    case ShuckleMessageKind::ALL_BLOCK_SERVICES:
        _data.emplace<6>().unpack(buf);
        break;
    case ShuckleMessageKind::REGISTER_CDC:
        _data.emplace<7>().unpack(buf);
        break;
    case ShuckleMessageKind::SET_BLOCK_SERVICE_FLAGS:
        _data.emplace<8>().unpack(buf);
        break;
    case ShuckleMessageKind::BLOCK_SERVICE:
        _data.emplace<9>().unpack(buf);
        break;
    case ShuckleMessageKind::INSERT_STATS:
        _data.emplace<10>().unpack(buf);
        break;
    case ShuckleMessageKind::SHARD:
        _data.emplace<11>().unpack(buf);
        break;
    case ShuckleMessageKind::GET_STATS:
        _data.emplace<12>().unpack(buf);
        break;
    case ShuckleMessageKind::REGISTER_SHARD_REPLICA:
        _data.emplace<13>().unpack(buf);
        break;
    case ShuckleMessageKind::SHARD_REPLICAS:
        _data.emplace<14>().unpack(buf);
        break;
    case ShuckleMessageKind::SHARD_BLOCK_SERVICES:
        _data.emplace<15>().unpack(buf);
        break;
    case ShuckleMessageKind::REGISTER_CDC_REPLICA:
        _data.emplace<16>().unpack(buf);
        break;
    case ShuckleMessageKind::CDC_REPLICAS:
        _data.emplace<17>().unpack(buf);
        break;
    default:
        throw BINCODE_EXCEPTION("bad ShuckleMessageKind kind %s", kind);
    }
}

bool ShuckleReqContainer::operator==(const ShuckleReqContainer& other) const {
    if (_kind != other.kind()) { return false; }
    if (_kind == (ShuckleMessageKind)0) { return true; }
    switch (_kind) {
    case ShuckleMessageKind::SHARDS:
        return getShards() == other.getShards();
    case ShuckleMessageKind::CDC:
        return getCdc() == other.getCdc();
    case ShuckleMessageKind::INFO:
        return getInfo() == other.getInfo();
    case ShuckleMessageKind::SHUCKLE:
        return getShuckle() == other.getShuckle();
    case ShuckleMessageKind::REGISTER_BLOCK_SERVICES:
        return getRegisterBlockServices() == other.getRegisterBlockServices();
    case ShuckleMessageKind::REGISTER_SHARD:
        return getRegisterShard() == other.getRegisterShard();
    case ShuckleMessageKind::ALL_BLOCK_SERVICES:
        return getAllBlockServices() == other.getAllBlockServices();
    case ShuckleMessageKind::REGISTER_CDC:
        return getRegisterCdc() == other.getRegisterCdc();
    case ShuckleMessageKind::SET_BLOCK_SERVICE_FLAGS:
        return getSetBlockServiceFlags() == other.getSetBlockServiceFlags();
    case ShuckleMessageKind::BLOCK_SERVICE:
        return getBlockService() == other.getBlockService();
    case ShuckleMessageKind::INSERT_STATS:
        return getInsertStats() == other.getInsertStats();
    case ShuckleMessageKind::SHARD:
        return getShard() == other.getShard();
    case ShuckleMessageKind::GET_STATS:
        return getGetStats() == other.getGetStats();
    case ShuckleMessageKind::REGISTER_SHARD_REPLICA:
        return getRegisterShardReplica() == other.getRegisterShardReplica();
    case ShuckleMessageKind::SHARD_REPLICAS:
        return getShardReplicas() == other.getShardReplicas();
    case ShuckleMessageKind::SHARD_BLOCK_SERVICES:
        return getShardBlockServices() == other.getShardBlockServices();
    case ShuckleMessageKind::REGISTER_CDC_REPLICA:
        return getRegisterCdcReplica() == other.getRegisterCdcReplica();
    case ShuckleMessageKind::CDC_REPLICAS:
        return getCdcReplicas() == other.getCdcReplicas();
    default:
        throw BINCODE_EXCEPTION("bad ShuckleMessageKind kind %s", _kind);
    }
}

std::ostream& operator<<(std::ostream& out, const ShuckleReqContainer& x) {
    switch (x.kind()) {
    case ShuckleMessageKind::SHARDS:
        out << x.getShards();
        break;
    case ShuckleMessageKind::CDC:
        out << x.getCdc();
        break;
    case ShuckleMessageKind::INFO:
        out << x.getInfo();
        break;
    case ShuckleMessageKind::SHUCKLE:
        out << x.getShuckle();
        break;
    case ShuckleMessageKind::REGISTER_BLOCK_SERVICES:
        out << x.getRegisterBlockServices();
        break;
    case ShuckleMessageKind::REGISTER_SHARD:
        out << x.getRegisterShard();
        break;
    case ShuckleMessageKind::ALL_BLOCK_SERVICES:
        out << x.getAllBlockServices();
        break;
    case ShuckleMessageKind::REGISTER_CDC:
        out << x.getRegisterCdc();
        break;
    case ShuckleMessageKind::SET_BLOCK_SERVICE_FLAGS:
        out << x.getSetBlockServiceFlags();
        break;
    case ShuckleMessageKind::BLOCK_SERVICE:
        out << x.getBlockService();
        break;
    case ShuckleMessageKind::INSERT_STATS:
        out << x.getInsertStats();
        break;
    case ShuckleMessageKind::SHARD:
        out << x.getShard();
        break;
    case ShuckleMessageKind::GET_STATS:
        out << x.getGetStats();
        break;
    case ShuckleMessageKind::REGISTER_SHARD_REPLICA:
        out << x.getRegisterShardReplica();
        break;
    case ShuckleMessageKind::SHARD_REPLICAS:
        out << x.getShardReplicas();
        break;
    case ShuckleMessageKind::SHARD_BLOCK_SERVICES:
        out << x.getShardBlockServices();
        break;
    case ShuckleMessageKind::REGISTER_CDC_REPLICA:
        out << x.getRegisterCdcReplica();
        break;
    case ShuckleMessageKind::CDC_REPLICAS:
        out << x.getCdcReplicas();
        break;
    default:
        throw EGGS_EXCEPTION("bad ShuckleMessageKind kind %s", x.kind());
    }
    return out;
}

const ShardsResp& ShuckleRespContainer::getShards() const {
    ALWAYS_ASSERT(_kind == ShuckleMessageKind::SHARDS, "%s != %s", _kind, ShuckleMessageKind::SHARDS);
    return std::get<0>(_data);
}
ShardsResp& ShuckleRespContainer::setShards() {
    _kind = ShuckleMessageKind::SHARDS;
    auto& x = _data.emplace<0>();
    return x;
}
const CdcResp& ShuckleRespContainer::getCdc() const {
    ALWAYS_ASSERT(_kind == ShuckleMessageKind::CDC, "%s != %s", _kind, ShuckleMessageKind::CDC);
    return std::get<1>(_data);
}
CdcResp& ShuckleRespContainer::setCdc() {
    _kind = ShuckleMessageKind::CDC;
    auto& x = _data.emplace<1>();
    return x;
}
const InfoResp& ShuckleRespContainer::getInfo() const {
    ALWAYS_ASSERT(_kind == ShuckleMessageKind::INFO, "%s != %s", _kind, ShuckleMessageKind::INFO);
    return std::get<2>(_data);
}
InfoResp& ShuckleRespContainer::setInfo() {
    _kind = ShuckleMessageKind::INFO;
    auto& x = _data.emplace<2>();
    return x;
}
const ShuckleResp& ShuckleRespContainer::getShuckle() const {
    ALWAYS_ASSERT(_kind == ShuckleMessageKind::SHUCKLE, "%s != %s", _kind, ShuckleMessageKind::SHUCKLE);
    return std::get<3>(_data);
}
ShuckleResp& ShuckleRespContainer::setShuckle() {
    _kind = ShuckleMessageKind::SHUCKLE;
    auto& x = _data.emplace<3>();
    return x;
}
const RegisterBlockServicesResp& ShuckleRespContainer::getRegisterBlockServices() const {
    ALWAYS_ASSERT(_kind == ShuckleMessageKind::REGISTER_BLOCK_SERVICES, "%s != %s", _kind, ShuckleMessageKind::REGISTER_BLOCK_SERVICES);
    return std::get<4>(_data);
}
RegisterBlockServicesResp& ShuckleRespContainer::setRegisterBlockServices() {
    _kind = ShuckleMessageKind::REGISTER_BLOCK_SERVICES;
    auto& x = _data.emplace<4>();
    return x;
}
const RegisterShardResp& ShuckleRespContainer::getRegisterShard() const {
    ALWAYS_ASSERT(_kind == ShuckleMessageKind::REGISTER_SHARD, "%s != %s", _kind, ShuckleMessageKind::REGISTER_SHARD);
    return std::get<5>(_data);
}
RegisterShardResp& ShuckleRespContainer::setRegisterShard() {
    _kind = ShuckleMessageKind::REGISTER_SHARD;
    auto& x = _data.emplace<5>();
    return x;
}
const AllBlockServicesResp& ShuckleRespContainer::getAllBlockServices() const {
    ALWAYS_ASSERT(_kind == ShuckleMessageKind::ALL_BLOCK_SERVICES, "%s != %s", _kind, ShuckleMessageKind::ALL_BLOCK_SERVICES);
    return std::get<6>(_data);
}
AllBlockServicesResp& ShuckleRespContainer::setAllBlockServices() {
    _kind = ShuckleMessageKind::ALL_BLOCK_SERVICES;
    auto& x = _data.emplace<6>();
    return x;
}
const RegisterCdcResp& ShuckleRespContainer::getRegisterCdc() const {
    ALWAYS_ASSERT(_kind == ShuckleMessageKind::REGISTER_CDC, "%s != %s", _kind, ShuckleMessageKind::REGISTER_CDC);
    return std::get<7>(_data);
}
RegisterCdcResp& ShuckleRespContainer::setRegisterCdc() {
    _kind = ShuckleMessageKind::REGISTER_CDC;
    auto& x = _data.emplace<7>();
    return x;
}
const SetBlockServiceFlagsResp& ShuckleRespContainer::getSetBlockServiceFlags() const {
    ALWAYS_ASSERT(_kind == ShuckleMessageKind::SET_BLOCK_SERVICE_FLAGS, "%s != %s", _kind, ShuckleMessageKind::SET_BLOCK_SERVICE_FLAGS);
    return std::get<8>(_data);
}
SetBlockServiceFlagsResp& ShuckleRespContainer::setSetBlockServiceFlags() {
    _kind = ShuckleMessageKind::SET_BLOCK_SERVICE_FLAGS;
    auto& x = _data.emplace<8>();
    return x;
}
const BlockServiceResp& ShuckleRespContainer::getBlockService() const {
    ALWAYS_ASSERT(_kind == ShuckleMessageKind::BLOCK_SERVICE, "%s != %s", _kind, ShuckleMessageKind::BLOCK_SERVICE);
    return std::get<9>(_data);
}
BlockServiceResp& ShuckleRespContainer::setBlockService() {
    _kind = ShuckleMessageKind::BLOCK_SERVICE;
    auto& x = _data.emplace<9>();
    return x;
}
const InsertStatsResp& ShuckleRespContainer::getInsertStats() const {
    ALWAYS_ASSERT(_kind == ShuckleMessageKind::INSERT_STATS, "%s != %s", _kind, ShuckleMessageKind::INSERT_STATS);
    return std::get<10>(_data);
}
InsertStatsResp& ShuckleRespContainer::setInsertStats() {
    _kind = ShuckleMessageKind::INSERT_STATS;
    auto& x = _data.emplace<10>();
    return x;
}
const ShardResp& ShuckleRespContainer::getShard() const {
    ALWAYS_ASSERT(_kind == ShuckleMessageKind::SHARD, "%s != %s", _kind, ShuckleMessageKind::SHARD);
    return std::get<11>(_data);
}
ShardResp& ShuckleRespContainer::setShard() {
    _kind = ShuckleMessageKind::SHARD;
    auto& x = _data.emplace<11>();
    return x;
}
const GetStatsResp& ShuckleRespContainer::getGetStats() const {
    ALWAYS_ASSERT(_kind == ShuckleMessageKind::GET_STATS, "%s != %s", _kind, ShuckleMessageKind::GET_STATS);
    return std::get<12>(_data);
}
GetStatsResp& ShuckleRespContainer::setGetStats() {
    _kind = ShuckleMessageKind::GET_STATS;
    auto& x = _data.emplace<12>();
    return x;
}
const RegisterShardReplicaResp& ShuckleRespContainer::getRegisterShardReplica() const {
    ALWAYS_ASSERT(_kind == ShuckleMessageKind::REGISTER_SHARD_REPLICA, "%s != %s", _kind, ShuckleMessageKind::REGISTER_SHARD_REPLICA);
    return std::get<13>(_data);
}
RegisterShardReplicaResp& ShuckleRespContainer::setRegisterShardReplica() {
    _kind = ShuckleMessageKind::REGISTER_SHARD_REPLICA;
    auto& x = _data.emplace<13>();
    return x;
}
const ShardReplicasResp& ShuckleRespContainer::getShardReplicas() const {
    ALWAYS_ASSERT(_kind == ShuckleMessageKind::SHARD_REPLICAS, "%s != %s", _kind, ShuckleMessageKind::SHARD_REPLICAS);
    return std::get<14>(_data);
}
ShardReplicasResp& ShuckleRespContainer::setShardReplicas() {
    _kind = ShuckleMessageKind::SHARD_REPLICAS;
    auto& x = _data.emplace<14>();
    return x;
}
const ShardBlockServicesResp& ShuckleRespContainer::getShardBlockServices() const {
    ALWAYS_ASSERT(_kind == ShuckleMessageKind::SHARD_BLOCK_SERVICES, "%s != %s", _kind, ShuckleMessageKind::SHARD_BLOCK_SERVICES);
    return std::get<15>(_data);
}
ShardBlockServicesResp& ShuckleRespContainer::setShardBlockServices() {
    _kind = ShuckleMessageKind::SHARD_BLOCK_SERVICES;
    auto& x = _data.emplace<15>();
    return x;
}
const RegisterCdcReplicaResp& ShuckleRespContainer::getRegisterCdcReplica() const {
    ALWAYS_ASSERT(_kind == ShuckleMessageKind::REGISTER_CDC_REPLICA, "%s != %s", _kind, ShuckleMessageKind::REGISTER_CDC_REPLICA);
    return std::get<16>(_data);
}
RegisterCdcReplicaResp& ShuckleRespContainer::setRegisterCdcReplica() {
    _kind = ShuckleMessageKind::REGISTER_CDC_REPLICA;
    auto& x = _data.emplace<16>();
    return x;
}
const CdcReplicasResp& ShuckleRespContainer::getCdcReplicas() const {
    ALWAYS_ASSERT(_kind == ShuckleMessageKind::CDC_REPLICAS, "%s != %s", _kind, ShuckleMessageKind::CDC_REPLICAS);
    return std::get<17>(_data);
}
CdcReplicasResp& ShuckleRespContainer::setCdcReplicas() {
    _kind = ShuckleMessageKind::CDC_REPLICAS;
    auto& x = _data.emplace<17>();
    return x;
}
ShuckleRespContainer::ShuckleRespContainer() {
    clear();
}

ShuckleRespContainer::ShuckleRespContainer(const ShuckleRespContainer& other) {
    *this = other;
}

ShuckleRespContainer::ShuckleRespContainer(ShuckleRespContainer&& other) {
    _data = std::move(other._data);
    _kind = other._kind;
    other._kind = (ShuckleMessageKind)0;
}

void ShuckleRespContainer::operator=(const ShuckleRespContainer& other) {
    if (other.kind() == (ShuckleMessageKind)0) { clear(); return; }
    switch (other.kind()) {
    case ShuckleMessageKind::SHARDS:
        setShards() = other.getShards();
        break;
    case ShuckleMessageKind::CDC:
        setCdc() = other.getCdc();
        break;
    case ShuckleMessageKind::INFO:
        setInfo() = other.getInfo();
        break;
    case ShuckleMessageKind::SHUCKLE:
        setShuckle() = other.getShuckle();
        break;
    case ShuckleMessageKind::REGISTER_BLOCK_SERVICES:
        setRegisterBlockServices() = other.getRegisterBlockServices();
        break;
    case ShuckleMessageKind::REGISTER_SHARD:
        setRegisterShard() = other.getRegisterShard();
        break;
    case ShuckleMessageKind::ALL_BLOCK_SERVICES:
        setAllBlockServices() = other.getAllBlockServices();
        break;
    case ShuckleMessageKind::REGISTER_CDC:
        setRegisterCdc() = other.getRegisterCdc();
        break;
    case ShuckleMessageKind::SET_BLOCK_SERVICE_FLAGS:
        setSetBlockServiceFlags() = other.getSetBlockServiceFlags();
        break;
    case ShuckleMessageKind::BLOCK_SERVICE:
        setBlockService() = other.getBlockService();
        break;
    case ShuckleMessageKind::INSERT_STATS:
        setInsertStats() = other.getInsertStats();
        break;
    case ShuckleMessageKind::SHARD:
        setShard() = other.getShard();
        break;
    case ShuckleMessageKind::GET_STATS:
        setGetStats() = other.getGetStats();
        break;
    case ShuckleMessageKind::REGISTER_SHARD_REPLICA:
        setRegisterShardReplica() = other.getRegisterShardReplica();
        break;
    case ShuckleMessageKind::SHARD_REPLICAS:
        setShardReplicas() = other.getShardReplicas();
        break;
    case ShuckleMessageKind::SHARD_BLOCK_SERVICES:
        setShardBlockServices() = other.getShardBlockServices();
        break;
    case ShuckleMessageKind::REGISTER_CDC_REPLICA:
        setRegisterCdcReplica() = other.getRegisterCdcReplica();
        break;
    case ShuckleMessageKind::CDC_REPLICAS:
        setCdcReplicas() = other.getCdcReplicas();
        break;
    default:
        throw EGGS_EXCEPTION("bad ShuckleMessageKind kind %s", other.kind());
    }
}

void ShuckleRespContainer::operator=(ShuckleRespContainer&& other) {
    _data = std::move(other._data);
    _kind = other._kind;
    other._kind = (ShuckleMessageKind)0;
}

size_t ShuckleRespContainer::packedSize() const {
    switch (_kind) {
    case ShuckleMessageKind::SHARDS:
        return std::get<0>(_data).packedSize();
    case ShuckleMessageKind::CDC:
        return std::get<1>(_data).packedSize();
    case ShuckleMessageKind::INFO:
        return std::get<2>(_data).packedSize();
    case ShuckleMessageKind::SHUCKLE:
        return std::get<3>(_data).packedSize();
    case ShuckleMessageKind::REGISTER_BLOCK_SERVICES:
        return std::get<4>(_data).packedSize();
    case ShuckleMessageKind::REGISTER_SHARD:
        return std::get<5>(_data).packedSize();
    case ShuckleMessageKind::ALL_BLOCK_SERVICES:
        return std::get<6>(_data).packedSize();
    case ShuckleMessageKind::REGISTER_CDC:
        return std::get<7>(_data).packedSize();
    case ShuckleMessageKind::SET_BLOCK_SERVICE_FLAGS:
        return std::get<8>(_data).packedSize();
    case ShuckleMessageKind::BLOCK_SERVICE:
        return std::get<9>(_data).packedSize();
    case ShuckleMessageKind::INSERT_STATS:
        return std::get<10>(_data).packedSize();
    case ShuckleMessageKind::SHARD:
        return std::get<11>(_data).packedSize();
    case ShuckleMessageKind::GET_STATS:
        return std::get<12>(_data).packedSize();
    case ShuckleMessageKind::REGISTER_SHARD_REPLICA:
        return std::get<13>(_data).packedSize();
    case ShuckleMessageKind::SHARD_REPLICAS:
        return std::get<14>(_data).packedSize();
    case ShuckleMessageKind::SHARD_BLOCK_SERVICES:
        return std::get<15>(_data).packedSize();
    case ShuckleMessageKind::REGISTER_CDC_REPLICA:
        return std::get<16>(_data).packedSize();
    case ShuckleMessageKind::CDC_REPLICAS:
        return std::get<17>(_data).packedSize();
    default:
        throw EGGS_EXCEPTION("bad ShuckleMessageKind kind %s", _kind);
    }
}

void ShuckleRespContainer::pack(BincodeBuf& buf) const {
    switch (_kind) {
    case ShuckleMessageKind::SHARDS:
        std::get<0>(_data).pack(buf);
        break;
    case ShuckleMessageKind::CDC:
        std::get<1>(_data).pack(buf);
        break;
    case ShuckleMessageKind::INFO:
        std::get<2>(_data).pack(buf);
        break;
    case ShuckleMessageKind::SHUCKLE:
        std::get<3>(_data).pack(buf);
        break;
    case ShuckleMessageKind::REGISTER_BLOCK_SERVICES:
        std::get<4>(_data).pack(buf);
        break;
    case ShuckleMessageKind::REGISTER_SHARD:
        std::get<5>(_data).pack(buf);
        break;
    case ShuckleMessageKind::ALL_BLOCK_SERVICES:
        std::get<6>(_data).pack(buf);
        break;
    case ShuckleMessageKind::REGISTER_CDC:
        std::get<7>(_data).pack(buf);
        break;
    case ShuckleMessageKind::SET_BLOCK_SERVICE_FLAGS:
        std::get<8>(_data).pack(buf);
        break;
    case ShuckleMessageKind::BLOCK_SERVICE:
        std::get<9>(_data).pack(buf);
        break;
    case ShuckleMessageKind::INSERT_STATS:
        std::get<10>(_data).pack(buf);
        break;
    case ShuckleMessageKind::SHARD:
        std::get<11>(_data).pack(buf);
        break;
    case ShuckleMessageKind::GET_STATS:
        std::get<12>(_data).pack(buf);
        break;
    case ShuckleMessageKind::REGISTER_SHARD_REPLICA:
        std::get<13>(_data).pack(buf);
        break;
    case ShuckleMessageKind::SHARD_REPLICAS:
        std::get<14>(_data).pack(buf);
        break;
    case ShuckleMessageKind::SHARD_BLOCK_SERVICES:
        std::get<15>(_data).pack(buf);
        break;
    case ShuckleMessageKind::REGISTER_CDC_REPLICA:
        std::get<16>(_data).pack(buf);
        break;
    case ShuckleMessageKind::CDC_REPLICAS:
        std::get<17>(_data).pack(buf);
        break;
    default:
        throw EGGS_EXCEPTION("bad ShuckleMessageKind kind %s", _kind);
    }
}

void ShuckleRespContainer::unpack(BincodeBuf& buf, ShuckleMessageKind kind) {
    _kind = kind;
    switch (kind) {
    case ShuckleMessageKind::SHARDS:
        _data.emplace<0>().unpack(buf);
        break;
    case ShuckleMessageKind::CDC:
        _data.emplace<1>().unpack(buf);
        break;
    case ShuckleMessageKind::INFO:
        _data.emplace<2>().unpack(buf);
        break;
    case ShuckleMessageKind::SHUCKLE:
        _data.emplace<3>().unpack(buf);
        break;
    case ShuckleMessageKind::REGISTER_BLOCK_SERVICES:
        _data.emplace<4>().unpack(buf);
        break;
    case ShuckleMessageKind::REGISTER_SHARD:
        _data.emplace<5>().unpack(buf);
        break;
    case ShuckleMessageKind::ALL_BLOCK_SERVICES:
        _data.emplace<6>().unpack(buf);
        break;
    case ShuckleMessageKind::REGISTER_CDC:
        _data.emplace<7>().unpack(buf);
        break;
    case ShuckleMessageKind::SET_BLOCK_SERVICE_FLAGS:
        _data.emplace<8>().unpack(buf);
        break;
    case ShuckleMessageKind::BLOCK_SERVICE:
        _data.emplace<9>().unpack(buf);
        break;
    case ShuckleMessageKind::INSERT_STATS:
        _data.emplace<10>().unpack(buf);
        break;
    case ShuckleMessageKind::SHARD:
        _data.emplace<11>().unpack(buf);
        break;
    case ShuckleMessageKind::GET_STATS:
        _data.emplace<12>().unpack(buf);
        break;
    case ShuckleMessageKind::REGISTER_SHARD_REPLICA:
        _data.emplace<13>().unpack(buf);
        break;
    case ShuckleMessageKind::SHARD_REPLICAS:
        _data.emplace<14>().unpack(buf);
        break;
    case ShuckleMessageKind::SHARD_BLOCK_SERVICES:
        _data.emplace<15>().unpack(buf);
        break;
    case ShuckleMessageKind::REGISTER_CDC_REPLICA:
        _data.emplace<16>().unpack(buf);
        break;
    case ShuckleMessageKind::CDC_REPLICAS:
        _data.emplace<17>().unpack(buf);
        break;
    default:
        throw BINCODE_EXCEPTION("bad ShuckleMessageKind kind %s", kind);
    }
}

bool ShuckleRespContainer::operator==(const ShuckleRespContainer& other) const {
    if (_kind != other.kind()) { return false; }
    if (_kind == (ShuckleMessageKind)0) { return true; }
    switch (_kind) {
    case ShuckleMessageKind::SHARDS:
        return getShards() == other.getShards();
    case ShuckleMessageKind::CDC:
        return getCdc() == other.getCdc();
    case ShuckleMessageKind::INFO:
        return getInfo() == other.getInfo();
    case ShuckleMessageKind::SHUCKLE:
        return getShuckle() == other.getShuckle();
    case ShuckleMessageKind::REGISTER_BLOCK_SERVICES:
        return getRegisterBlockServices() == other.getRegisterBlockServices();
    case ShuckleMessageKind::REGISTER_SHARD:
        return getRegisterShard() == other.getRegisterShard();
    case ShuckleMessageKind::ALL_BLOCK_SERVICES:
        return getAllBlockServices() == other.getAllBlockServices();
    case ShuckleMessageKind::REGISTER_CDC:
        return getRegisterCdc() == other.getRegisterCdc();
    case ShuckleMessageKind::SET_BLOCK_SERVICE_FLAGS:
        return getSetBlockServiceFlags() == other.getSetBlockServiceFlags();
    case ShuckleMessageKind::BLOCK_SERVICE:
        return getBlockService() == other.getBlockService();
    case ShuckleMessageKind::INSERT_STATS:
        return getInsertStats() == other.getInsertStats();
    case ShuckleMessageKind::SHARD:
        return getShard() == other.getShard();
    case ShuckleMessageKind::GET_STATS:
        return getGetStats() == other.getGetStats();
    case ShuckleMessageKind::REGISTER_SHARD_REPLICA:
        return getRegisterShardReplica() == other.getRegisterShardReplica();
    case ShuckleMessageKind::SHARD_REPLICAS:
        return getShardReplicas() == other.getShardReplicas();
    case ShuckleMessageKind::SHARD_BLOCK_SERVICES:
        return getShardBlockServices() == other.getShardBlockServices();
    case ShuckleMessageKind::REGISTER_CDC_REPLICA:
        return getRegisterCdcReplica() == other.getRegisterCdcReplica();
    case ShuckleMessageKind::CDC_REPLICAS:
        return getCdcReplicas() == other.getCdcReplicas();
    default:
        throw BINCODE_EXCEPTION("bad ShuckleMessageKind kind %s", _kind);
    }
}

std::ostream& operator<<(std::ostream& out, const ShuckleRespContainer& x) {
    switch (x.kind()) {
    case ShuckleMessageKind::SHARDS:
        out << x.getShards();
        break;
    case ShuckleMessageKind::CDC:
        out << x.getCdc();
        break;
    case ShuckleMessageKind::INFO:
        out << x.getInfo();
        break;
    case ShuckleMessageKind::SHUCKLE:
        out << x.getShuckle();
        break;
    case ShuckleMessageKind::REGISTER_BLOCK_SERVICES:
        out << x.getRegisterBlockServices();
        break;
    case ShuckleMessageKind::REGISTER_SHARD:
        out << x.getRegisterShard();
        break;
    case ShuckleMessageKind::ALL_BLOCK_SERVICES:
        out << x.getAllBlockServices();
        break;
    case ShuckleMessageKind::REGISTER_CDC:
        out << x.getRegisterCdc();
        break;
    case ShuckleMessageKind::SET_BLOCK_SERVICE_FLAGS:
        out << x.getSetBlockServiceFlags();
        break;
    case ShuckleMessageKind::BLOCK_SERVICE:
        out << x.getBlockService();
        break;
    case ShuckleMessageKind::INSERT_STATS:
        out << x.getInsertStats();
        break;
    case ShuckleMessageKind::SHARD:
        out << x.getShard();
        break;
    case ShuckleMessageKind::GET_STATS:
        out << x.getGetStats();
        break;
    case ShuckleMessageKind::REGISTER_SHARD_REPLICA:
        out << x.getRegisterShardReplica();
        break;
    case ShuckleMessageKind::SHARD_REPLICAS:
        out << x.getShardReplicas();
        break;
    case ShuckleMessageKind::SHARD_BLOCK_SERVICES:
        out << x.getShardBlockServices();
        break;
    case ShuckleMessageKind::REGISTER_CDC_REPLICA:
        out << x.getRegisterCdcReplica();
        break;
    case ShuckleMessageKind::CDC_REPLICAS:
        out << x.getCdcReplicas();
        break;
    default:
        throw EGGS_EXCEPTION("bad ShuckleMessageKind kind %s", x.kind());
    }
    return out;
}

std::ostream& operator<<(std::ostream& out, ShardLogEntryKind err) {
    switch (err) {
    case ShardLogEntryKind::CONSTRUCT_FILE:
        out << "CONSTRUCT_FILE";
        break;
    case ShardLogEntryKind::LINK_FILE:
        out << "LINK_FILE";
        break;
    case ShardLogEntryKind::SAME_DIRECTORY_RENAME:
        out << "SAME_DIRECTORY_RENAME";
        break;
    case ShardLogEntryKind::SOFT_UNLINK_FILE:
        out << "SOFT_UNLINK_FILE";
        break;
    case ShardLogEntryKind::CREATE_DIRECTORY_INODE:
        out << "CREATE_DIRECTORY_INODE";
        break;
    case ShardLogEntryKind::CREATE_LOCKED_CURRENT_EDGE:
        out << "CREATE_LOCKED_CURRENT_EDGE";
        break;
    case ShardLogEntryKind::UNLOCK_CURRENT_EDGE:
        out << "UNLOCK_CURRENT_EDGE";
        break;
    case ShardLogEntryKind::LOCK_CURRENT_EDGE:
        out << "LOCK_CURRENT_EDGE";
        break;
    case ShardLogEntryKind::REMOVE_DIRECTORY_OWNER:
        out << "REMOVE_DIRECTORY_OWNER";
        break;
    case ShardLogEntryKind::REMOVE_INODE:
        out << "REMOVE_INODE";
        break;
    case ShardLogEntryKind::SET_DIRECTORY_OWNER:
        out << "SET_DIRECTORY_OWNER";
        break;
    case ShardLogEntryKind::SET_DIRECTORY_INFO:
        out << "SET_DIRECTORY_INFO";
        break;
    case ShardLogEntryKind::REMOVE_NON_OWNED_EDGE:
        out << "REMOVE_NON_OWNED_EDGE";
        break;
    case ShardLogEntryKind::SAME_SHARD_HARD_FILE_UNLINK:
        out << "SAME_SHARD_HARD_FILE_UNLINK";
        break;
    case ShardLogEntryKind::REMOVE_SPAN_INITIATE:
        out << "REMOVE_SPAN_INITIATE";
        break;
    case ShardLogEntryKind::UPDATE_BLOCK_SERVICES:
        out << "UPDATE_BLOCK_SERVICES";
        break;
    case ShardLogEntryKind::ADD_SPAN_INITIATE:
        out << "ADD_SPAN_INITIATE";
        break;
    case ShardLogEntryKind::ADD_SPAN_CERTIFY:
        out << "ADD_SPAN_CERTIFY";
        break;
    case ShardLogEntryKind::ADD_INLINE_SPAN:
        out << "ADD_INLINE_SPAN";
        break;
    case ShardLogEntryKind::MAKE_FILE_TRANSIENT:
        out << "MAKE_FILE_TRANSIENT";
        break;
    case ShardLogEntryKind::REMOVE_SPAN_CERTIFY:
        out << "REMOVE_SPAN_CERTIFY";
        break;
    case ShardLogEntryKind::REMOVE_OWNED_SNAPSHOT_FILE_EDGE:
        out << "REMOVE_OWNED_SNAPSHOT_FILE_EDGE";
        break;
    case ShardLogEntryKind::SWAP_BLOCKS:
        out << "SWAP_BLOCKS";
        break;
    case ShardLogEntryKind::MOVE_SPAN:
        out << "MOVE_SPAN";
        break;
    case ShardLogEntryKind::SET_TIME:
        out << "SET_TIME";
        break;
    case ShardLogEntryKind::REMOVE_ZERO_BLOCK_SERVICE_FILES:
        out << "REMOVE_ZERO_BLOCK_SERVICE_FILES";
        break;
    default:
        out << "ShardLogEntryKind(" << ((int)err) << ")";
        break;
    }
    return out;
}

void ConstructFileEntry::pack(BincodeBuf& buf) const {
    buf.packScalar<uint8_t>(type);
    deadlineTime.pack(buf);
    buf.packBytes(note);
}
void ConstructFileEntry::unpack(BincodeBuf& buf) {
    type = buf.unpackScalar<uint8_t>();
    deadlineTime.unpack(buf);
    buf.unpackBytes(note);
}
void ConstructFileEntry::clear() {
    type = uint8_t(0);
    deadlineTime = EggsTime();
    note.clear();
}
bool ConstructFileEntry::operator==(const ConstructFileEntry& rhs) const {
    if ((uint8_t)this->type != (uint8_t)rhs.type) { return false; };
    if ((EggsTime)this->deadlineTime != (EggsTime)rhs.deadlineTime) { return false; };
    if (note != rhs.note) { return false; };
    return true;
}
std::ostream& operator<<(std::ostream& out, const ConstructFileEntry& x) {
    out << "ConstructFileEntry(" << "Type=" << (int)x.type << ", " << "DeadlineTime=" << x.deadlineTime << ", " << "Note=" << GoLangQuotedStringFmt(x.note.data(), x.note.size()) << ")";
    return out;
}

void LinkFileEntry::pack(BincodeBuf& buf) const {
    fileId.pack(buf);
    ownerId.pack(buf);
    buf.packBytes(name);
}
void LinkFileEntry::unpack(BincodeBuf& buf) {
    fileId.unpack(buf);
    ownerId.unpack(buf);
    buf.unpackBytes(name);
}
void LinkFileEntry::clear() {
    fileId = InodeId();
    ownerId = InodeId();
    name.clear();
}
bool LinkFileEntry::operator==(const LinkFileEntry& rhs) const {
    if ((InodeId)this->fileId != (InodeId)rhs.fileId) { return false; };
    if ((InodeId)this->ownerId != (InodeId)rhs.ownerId) { return false; };
    if (name != rhs.name) { return false; };
    return true;
}
std::ostream& operator<<(std::ostream& out, const LinkFileEntry& x) {
    out << "LinkFileEntry(" << "FileId=" << x.fileId << ", " << "OwnerId=" << x.ownerId << ", " << "Name=" << GoLangQuotedStringFmt(x.name.data(), x.name.size()) << ")";
    return out;
}

void SameDirectoryRenameEntry::pack(BincodeBuf& buf) const {
    dirId.pack(buf);
    targetId.pack(buf);
    buf.packBytes(oldName);
    oldCreationTime.pack(buf);
    buf.packBytes(newName);
}
void SameDirectoryRenameEntry::unpack(BincodeBuf& buf) {
    dirId.unpack(buf);
    targetId.unpack(buf);
    buf.unpackBytes(oldName);
    oldCreationTime.unpack(buf);
    buf.unpackBytes(newName);
}
void SameDirectoryRenameEntry::clear() {
    dirId = InodeId();
    targetId = InodeId();
    oldName.clear();
    oldCreationTime = EggsTime();
    newName.clear();
}
bool SameDirectoryRenameEntry::operator==(const SameDirectoryRenameEntry& rhs) const {
    if ((InodeId)this->dirId != (InodeId)rhs.dirId) { return false; };
    if ((InodeId)this->targetId != (InodeId)rhs.targetId) { return false; };
    if (oldName != rhs.oldName) { return false; };
    if ((EggsTime)this->oldCreationTime != (EggsTime)rhs.oldCreationTime) { return false; };
    if (newName != rhs.newName) { return false; };
    return true;
}
std::ostream& operator<<(std::ostream& out, const SameDirectoryRenameEntry& x) {
    out << "SameDirectoryRenameEntry(" << "DirId=" << x.dirId << ", " << "TargetId=" << x.targetId << ", " << "OldName=" << GoLangQuotedStringFmt(x.oldName.data(), x.oldName.size()) << ", " << "OldCreationTime=" << x.oldCreationTime << ", " << "NewName=" << GoLangQuotedStringFmt(x.newName.data(), x.newName.size()) << ")";
    return out;
}

void SoftUnlinkFileEntry::pack(BincodeBuf& buf) const {
    ownerId.pack(buf);
    fileId.pack(buf);
    buf.packBytes(name);
    creationTime.pack(buf);
}
void SoftUnlinkFileEntry::unpack(BincodeBuf& buf) {
    ownerId.unpack(buf);
    fileId.unpack(buf);
    buf.unpackBytes(name);
    creationTime.unpack(buf);
}
void SoftUnlinkFileEntry::clear() {
    ownerId = InodeId();
    fileId = InodeId();
    name.clear();
    creationTime = EggsTime();
}
bool SoftUnlinkFileEntry::operator==(const SoftUnlinkFileEntry& rhs) const {
    if ((InodeId)this->ownerId != (InodeId)rhs.ownerId) { return false; };
    if ((InodeId)this->fileId != (InodeId)rhs.fileId) { return false; };
    if (name != rhs.name) { return false; };
    if ((EggsTime)this->creationTime != (EggsTime)rhs.creationTime) { return false; };
    return true;
}
std::ostream& operator<<(std::ostream& out, const SoftUnlinkFileEntry& x) {
    out << "SoftUnlinkFileEntry(" << "OwnerId=" << x.ownerId << ", " << "FileId=" << x.fileId << ", " << "Name=" << GoLangQuotedStringFmt(x.name.data(), x.name.size()) << ", " << "CreationTime=" << x.creationTime << ")";
    return out;
}

void CreateDirectoryInodeEntry::pack(BincodeBuf& buf) const {
    id.pack(buf);
    ownerId.pack(buf);
    info.pack(buf);
}
void CreateDirectoryInodeEntry::unpack(BincodeBuf& buf) {
    id.unpack(buf);
    ownerId.unpack(buf);
    info.unpack(buf);
}
void CreateDirectoryInodeEntry::clear() {
    id = InodeId();
    ownerId = InodeId();
    info.clear();
}
bool CreateDirectoryInodeEntry::operator==(const CreateDirectoryInodeEntry& rhs) const {
    if ((InodeId)this->id != (InodeId)rhs.id) { return false; };
    if ((InodeId)this->ownerId != (InodeId)rhs.ownerId) { return false; };
    if (info != rhs.info) { return false; };
    return true;
}
std::ostream& operator<<(std::ostream& out, const CreateDirectoryInodeEntry& x) {
    out << "CreateDirectoryInodeEntry(" << "Id=" << x.id << ", " << "OwnerId=" << x.ownerId << ", " << "Info=" << x.info << ")";
    return out;
}

void CreateLockedCurrentEdgeEntry::pack(BincodeBuf& buf) const {
    dirId.pack(buf);
    buf.packBytes(name);
    targetId.pack(buf);
    oldCreationTime.pack(buf);
}
void CreateLockedCurrentEdgeEntry::unpack(BincodeBuf& buf) {
    dirId.unpack(buf);
    buf.unpackBytes(name);
    targetId.unpack(buf);
    oldCreationTime.unpack(buf);
}
void CreateLockedCurrentEdgeEntry::clear() {
    dirId = InodeId();
    name.clear();
    targetId = InodeId();
    oldCreationTime = EggsTime();
}
bool CreateLockedCurrentEdgeEntry::operator==(const CreateLockedCurrentEdgeEntry& rhs) const {
    if ((InodeId)this->dirId != (InodeId)rhs.dirId) { return false; };
    if (name != rhs.name) { return false; };
    if ((InodeId)this->targetId != (InodeId)rhs.targetId) { return false; };
    if ((EggsTime)this->oldCreationTime != (EggsTime)rhs.oldCreationTime) { return false; };
    return true;
}
std::ostream& operator<<(std::ostream& out, const CreateLockedCurrentEdgeEntry& x) {
    out << "CreateLockedCurrentEdgeEntry(" << "DirId=" << x.dirId << ", " << "Name=" << GoLangQuotedStringFmt(x.name.data(), x.name.size()) << ", " << "TargetId=" << x.targetId << ", " << "OldCreationTime=" << x.oldCreationTime << ")";
    return out;
}

void UnlockCurrentEdgeEntry::pack(BincodeBuf& buf) const {
    dirId.pack(buf);
    buf.packBytes(name);
    creationTime.pack(buf);
    targetId.pack(buf);
    buf.packScalar<bool>(wasMoved);
}
void UnlockCurrentEdgeEntry::unpack(BincodeBuf& buf) {
    dirId.unpack(buf);
    buf.unpackBytes(name);
    creationTime.unpack(buf);
    targetId.unpack(buf);
    wasMoved = buf.unpackScalar<bool>();
}
void UnlockCurrentEdgeEntry::clear() {
    dirId = InodeId();
    name.clear();
    creationTime = EggsTime();
    targetId = InodeId();
    wasMoved = bool(0);
}
bool UnlockCurrentEdgeEntry::operator==(const UnlockCurrentEdgeEntry& rhs) const {
    if ((InodeId)this->dirId != (InodeId)rhs.dirId) { return false; };
    if (name != rhs.name) { return false; };
    if ((EggsTime)this->creationTime != (EggsTime)rhs.creationTime) { return false; };
    if ((InodeId)this->targetId != (InodeId)rhs.targetId) { return false; };
    if ((bool)this->wasMoved != (bool)rhs.wasMoved) { return false; };
    return true;
}
std::ostream& operator<<(std::ostream& out, const UnlockCurrentEdgeEntry& x) {
    out << "UnlockCurrentEdgeEntry(" << "DirId=" << x.dirId << ", " << "Name=" << GoLangQuotedStringFmt(x.name.data(), x.name.size()) << ", " << "CreationTime=" << x.creationTime << ", " << "TargetId=" << x.targetId << ", " << "WasMoved=" << x.wasMoved << ")";
    return out;
}

void LockCurrentEdgeEntry::pack(BincodeBuf& buf) const {
    dirId.pack(buf);
    buf.packBytes(name);
    creationTime.pack(buf);
    targetId.pack(buf);
}
void LockCurrentEdgeEntry::unpack(BincodeBuf& buf) {
    dirId.unpack(buf);
    buf.unpackBytes(name);
    creationTime.unpack(buf);
    targetId.unpack(buf);
}
void LockCurrentEdgeEntry::clear() {
    dirId = InodeId();
    name.clear();
    creationTime = EggsTime();
    targetId = InodeId();
}
bool LockCurrentEdgeEntry::operator==(const LockCurrentEdgeEntry& rhs) const {
    if ((InodeId)this->dirId != (InodeId)rhs.dirId) { return false; };
    if (name != rhs.name) { return false; };
    if ((EggsTime)this->creationTime != (EggsTime)rhs.creationTime) { return false; };
    if ((InodeId)this->targetId != (InodeId)rhs.targetId) { return false; };
    return true;
}
std::ostream& operator<<(std::ostream& out, const LockCurrentEdgeEntry& x) {
    out << "LockCurrentEdgeEntry(" << "DirId=" << x.dirId << ", " << "Name=" << GoLangQuotedStringFmt(x.name.data(), x.name.size()) << ", " << "CreationTime=" << x.creationTime << ", " << "TargetId=" << x.targetId << ")";
    return out;
}

void RemoveDirectoryOwnerEntry::pack(BincodeBuf& buf) const {
    dirId.pack(buf);
    info.pack(buf);
}
void RemoveDirectoryOwnerEntry::unpack(BincodeBuf& buf) {
    dirId.unpack(buf);
    info.unpack(buf);
}
void RemoveDirectoryOwnerEntry::clear() {
    dirId = InodeId();
    info.clear();
}
bool RemoveDirectoryOwnerEntry::operator==(const RemoveDirectoryOwnerEntry& rhs) const {
    if ((InodeId)this->dirId != (InodeId)rhs.dirId) { return false; };
    if (info != rhs.info) { return false; };
    return true;
}
std::ostream& operator<<(std::ostream& out, const RemoveDirectoryOwnerEntry& x) {
    out << "RemoveDirectoryOwnerEntry(" << "DirId=" << x.dirId << ", " << "Info=" << x.info << ")";
    return out;
}

void RemoveInodeEntry::pack(BincodeBuf& buf) const {
    id.pack(buf);
}
void RemoveInodeEntry::unpack(BincodeBuf& buf) {
    id.unpack(buf);
}
void RemoveInodeEntry::clear() {
    id = InodeId();
}
bool RemoveInodeEntry::operator==(const RemoveInodeEntry& rhs) const {
    if ((InodeId)this->id != (InodeId)rhs.id) { return false; };
    return true;
}
std::ostream& operator<<(std::ostream& out, const RemoveInodeEntry& x) {
    out << "RemoveInodeEntry(" << "Id=" << x.id << ")";
    return out;
}

void SetDirectoryOwnerEntry::pack(BincodeBuf& buf) const {
    dirId.pack(buf);
    ownerId.pack(buf);
}
void SetDirectoryOwnerEntry::unpack(BincodeBuf& buf) {
    dirId.unpack(buf);
    ownerId.unpack(buf);
}
void SetDirectoryOwnerEntry::clear() {
    dirId = InodeId();
    ownerId = InodeId();
}
bool SetDirectoryOwnerEntry::operator==(const SetDirectoryOwnerEntry& rhs) const {
    if ((InodeId)this->dirId != (InodeId)rhs.dirId) { return false; };
    if ((InodeId)this->ownerId != (InodeId)rhs.ownerId) { return false; };
    return true;
}
std::ostream& operator<<(std::ostream& out, const SetDirectoryOwnerEntry& x) {
    out << "SetDirectoryOwnerEntry(" << "DirId=" << x.dirId << ", " << "OwnerId=" << x.ownerId << ")";
    return out;
}

void SetDirectoryInfoEntry::pack(BincodeBuf& buf) const {
    dirId.pack(buf);
    info.pack(buf);
}
void SetDirectoryInfoEntry::unpack(BincodeBuf& buf) {
    dirId.unpack(buf);
    info.unpack(buf);
}
void SetDirectoryInfoEntry::clear() {
    dirId = InodeId();
    info.clear();
}
bool SetDirectoryInfoEntry::operator==(const SetDirectoryInfoEntry& rhs) const {
    if ((InodeId)this->dirId != (InodeId)rhs.dirId) { return false; };
    if (info != rhs.info) { return false; };
    return true;
}
std::ostream& operator<<(std::ostream& out, const SetDirectoryInfoEntry& x) {
    out << "SetDirectoryInfoEntry(" << "DirId=" << x.dirId << ", " << "Info=" << x.info << ")";
    return out;
}

void RemoveNonOwnedEdgeEntry::pack(BincodeBuf& buf) const {
    dirId.pack(buf);
    targetId.pack(buf);
    buf.packBytes(name);
    creationTime.pack(buf);
}
void RemoveNonOwnedEdgeEntry::unpack(BincodeBuf& buf) {
    dirId.unpack(buf);
    targetId.unpack(buf);
    buf.unpackBytes(name);
    creationTime.unpack(buf);
}
void RemoveNonOwnedEdgeEntry::clear() {
    dirId = InodeId();
    targetId = InodeId();
    name.clear();
    creationTime = EggsTime();
}
bool RemoveNonOwnedEdgeEntry::operator==(const RemoveNonOwnedEdgeEntry& rhs) const {
    if ((InodeId)this->dirId != (InodeId)rhs.dirId) { return false; };
    if ((InodeId)this->targetId != (InodeId)rhs.targetId) { return false; };
    if (name != rhs.name) { return false; };
    if ((EggsTime)this->creationTime != (EggsTime)rhs.creationTime) { return false; };
    return true;
}
std::ostream& operator<<(std::ostream& out, const RemoveNonOwnedEdgeEntry& x) {
    out << "RemoveNonOwnedEdgeEntry(" << "DirId=" << x.dirId << ", " << "TargetId=" << x.targetId << ", " << "Name=" << GoLangQuotedStringFmt(x.name.data(), x.name.size()) << ", " << "CreationTime=" << x.creationTime << ")";
    return out;
}

void SameShardHardFileUnlinkEntry::pack(BincodeBuf& buf) const {
    ownerId.pack(buf);
    targetId.pack(buf);
    buf.packBytes(name);
    creationTime.pack(buf);
}
void SameShardHardFileUnlinkEntry::unpack(BincodeBuf& buf) {
    ownerId.unpack(buf);
    targetId.unpack(buf);
    buf.unpackBytes(name);
    creationTime.unpack(buf);
}
void SameShardHardFileUnlinkEntry::clear() {
    ownerId = InodeId();
    targetId = InodeId();
    name.clear();
    creationTime = EggsTime();
}
bool SameShardHardFileUnlinkEntry::operator==(const SameShardHardFileUnlinkEntry& rhs) const {
    if ((InodeId)this->ownerId != (InodeId)rhs.ownerId) { return false; };
    if ((InodeId)this->targetId != (InodeId)rhs.targetId) { return false; };
    if (name != rhs.name) { return false; };
    if ((EggsTime)this->creationTime != (EggsTime)rhs.creationTime) { return false; };
    return true;
}
std::ostream& operator<<(std::ostream& out, const SameShardHardFileUnlinkEntry& x) {
    out << "SameShardHardFileUnlinkEntry(" << "OwnerId=" << x.ownerId << ", " << "TargetId=" << x.targetId << ", " << "Name=" << GoLangQuotedStringFmt(x.name.data(), x.name.size()) << ", " << "CreationTime=" << x.creationTime << ")";
    return out;
}

void RemoveSpanInitiateEntry::pack(BincodeBuf& buf) const {
    fileId.pack(buf);
}
void RemoveSpanInitiateEntry::unpack(BincodeBuf& buf) {
    fileId.unpack(buf);
}
void RemoveSpanInitiateEntry::clear() {
    fileId = InodeId();
}
bool RemoveSpanInitiateEntry::operator==(const RemoveSpanInitiateEntry& rhs) const {
    if ((InodeId)this->fileId != (InodeId)rhs.fileId) { return false; };
    return true;
}
std::ostream& operator<<(std::ostream& out, const RemoveSpanInitiateEntry& x) {
    out << "RemoveSpanInitiateEntry(" << "FileId=" << x.fileId << ")";
    return out;
}

void UpdateBlockServicesEntry::pack(BincodeBuf& buf) const {
    buf.packList<BlockServiceInfo>(blockServices);
    buf.packList<BlockServiceId>(currentBlockServices);
}
void UpdateBlockServicesEntry::unpack(BincodeBuf& buf) {
    buf.unpackList<BlockServiceInfo>(blockServices);
    buf.unpackList<BlockServiceId>(currentBlockServices);
}
void UpdateBlockServicesEntry::clear() {
    blockServices.clear();
    currentBlockServices.clear();
}
bool UpdateBlockServicesEntry::operator==(const UpdateBlockServicesEntry& rhs) const {
    if (blockServices != rhs.blockServices) { return false; };
    if (currentBlockServices != rhs.currentBlockServices) { return false; };
    return true;
}
std::ostream& operator<<(std::ostream& out, const UpdateBlockServicesEntry& x) {
    out << "UpdateBlockServicesEntry(" << "BlockServices=" << x.blockServices << ", " << "CurrentBlockServices=" << x.currentBlockServices << ")";
    return out;
}

void AddSpanInitiateEntry::pack(BincodeBuf& buf) const {
    fileId.pack(buf);
    buf.packScalar<uint64_t>(byteOffset);
    buf.packScalar<uint32_t>(size);
    crc.pack(buf);
    buf.packScalar<uint8_t>(storageClass);
    parity.pack(buf);
    buf.packScalar<uint8_t>(stripes);
    buf.packScalar<uint32_t>(cellSize);
    buf.packList<EntryNewBlockInfo>(bodyBlocks);
    buf.packList<Crc>(bodyStripes);
}
void AddSpanInitiateEntry::unpack(BincodeBuf& buf) {
    fileId.unpack(buf);
    byteOffset = buf.unpackScalar<uint64_t>();
    size = buf.unpackScalar<uint32_t>();
    crc.unpack(buf);
    storageClass = buf.unpackScalar<uint8_t>();
    parity.unpack(buf);
    stripes = buf.unpackScalar<uint8_t>();
    cellSize = buf.unpackScalar<uint32_t>();
    buf.unpackList<EntryNewBlockInfo>(bodyBlocks);
    buf.unpackList<Crc>(bodyStripes);
}
void AddSpanInitiateEntry::clear() {
    fileId = InodeId();
    byteOffset = uint64_t(0);
    size = uint32_t(0);
    crc = Crc(0);
    storageClass = uint8_t(0);
    parity = Parity();
    stripes = uint8_t(0);
    cellSize = uint32_t(0);
    bodyBlocks.clear();
    bodyStripes.clear();
}
bool AddSpanInitiateEntry::operator==(const AddSpanInitiateEntry& rhs) const {
    if ((InodeId)this->fileId != (InodeId)rhs.fileId) { return false; };
    if ((uint64_t)this->byteOffset != (uint64_t)rhs.byteOffset) { return false; };
    if ((uint32_t)this->size != (uint32_t)rhs.size) { return false; };
    if ((Crc)this->crc != (Crc)rhs.crc) { return false; };
    if ((uint8_t)this->storageClass != (uint8_t)rhs.storageClass) { return false; };
    if ((Parity)this->parity != (Parity)rhs.parity) { return false; };
    if ((uint8_t)this->stripes != (uint8_t)rhs.stripes) { return false; };
    if ((uint32_t)this->cellSize != (uint32_t)rhs.cellSize) { return false; };
    if (bodyBlocks != rhs.bodyBlocks) { return false; };
    if (bodyStripes != rhs.bodyStripes) { return false; };
    return true;
}
std::ostream& operator<<(std::ostream& out, const AddSpanInitiateEntry& x) {
    out << "AddSpanInitiateEntry(" << "FileId=" << x.fileId << ", " << "ByteOffset=" << x.byteOffset << ", " << "Size=" << x.size << ", " << "Crc=" << x.crc << ", " << "StorageClass=" << (int)x.storageClass << ", " << "Parity=" << x.parity << ", " << "Stripes=" << (int)x.stripes << ", " << "CellSize=" << x.cellSize << ", " << "BodyBlocks=" << x.bodyBlocks << ", " << "BodyStripes=" << x.bodyStripes << ")";
    return out;
}

void AddSpanCertifyEntry::pack(BincodeBuf& buf) const {
    fileId.pack(buf);
    buf.packScalar<uint64_t>(byteOffset);
    buf.packList<BlockProof>(proofs);
}
void AddSpanCertifyEntry::unpack(BincodeBuf& buf) {
    fileId.unpack(buf);
    byteOffset = buf.unpackScalar<uint64_t>();
    buf.unpackList<BlockProof>(proofs);
}
void AddSpanCertifyEntry::clear() {
    fileId = InodeId();
    byteOffset = uint64_t(0);
    proofs.clear();
}
bool AddSpanCertifyEntry::operator==(const AddSpanCertifyEntry& rhs) const {
    if ((InodeId)this->fileId != (InodeId)rhs.fileId) { return false; };
    if ((uint64_t)this->byteOffset != (uint64_t)rhs.byteOffset) { return false; };
    if (proofs != rhs.proofs) { return false; };
    return true;
}
std::ostream& operator<<(std::ostream& out, const AddSpanCertifyEntry& x) {
    out << "AddSpanCertifyEntry(" << "FileId=" << x.fileId << ", " << "ByteOffset=" << x.byteOffset << ", " << "Proofs=" << x.proofs << ")";
    return out;
}

void AddInlineSpanEntry::pack(BincodeBuf& buf) const {
    fileId.pack(buf);
    buf.packScalar<uint8_t>(storageClass);
    buf.packScalar<uint64_t>(byteOffset);
    buf.packScalar<uint32_t>(size);
    crc.pack(buf);
    buf.packBytes(body);
}
void AddInlineSpanEntry::unpack(BincodeBuf& buf) {
    fileId.unpack(buf);
    storageClass = buf.unpackScalar<uint8_t>();
    byteOffset = buf.unpackScalar<uint64_t>();
    size = buf.unpackScalar<uint32_t>();
    crc.unpack(buf);
    buf.unpackBytes(body);
}
void AddInlineSpanEntry::clear() {
    fileId = InodeId();
    storageClass = uint8_t(0);
    byteOffset = uint64_t(0);
    size = uint32_t(0);
    crc = Crc(0);
    body.clear();
}
bool AddInlineSpanEntry::operator==(const AddInlineSpanEntry& rhs) const {
    if ((InodeId)this->fileId != (InodeId)rhs.fileId) { return false; };
    if ((uint8_t)this->storageClass != (uint8_t)rhs.storageClass) { return false; };
    if ((uint64_t)this->byteOffset != (uint64_t)rhs.byteOffset) { return false; };
    if ((uint32_t)this->size != (uint32_t)rhs.size) { return false; };
    if ((Crc)this->crc != (Crc)rhs.crc) { return false; };
    if (body != rhs.body) { return false; };
    return true;
}
std::ostream& operator<<(std::ostream& out, const AddInlineSpanEntry& x) {
    out << "AddInlineSpanEntry(" << "FileId=" << x.fileId << ", " << "StorageClass=" << (int)x.storageClass << ", " << "ByteOffset=" << x.byteOffset << ", " << "Size=" << x.size << ", " << "Crc=" << x.crc << ", " << "Body=" << x.body << ")";
    return out;
}

void MakeFileTransientEntry::pack(BincodeBuf& buf) const {
    id.pack(buf);
    buf.packBytes(note);
}
void MakeFileTransientEntry::unpack(BincodeBuf& buf) {
    id.unpack(buf);
    buf.unpackBytes(note);
}
void MakeFileTransientEntry::clear() {
    id = InodeId();
    note.clear();
}
bool MakeFileTransientEntry::operator==(const MakeFileTransientEntry& rhs) const {
    if ((InodeId)this->id != (InodeId)rhs.id) { return false; };
    if (note != rhs.note) { return false; };
    return true;
}
std::ostream& operator<<(std::ostream& out, const MakeFileTransientEntry& x) {
    out << "MakeFileTransientEntry(" << "Id=" << x.id << ", " << "Note=" << GoLangQuotedStringFmt(x.note.data(), x.note.size()) << ")";
    return out;
}

void RemoveSpanCertifyEntry::pack(BincodeBuf& buf) const {
    fileId.pack(buf);
    buf.packScalar<uint64_t>(byteOffset);
    buf.packList<BlockProof>(proofs);
}
void RemoveSpanCertifyEntry::unpack(BincodeBuf& buf) {
    fileId.unpack(buf);
    byteOffset = buf.unpackScalar<uint64_t>();
    buf.unpackList<BlockProof>(proofs);
}
void RemoveSpanCertifyEntry::clear() {
    fileId = InodeId();
    byteOffset = uint64_t(0);
    proofs.clear();
}
bool RemoveSpanCertifyEntry::operator==(const RemoveSpanCertifyEntry& rhs) const {
    if ((InodeId)this->fileId != (InodeId)rhs.fileId) { return false; };
    if ((uint64_t)this->byteOffset != (uint64_t)rhs.byteOffset) { return false; };
    if (proofs != rhs.proofs) { return false; };
    return true;
}
std::ostream& operator<<(std::ostream& out, const RemoveSpanCertifyEntry& x) {
    out << "RemoveSpanCertifyEntry(" << "FileId=" << x.fileId << ", " << "ByteOffset=" << x.byteOffset << ", " << "Proofs=" << x.proofs << ")";
    return out;
}

void RemoveOwnedSnapshotFileEdgeEntry::pack(BincodeBuf& buf) const {
    ownerId.pack(buf);
    targetId.pack(buf);
    buf.packBytes(name);
    creationTime.pack(buf);
}
void RemoveOwnedSnapshotFileEdgeEntry::unpack(BincodeBuf& buf) {
    ownerId.unpack(buf);
    targetId.unpack(buf);
    buf.unpackBytes(name);
    creationTime.unpack(buf);
}
void RemoveOwnedSnapshotFileEdgeEntry::clear() {
    ownerId = InodeId();
    targetId = InodeId();
    name.clear();
    creationTime = EggsTime();
}
bool RemoveOwnedSnapshotFileEdgeEntry::operator==(const RemoveOwnedSnapshotFileEdgeEntry& rhs) const {
    if ((InodeId)this->ownerId != (InodeId)rhs.ownerId) { return false; };
    if ((InodeId)this->targetId != (InodeId)rhs.targetId) { return false; };
    if (name != rhs.name) { return false; };
    if ((EggsTime)this->creationTime != (EggsTime)rhs.creationTime) { return false; };
    return true;
}
std::ostream& operator<<(std::ostream& out, const RemoveOwnedSnapshotFileEdgeEntry& x) {
    out << "RemoveOwnedSnapshotFileEdgeEntry(" << "OwnerId=" << x.ownerId << ", " << "TargetId=" << x.targetId << ", " << "Name=" << GoLangQuotedStringFmt(x.name.data(), x.name.size()) << ", " << "CreationTime=" << x.creationTime << ")";
    return out;
}

void SwapBlocksEntry::pack(BincodeBuf& buf) const {
    fileId1.pack(buf);
    buf.packScalar<uint64_t>(byteOffset1);
    buf.packScalar<uint64_t>(blockId1);
    fileId2.pack(buf);
    buf.packScalar<uint64_t>(byteOffset2);
    buf.packScalar<uint64_t>(blockId2);
}
void SwapBlocksEntry::unpack(BincodeBuf& buf) {
    fileId1.unpack(buf);
    byteOffset1 = buf.unpackScalar<uint64_t>();
    blockId1 = buf.unpackScalar<uint64_t>();
    fileId2.unpack(buf);
    byteOffset2 = buf.unpackScalar<uint64_t>();
    blockId2 = buf.unpackScalar<uint64_t>();
}
void SwapBlocksEntry::clear() {
    fileId1 = InodeId();
    byteOffset1 = uint64_t(0);
    blockId1 = uint64_t(0);
    fileId2 = InodeId();
    byteOffset2 = uint64_t(0);
    blockId2 = uint64_t(0);
}
bool SwapBlocksEntry::operator==(const SwapBlocksEntry& rhs) const {
    if ((InodeId)this->fileId1 != (InodeId)rhs.fileId1) { return false; };
    if ((uint64_t)this->byteOffset1 != (uint64_t)rhs.byteOffset1) { return false; };
    if ((uint64_t)this->blockId1 != (uint64_t)rhs.blockId1) { return false; };
    if ((InodeId)this->fileId2 != (InodeId)rhs.fileId2) { return false; };
    if ((uint64_t)this->byteOffset2 != (uint64_t)rhs.byteOffset2) { return false; };
    if ((uint64_t)this->blockId2 != (uint64_t)rhs.blockId2) { return false; };
    return true;
}
std::ostream& operator<<(std::ostream& out, const SwapBlocksEntry& x) {
    out << "SwapBlocksEntry(" << "FileId1=" << x.fileId1 << ", " << "ByteOffset1=" << x.byteOffset1 << ", " << "BlockId1=" << x.blockId1 << ", " << "FileId2=" << x.fileId2 << ", " << "ByteOffset2=" << x.byteOffset2 << ", " << "BlockId2=" << x.blockId2 << ")";
    return out;
}

void MoveSpanEntry::pack(BincodeBuf& buf) const {
    buf.packScalar<uint32_t>(spanSize);
    fileId1.pack(buf);
    buf.packScalar<uint64_t>(byteOffset1);
    buf.packFixedBytes<8>(cookie1);
    fileId2.pack(buf);
    buf.packScalar<uint64_t>(byteOffset2);
    buf.packFixedBytes<8>(cookie2);
}
void MoveSpanEntry::unpack(BincodeBuf& buf) {
    spanSize = buf.unpackScalar<uint32_t>();
    fileId1.unpack(buf);
    byteOffset1 = buf.unpackScalar<uint64_t>();
    buf.unpackFixedBytes<8>(cookie1);
    fileId2.unpack(buf);
    byteOffset2 = buf.unpackScalar<uint64_t>();
    buf.unpackFixedBytes<8>(cookie2);
}
void MoveSpanEntry::clear() {
    spanSize = uint32_t(0);
    fileId1 = InodeId();
    byteOffset1 = uint64_t(0);
    cookie1.clear();
    fileId2 = InodeId();
    byteOffset2 = uint64_t(0);
    cookie2.clear();
}
bool MoveSpanEntry::operator==(const MoveSpanEntry& rhs) const {
    if ((uint32_t)this->spanSize != (uint32_t)rhs.spanSize) { return false; };
    if ((InodeId)this->fileId1 != (InodeId)rhs.fileId1) { return false; };
    if ((uint64_t)this->byteOffset1 != (uint64_t)rhs.byteOffset1) { return false; };
    if (cookie1 != rhs.cookie1) { return false; };
    if ((InodeId)this->fileId2 != (InodeId)rhs.fileId2) { return false; };
    if ((uint64_t)this->byteOffset2 != (uint64_t)rhs.byteOffset2) { return false; };
    if (cookie2 != rhs.cookie2) { return false; };
    return true;
}
std::ostream& operator<<(std::ostream& out, const MoveSpanEntry& x) {
    out << "MoveSpanEntry(" << "SpanSize=" << x.spanSize << ", " << "FileId1=" << x.fileId1 << ", " << "ByteOffset1=" << x.byteOffset1 << ", " << "Cookie1=" << x.cookie1 << ", " << "FileId2=" << x.fileId2 << ", " << "ByteOffset2=" << x.byteOffset2 << ", " << "Cookie2=" << x.cookie2 << ")";
    return out;
}

void SetTimeEntry::pack(BincodeBuf& buf) const {
    id.pack(buf);
    buf.packScalar<uint64_t>(mtime);
    buf.packScalar<uint64_t>(atime);
}
void SetTimeEntry::unpack(BincodeBuf& buf) {
    id.unpack(buf);
    mtime = buf.unpackScalar<uint64_t>();
    atime = buf.unpackScalar<uint64_t>();
}
void SetTimeEntry::clear() {
    id = InodeId();
    mtime = uint64_t(0);
    atime = uint64_t(0);
}
bool SetTimeEntry::operator==(const SetTimeEntry& rhs) const {
    if ((InodeId)this->id != (InodeId)rhs.id) { return false; };
    if ((uint64_t)this->mtime != (uint64_t)rhs.mtime) { return false; };
    if ((uint64_t)this->atime != (uint64_t)rhs.atime) { return false; };
    return true;
}
std::ostream& operator<<(std::ostream& out, const SetTimeEntry& x) {
    out << "SetTimeEntry(" << "Id=" << x.id << ", " << "Mtime=" << x.mtime << ", " << "Atime=" << x.atime << ")";
    return out;
}

void RemoveZeroBlockServiceFilesEntry::pack(BincodeBuf& buf) const {
    startBlockService.pack(buf);
    startFile.pack(buf);
}
void RemoveZeroBlockServiceFilesEntry::unpack(BincodeBuf& buf) {
    startBlockService.unpack(buf);
    startFile.unpack(buf);
}
void RemoveZeroBlockServiceFilesEntry::clear() {
    startBlockService = BlockServiceId(0);
    startFile = InodeId();
}
bool RemoveZeroBlockServiceFilesEntry::operator==(const RemoveZeroBlockServiceFilesEntry& rhs) const {
    if ((BlockServiceId)this->startBlockService != (BlockServiceId)rhs.startBlockService) { return false; };
    if ((InodeId)this->startFile != (InodeId)rhs.startFile) { return false; };
    return true;
}
std::ostream& operator<<(std::ostream& out, const RemoveZeroBlockServiceFilesEntry& x) {
    out << "RemoveZeroBlockServiceFilesEntry(" << "StartBlockService=" << x.startBlockService << ", " << "StartFile=" << x.startFile << ")";
    return out;
}

const ConstructFileEntry& ShardLogEntryContainer::getConstructFile() const {
    ALWAYS_ASSERT(_kind == ShardLogEntryKind::CONSTRUCT_FILE, "%s != %s", _kind, ShardLogEntryKind::CONSTRUCT_FILE);
    return std::get<0>(_data);
}
ConstructFileEntry& ShardLogEntryContainer::setConstructFile() {
    _kind = ShardLogEntryKind::CONSTRUCT_FILE;
    auto& x = _data.emplace<0>();
    return x;
}
const LinkFileEntry& ShardLogEntryContainer::getLinkFile() const {
    ALWAYS_ASSERT(_kind == ShardLogEntryKind::LINK_FILE, "%s != %s", _kind, ShardLogEntryKind::LINK_FILE);
    return std::get<1>(_data);
}
LinkFileEntry& ShardLogEntryContainer::setLinkFile() {
    _kind = ShardLogEntryKind::LINK_FILE;
    auto& x = _data.emplace<1>();
    return x;
}
const SameDirectoryRenameEntry& ShardLogEntryContainer::getSameDirectoryRename() const {
    ALWAYS_ASSERT(_kind == ShardLogEntryKind::SAME_DIRECTORY_RENAME, "%s != %s", _kind, ShardLogEntryKind::SAME_DIRECTORY_RENAME);
    return std::get<2>(_data);
}
SameDirectoryRenameEntry& ShardLogEntryContainer::setSameDirectoryRename() {
    _kind = ShardLogEntryKind::SAME_DIRECTORY_RENAME;
    auto& x = _data.emplace<2>();
    return x;
}
const SoftUnlinkFileEntry& ShardLogEntryContainer::getSoftUnlinkFile() const {
    ALWAYS_ASSERT(_kind == ShardLogEntryKind::SOFT_UNLINK_FILE, "%s != %s", _kind, ShardLogEntryKind::SOFT_UNLINK_FILE);
    return std::get<3>(_data);
}
SoftUnlinkFileEntry& ShardLogEntryContainer::setSoftUnlinkFile() {
    _kind = ShardLogEntryKind::SOFT_UNLINK_FILE;
    auto& x = _data.emplace<3>();
    return x;
}
const CreateDirectoryInodeEntry& ShardLogEntryContainer::getCreateDirectoryInode() const {
    ALWAYS_ASSERT(_kind == ShardLogEntryKind::CREATE_DIRECTORY_INODE, "%s != %s", _kind, ShardLogEntryKind::CREATE_DIRECTORY_INODE);
    return std::get<4>(_data);
}
CreateDirectoryInodeEntry& ShardLogEntryContainer::setCreateDirectoryInode() {
    _kind = ShardLogEntryKind::CREATE_DIRECTORY_INODE;
    auto& x = _data.emplace<4>();
    return x;
}
const CreateLockedCurrentEdgeEntry& ShardLogEntryContainer::getCreateLockedCurrentEdge() const {
    ALWAYS_ASSERT(_kind == ShardLogEntryKind::CREATE_LOCKED_CURRENT_EDGE, "%s != %s", _kind, ShardLogEntryKind::CREATE_LOCKED_CURRENT_EDGE);
    return std::get<5>(_data);
}
CreateLockedCurrentEdgeEntry& ShardLogEntryContainer::setCreateLockedCurrentEdge() {
    _kind = ShardLogEntryKind::CREATE_LOCKED_CURRENT_EDGE;
    auto& x = _data.emplace<5>();
    return x;
}
const UnlockCurrentEdgeEntry& ShardLogEntryContainer::getUnlockCurrentEdge() const {
    ALWAYS_ASSERT(_kind == ShardLogEntryKind::UNLOCK_CURRENT_EDGE, "%s != %s", _kind, ShardLogEntryKind::UNLOCK_CURRENT_EDGE);
    return std::get<6>(_data);
}
UnlockCurrentEdgeEntry& ShardLogEntryContainer::setUnlockCurrentEdge() {
    _kind = ShardLogEntryKind::UNLOCK_CURRENT_EDGE;
    auto& x = _data.emplace<6>();
    return x;
}
const LockCurrentEdgeEntry& ShardLogEntryContainer::getLockCurrentEdge() const {
    ALWAYS_ASSERT(_kind == ShardLogEntryKind::LOCK_CURRENT_EDGE, "%s != %s", _kind, ShardLogEntryKind::LOCK_CURRENT_EDGE);
    return std::get<7>(_data);
}
LockCurrentEdgeEntry& ShardLogEntryContainer::setLockCurrentEdge() {
    _kind = ShardLogEntryKind::LOCK_CURRENT_EDGE;
    auto& x = _data.emplace<7>();
    return x;
}
const RemoveDirectoryOwnerEntry& ShardLogEntryContainer::getRemoveDirectoryOwner() const {
    ALWAYS_ASSERT(_kind == ShardLogEntryKind::REMOVE_DIRECTORY_OWNER, "%s != %s", _kind, ShardLogEntryKind::REMOVE_DIRECTORY_OWNER);
    return std::get<8>(_data);
}
RemoveDirectoryOwnerEntry& ShardLogEntryContainer::setRemoveDirectoryOwner() {
    _kind = ShardLogEntryKind::REMOVE_DIRECTORY_OWNER;
    auto& x = _data.emplace<8>();
    return x;
}
const RemoveInodeEntry& ShardLogEntryContainer::getRemoveInode() const {
    ALWAYS_ASSERT(_kind == ShardLogEntryKind::REMOVE_INODE, "%s != %s", _kind, ShardLogEntryKind::REMOVE_INODE);
    return std::get<9>(_data);
}
RemoveInodeEntry& ShardLogEntryContainer::setRemoveInode() {
    _kind = ShardLogEntryKind::REMOVE_INODE;
    auto& x = _data.emplace<9>();
    return x;
}
const SetDirectoryOwnerEntry& ShardLogEntryContainer::getSetDirectoryOwner() const {
    ALWAYS_ASSERT(_kind == ShardLogEntryKind::SET_DIRECTORY_OWNER, "%s != %s", _kind, ShardLogEntryKind::SET_DIRECTORY_OWNER);
    return std::get<10>(_data);
}
SetDirectoryOwnerEntry& ShardLogEntryContainer::setSetDirectoryOwner() {
    _kind = ShardLogEntryKind::SET_DIRECTORY_OWNER;
    auto& x = _data.emplace<10>();
    return x;
}
const SetDirectoryInfoEntry& ShardLogEntryContainer::getSetDirectoryInfo() const {
    ALWAYS_ASSERT(_kind == ShardLogEntryKind::SET_DIRECTORY_INFO, "%s != %s", _kind, ShardLogEntryKind::SET_DIRECTORY_INFO);
    return std::get<11>(_data);
}
SetDirectoryInfoEntry& ShardLogEntryContainer::setSetDirectoryInfo() {
    _kind = ShardLogEntryKind::SET_DIRECTORY_INFO;
    auto& x = _data.emplace<11>();
    return x;
}
const RemoveNonOwnedEdgeEntry& ShardLogEntryContainer::getRemoveNonOwnedEdge() const {
    ALWAYS_ASSERT(_kind == ShardLogEntryKind::REMOVE_NON_OWNED_EDGE, "%s != %s", _kind, ShardLogEntryKind::REMOVE_NON_OWNED_EDGE);
    return std::get<12>(_data);
}
RemoveNonOwnedEdgeEntry& ShardLogEntryContainer::setRemoveNonOwnedEdge() {
    _kind = ShardLogEntryKind::REMOVE_NON_OWNED_EDGE;
    auto& x = _data.emplace<12>();
    return x;
}
const SameShardHardFileUnlinkEntry& ShardLogEntryContainer::getSameShardHardFileUnlink() const {
    ALWAYS_ASSERT(_kind == ShardLogEntryKind::SAME_SHARD_HARD_FILE_UNLINK, "%s != %s", _kind, ShardLogEntryKind::SAME_SHARD_HARD_FILE_UNLINK);
    return std::get<13>(_data);
}
SameShardHardFileUnlinkEntry& ShardLogEntryContainer::setSameShardHardFileUnlink() {
    _kind = ShardLogEntryKind::SAME_SHARD_HARD_FILE_UNLINK;
    auto& x = _data.emplace<13>();
    return x;
}
const RemoveSpanInitiateEntry& ShardLogEntryContainer::getRemoveSpanInitiate() const {
    ALWAYS_ASSERT(_kind == ShardLogEntryKind::REMOVE_SPAN_INITIATE, "%s != %s", _kind, ShardLogEntryKind::REMOVE_SPAN_INITIATE);
    return std::get<14>(_data);
}
RemoveSpanInitiateEntry& ShardLogEntryContainer::setRemoveSpanInitiate() {
    _kind = ShardLogEntryKind::REMOVE_SPAN_INITIATE;
    auto& x = _data.emplace<14>();
    return x;
}
const UpdateBlockServicesEntry& ShardLogEntryContainer::getUpdateBlockServices() const {
    ALWAYS_ASSERT(_kind == ShardLogEntryKind::UPDATE_BLOCK_SERVICES, "%s != %s", _kind, ShardLogEntryKind::UPDATE_BLOCK_SERVICES);
    return std::get<15>(_data);
}
UpdateBlockServicesEntry& ShardLogEntryContainer::setUpdateBlockServices() {
    _kind = ShardLogEntryKind::UPDATE_BLOCK_SERVICES;
    auto& x = _data.emplace<15>();
    return x;
}
const AddSpanInitiateEntry& ShardLogEntryContainer::getAddSpanInitiate() const {
    ALWAYS_ASSERT(_kind == ShardLogEntryKind::ADD_SPAN_INITIATE, "%s != %s", _kind, ShardLogEntryKind::ADD_SPAN_INITIATE);
    return std::get<16>(_data);
}
AddSpanInitiateEntry& ShardLogEntryContainer::setAddSpanInitiate() {
    _kind = ShardLogEntryKind::ADD_SPAN_INITIATE;
    auto& x = _data.emplace<16>();
    return x;
}
const AddSpanCertifyEntry& ShardLogEntryContainer::getAddSpanCertify() const {
    ALWAYS_ASSERT(_kind == ShardLogEntryKind::ADD_SPAN_CERTIFY, "%s != %s", _kind, ShardLogEntryKind::ADD_SPAN_CERTIFY);
    return std::get<17>(_data);
}
AddSpanCertifyEntry& ShardLogEntryContainer::setAddSpanCertify() {
    _kind = ShardLogEntryKind::ADD_SPAN_CERTIFY;
    auto& x = _data.emplace<17>();
    return x;
}
const AddInlineSpanEntry& ShardLogEntryContainer::getAddInlineSpan() const {
    ALWAYS_ASSERT(_kind == ShardLogEntryKind::ADD_INLINE_SPAN, "%s != %s", _kind, ShardLogEntryKind::ADD_INLINE_SPAN);
    return std::get<18>(_data);
}
AddInlineSpanEntry& ShardLogEntryContainer::setAddInlineSpan() {
    _kind = ShardLogEntryKind::ADD_INLINE_SPAN;
    auto& x = _data.emplace<18>();
    return x;
}
const MakeFileTransientEntry& ShardLogEntryContainer::getMakeFileTransient() const {
    ALWAYS_ASSERT(_kind == ShardLogEntryKind::MAKE_FILE_TRANSIENT, "%s != %s", _kind, ShardLogEntryKind::MAKE_FILE_TRANSIENT);
    return std::get<19>(_data);
}
MakeFileTransientEntry& ShardLogEntryContainer::setMakeFileTransient() {
    _kind = ShardLogEntryKind::MAKE_FILE_TRANSIENT;
    auto& x = _data.emplace<19>();
    return x;
}
const RemoveSpanCertifyEntry& ShardLogEntryContainer::getRemoveSpanCertify() const {
    ALWAYS_ASSERT(_kind == ShardLogEntryKind::REMOVE_SPAN_CERTIFY, "%s != %s", _kind, ShardLogEntryKind::REMOVE_SPAN_CERTIFY);
    return std::get<20>(_data);
}
RemoveSpanCertifyEntry& ShardLogEntryContainer::setRemoveSpanCertify() {
    _kind = ShardLogEntryKind::REMOVE_SPAN_CERTIFY;
    auto& x = _data.emplace<20>();
    return x;
}
const RemoveOwnedSnapshotFileEdgeEntry& ShardLogEntryContainer::getRemoveOwnedSnapshotFileEdge() const {
    ALWAYS_ASSERT(_kind == ShardLogEntryKind::REMOVE_OWNED_SNAPSHOT_FILE_EDGE, "%s != %s", _kind, ShardLogEntryKind::REMOVE_OWNED_SNAPSHOT_FILE_EDGE);
    return std::get<21>(_data);
}
RemoveOwnedSnapshotFileEdgeEntry& ShardLogEntryContainer::setRemoveOwnedSnapshotFileEdge() {
    _kind = ShardLogEntryKind::REMOVE_OWNED_SNAPSHOT_FILE_EDGE;
    auto& x = _data.emplace<21>();
    return x;
}
const SwapBlocksEntry& ShardLogEntryContainer::getSwapBlocks() const {
    ALWAYS_ASSERT(_kind == ShardLogEntryKind::SWAP_BLOCKS, "%s != %s", _kind, ShardLogEntryKind::SWAP_BLOCKS);
    return std::get<22>(_data);
}
SwapBlocksEntry& ShardLogEntryContainer::setSwapBlocks() {
    _kind = ShardLogEntryKind::SWAP_BLOCKS;
    auto& x = _data.emplace<22>();
    return x;
}
const MoveSpanEntry& ShardLogEntryContainer::getMoveSpan() const {
    ALWAYS_ASSERT(_kind == ShardLogEntryKind::MOVE_SPAN, "%s != %s", _kind, ShardLogEntryKind::MOVE_SPAN);
    return std::get<23>(_data);
}
MoveSpanEntry& ShardLogEntryContainer::setMoveSpan() {
    _kind = ShardLogEntryKind::MOVE_SPAN;
    auto& x = _data.emplace<23>();
    return x;
}
const SetTimeEntry& ShardLogEntryContainer::getSetTime() const {
    ALWAYS_ASSERT(_kind == ShardLogEntryKind::SET_TIME, "%s != %s", _kind, ShardLogEntryKind::SET_TIME);
    return std::get<24>(_data);
}
SetTimeEntry& ShardLogEntryContainer::setSetTime() {
    _kind = ShardLogEntryKind::SET_TIME;
    auto& x = _data.emplace<24>();
    return x;
}
const RemoveZeroBlockServiceFilesEntry& ShardLogEntryContainer::getRemoveZeroBlockServiceFiles() const {
    ALWAYS_ASSERT(_kind == ShardLogEntryKind::REMOVE_ZERO_BLOCK_SERVICE_FILES, "%s != %s", _kind, ShardLogEntryKind::REMOVE_ZERO_BLOCK_SERVICE_FILES);
    return std::get<25>(_data);
}
RemoveZeroBlockServiceFilesEntry& ShardLogEntryContainer::setRemoveZeroBlockServiceFiles() {
    _kind = ShardLogEntryKind::REMOVE_ZERO_BLOCK_SERVICE_FILES;
    auto& x = _data.emplace<25>();
    return x;
}
ShardLogEntryContainer::ShardLogEntryContainer() {
    clear();
}

ShardLogEntryContainer::ShardLogEntryContainer(const ShardLogEntryContainer& other) {
    *this = other;
}

ShardLogEntryContainer::ShardLogEntryContainer(ShardLogEntryContainer&& other) {
    _data = std::move(other._data);
    _kind = other._kind;
    other._kind = (ShardLogEntryKind)0;
}

void ShardLogEntryContainer::operator=(const ShardLogEntryContainer& other) {
    if (other.kind() == (ShardLogEntryKind)0) { clear(); return; }
    switch (other.kind()) {
    case ShardLogEntryKind::CONSTRUCT_FILE:
        setConstructFile() = other.getConstructFile();
        break;
    case ShardLogEntryKind::LINK_FILE:
        setLinkFile() = other.getLinkFile();
        break;
    case ShardLogEntryKind::SAME_DIRECTORY_RENAME:
        setSameDirectoryRename() = other.getSameDirectoryRename();
        break;
    case ShardLogEntryKind::SOFT_UNLINK_FILE:
        setSoftUnlinkFile() = other.getSoftUnlinkFile();
        break;
    case ShardLogEntryKind::CREATE_DIRECTORY_INODE:
        setCreateDirectoryInode() = other.getCreateDirectoryInode();
        break;
    case ShardLogEntryKind::CREATE_LOCKED_CURRENT_EDGE:
        setCreateLockedCurrentEdge() = other.getCreateLockedCurrentEdge();
        break;
    case ShardLogEntryKind::UNLOCK_CURRENT_EDGE:
        setUnlockCurrentEdge() = other.getUnlockCurrentEdge();
        break;
    case ShardLogEntryKind::LOCK_CURRENT_EDGE:
        setLockCurrentEdge() = other.getLockCurrentEdge();
        break;
    case ShardLogEntryKind::REMOVE_DIRECTORY_OWNER:
        setRemoveDirectoryOwner() = other.getRemoveDirectoryOwner();
        break;
    case ShardLogEntryKind::REMOVE_INODE:
        setRemoveInode() = other.getRemoveInode();
        break;
    case ShardLogEntryKind::SET_DIRECTORY_OWNER:
        setSetDirectoryOwner() = other.getSetDirectoryOwner();
        break;
    case ShardLogEntryKind::SET_DIRECTORY_INFO:
        setSetDirectoryInfo() = other.getSetDirectoryInfo();
        break;
    case ShardLogEntryKind::REMOVE_NON_OWNED_EDGE:
        setRemoveNonOwnedEdge() = other.getRemoveNonOwnedEdge();
        break;
    case ShardLogEntryKind::SAME_SHARD_HARD_FILE_UNLINK:
        setSameShardHardFileUnlink() = other.getSameShardHardFileUnlink();
        break;
    case ShardLogEntryKind::REMOVE_SPAN_INITIATE:
        setRemoveSpanInitiate() = other.getRemoveSpanInitiate();
        break;
    case ShardLogEntryKind::UPDATE_BLOCK_SERVICES:
        setUpdateBlockServices() = other.getUpdateBlockServices();
        break;
    case ShardLogEntryKind::ADD_SPAN_INITIATE:
        setAddSpanInitiate() = other.getAddSpanInitiate();
        break;
    case ShardLogEntryKind::ADD_SPAN_CERTIFY:
        setAddSpanCertify() = other.getAddSpanCertify();
        break;
    case ShardLogEntryKind::ADD_INLINE_SPAN:
        setAddInlineSpan() = other.getAddInlineSpan();
        break;
    case ShardLogEntryKind::MAKE_FILE_TRANSIENT:
        setMakeFileTransient() = other.getMakeFileTransient();
        break;
    case ShardLogEntryKind::REMOVE_SPAN_CERTIFY:
        setRemoveSpanCertify() = other.getRemoveSpanCertify();
        break;
    case ShardLogEntryKind::REMOVE_OWNED_SNAPSHOT_FILE_EDGE:
        setRemoveOwnedSnapshotFileEdge() = other.getRemoveOwnedSnapshotFileEdge();
        break;
    case ShardLogEntryKind::SWAP_BLOCKS:
        setSwapBlocks() = other.getSwapBlocks();
        break;
    case ShardLogEntryKind::MOVE_SPAN:
        setMoveSpan() = other.getMoveSpan();
        break;
    case ShardLogEntryKind::SET_TIME:
        setSetTime() = other.getSetTime();
        break;
    case ShardLogEntryKind::REMOVE_ZERO_BLOCK_SERVICE_FILES:
        setRemoveZeroBlockServiceFiles() = other.getRemoveZeroBlockServiceFiles();
        break;
    default:
        throw EGGS_EXCEPTION("bad ShardLogEntryKind kind %s", other.kind());
    }
}

void ShardLogEntryContainer::operator=(ShardLogEntryContainer&& other) {
    _data = std::move(other._data);
    _kind = other._kind;
    other._kind = (ShardLogEntryKind)0;
}

size_t ShardLogEntryContainer::packedSize() const {
    switch (_kind) {
    case ShardLogEntryKind::CONSTRUCT_FILE:
        return std::get<0>(_data).packedSize();
    case ShardLogEntryKind::LINK_FILE:
        return std::get<1>(_data).packedSize();
    case ShardLogEntryKind::SAME_DIRECTORY_RENAME:
        return std::get<2>(_data).packedSize();
    case ShardLogEntryKind::SOFT_UNLINK_FILE:
        return std::get<3>(_data).packedSize();
    case ShardLogEntryKind::CREATE_DIRECTORY_INODE:
        return std::get<4>(_data).packedSize();
    case ShardLogEntryKind::CREATE_LOCKED_CURRENT_EDGE:
        return std::get<5>(_data).packedSize();
    case ShardLogEntryKind::UNLOCK_CURRENT_EDGE:
        return std::get<6>(_data).packedSize();
    case ShardLogEntryKind::LOCK_CURRENT_EDGE:
        return std::get<7>(_data).packedSize();
    case ShardLogEntryKind::REMOVE_DIRECTORY_OWNER:
        return std::get<8>(_data).packedSize();
    case ShardLogEntryKind::REMOVE_INODE:
        return std::get<9>(_data).packedSize();
    case ShardLogEntryKind::SET_DIRECTORY_OWNER:
        return std::get<10>(_data).packedSize();
    case ShardLogEntryKind::SET_DIRECTORY_INFO:
        return std::get<11>(_data).packedSize();
    case ShardLogEntryKind::REMOVE_NON_OWNED_EDGE:
        return std::get<12>(_data).packedSize();
    case ShardLogEntryKind::SAME_SHARD_HARD_FILE_UNLINK:
        return std::get<13>(_data).packedSize();
    case ShardLogEntryKind::REMOVE_SPAN_INITIATE:
        return std::get<14>(_data).packedSize();
    case ShardLogEntryKind::UPDATE_BLOCK_SERVICES:
        return std::get<15>(_data).packedSize();
    case ShardLogEntryKind::ADD_SPAN_INITIATE:
        return std::get<16>(_data).packedSize();
    case ShardLogEntryKind::ADD_SPAN_CERTIFY:
        return std::get<17>(_data).packedSize();
    case ShardLogEntryKind::ADD_INLINE_SPAN:
        return std::get<18>(_data).packedSize();
    case ShardLogEntryKind::MAKE_FILE_TRANSIENT:
        return std::get<19>(_data).packedSize();
    case ShardLogEntryKind::REMOVE_SPAN_CERTIFY:
        return std::get<20>(_data).packedSize();
    case ShardLogEntryKind::REMOVE_OWNED_SNAPSHOT_FILE_EDGE:
        return std::get<21>(_data).packedSize();
    case ShardLogEntryKind::SWAP_BLOCKS:
        return std::get<22>(_data).packedSize();
    case ShardLogEntryKind::MOVE_SPAN:
        return std::get<23>(_data).packedSize();
    case ShardLogEntryKind::SET_TIME:
        return std::get<24>(_data).packedSize();
    case ShardLogEntryKind::REMOVE_ZERO_BLOCK_SERVICE_FILES:
        return std::get<25>(_data).packedSize();
    default:
        throw EGGS_EXCEPTION("bad ShardLogEntryKind kind %s", _kind);
    }
}

void ShardLogEntryContainer::pack(BincodeBuf& buf) const {
    switch (_kind) {
    case ShardLogEntryKind::CONSTRUCT_FILE:
        std::get<0>(_data).pack(buf);
        break;
    case ShardLogEntryKind::LINK_FILE:
        std::get<1>(_data).pack(buf);
        break;
    case ShardLogEntryKind::SAME_DIRECTORY_RENAME:
        std::get<2>(_data).pack(buf);
        break;
    case ShardLogEntryKind::SOFT_UNLINK_FILE:
        std::get<3>(_data).pack(buf);
        break;
    case ShardLogEntryKind::CREATE_DIRECTORY_INODE:
        std::get<4>(_data).pack(buf);
        break;
    case ShardLogEntryKind::CREATE_LOCKED_CURRENT_EDGE:
        std::get<5>(_data).pack(buf);
        break;
    case ShardLogEntryKind::UNLOCK_CURRENT_EDGE:
        std::get<6>(_data).pack(buf);
        break;
    case ShardLogEntryKind::LOCK_CURRENT_EDGE:
        std::get<7>(_data).pack(buf);
        break;
    case ShardLogEntryKind::REMOVE_DIRECTORY_OWNER:
        std::get<8>(_data).pack(buf);
        break;
    case ShardLogEntryKind::REMOVE_INODE:
        std::get<9>(_data).pack(buf);
        break;
    case ShardLogEntryKind::SET_DIRECTORY_OWNER:
        std::get<10>(_data).pack(buf);
        break;
    case ShardLogEntryKind::SET_DIRECTORY_INFO:
        std::get<11>(_data).pack(buf);
        break;
    case ShardLogEntryKind::REMOVE_NON_OWNED_EDGE:
        std::get<12>(_data).pack(buf);
        break;
    case ShardLogEntryKind::SAME_SHARD_HARD_FILE_UNLINK:
        std::get<13>(_data).pack(buf);
        break;
    case ShardLogEntryKind::REMOVE_SPAN_INITIATE:
        std::get<14>(_data).pack(buf);
        break;
    case ShardLogEntryKind::UPDATE_BLOCK_SERVICES:
        std::get<15>(_data).pack(buf);
        break;
    case ShardLogEntryKind::ADD_SPAN_INITIATE:
        std::get<16>(_data).pack(buf);
        break;
    case ShardLogEntryKind::ADD_SPAN_CERTIFY:
        std::get<17>(_data).pack(buf);
        break;
    case ShardLogEntryKind::ADD_INLINE_SPAN:
        std::get<18>(_data).pack(buf);
        break;
    case ShardLogEntryKind::MAKE_FILE_TRANSIENT:
        std::get<19>(_data).pack(buf);
        break;
    case ShardLogEntryKind::REMOVE_SPAN_CERTIFY:
        std::get<20>(_data).pack(buf);
        break;
    case ShardLogEntryKind::REMOVE_OWNED_SNAPSHOT_FILE_EDGE:
        std::get<21>(_data).pack(buf);
        break;
    case ShardLogEntryKind::SWAP_BLOCKS:
        std::get<22>(_data).pack(buf);
        break;
    case ShardLogEntryKind::MOVE_SPAN:
        std::get<23>(_data).pack(buf);
        break;
    case ShardLogEntryKind::SET_TIME:
        std::get<24>(_data).pack(buf);
        break;
    case ShardLogEntryKind::REMOVE_ZERO_BLOCK_SERVICE_FILES:
        std::get<25>(_data).pack(buf);
        break;
    default:
        throw EGGS_EXCEPTION("bad ShardLogEntryKind kind %s", _kind);
    }
}

void ShardLogEntryContainer::unpack(BincodeBuf& buf, ShardLogEntryKind kind) {
    _kind = kind;
    switch (kind) {
    case ShardLogEntryKind::CONSTRUCT_FILE:
        _data.emplace<0>().unpack(buf);
        break;
    case ShardLogEntryKind::LINK_FILE:
        _data.emplace<1>().unpack(buf);
        break;
    case ShardLogEntryKind::SAME_DIRECTORY_RENAME:
        _data.emplace<2>().unpack(buf);
        break;
    case ShardLogEntryKind::SOFT_UNLINK_FILE:
        _data.emplace<3>().unpack(buf);
        break;
    case ShardLogEntryKind::CREATE_DIRECTORY_INODE:
        _data.emplace<4>().unpack(buf);
        break;
    case ShardLogEntryKind::CREATE_LOCKED_CURRENT_EDGE:
        _data.emplace<5>().unpack(buf);
        break;
    case ShardLogEntryKind::UNLOCK_CURRENT_EDGE:
        _data.emplace<6>().unpack(buf);
        break;
    case ShardLogEntryKind::LOCK_CURRENT_EDGE:
        _data.emplace<7>().unpack(buf);
        break;
    case ShardLogEntryKind::REMOVE_DIRECTORY_OWNER:
        _data.emplace<8>().unpack(buf);
        break;
    case ShardLogEntryKind::REMOVE_INODE:
        _data.emplace<9>().unpack(buf);
        break;
    case ShardLogEntryKind::SET_DIRECTORY_OWNER:
        _data.emplace<10>().unpack(buf);
        break;
    case ShardLogEntryKind::SET_DIRECTORY_INFO:
        _data.emplace<11>().unpack(buf);
        break;
    case ShardLogEntryKind::REMOVE_NON_OWNED_EDGE:
        _data.emplace<12>().unpack(buf);
        break;
    case ShardLogEntryKind::SAME_SHARD_HARD_FILE_UNLINK:
        _data.emplace<13>().unpack(buf);
        break;
    case ShardLogEntryKind::REMOVE_SPAN_INITIATE:
        _data.emplace<14>().unpack(buf);
        break;
    case ShardLogEntryKind::UPDATE_BLOCK_SERVICES:
        _data.emplace<15>().unpack(buf);
        break;
    case ShardLogEntryKind::ADD_SPAN_INITIATE:
        _data.emplace<16>().unpack(buf);
        break;
    case ShardLogEntryKind::ADD_SPAN_CERTIFY:
        _data.emplace<17>().unpack(buf);
        break;
    case ShardLogEntryKind::ADD_INLINE_SPAN:
        _data.emplace<18>().unpack(buf);
        break;
    case ShardLogEntryKind::MAKE_FILE_TRANSIENT:
        _data.emplace<19>().unpack(buf);
        break;
    case ShardLogEntryKind::REMOVE_SPAN_CERTIFY:
        _data.emplace<20>().unpack(buf);
        break;
    case ShardLogEntryKind::REMOVE_OWNED_SNAPSHOT_FILE_EDGE:
        _data.emplace<21>().unpack(buf);
        break;
    case ShardLogEntryKind::SWAP_BLOCKS:
        _data.emplace<22>().unpack(buf);
        break;
    case ShardLogEntryKind::MOVE_SPAN:
        _data.emplace<23>().unpack(buf);
        break;
    case ShardLogEntryKind::SET_TIME:
        _data.emplace<24>().unpack(buf);
        break;
    case ShardLogEntryKind::REMOVE_ZERO_BLOCK_SERVICE_FILES:
        _data.emplace<25>().unpack(buf);
        break;
    default:
        throw BINCODE_EXCEPTION("bad ShardLogEntryKind kind %s", kind);
    }
}

bool ShardLogEntryContainer::operator==(const ShardLogEntryContainer& other) const {
    if (_kind != other.kind()) { return false; }
    if (_kind == (ShardLogEntryKind)0) { return true; }
    switch (_kind) {
    case ShardLogEntryKind::CONSTRUCT_FILE:
        return getConstructFile() == other.getConstructFile();
    case ShardLogEntryKind::LINK_FILE:
        return getLinkFile() == other.getLinkFile();
    case ShardLogEntryKind::SAME_DIRECTORY_RENAME:
        return getSameDirectoryRename() == other.getSameDirectoryRename();
    case ShardLogEntryKind::SOFT_UNLINK_FILE:
        return getSoftUnlinkFile() == other.getSoftUnlinkFile();
    case ShardLogEntryKind::CREATE_DIRECTORY_INODE:
        return getCreateDirectoryInode() == other.getCreateDirectoryInode();
    case ShardLogEntryKind::CREATE_LOCKED_CURRENT_EDGE:
        return getCreateLockedCurrentEdge() == other.getCreateLockedCurrentEdge();
    case ShardLogEntryKind::UNLOCK_CURRENT_EDGE:
        return getUnlockCurrentEdge() == other.getUnlockCurrentEdge();
    case ShardLogEntryKind::LOCK_CURRENT_EDGE:
        return getLockCurrentEdge() == other.getLockCurrentEdge();
    case ShardLogEntryKind::REMOVE_DIRECTORY_OWNER:
        return getRemoveDirectoryOwner() == other.getRemoveDirectoryOwner();
    case ShardLogEntryKind::REMOVE_INODE:
        return getRemoveInode() == other.getRemoveInode();
    case ShardLogEntryKind::SET_DIRECTORY_OWNER:
        return getSetDirectoryOwner() == other.getSetDirectoryOwner();
    case ShardLogEntryKind::SET_DIRECTORY_INFO:
        return getSetDirectoryInfo() == other.getSetDirectoryInfo();
    case ShardLogEntryKind::REMOVE_NON_OWNED_EDGE:
        return getRemoveNonOwnedEdge() == other.getRemoveNonOwnedEdge();
    case ShardLogEntryKind::SAME_SHARD_HARD_FILE_UNLINK:
        return getSameShardHardFileUnlink() == other.getSameShardHardFileUnlink();
    case ShardLogEntryKind::REMOVE_SPAN_INITIATE:
        return getRemoveSpanInitiate() == other.getRemoveSpanInitiate();
    case ShardLogEntryKind::UPDATE_BLOCK_SERVICES:
        return getUpdateBlockServices() == other.getUpdateBlockServices();
    case ShardLogEntryKind::ADD_SPAN_INITIATE:
        return getAddSpanInitiate() == other.getAddSpanInitiate();
    case ShardLogEntryKind::ADD_SPAN_CERTIFY:
        return getAddSpanCertify() == other.getAddSpanCertify();
    case ShardLogEntryKind::ADD_INLINE_SPAN:
        return getAddInlineSpan() == other.getAddInlineSpan();
    case ShardLogEntryKind::MAKE_FILE_TRANSIENT:
        return getMakeFileTransient() == other.getMakeFileTransient();
    case ShardLogEntryKind::REMOVE_SPAN_CERTIFY:
        return getRemoveSpanCertify() == other.getRemoveSpanCertify();
    case ShardLogEntryKind::REMOVE_OWNED_SNAPSHOT_FILE_EDGE:
        return getRemoveOwnedSnapshotFileEdge() == other.getRemoveOwnedSnapshotFileEdge();
    case ShardLogEntryKind::SWAP_BLOCKS:
        return getSwapBlocks() == other.getSwapBlocks();
    case ShardLogEntryKind::MOVE_SPAN:
        return getMoveSpan() == other.getMoveSpan();
    case ShardLogEntryKind::SET_TIME:
        return getSetTime() == other.getSetTime();
    case ShardLogEntryKind::REMOVE_ZERO_BLOCK_SERVICE_FILES:
        return getRemoveZeroBlockServiceFiles() == other.getRemoveZeroBlockServiceFiles();
    default:
        throw BINCODE_EXCEPTION("bad ShardLogEntryKind kind %s", _kind);
    }
}

std::ostream& operator<<(std::ostream& out, const ShardLogEntryContainer& x) {
    switch (x.kind()) {
    case ShardLogEntryKind::CONSTRUCT_FILE:
        out << x.getConstructFile();
        break;
    case ShardLogEntryKind::LINK_FILE:
        out << x.getLinkFile();
        break;
    case ShardLogEntryKind::SAME_DIRECTORY_RENAME:
        out << x.getSameDirectoryRename();
        break;
    case ShardLogEntryKind::SOFT_UNLINK_FILE:
        out << x.getSoftUnlinkFile();
        break;
    case ShardLogEntryKind::CREATE_DIRECTORY_INODE:
        out << x.getCreateDirectoryInode();
        break;
    case ShardLogEntryKind::CREATE_LOCKED_CURRENT_EDGE:
        out << x.getCreateLockedCurrentEdge();
        break;
    case ShardLogEntryKind::UNLOCK_CURRENT_EDGE:
        out << x.getUnlockCurrentEdge();
        break;
    case ShardLogEntryKind::LOCK_CURRENT_EDGE:
        out << x.getLockCurrentEdge();
        break;
    case ShardLogEntryKind::REMOVE_DIRECTORY_OWNER:
        out << x.getRemoveDirectoryOwner();
        break;
    case ShardLogEntryKind::REMOVE_INODE:
        out << x.getRemoveInode();
        break;
    case ShardLogEntryKind::SET_DIRECTORY_OWNER:
        out << x.getSetDirectoryOwner();
        break;
    case ShardLogEntryKind::SET_DIRECTORY_INFO:
        out << x.getSetDirectoryInfo();
        break;
    case ShardLogEntryKind::REMOVE_NON_OWNED_EDGE:
        out << x.getRemoveNonOwnedEdge();
        break;
    case ShardLogEntryKind::SAME_SHARD_HARD_FILE_UNLINK:
        out << x.getSameShardHardFileUnlink();
        break;
    case ShardLogEntryKind::REMOVE_SPAN_INITIATE:
        out << x.getRemoveSpanInitiate();
        break;
    case ShardLogEntryKind::UPDATE_BLOCK_SERVICES:
        out << x.getUpdateBlockServices();
        break;
    case ShardLogEntryKind::ADD_SPAN_INITIATE:
        out << x.getAddSpanInitiate();
        break;
    case ShardLogEntryKind::ADD_SPAN_CERTIFY:
        out << x.getAddSpanCertify();
        break;
    case ShardLogEntryKind::ADD_INLINE_SPAN:
        out << x.getAddInlineSpan();
        break;
    case ShardLogEntryKind::MAKE_FILE_TRANSIENT:
        out << x.getMakeFileTransient();
        break;
    case ShardLogEntryKind::REMOVE_SPAN_CERTIFY:
        out << x.getRemoveSpanCertify();
        break;
    case ShardLogEntryKind::REMOVE_OWNED_SNAPSHOT_FILE_EDGE:
        out << x.getRemoveOwnedSnapshotFileEdge();
        break;
    case ShardLogEntryKind::SWAP_BLOCKS:
        out << x.getSwapBlocks();
        break;
    case ShardLogEntryKind::MOVE_SPAN:
        out << x.getMoveSpan();
        break;
    case ShardLogEntryKind::SET_TIME:
        out << x.getSetTime();
        break;
    case ShardLogEntryKind::REMOVE_ZERO_BLOCK_SERVICE_FILES:
        out << x.getRemoveZeroBlockServiceFiles();
        break;
    default:
        throw EGGS_EXCEPTION("bad ShardLogEntryKind kind %s", x.kind());
    }
    return out;
}

