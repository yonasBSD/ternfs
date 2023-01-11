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
    default:
        out << "EggsError(" << ((int)err) << ")";
        break;
    }
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

void FetchedBlock::pack(BincodeBuf& buf) const {
    buf.packScalar<uint8_t>(blockServiceIx);
    buf.packScalar<uint64_t>(blockId);
    buf.packFixedBytes<4>(crc32);
}
void FetchedBlock::unpack(BincodeBuf& buf) {
    blockServiceIx = buf.unpackScalar<uint8_t>();
    blockId = buf.unpackScalar<uint64_t>();
    buf.unpackFixedBytes<4>(crc32);
}
void FetchedBlock::clear() {
    blockServiceIx = uint8_t(0);
    blockId = uint64_t(0);
    crc32.clear();
}
bool FetchedBlock::operator==(const FetchedBlock& rhs) const {
    if ((uint8_t)this->blockServiceIx != (uint8_t)rhs.blockServiceIx) { return false; };
    if ((uint64_t)this->blockId != (uint64_t)rhs.blockId) { return false; };
    if (crc32 != rhs.crc32) { return false; };
    return true;
}
std::ostream& operator<<(std::ostream& out, const FetchedBlock& x) {
    out << "FetchedBlock(" << "BlockServiceIx=" << (int)x.blockServiceIx << ", " << "BlockId=" << x.blockId << ", " << "Crc32=" << x.crc32 << ")";
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
    out << "CurrentEdge(" << "TargetId=" << x.targetId << ", " << "NameHash=" << x.nameHash << ", " << "Name=" << x.name << ", " << "CreationTime=" << x.creationTime << ")";
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
    out << "Edge(" << "Current=" << x.current << ", " << "TargetId=" << x.targetId << ", " << "NameHash=" << x.nameHash << ", " << "Name=" << x.name << ", " << "CreationTime=" << x.creationTime << ")";
    return out;
}

void FetchedSpan::pack(BincodeBuf& buf) const {
    buf.packVarU61(byteOffset);
    parity.pack(buf);
    buf.packScalar<uint8_t>(storageClass);
    buf.packFixedBytes<4>(crc32);
    buf.packVarU61(size);
    buf.packVarU61(blockSize);
    buf.packBytes(bodyBytes);
    buf.packList<FetchedBlock>(bodyBlocks);
}
void FetchedSpan::unpack(BincodeBuf& buf) {
    byteOffset = buf.unpackVarU61();
    parity.unpack(buf);
    storageClass = buf.unpackScalar<uint8_t>();
    buf.unpackFixedBytes<4>(crc32);
    size = buf.unpackVarU61();
    blockSize = buf.unpackVarU61();
    buf.unpackBytes(bodyBytes);
    buf.unpackList<FetchedBlock>(bodyBlocks);
}
void FetchedSpan::clear() {
    byteOffset = uint64_t(0);
    parity = Parity();
    storageClass = uint8_t(0);
    crc32.clear();
    size = uint64_t(0);
    blockSize = uint64_t(0);
    bodyBytes.clear();
    bodyBlocks.clear();
}
bool FetchedSpan::operator==(const FetchedSpan& rhs) const {
    if ((uint64_t)this->byteOffset != (uint64_t)rhs.byteOffset) { return false; };
    if ((Parity)this->parity != (Parity)rhs.parity) { return false; };
    if ((uint8_t)this->storageClass != (uint8_t)rhs.storageClass) { return false; };
    if (crc32 != rhs.crc32) { return false; };
    if ((uint64_t)this->size != (uint64_t)rhs.size) { return false; };
    if ((uint64_t)this->blockSize != (uint64_t)rhs.blockSize) { return false; };
    if (bodyBytes != rhs.bodyBytes) { return false; };
    if (bodyBlocks != rhs.bodyBlocks) { return false; };
    return true;
}
std::ostream& operator<<(std::ostream& out, const FetchedSpan& x) {
    out << "FetchedSpan(" << "ByteOffset=" << x.byteOffset << ", " << "Parity=" << x.parity << ", " << "StorageClass=" << (int)x.storageClass << ", " << "Crc32=" << x.crc32 << ", " << "Size=" << x.size << ", " << "BlockSize=" << x.blockSize << ", " << "BodyBytes=" << x.bodyBytes << ", " << "BodyBlocks=" << x.bodyBlocks << ")";
    return out;
}

void BlockInfo::pack(BincodeBuf& buf) const {
    buf.packFixedBytes<4>(blockServiceIp);
    buf.packScalar<uint16_t>(blockServicePort);
    buf.packScalar<uint64_t>(blockServiceId);
    buf.packScalar<uint64_t>(blockId);
    buf.packFixedBytes<8>(certificate);
}
void BlockInfo::unpack(BincodeBuf& buf) {
    buf.unpackFixedBytes<4>(blockServiceIp);
    blockServicePort = buf.unpackScalar<uint16_t>();
    blockServiceId = buf.unpackScalar<uint64_t>();
    blockId = buf.unpackScalar<uint64_t>();
    buf.unpackFixedBytes<8>(certificate);
}
void BlockInfo::clear() {
    blockServiceIp.clear();
    blockServicePort = uint16_t(0);
    blockServiceId = uint64_t(0);
    blockId = uint64_t(0);
    certificate.clear();
}
bool BlockInfo::operator==(const BlockInfo& rhs) const {
    if (blockServiceIp != rhs.blockServiceIp) { return false; };
    if ((uint16_t)this->blockServicePort != (uint16_t)rhs.blockServicePort) { return false; };
    if ((uint64_t)this->blockServiceId != (uint64_t)rhs.blockServiceId) { return false; };
    if ((uint64_t)this->blockId != (uint64_t)rhs.blockId) { return false; };
    if (certificate != rhs.certificate) { return false; };
    return true;
}
std::ostream& operator<<(std::ostream& out, const BlockInfo& x) {
    out << "BlockInfo(" << "BlockServiceIp=" << x.blockServiceIp << ", " << "BlockServicePort=" << x.blockServicePort << ", " << "BlockServiceId=" << x.blockServiceId << ", " << "BlockId=" << x.blockId << ", " << "Certificate=" << x.certificate << ")";
    return out;
}

void NewBlockInfo::pack(BincodeBuf& buf) const {
    buf.packFixedBytes<4>(crc32);
}
void NewBlockInfo::unpack(BincodeBuf& buf) {
    buf.unpackFixedBytes<4>(crc32);
}
void NewBlockInfo::clear() {
    crc32.clear();
}
bool NewBlockInfo::operator==(const NewBlockInfo& rhs) const {
    if (crc32 != rhs.crc32) { return false; };
    return true;
}
std::ostream& operator<<(std::ostream& out, const NewBlockInfo& x) {
    out << "NewBlockInfo(" << "Crc32=" << x.crc32 << ")";
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

void SpanPolicy::pack(BincodeBuf& buf) const {
    buf.packScalar<uint64_t>(maxSize);
    buf.packScalar<uint8_t>(storageClass);
    parity.pack(buf);
}
void SpanPolicy::unpack(BincodeBuf& buf) {
    maxSize = buf.unpackScalar<uint64_t>();
    storageClass = buf.unpackScalar<uint8_t>();
    parity.unpack(buf);
}
void SpanPolicy::clear() {
    maxSize = uint64_t(0);
    storageClass = uint8_t(0);
    parity = Parity();
}
bool SpanPolicy::operator==(const SpanPolicy& rhs) const {
    if ((uint64_t)this->maxSize != (uint64_t)rhs.maxSize) { return false; };
    if ((uint8_t)this->storageClass != (uint8_t)rhs.storageClass) { return false; };
    if ((Parity)this->parity != (Parity)rhs.parity) { return false; };
    return true;
}
std::ostream& operator<<(std::ostream& out, const SpanPolicy& x) {
    out << "SpanPolicy(" << "MaxSize=" << x.maxSize << ", " << "StorageClass=" << (int)x.storageClass << ", " << "Parity=" << x.parity << ")";
    return out;
}

void DirectoryInfoBody::pack(BincodeBuf& buf) const {
    buf.packScalar<uint8_t>(version);
    buf.packScalar<uint64_t>(deleteAfterTime);
    buf.packScalar<uint16_t>(deleteAfterVersions);
    buf.packList<SpanPolicy>(spanPolicies);
}
void DirectoryInfoBody::unpack(BincodeBuf& buf) {
    version = buf.unpackScalar<uint8_t>();
    deleteAfterTime = buf.unpackScalar<uint64_t>();
    deleteAfterVersions = buf.unpackScalar<uint16_t>();
    buf.unpackList<SpanPolicy>(spanPolicies);
}
void DirectoryInfoBody::clear() {
    version = uint8_t(0);
    deleteAfterTime = uint64_t(0);
    deleteAfterVersions = uint16_t(0);
    spanPolicies.clear();
}
bool DirectoryInfoBody::operator==(const DirectoryInfoBody& rhs) const {
    if ((uint8_t)this->version != (uint8_t)rhs.version) { return false; };
    if ((uint64_t)this->deleteAfterTime != (uint64_t)rhs.deleteAfterTime) { return false; };
    if ((uint16_t)this->deleteAfterVersions != (uint16_t)rhs.deleteAfterVersions) { return false; };
    if (spanPolicies != rhs.spanPolicies) { return false; };
    return true;
}
std::ostream& operator<<(std::ostream& out, const DirectoryInfoBody& x) {
    out << "DirectoryInfoBody(" << "Version=" << (int)x.version << ", " << "DeleteAfterTime=" << x.deleteAfterTime << ", " << "DeleteAfterVersions=" << x.deleteAfterVersions << ", " << "SpanPolicies=" << x.spanPolicies << ")";
    return out;
}

void SetDirectoryInfo::pack(BincodeBuf& buf) const {
    buf.packScalar<bool>(inherited);
    buf.packBytes(body);
}
void SetDirectoryInfo::unpack(BincodeBuf& buf) {
    inherited = buf.unpackScalar<bool>();
    buf.unpackBytes(body);
}
void SetDirectoryInfo::clear() {
    inherited = bool(0);
    body.clear();
}
bool SetDirectoryInfo::operator==(const SetDirectoryInfo& rhs) const {
    if ((bool)this->inherited != (bool)rhs.inherited) { return false; };
    if (body != rhs.body) { return false; };
    return true;
}
std::ostream& operator<<(std::ostream& out, const SetDirectoryInfo& x) {
    out << "SetDirectoryInfo(" << "Inherited=" << x.inherited << ", " << "Body=" << x.body << ")";
    return out;
}

void BlockServiceBlacklist::pack(BincodeBuf& buf) const {
    buf.packFixedBytes<4>(ip);
    buf.packScalar<uint16_t>(port);
    buf.packScalar<uint64_t>(id);
}
void BlockServiceBlacklist::unpack(BincodeBuf& buf) {
    buf.unpackFixedBytes<4>(ip);
    port = buf.unpackScalar<uint16_t>();
    id = buf.unpackScalar<uint64_t>();
}
void BlockServiceBlacklist::clear() {
    ip.clear();
    port = uint16_t(0);
    id = uint64_t(0);
}
bool BlockServiceBlacklist::operator==(const BlockServiceBlacklist& rhs) const {
    if (ip != rhs.ip) { return false; };
    if ((uint16_t)this->port != (uint16_t)rhs.port) { return false; };
    if ((uint64_t)this->id != (uint64_t)rhs.id) { return false; };
    return true;
}
std::ostream& operator<<(std::ostream& out, const BlockServiceBlacklist& x) {
    out << "BlockServiceBlacklist(" << "Ip=" << x.ip << ", " << "Port=" << x.port << ", " << "Id=" << x.id << ")";
    return out;
}

void BlockService::pack(BincodeBuf& buf) const {
    buf.packFixedBytes<4>(ip);
    buf.packScalar<uint16_t>(port);
    buf.packScalar<uint64_t>(id);
    buf.packScalar<uint8_t>(flags);
}
void BlockService::unpack(BincodeBuf& buf) {
    buf.unpackFixedBytes<4>(ip);
    port = buf.unpackScalar<uint16_t>();
    id = buf.unpackScalar<uint64_t>();
    flags = buf.unpackScalar<uint8_t>();
}
void BlockService::clear() {
    ip.clear();
    port = uint16_t(0);
    id = uint64_t(0);
    flags = uint8_t(0);
}
bool BlockService::operator==(const BlockService& rhs) const {
    if (ip != rhs.ip) { return false; };
    if ((uint16_t)this->port != (uint16_t)rhs.port) { return false; };
    if ((uint64_t)this->id != (uint64_t)rhs.id) { return false; };
    if ((uint8_t)this->flags != (uint8_t)rhs.flags) { return false; };
    return true;
}
std::ostream& operator<<(std::ostream& out, const BlockService& x) {
    out << "BlockService(" << "Ip=" << x.ip << ", " << "Port=" << x.port << ", " << "Id=" << x.id << ", " << "Flags=" << (int)x.flags << ")";
    return out;
}

void FullReadDirCursor::pack(BincodeBuf& buf) const {
    buf.packScalar<bool>(current);
    buf.packScalar<uint64_t>(startHash);
    buf.packBytes(startName);
    startTime.pack(buf);
}
void FullReadDirCursor::unpack(BincodeBuf& buf) {
    current = buf.unpackScalar<bool>();
    startHash = buf.unpackScalar<uint64_t>();
    buf.unpackBytes(startName);
    startTime.unpack(buf);
}
void FullReadDirCursor::clear() {
    current = bool(0);
    startHash = uint64_t(0);
    startName.clear();
    startTime = EggsTime();
}
bool FullReadDirCursor::operator==(const FullReadDirCursor& rhs) const {
    if ((bool)this->current != (bool)rhs.current) { return false; };
    if ((uint64_t)this->startHash != (uint64_t)rhs.startHash) { return false; };
    if (startName != rhs.startName) { return false; };
    if ((EggsTime)this->startTime != (EggsTime)rhs.startTime) { return false; };
    return true;
}
std::ostream& operator<<(std::ostream& out, const FullReadDirCursor& x) {
    out << "FullReadDirCursor(" << "Current=" << x.current << ", " << "StartHash=" << x.startHash << ", " << "StartName=" << x.startName << ", " << "StartTime=" << x.startTime << ")";
    return out;
}

void EntryBlockService::pack(BincodeBuf& buf) const {
    buf.packScalar<uint64_t>(id);
    buf.packFixedBytes<4>(ip);
    buf.packScalar<uint16_t>(port);
    buf.packScalar<uint8_t>(storageClass);
    buf.packFixedBytes<16>(failureDomain);
    buf.packFixedBytes<16>(secretKey);
}
void EntryBlockService::unpack(BincodeBuf& buf) {
    id = buf.unpackScalar<uint64_t>();
    buf.unpackFixedBytes<4>(ip);
    port = buf.unpackScalar<uint16_t>();
    storageClass = buf.unpackScalar<uint8_t>();
    buf.unpackFixedBytes<16>(failureDomain);
    buf.unpackFixedBytes<16>(secretKey);
}
void EntryBlockService::clear() {
    id = uint64_t(0);
    ip.clear();
    port = uint16_t(0);
    storageClass = uint8_t(0);
    failureDomain.clear();
    secretKey.clear();
}
bool EntryBlockService::operator==(const EntryBlockService& rhs) const {
    if ((uint64_t)this->id != (uint64_t)rhs.id) { return false; };
    if (ip != rhs.ip) { return false; };
    if ((uint16_t)this->port != (uint16_t)rhs.port) { return false; };
    if ((uint8_t)this->storageClass != (uint8_t)rhs.storageClass) { return false; };
    if (failureDomain != rhs.failureDomain) { return false; };
    if (secretKey != rhs.secretKey) { return false; };
    return true;
}
std::ostream& operator<<(std::ostream& out, const EntryBlockService& x) {
    out << "EntryBlockService(" << "Id=" << x.id << ", " << "Ip=" << x.ip << ", " << "Port=" << x.port << ", " << "StorageClass=" << (int)x.storageClass << ", " << "FailureDomain=" << x.failureDomain << ", " << "SecretKey=" << x.secretKey << ")";
    return out;
}

void EntryNewBlockInfo::pack(BincodeBuf& buf) const {
    buf.packScalar<uint64_t>(blockServiceId);
    buf.packFixedBytes<4>(crc32);
}
void EntryNewBlockInfo::unpack(BincodeBuf& buf) {
    blockServiceId = buf.unpackScalar<uint64_t>();
    buf.unpackFixedBytes<4>(crc32);
}
void EntryNewBlockInfo::clear() {
    blockServiceId = uint64_t(0);
    crc32.clear();
}
bool EntryNewBlockInfo::operator==(const EntryNewBlockInfo& rhs) const {
    if ((uint64_t)this->blockServiceId != (uint64_t)rhs.blockServiceId) { return false; };
    if (crc32 != rhs.crc32) { return false; };
    return true;
}
std::ostream& operator<<(std::ostream& out, const EntryNewBlockInfo& x) {
    out << "EntryNewBlockInfo(" << "BlockServiceId=" << x.blockServiceId << ", " << "Crc32=" << x.crc32 << ")";
    return out;
}

void SnapshotLookupEdge::pack(BincodeBuf& buf) const {
    targetId.pack(buf);
    creationTime.pack(buf);
}
void SnapshotLookupEdge::unpack(BincodeBuf& buf) {
    targetId.unpack(buf);
    creationTime.unpack(buf);
}
void SnapshotLookupEdge::clear() {
    targetId = InodeIdExtra();
    creationTime = EggsTime();
}
bool SnapshotLookupEdge::operator==(const SnapshotLookupEdge& rhs) const {
    if ((InodeIdExtra)this->targetId != (InodeIdExtra)rhs.targetId) { return false; };
    if ((EggsTime)this->creationTime != (EggsTime)rhs.creationTime) { return false; };
    return true;
}
std::ostream& operator<<(std::ostream& out, const SnapshotLookupEdge& x) {
    out << "SnapshotLookupEdge(" << "TargetId=" << x.targetId << ", " << "CreationTime=" << x.creationTime << ")";
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
    out << "LookupReq(" << "DirId=" << x.dirId << ", " << "Name=" << x.name << ")";
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
    buf.packScalar<uint64_t>(size);
}
void StatFileResp::unpack(BincodeBuf& buf) {
    mtime.unpack(buf);
    size = buf.unpackScalar<uint64_t>();
}
void StatFileResp::clear() {
    mtime = EggsTime();
    size = uint64_t(0);
}
bool StatFileResp::operator==(const StatFileResp& rhs) const {
    if ((EggsTime)this->mtime != (EggsTime)rhs.mtime) { return false; };
    if ((uint64_t)this->size != (uint64_t)rhs.size) { return false; };
    return true;
}
std::ostream& operator<<(std::ostream& out, const StatFileResp& x) {
    out << "StatFileResp(" << "Mtime=" << x.mtime << ", " << "Size=" << x.size << ")";
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
    out << "StatTransientFileResp(" << "Mtime=" << x.mtime << ", " << "Size=" << x.size << ", " << "Note=" << x.note << ")";
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
    buf.packBytes(info);
}
void StatDirectoryResp::unpack(BincodeBuf& buf) {
    mtime.unpack(buf);
    owner.unpack(buf);
    buf.unpackBytes(info);
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
}
void ReadDirReq::unpack(BincodeBuf& buf) {
    dirId.unpack(buf);
    startHash = buf.unpackScalar<uint64_t>();
}
void ReadDirReq::clear() {
    dirId = InodeId();
    startHash = uint64_t(0);
}
bool ReadDirReq::operator==(const ReadDirReq& rhs) const {
    if ((InodeId)this->dirId != (InodeId)rhs.dirId) { return false; };
    if ((uint64_t)this->startHash != (uint64_t)rhs.startHash) { return false; };
    return true;
}
std::ostream& operator<<(std::ostream& out, const ReadDirReq& x) {
    out << "ReadDirReq(" << "DirId=" << x.dirId << ", " << "StartHash=" << x.startHash << ")";
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
    out << "ConstructFileReq(" << "Type=" << (int)x.type << ", " << "Note=" << x.note << ")";
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
    buf.packVarU61(byteOffset);
    buf.packScalar<uint8_t>(storageClass);
    buf.packList<BlockServiceBlacklist>(blacklist);
    parity.pack(buf);
    buf.packFixedBytes<4>(crc32);
    buf.packVarU61(size);
    buf.packVarU61(blockSize);
    buf.packBytes(bodyBytes);
    buf.packList<NewBlockInfo>(bodyBlocks);
}
void AddSpanInitiateReq::unpack(BincodeBuf& buf) {
    fileId.unpack(buf);
    buf.unpackFixedBytes<8>(cookie);
    byteOffset = buf.unpackVarU61();
    storageClass = buf.unpackScalar<uint8_t>();
    buf.unpackList<BlockServiceBlacklist>(blacklist);
    parity.unpack(buf);
    buf.unpackFixedBytes<4>(crc32);
    size = buf.unpackVarU61();
    blockSize = buf.unpackVarU61();
    buf.unpackBytes(bodyBytes);
    buf.unpackList<NewBlockInfo>(bodyBlocks);
}
void AddSpanInitiateReq::clear() {
    fileId = InodeId();
    cookie.clear();
    byteOffset = uint64_t(0);
    storageClass = uint8_t(0);
    blacklist.clear();
    parity = Parity();
    crc32.clear();
    size = uint64_t(0);
    blockSize = uint64_t(0);
    bodyBytes.clear();
    bodyBlocks.clear();
}
bool AddSpanInitiateReq::operator==(const AddSpanInitiateReq& rhs) const {
    if ((InodeId)this->fileId != (InodeId)rhs.fileId) { return false; };
    if (cookie != rhs.cookie) { return false; };
    if ((uint64_t)this->byteOffset != (uint64_t)rhs.byteOffset) { return false; };
    if ((uint8_t)this->storageClass != (uint8_t)rhs.storageClass) { return false; };
    if (blacklist != rhs.blacklist) { return false; };
    if ((Parity)this->parity != (Parity)rhs.parity) { return false; };
    if (crc32 != rhs.crc32) { return false; };
    if ((uint64_t)this->size != (uint64_t)rhs.size) { return false; };
    if ((uint64_t)this->blockSize != (uint64_t)rhs.blockSize) { return false; };
    if (bodyBytes != rhs.bodyBytes) { return false; };
    if (bodyBlocks != rhs.bodyBlocks) { return false; };
    return true;
}
std::ostream& operator<<(std::ostream& out, const AddSpanInitiateReq& x) {
    out << "AddSpanInitiateReq(" << "FileId=" << x.fileId << ", " << "Cookie=" << x.cookie << ", " << "ByteOffset=" << x.byteOffset << ", " << "StorageClass=" << (int)x.storageClass << ", " << "Blacklist=" << x.blacklist << ", " << "Parity=" << x.parity << ", " << "Crc32=" << x.crc32 << ", " << "Size=" << x.size << ", " << "BlockSize=" << x.blockSize << ", " << "BodyBytes=" << x.bodyBytes << ", " << "BodyBlocks=" << x.bodyBlocks << ")";
    return out;
}

void AddSpanInitiateResp::pack(BincodeBuf& buf) const {
    buf.packList<BlockInfo>(blocks);
}
void AddSpanInitiateResp::unpack(BincodeBuf& buf) {
    buf.unpackList<BlockInfo>(blocks);
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
    buf.packVarU61(byteOffset);
    buf.packList<BlockProof>(proofs);
}
void AddSpanCertifyReq::unpack(BincodeBuf& buf) {
    fileId.unpack(buf);
    buf.unpackFixedBytes<8>(cookie);
    byteOffset = buf.unpackVarU61();
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
    out << "LinkFileReq(" << "FileId=" << x.fileId << ", " << "Cookie=" << x.cookie << ", " << "OwnerId=" << x.ownerId << ", " << "Name=" << x.name << ")";
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
    out << "SoftUnlinkFileReq(" << "OwnerId=" << x.ownerId << ", " << "FileId=" << x.fileId << ", " << "Name=" << x.name << ", " << "CreationTime=" << x.creationTime << ")";
    return out;
}

void SoftUnlinkFileResp::pack(BincodeBuf& buf) const {
}
void SoftUnlinkFileResp::unpack(BincodeBuf& buf) {
}
void SoftUnlinkFileResp::clear() {
}
bool SoftUnlinkFileResp::operator==(const SoftUnlinkFileResp& rhs) const {
    return true;
}
std::ostream& operator<<(std::ostream& out, const SoftUnlinkFileResp& x) {
    out << "SoftUnlinkFileResp(" << ")";
    return out;
}

void FileSpansReq::pack(BincodeBuf& buf) const {
    fileId.pack(buf);
    buf.packVarU61(byteOffset);
}
void FileSpansReq::unpack(BincodeBuf& buf) {
    fileId.unpack(buf);
    byteOffset = buf.unpackVarU61();
}
void FileSpansReq::clear() {
    fileId = InodeId();
    byteOffset = uint64_t(0);
}
bool FileSpansReq::operator==(const FileSpansReq& rhs) const {
    if ((InodeId)this->fileId != (InodeId)rhs.fileId) { return false; };
    if ((uint64_t)this->byteOffset != (uint64_t)rhs.byteOffset) { return false; };
    return true;
}
std::ostream& operator<<(std::ostream& out, const FileSpansReq& x) {
    out << "FileSpansReq(" << "FileId=" << x.fileId << ", " << "ByteOffset=" << x.byteOffset << ")";
    return out;
}

void FileSpansResp::pack(BincodeBuf& buf) const {
    buf.packVarU61(nextOffset);
    buf.packList<BlockService>(blockServices);
    buf.packList<FetchedSpan>(spans);
}
void FileSpansResp::unpack(BincodeBuf& buf) {
    nextOffset = buf.unpackVarU61();
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
    out << "SameDirectoryRenameReq(" << "TargetId=" << x.targetId << ", " << "DirId=" << x.dirId << ", " << "OldName=" << x.oldName << ", " << "OldCreationTime=" << x.oldCreationTime << ", " << "NewName=" << x.newName << ")";
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

void SnapshotLookupReq::pack(BincodeBuf& buf) const {
    dirId.pack(buf);
    buf.packBytes(name);
    startFrom.pack(buf);
}
void SnapshotLookupReq::unpack(BincodeBuf& buf) {
    dirId.unpack(buf);
    buf.unpackBytes(name);
    startFrom.unpack(buf);
}
void SnapshotLookupReq::clear() {
    dirId = InodeId();
    name.clear();
    startFrom = EggsTime();
}
bool SnapshotLookupReq::operator==(const SnapshotLookupReq& rhs) const {
    if ((InodeId)this->dirId != (InodeId)rhs.dirId) { return false; };
    if (name != rhs.name) { return false; };
    if ((EggsTime)this->startFrom != (EggsTime)rhs.startFrom) { return false; };
    return true;
}
std::ostream& operator<<(std::ostream& out, const SnapshotLookupReq& x) {
    out << "SnapshotLookupReq(" << "DirId=" << x.dirId << ", " << "Name=" << x.name << ", " << "StartFrom=" << x.startFrom << ")";
    return out;
}

void SnapshotLookupResp::pack(BincodeBuf& buf) const {
    nextTime.pack(buf);
    buf.packList<SnapshotLookupEdge>(edges);
}
void SnapshotLookupResp::unpack(BincodeBuf& buf) {
    nextTime.unpack(buf);
    buf.unpackList<SnapshotLookupEdge>(edges);
}
void SnapshotLookupResp::clear() {
    nextTime = EggsTime();
    edges.clear();
}
bool SnapshotLookupResp::operator==(const SnapshotLookupResp& rhs) const {
    if ((EggsTime)this->nextTime != (EggsTime)rhs.nextTime) { return false; };
    if (edges != rhs.edges) { return false; };
    return true;
}
std::ostream& operator<<(std::ostream& out, const SnapshotLookupResp& x) {
    out << "SnapshotLookupResp(" << "NextTime=" << x.nextTime << ", " << "Edges=" << x.edges << ")";
    return out;
}

void VisitDirectoriesReq::pack(BincodeBuf& buf) const {
    beginId.pack(buf);
}
void VisitDirectoriesReq::unpack(BincodeBuf& buf) {
    beginId.unpack(buf);
}
void VisitDirectoriesReq::clear() {
    beginId = InodeId();
}
bool VisitDirectoriesReq::operator==(const VisitDirectoriesReq& rhs) const {
    if ((InodeId)this->beginId != (InodeId)rhs.beginId) { return false; };
    return true;
}
std::ostream& operator<<(std::ostream& out, const VisitDirectoriesReq& x) {
    out << "VisitDirectoriesReq(" << "BeginId=" << x.beginId << ")";
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
}
void VisitFilesReq::unpack(BincodeBuf& buf) {
    beginId.unpack(buf);
}
void VisitFilesReq::clear() {
    beginId = InodeId();
}
bool VisitFilesReq::operator==(const VisitFilesReq& rhs) const {
    if ((InodeId)this->beginId != (InodeId)rhs.beginId) { return false; };
    return true;
}
std::ostream& operator<<(std::ostream& out, const VisitFilesReq& x) {
    out << "VisitFilesReq(" << "BeginId=" << x.beginId << ")";
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
}
void VisitTransientFilesReq::unpack(BincodeBuf& buf) {
    beginId.unpack(buf);
}
void VisitTransientFilesReq::clear() {
    beginId = InodeId();
}
bool VisitTransientFilesReq::operator==(const VisitTransientFilesReq& rhs) const {
    if ((InodeId)this->beginId != (InodeId)rhs.beginId) { return false; };
    return true;
}
std::ostream& operator<<(std::ostream& out, const VisitTransientFilesReq& x) {
    out << "VisitTransientFilesReq(" << "BeginId=" << x.beginId << ")";
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

void FullReadDirReq::pack(BincodeBuf& buf) const {
    dirId.pack(buf);
    cursor.pack(buf);
}
void FullReadDirReq::unpack(BincodeBuf& buf) {
    dirId.unpack(buf);
    cursor.unpack(buf);
}
void FullReadDirReq::clear() {
    dirId = InodeId();
    cursor.clear();
}
bool FullReadDirReq::operator==(const FullReadDirReq& rhs) const {
    if ((InodeId)this->dirId != (InodeId)rhs.dirId) { return false; };
    if (cursor != rhs.cursor) { return false; };
    return true;
}
std::ostream& operator<<(std::ostream& out, const FullReadDirReq& x) {
    out << "FullReadDirReq(" << "DirId=" << x.dirId << ", " << "Cursor=" << x.cursor << ")";
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
    out << "RemoveNonOwnedEdgeReq(" << "DirId=" << x.dirId << ", " << "TargetId=" << x.targetId << ", " << "Name=" << x.name << ", " << "CreationTime=" << x.creationTime << ")";
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
    out << "SameShardHardFileUnlinkReq(" << "OwnerId=" << x.ownerId << ", " << "TargetId=" << x.targetId << ", " << "Name=" << x.name << ", " << "CreationTime=" << x.creationTime << ")";
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
    buf.packVarU61(byteOffset);
    buf.packList<BlockInfo>(blocks);
}
void RemoveSpanInitiateResp::unpack(BincodeBuf& buf) {
    byteOffset = buf.unpackVarU61();
    buf.unpackList<BlockInfo>(blocks);
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
    buf.packVarU61(byteOffset);
    buf.packList<BlockProof>(proofs);
}
void RemoveSpanCertifyReq::unpack(BincodeBuf& buf) {
    fileId.unpack(buf);
    buf.unpackFixedBytes<8>(cookie);
    byteOffset = buf.unpackVarU61();
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
    buf.packScalar<uint64_t>(blockServiceId);
    startFrom.pack(buf);
}
void BlockServiceFilesReq::unpack(BincodeBuf& buf) {
    blockServiceId = buf.unpackScalar<uint64_t>();
    startFrom.unpack(buf);
}
void BlockServiceFilesReq::clear() {
    blockServiceId = uint64_t(0);
    startFrom = InodeId();
}
bool BlockServiceFilesReq::operator==(const BlockServiceFilesReq& rhs) const {
    if ((uint64_t)this->blockServiceId != (uint64_t)rhs.blockServiceId) { return false; };
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
    buf.packBytes(info);
}
void RemoveDirectoryOwnerReq::unpack(BincodeBuf& buf) {
    dirId.unpack(buf);
    buf.unpackBytes(info);
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
}
void CreateLockedCurrentEdgeReq::unpack(BincodeBuf& buf) {
    dirId.unpack(buf);
    buf.unpackBytes(name);
    targetId.unpack(buf);
}
void CreateLockedCurrentEdgeReq::clear() {
    dirId = InodeId();
    name.clear();
    targetId = InodeId();
}
bool CreateLockedCurrentEdgeReq::operator==(const CreateLockedCurrentEdgeReq& rhs) const {
    if ((InodeId)this->dirId != (InodeId)rhs.dirId) { return false; };
    if (name != rhs.name) { return false; };
    if ((InodeId)this->targetId != (InodeId)rhs.targetId) { return false; };
    return true;
}
std::ostream& operator<<(std::ostream& out, const CreateLockedCurrentEdgeReq& x) {
    out << "CreateLockedCurrentEdgeReq(" << "DirId=" << x.dirId << ", " << "Name=" << x.name << ", " << "TargetId=" << x.targetId << ")";
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
    out << "LockCurrentEdgeReq(" << "DirId=" << x.dirId << ", " << "TargetId=" << x.targetId << ", " << "CreationTime=" << x.creationTime << ", " << "Name=" << x.name << ")";
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
    out << "UnlockCurrentEdgeReq(" << "DirId=" << x.dirId << ", " << "Name=" << x.name << ", " << "CreationTime=" << x.creationTime << ", " << "TargetId=" << x.targetId << ", " << "WasMoved=" << x.wasMoved << ")";
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
    out << "RemoveOwnedSnapshotFileEdgeReq(" << "OwnerId=" << x.ownerId << ", " << "TargetId=" << x.targetId << ", " << "Name=" << x.name << ", " << "CreationTime=" << x.creationTime << ")";
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
    out << "MakeFileTransientReq(" << "Id=" << x.id << ", " << "Note=" << x.note << ")";
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
    info.pack(buf);
}
void MakeDirectoryReq::unpack(BincodeBuf& buf) {
    ownerId.unpack(buf);
    buf.unpackBytes(name);
    info.unpack(buf);
}
void MakeDirectoryReq::clear() {
    ownerId = InodeId();
    name.clear();
    info.clear();
}
bool MakeDirectoryReq::operator==(const MakeDirectoryReq& rhs) const {
    if ((InodeId)this->ownerId != (InodeId)rhs.ownerId) { return false; };
    if (name != rhs.name) { return false; };
    if (info != rhs.info) { return false; };
    return true;
}
std::ostream& operator<<(std::ostream& out, const MakeDirectoryReq& x) {
    out << "MakeDirectoryReq(" << "OwnerId=" << x.ownerId << ", " << "Name=" << x.name << ", " << "Info=" << x.info << ")";
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
    out << "RenameFileReq(" << "TargetId=" << x.targetId << ", " << "OldOwnerId=" << x.oldOwnerId << ", " << "OldName=" << x.oldName << ", " << "OldCreationTime=" << x.oldCreationTime << ", " << "NewOwnerId=" << x.newOwnerId << ", " << "NewName=" << x.newName << ")";
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
    out << "SoftUnlinkDirectoryReq(" << "OwnerId=" << x.ownerId << ", " << "TargetId=" << x.targetId << ", " << "CreationTime=" << x.creationTime << ", " << "Name=" << x.name << ")";
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
    out << "RenameDirectoryReq(" << "TargetId=" << x.targetId << ", " << "OldOwnerId=" << x.oldOwnerId << ", " << "OldName=" << x.oldName << ", " << "OldCreationTime=" << x.oldCreationTime << ", " << "NewOwnerId=" << x.newOwnerId << ", " << "NewName=" << x.newName << ")";
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
    out << "CrossShardHardUnlinkFileReq(" << "OwnerId=" << x.ownerId << ", " << "TargetId=" << x.targetId << ", " << "Name=" << x.name << ", " << "CreationTime=" << x.creationTime << ")";
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

std::ostream& operator<<(std::ostream& out, ShardMessageKind kind) {
    switch (kind) {
    case ShardMessageKind::LOOKUP:
        out << "LOOKUP";
        break;
    case ShardMessageKind::STAT_FILE:
        out << "STAT_FILE";
        break;
    case ShardMessageKind::STAT_TRANSIENT_FILE:
        out << "STAT_TRANSIENT_FILE";
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
    case ShardMessageKind::SET_DIRECTORY_INFO:
        out << "SET_DIRECTORY_INFO";
        break;
    case ShardMessageKind::SNAPSHOT_LOOKUP:
        out << "SNAPSHOT_LOOKUP";
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
    case ShardMessageKind::FULL_READ_DIR:
        out << "FULL_READ_DIR";
        break;
    case ShardMessageKind::REMOVE_NON_OWNED_EDGE:
        out << "REMOVE_NON_OWNED_EDGE";
        break;
    case ShardMessageKind::SAME_SHARD_HARD_FILE_UNLINK:
        out << "SAME_SHARD_HARD_FILE_UNLINK";
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

const LookupReq& ShardReqContainer::getLookup() const {
    ALWAYS_ASSERT(_kind == ShardMessageKind::LOOKUP, "%s != %s", _kind, ShardMessageKind::LOOKUP);
    return std::get<0>(_data);
}
LookupReq& ShardReqContainer::setLookup() {
    _kind = ShardMessageKind::LOOKUP;
    auto& x = std::get<0>(_data);
    x.clear();
    return x;
}
const StatFileReq& ShardReqContainer::getStatFile() const {
    ALWAYS_ASSERT(_kind == ShardMessageKind::STAT_FILE, "%s != %s", _kind, ShardMessageKind::STAT_FILE);
    return std::get<1>(_data);
}
StatFileReq& ShardReqContainer::setStatFile() {
    _kind = ShardMessageKind::STAT_FILE;
    auto& x = std::get<1>(_data);
    x.clear();
    return x;
}
const StatTransientFileReq& ShardReqContainer::getStatTransientFile() const {
    ALWAYS_ASSERT(_kind == ShardMessageKind::STAT_TRANSIENT_FILE, "%s != %s", _kind, ShardMessageKind::STAT_TRANSIENT_FILE);
    return std::get<2>(_data);
}
StatTransientFileReq& ShardReqContainer::setStatTransientFile() {
    _kind = ShardMessageKind::STAT_TRANSIENT_FILE;
    auto& x = std::get<2>(_data);
    x.clear();
    return x;
}
const StatDirectoryReq& ShardReqContainer::getStatDirectory() const {
    ALWAYS_ASSERT(_kind == ShardMessageKind::STAT_DIRECTORY, "%s != %s", _kind, ShardMessageKind::STAT_DIRECTORY);
    return std::get<3>(_data);
}
StatDirectoryReq& ShardReqContainer::setStatDirectory() {
    _kind = ShardMessageKind::STAT_DIRECTORY;
    auto& x = std::get<3>(_data);
    x.clear();
    return x;
}
const ReadDirReq& ShardReqContainer::getReadDir() const {
    ALWAYS_ASSERT(_kind == ShardMessageKind::READ_DIR, "%s != %s", _kind, ShardMessageKind::READ_DIR);
    return std::get<4>(_data);
}
ReadDirReq& ShardReqContainer::setReadDir() {
    _kind = ShardMessageKind::READ_DIR;
    auto& x = std::get<4>(_data);
    x.clear();
    return x;
}
const ConstructFileReq& ShardReqContainer::getConstructFile() const {
    ALWAYS_ASSERT(_kind == ShardMessageKind::CONSTRUCT_FILE, "%s != %s", _kind, ShardMessageKind::CONSTRUCT_FILE);
    return std::get<5>(_data);
}
ConstructFileReq& ShardReqContainer::setConstructFile() {
    _kind = ShardMessageKind::CONSTRUCT_FILE;
    auto& x = std::get<5>(_data);
    x.clear();
    return x;
}
const AddSpanInitiateReq& ShardReqContainer::getAddSpanInitiate() const {
    ALWAYS_ASSERT(_kind == ShardMessageKind::ADD_SPAN_INITIATE, "%s != %s", _kind, ShardMessageKind::ADD_SPAN_INITIATE);
    return std::get<6>(_data);
}
AddSpanInitiateReq& ShardReqContainer::setAddSpanInitiate() {
    _kind = ShardMessageKind::ADD_SPAN_INITIATE;
    auto& x = std::get<6>(_data);
    x.clear();
    return x;
}
const AddSpanCertifyReq& ShardReqContainer::getAddSpanCertify() const {
    ALWAYS_ASSERT(_kind == ShardMessageKind::ADD_SPAN_CERTIFY, "%s != %s", _kind, ShardMessageKind::ADD_SPAN_CERTIFY);
    return std::get<7>(_data);
}
AddSpanCertifyReq& ShardReqContainer::setAddSpanCertify() {
    _kind = ShardMessageKind::ADD_SPAN_CERTIFY;
    auto& x = std::get<7>(_data);
    x.clear();
    return x;
}
const LinkFileReq& ShardReqContainer::getLinkFile() const {
    ALWAYS_ASSERT(_kind == ShardMessageKind::LINK_FILE, "%s != %s", _kind, ShardMessageKind::LINK_FILE);
    return std::get<8>(_data);
}
LinkFileReq& ShardReqContainer::setLinkFile() {
    _kind = ShardMessageKind::LINK_FILE;
    auto& x = std::get<8>(_data);
    x.clear();
    return x;
}
const SoftUnlinkFileReq& ShardReqContainer::getSoftUnlinkFile() const {
    ALWAYS_ASSERT(_kind == ShardMessageKind::SOFT_UNLINK_FILE, "%s != %s", _kind, ShardMessageKind::SOFT_UNLINK_FILE);
    return std::get<9>(_data);
}
SoftUnlinkFileReq& ShardReqContainer::setSoftUnlinkFile() {
    _kind = ShardMessageKind::SOFT_UNLINK_FILE;
    auto& x = std::get<9>(_data);
    x.clear();
    return x;
}
const FileSpansReq& ShardReqContainer::getFileSpans() const {
    ALWAYS_ASSERT(_kind == ShardMessageKind::FILE_SPANS, "%s != %s", _kind, ShardMessageKind::FILE_SPANS);
    return std::get<10>(_data);
}
FileSpansReq& ShardReqContainer::setFileSpans() {
    _kind = ShardMessageKind::FILE_SPANS;
    auto& x = std::get<10>(_data);
    x.clear();
    return x;
}
const SameDirectoryRenameReq& ShardReqContainer::getSameDirectoryRename() const {
    ALWAYS_ASSERT(_kind == ShardMessageKind::SAME_DIRECTORY_RENAME, "%s != %s", _kind, ShardMessageKind::SAME_DIRECTORY_RENAME);
    return std::get<11>(_data);
}
SameDirectoryRenameReq& ShardReqContainer::setSameDirectoryRename() {
    _kind = ShardMessageKind::SAME_DIRECTORY_RENAME;
    auto& x = std::get<11>(_data);
    x.clear();
    return x;
}
const SetDirectoryInfoReq& ShardReqContainer::getSetDirectoryInfo() const {
    ALWAYS_ASSERT(_kind == ShardMessageKind::SET_DIRECTORY_INFO, "%s != %s", _kind, ShardMessageKind::SET_DIRECTORY_INFO);
    return std::get<12>(_data);
}
SetDirectoryInfoReq& ShardReqContainer::setSetDirectoryInfo() {
    _kind = ShardMessageKind::SET_DIRECTORY_INFO;
    auto& x = std::get<12>(_data);
    x.clear();
    return x;
}
const SnapshotLookupReq& ShardReqContainer::getSnapshotLookup() const {
    ALWAYS_ASSERT(_kind == ShardMessageKind::SNAPSHOT_LOOKUP, "%s != %s", _kind, ShardMessageKind::SNAPSHOT_LOOKUP);
    return std::get<13>(_data);
}
SnapshotLookupReq& ShardReqContainer::setSnapshotLookup() {
    _kind = ShardMessageKind::SNAPSHOT_LOOKUP;
    auto& x = std::get<13>(_data);
    x.clear();
    return x;
}
const VisitDirectoriesReq& ShardReqContainer::getVisitDirectories() const {
    ALWAYS_ASSERT(_kind == ShardMessageKind::VISIT_DIRECTORIES, "%s != %s", _kind, ShardMessageKind::VISIT_DIRECTORIES);
    return std::get<14>(_data);
}
VisitDirectoriesReq& ShardReqContainer::setVisitDirectories() {
    _kind = ShardMessageKind::VISIT_DIRECTORIES;
    auto& x = std::get<14>(_data);
    x.clear();
    return x;
}
const VisitFilesReq& ShardReqContainer::getVisitFiles() const {
    ALWAYS_ASSERT(_kind == ShardMessageKind::VISIT_FILES, "%s != %s", _kind, ShardMessageKind::VISIT_FILES);
    return std::get<15>(_data);
}
VisitFilesReq& ShardReqContainer::setVisitFiles() {
    _kind = ShardMessageKind::VISIT_FILES;
    auto& x = std::get<15>(_data);
    x.clear();
    return x;
}
const VisitTransientFilesReq& ShardReqContainer::getVisitTransientFiles() const {
    ALWAYS_ASSERT(_kind == ShardMessageKind::VISIT_TRANSIENT_FILES, "%s != %s", _kind, ShardMessageKind::VISIT_TRANSIENT_FILES);
    return std::get<16>(_data);
}
VisitTransientFilesReq& ShardReqContainer::setVisitTransientFiles() {
    _kind = ShardMessageKind::VISIT_TRANSIENT_FILES;
    auto& x = std::get<16>(_data);
    x.clear();
    return x;
}
const FullReadDirReq& ShardReqContainer::getFullReadDir() const {
    ALWAYS_ASSERT(_kind == ShardMessageKind::FULL_READ_DIR, "%s != %s", _kind, ShardMessageKind::FULL_READ_DIR);
    return std::get<17>(_data);
}
FullReadDirReq& ShardReqContainer::setFullReadDir() {
    _kind = ShardMessageKind::FULL_READ_DIR;
    auto& x = std::get<17>(_data);
    x.clear();
    return x;
}
const RemoveNonOwnedEdgeReq& ShardReqContainer::getRemoveNonOwnedEdge() const {
    ALWAYS_ASSERT(_kind == ShardMessageKind::REMOVE_NON_OWNED_EDGE, "%s != %s", _kind, ShardMessageKind::REMOVE_NON_OWNED_EDGE);
    return std::get<18>(_data);
}
RemoveNonOwnedEdgeReq& ShardReqContainer::setRemoveNonOwnedEdge() {
    _kind = ShardMessageKind::REMOVE_NON_OWNED_EDGE;
    auto& x = std::get<18>(_data);
    x.clear();
    return x;
}
const SameShardHardFileUnlinkReq& ShardReqContainer::getSameShardHardFileUnlink() const {
    ALWAYS_ASSERT(_kind == ShardMessageKind::SAME_SHARD_HARD_FILE_UNLINK, "%s != %s", _kind, ShardMessageKind::SAME_SHARD_HARD_FILE_UNLINK);
    return std::get<19>(_data);
}
SameShardHardFileUnlinkReq& ShardReqContainer::setSameShardHardFileUnlink() {
    _kind = ShardMessageKind::SAME_SHARD_HARD_FILE_UNLINK;
    auto& x = std::get<19>(_data);
    x.clear();
    return x;
}
const RemoveSpanInitiateReq& ShardReqContainer::getRemoveSpanInitiate() const {
    ALWAYS_ASSERT(_kind == ShardMessageKind::REMOVE_SPAN_INITIATE, "%s != %s", _kind, ShardMessageKind::REMOVE_SPAN_INITIATE);
    return std::get<20>(_data);
}
RemoveSpanInitiateReq& ShardReqContainer::setRemoveSpanInitiate() {
    _kind = ShardMessageKind::REMOVE_SPAN_INITIATE;
    auto& x = std::get<20>(_data);
    x.clear();
    return x;
}
const RemoveSpanCertifyReq& ShardReqContainer::getRemoveSpanCertify() const {
    ALWAYS_ASSERT(_kind == ShardMessageKind::REMOVE_SPAN_CERTIFY, "%s != %s", _kind, ShardMessageKind::REMOVE_SPAN_CERTIFY);
    return std::get<21>(_data);
}
RemoveSpanCertifyReq& ShardReqContainer::setRemoveSpanCertify() {
    _kind = ShardMessageKind::REMOVE_SPAN_CERTIFY;
    auto& x = std::get<21>(_data);
    x.clear();
    return x;
}
const SwapBlocksReq& ShardReqContainer::getSwapBlocks() const {
    ALWAYS_ASSERT(_kind == ShardMessageKind::SWAP_BLOCKS, "%s != %s", _kind, ShardMessageKind::SWAP_BLOCKS);
    return std::get<22>(_data);
}
SwapBlocksReq& ShardReqContainer::setSwapBlocks() {
    _kind = ShardMessageKind::SWAP_BLOCKS;
    auto& x = std::get<22>(_data);
    x.clear();
    return x;
}
const BlockServiceFilesReq& ShardReqContainer::getBlockServiceFiles() const {
    ALWAYS_ASSERT(_kind == ShardMessageKind::BLOCK_SERVICE_FILES, "%s != %s", _kind, ShardMessageKind::BLOCK_SERVICE_FILES);
    return std::get<23>(_data);
}
BlockServiceFilesReq& ShardReqContainer::setBlockServiceFiles() {
    _kind = ShardMessageKind::BLOCK_SERVICE_FILES;
    auto& x = std::get<23>(_data);
    x.clear();
    return x;
}
const RemoveInodeReq& ShardReqContainer::getRemoveInode() const {
    ALWAYS_ASSERT(_kind == ShardMessageKind::REMOVE_INODE, "%s != %s", _kind, ShardMessageKind::REMOVE_INODE);
    return std::get<24>(_data);
}
RemoveInodeReq& ShardReqContainer::setRemoveInode() {
    _kind = ShardMessageKind::REMOVE_INODE;
    auto& x = std::get<24>(_data);
    x.clear();
    return x;
}
const CreateDirectoryInodeReq& ShardReqContainer::getCreateDirectoryInode() const {
    ALWAYS_ASSERT(_kind == ShardMessageKind::CREATE_DIRECTORY_INODE, "%s != %s", _kind, ShardMessageKind::CREATE_DIRECTORY_INODE);
    return std::get<25>(_data);
}
CreateDirectoryInodeReq& ShardReqContainer::setCreateDirectoryInode() {
    _kind = ShardMessageKind::CREATE_DIRECTORY_INODE;
    auto& x = std::get<25>(_data);
    x.clear();
    return x;
}
const SetDirectoryOwnerReq& ShardReqContainer::getSetDirectoryOwner() const {
    ALWAYS_ASSERT(_kind == ShardMessageKind::SET_DIRECTORY_OWNER, "%s != %s", _kind, ShardMessageKind::SET_DIRECTORY_OWNER);
    return std::get<26>(_data);
}
SetDirectoryOwnerReq& ShardReqContainer::setSetDirectoryOwner() {
    _kind = ShardMessageKind::SET_DIRECTORY_OWNER;
    auto& x = std::get<26>(_data);
    x.clear();
    return x;
}
const RemoveDirectoryOwnerReq& ShardReqContainer::getRemoveDirectoryOwner() const {
    ALWAYS_ASSERT(_kind == ShardMessageKind::REMOVE_DIRECTORY_OWNER, "%s != %s", _kind, ShardMessageKind::REMOVE_DIRECTORY_OWNER);
    return std::get<27>(_data);
}
RemoveDirectoryOwnerReq& ShardReqContainer::setRemoveDirectoryOwner() {
    _kind = ShardMessageKind::REMOVE_DIRECTORY_OWNER;
    auto& x = std::get<27>(_data);
    x.clear();
    return x;
}
const CreateLockedCurrentEdgeReq& ShardReqContainer::getCreateLockedCurrentEdge() const {
    ALWAYS_ASSERT(_kind == ShardMessageKind::CREATE_LOCKED_CURRENT_EDGE, "%s != %s", _kind, ShardMessageKind::CREATE_LOCKED_CURRENT_EDGE);
    return std::get<28>(_data);
}
CreateLockedCurrentEdgeReq& ShardReqContainer::setCreateLockedCurrentEdge() {
    _kind = ShardMessageKind::CREATE_LOCKED_CURRENT_EDGE;
    auto& x = std::get<28>(_data);
    x.clear();
    return x;
}
const LockCurrentEdgeReq& ShardReqContainer::getLockCurrentEdge() const {
    ALWAYS_ASSERT(_kind == ShardMessageKind::LOCK_CURRENT_EDGE, "%s != %s", _kind, ShardMessageKind::LOCK_CURRENT_EDGE);
    return std::get<29>(_data);
}
LockCurrentEdgeReq& ShardReqContainer::setLockCurrentEdge() {
    _kind = ShardMessageKind::LOCK_CURRENT_EDGE;
    auto& x = std::get<29>(_data);
    x.clear();
    return x;
}
const UnlockCurrentEdgeReq& ShardReqContainer::getUnlockCurrentEdge() const {
    ALWAYS_ASSERT(_kind == ShardMessageKind::UNLOCK_CURRENT_EDGE, "%s != %s", _kind, ShardMessageKind::UNLOCK_CURRENT_EDGE);
    return std::get<30>(_data);
}
UnlockCurrentEdgeReq& ShardReqContainer::setUnlockCurrentEdge() {
    _kind = ShardMessageKind::UNLOCK_CURRENT_EDGE;
    auto& x = std::get<30>(_data);
    x.clear();
    return x;
}
const RemoveOwnedSnapshotFileEdgeReq& ShardReqContainer::getRemoveOwnedSnapshotFileEdge() const {
    ALWAYS_ASSERT(_kind == ShardMessageKind::REMOVE_OWNED_SNAPSHOT_FILE_EDGE, "%s != %s", _kind, ShardMessageKind::REMOVE_OWNED_SNAPSHOT_FILE_EDGE);
    return std::get<31>(_data);
}
RemoveOwnedSnapshotFileEdgeReq& ShardReqContainer::setRemoveOwnedSnapshotFileEdge() {
    _kind = ShardMessageKind::REMOVE_OWNED_SNAPSHOT_FILE_EDGE;
    auto& x = std::get<31>(_data);
    x.clear();
    return x;
}
const MakeFileTransientReq& ShardReqContainer::getMakeFileTransient() const {
    ALWAYS_ASSERT(_kind == ShardMessageKind::MAKE_FILE_TRANSIENT, "%s != %s", _kind, ShardMessageKind::MAKE_FILE_TRANSIENT);
    return std::get<32>(_data);
}
MakeFileTransientReq& ShardReqContainer::setMakeFileTransient() {
    _kind = ShardMessageKind::MAKE_FILE_TRANSIENT;
    auto& x = std::get<32>(_data);
    x.clear();
    return x;
}
size_t ShardReqContainer::packedSize() const {
    switch (_kind) {
    case ShardMessageKind::LOOKUP:
        return std::get<0>(_data).packedSize();
    case ShardMessageKind::STAT_FILE:
        return std::get<1>(_data).packedSize();
    case ShardMessageKind::STAT_TRANSIENT_FILE:
        return std::get<2>(_data).packedSize();
    case ShardMessageKind::STAT_DIRECTORY:
        return std::get<3>(_data).packedSize();
    case ShardMessageKind::READ_DIR:
        return std::get<4>(_data).packedSize();
    case ShardMessageKind::CONSTRUCT_FILE:
        return std::get<5>(_data).packedSize();
    case ShardMessageKind::ADD_SPAN_INITIATE:
        return std::get<6>(_data).packedSize();
    case ShardMessageKind::ADD_SPAN_CERTIFY:
        return std::get<7>(_data).packedSize();
    case ShardMessageKind::LINK_FILE:
        return std::get<8>(_data).packedSize();
    case ShardMessageKind::SOFT_UNLINK_FILE:
        return std::get<9>(_data).packedSize();
    case ShardMessageKind::FILE_SPANS:
        return std::get<10>(_data).packedSize();
    case ShardMessageKind::SAME_DIRECTORY_RENAME:
        return std::get<11>(_data).packedSize();
    case ShardMessageKind::SET_DIRECTORY_INFO:
        return std::get<12>(_data).packedSize();
    case ShardMessageKind::SNAPSHOT_LOOKUP:
        return std::get<13>(_data).packedSize();
    case ShardMessageKind::VISIT_DIRECTORIES:
        return std::get<14>(_data).packedSize();
    case ShardMessageKind::VISIT_FILES:
        return std::get<15>(_data).packedSize();
    case ShardMessageKind::VISIT_TRANSIENT_FILES:
        return std::get<16>(_data).packedSize();
    case ShardMessageKind::FULL_READ_DIR:
        return std::get<17>(_data).packedSize();
    case ShardMessageKind::REMOVE_NON_OWNED_EDGE:
        return std::get<18>(_data).packedSize();
    case ShardMessageKind::SAME_SHARD_HARD_FILE_UNLINK:
        return std::get<19>(_data).packedSize();
    case ShardMessageKind::REMOVE_SPAN_INITIATE:
        return std::get<20>(_data).packedSize();
    case ShardMessageKind::REMOVE_SPAN_CERTIFY:
        return std::get<21>(_data).packedSize();
    case ShardMessageKind::SWAP_BLOCKS:
        return std::get<22>(_data).packedSize();
    case ShardMessageKind::BLOCK_SERVICE_FILES:
        return std::get<23>(_data).packedSize();
    case ShardMessageKind::REMOVE_INODE:
        return std::get<24>(_data).packedSize();
    case ShardMessageKind::CREATE_DIRECTORY_INODE:
        return std::get<25>(_data).packedSize();
    case ShardMessageKind::SET_DIRECTORY_OWNER:
        return std::get<26>(_data).packedSize();
    case ShardMessageKind::REMOVE_DIRECTORY_OWNER:
        return std::get<27>(_data).packedSize();
    case ShardMessageKind::CREATE_LOCKED_CURRENT_EDGE:
        return std::get<28>(_data).packedSize();
    case ShardMessageKind::LOCK_CURRENT_EDGE:
        return std::get<29>(_data).packedSize();
    case ShardMessageKind::UNLOCK_CURRENT_EDGE:
        return std::get<30>(_data).packedSize();
    case ShardMessageKind::REMOVE_OWNED_SNAPSHOT_FILE_EDGE:
        return std::get<31>(_data).packedSize();
    case ShardMessageKind::MAKE_FILE_TRANSIENT:
        return std::get<32>(_data).packedSize();
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
    case ShardMessageKind::STAT_TRANSIENT_FILE:
        std::get<2>(_data).pack(buf);
        break;
    case ShardMessageKind::STAT_DIRECTORY:
        std::get<3>(_data).pack(buf);
        break;
    case ShardMessageKind::READ_DIR:
        std::get<4>(_data).pack(buf);
        break;
    case ShardMessageKind::CONSTRUCT_FILE:
        std::get<5>(_data).pack(buf);
        break;
    case ShardMessageKind::ADD_SPAN_INITIATE:
        std::get<6>(_data).pack(buf);
        break;
    case ShardMessageKind::ADD_SPAN_CERTIFY:
        std::get<7>(_data).pack(buf);
        break;
    case ShardMessageKind::LINK_FILE:
        std::get<8>(_data).pack(buf);
        break;
    case ShardMessageKind::SOFT_UNLINK_FILE:
        std::get<9>(_data).pack(buf);
        break;
    case ShardMessageKind::FILE_SPANS:
        std::get<10>(_data).pack(buf);
        break;
    case ShardMessageKind::SAME_DIRECTORY_RENAME:
        std::get<11>(_data).pack(buf);
        break;
    case ShardMessageKind::SET_DIRECTORY_INFO:
        std::get<12>(_data).pack(buf);
        break;
    case ShardMessageKind::SNAPSHOT_LOOKUP:
        std::get<13>(_data).pack(buf);
        break;
    case ShardMessageKind::VISIT_DIRECTORIES:
        std::get<14>(_data).pack(buf);
        break;
    case ShardMessageKind::VISIT_FILES:
        std::get<15>(_data).pack(buf);
        break;
    case ShardMessageKind::VISIT_TRANSIENT_FILES:
        std::get<16>(_data).pack(buf);
        break;
    case ShardMessageKind::FULL_READ_DIR:
        std::get<17>(_data).pack(buf);
        break;
    case ShardMessageKind::REMOVE_NON_OWNED_EDGE:
        std::get<18>(_data).pack(buf);
        break;
    case ShardMessageKind::SAME_SHARD_HARD_FILE_UNLINK:
        std::get<19>(_data).pack(buf);
        break;
    case ShardMessageKind::REMOVE_SPAN_INITIATE:
        std::get<20>(_data).pack(buf);
        break;
    case ShardMessageKind::REMOVE_SPAN_CERTIFY:
        std::get<21>(_data).pack(buf);
        break;
    case ShardMessageKind::SWAP_BLOCKS:
        std::get<22>(_data).pack(buf);
        break;
    case ShardMessageKind::BLOCK_SERVICE_FILES:
        std::get<23>(_data).pack(buf);
        break;
    case ShardMessageKind::REMOVE_INODE:
        std::get<24>(_data).pack(buf);
        break;
    case ShardMessageKind::CREATE_DIRECTORY_INODE:
        std::get<25>(_data).pack(buf);
        break;
    case ShardMessageKind::SET_DIRECTORY_OWNER:
        std::get<26>(_data).pack(buf);
        break;
    case ShardMessageKind::REMOVE_DIRECTORY_OWNER:
        std::get<27>(_data).pack(buf);
        break;
    case ShardMessageKind::CREATE_LOCKED_CURRENT_EDGE:
        std::get<28>(_data).pack(buf);
        break;
    case ShardMessageKind::LOCK_CURRENT_EDGE:
        std::get<29>(_data).pack(buf);
        break;
    case ShardMessageKind::UNLOCK_CURRENT_EDGE:
        std::get<30>(_data).pack(buf);
        break;
    case ShardMessageKind::REMOVE_OWNED_SNAPSHOT_FILE_EDGE:
        std::get<31>(_data).pack(buf);
        break;
    case ShardMessageKind::MAKE_FILE_TRANSIENT:
        std::get<32>(_data).pack(buf);
        break;
    default:
        throw EGGS_EXCEPTION("bad ShardMessageKind kind %s", _kind);
    }
}

void ShardReqContainer::unpack(BincodeBuf& buf, ShardMessageKind kind) {
    _kind = kind;
    switch (kind) {
    case ShardMessageKind::LOOKUP:
        std::get<0>(_data).unpack(buf);
        break;
    case ShardMessageKind::STAT_FILE:
        std::get<1>(_data).unpack(buf);
        break;
    case ShardMessageKind::STAT_TRANSIENT_FILE:
        std::get<2>(_data).unpack(buf);
        break;
    case ShardMessageKind::STAT_DIRECTORY:
        std::get<3>(_data).unpack(buf);
        break;
    case ShardMessageKind::READ_DIR:
        std::get<4>(_data).unpack(buf);
        break;
    case ShardMessageKind::CONSTRUCT_FILE:
        std::get<5>(_data).unpack(buf);
        break;
    case ShardMessageKind::ADD_SPAN_INITIATE:
        std::get<6>(_data).unpack(buf);
        break;
    case ShardMessageKind::ADD_SPAN_CERTIFY:
        std::get<7>(_data).unpack(buf);
        break;
    case ShardMessageKind::LINK_FILE:
        std::get<8>(_data).unpack(buf);
        break;
    case ShardMessageKind::SOFT_UNLINK_FILE:
        std::get<9>(_data).unpack(buf);
        break;
    case ShardMessageKind::FILE_SPANS:
        std::get<10>(_data).unpack(buf);
        break;
    case ShardMessageKind::SAME_DIRECTORY_RENAME:
        std::get<11>(_data).unpack(buf);
        break;
    case ShardMessageKind::SET_DIRECTORY_INFO:
        std::get<12>(_data).unpack(buf);
        break;
    case ShardMessageKind::SNAPSHOT_LOOKUP:
        std::get<13>(_data).unpack(buf);
        break;
    case ShardMessageKind::VISIT_DIRECTORIES:
        std::get<14>(_data).unpack(buf);
        break;
    case ShardMessageKind::VISIT_FILES:
        std::get<15>(_data).unpack(buf);
        break;
    case ShardMessageKind::VISIT_TRANSIENT_FILES:
        std::get<16>(_data).unpack(buf);
        break;
    case ShardMessageKind::FULL_READ_DIR:
        std::get<17>(_data).unpack(buf);
        break;
    case ShardMessageKind::REMOVE_NON_OWNED_EDGE:
        std::get<18>(_data).unpack(buf);
        break;
    case ShardMessageKind::SAME_SHARD_HARD_FILE_UNLINK:
        std::get<19>(_data).unpack(buf);
        break;
    case ShardMessageKind::REMOVE_SPAN_INITIATE:
        std::get<20>(_data).unpack(buf);
        break;
    case ShardMessageKind::REMOVE_SPAN_CERTIFY:
        std::get<21>(_data).unpack(buf);
        break;
    case ShardMessageKind::SWAP_BLOCKS:
        std::get<22>(_data).unpack(buf);
        break;
    case ShardMessageKind::BLOCK_SERVICE_FILES:
        std::get<23>(_data).unpack(buf);
        break;
    case ShardMessageKind::REMOVE_INODE:
        std::get<24>(_data).unpack(buf);
        break;
    case ShardMessageKind::CREATE_DIRECTORY_INODE:
        std::get<25>(_data).unpack(buf);
        break;
    case ShardMessageKind::SET_DIRECTORY_OWNER:
        std::get<26>(_data).unpack(buf);
        break;
    case ShardMessageKind::REMOVE_DIRECTORY_OWNER:
        std::get<27>(_data).unpack(buf);
        break;
    case ShardMessageKind::CREATE_LOCKED_CURRENT_EDGE:
        std::get<28>(_data).unpack(buf);
        break;
    case ShardMessageKind::LOCK_CURRENT_EDGE:
        std::get<29>(_data).unpack(buf);
        break;
    case ShardMessageKind::UNLOCK_CURRENT_EDGE:
        std::get<30>(_data).unpack(buf);
        break;
    case ShardMessageKind::REMOVE_OWNED_SNAPSHOT_FILE_EDGE:
        std::get<31>(_data).unpack(buf);
        break;
    case ShardMessageKind::MAKE_FILE_TRANSIENT:
        std::get<32>(_data).unpack(buf);
        break;
    default:
        throw BINCODE_EXCEPTION("bad ShardMessageKind kind %s", kind);
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
    case ShardMessageKind::STAT_TRANSIENT_FILE:
        out << x.getStatTransientFile();
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
    case ShardMessageKind::SET_DIRECTORY_INFO:
        out << x.getSetDirectoryInfo();
        break;
    case ShardMessageKind::SNAPSHOT_LOOKUP:
        out << x.getSnapshotLookup();
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
    case ShardMessageKind::FULL_READ_DIR:
        out << x.getFullReadDir();
        break;
    case ShardMessageKind::REMOVE_NON_OWNED_EDGE:
        out << x.getRemoveNonOwnedEdge();
        break;
    case ShardMessageKind::SAME_SHARD_HARD_FILE_UNLINK:
        out << x.getSameShardHardFileUnlink();
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
    auto& x = std::get<0>(_data);
    x.clear();
    return x;
}
const StatFileResp& ShardRespContainer::getStatFile() const {
    ALWAYS_ASSERT(_kind == ShardMessageKind::STAT_FILE, "%s != %s", _kind, ShardMessageKind::STAT_FILE);
    return std::get<1>(_data);
}
StatFileResp& ShardRespContainer::setStatFile() {
    _kind = ShardMessageKind::STAT_FILE;
    auto& x = std::get<1>(_data);
    x.clear();
    return x;
}
const StatTransientFileResp& ShardRespContainer::getStatTransientFile() const {
    ALWAYS_ASSERT(_kind == ShardMessageKind::STAT_TRANSIENT_FILE, "%s != %s", _kind, ShardMessageKind::STAT_TRANSIENT_FILE);
    return std::get<2>(_data);
}
StatTransientFileResp& ShardRespContainer::setStatTransientFile() {
    _kind = ShardMessageKind::STAT_TRANSIENT_FILE;
    auto& x = std::get<2>(_data);
    x.clear();
    return x;
}
const StatDirectoryResp& ShardRespContainer::getStatDirectory() const {
    ALWAYS_ASSERT(_kind == ShardMessageKind::STAT_DIRECTORY, "%s != %s", _kind, ShardMessageKind::STAT_DIRECTORY);
    return std::get<3>(_data);
}
StatDirectoryResp& ShardRespContainer::setStatDirectory() {
    _kind = ShardMessageKind::STAT_DIRECTORY;
    auto& x = std::get<3>(_data);
    x.clear();
    return x;
}
const ReadDirResp& ShardRespContainer::getReadDir() const {
    ALWAYS_ASSERT(_kind == ShardMessageKind::READ_DIR, "%s != %s", _kind, ShardMessageKind::READ_DIR);
    return std::get<4>(_data);
}
ReadDirResp& ShardRespContainer::setReadDir() {
    _kind = ShardMessageKind::READ_DIR;
    auto& x = std::get<4>(_data);
    x.clear();
    return x;
}
const ConstructFileResp& ShardRespContainer::getConstructFile() const {
    ALWAYS_ASSERT(_kind == ShardMessageKind::CONSTRUCT_FILE, "%s != %s", _kind, ShardMessageKind::CONSTRUCT_FILE);
    return std::get<5>(_data);
}
ConstructFileResp& ShardRespContainer::setConstructFile() {
    _kind = ShardMessageKind::CONSTRUCT_FILE;
    auto& x = std::get<5>(_data);
    x.clear();
    return x;
}
const AddSpanInitiateResp& ShardRespContainer::getAddSpanInitiate() const {
    ALWAYS_ASSERT(_kind == ShardMessageKind::ADD_SPAN_INITIATE, "%s != %s", _kind, ShardMessageKind::ADD_SPAN_INITIATE);
    return std::get<6>(_data);
}
AddSpanInitiateResp& ShardRespContainer::setAddSpanInitiate() {
    _kind = ShardMessageKind::ADD_SPAN_INITIATE;
    auto& x = std::get<6>(_data);
    x.clear();
    return x;
}
const AddSpanCertifyResp& ShardRespContainer::getAddSpanCertify() const {
    ALWAYS_ASSERT(_kind == ShardMessageKind::ADD_SPAN_CERTIFY, "%s != %s", _kind, ShardMessageKind::ADD_SPAN_CERTIFY);
    return std::get<7>(_data);
}
AddSpanCertifyResp& ShardRespContainer::setAddSpanCertify() {
    _kind = ShardMessageKind::ADD_SPAN_CERTIFY;
    auto& x = std::get<7>(_data);
    x.clear();
    return x;
}
const LinkFileResp& ShardRespContainer::getLinkFile() const {
    ALWAYS_ASSERT(_kind == ShardMessageKind::LINK_FILE, "%s != %s", _kind, ShardMessageKind::LINK_FILE);
    return std::get<8>(_data);
}
LinkFileResp& ShardRespContainer::setLinkFile() {
    _kind = ShardMessageKind::LINK_FILE;
    auto& x = std::get<8>(_data);
    x.clear();
    return x;
}
const SoftUnlinkFileResp& ShardRespContainer::getSoftUnlinkFile() const {
    ALWAYS_ASSERT(_kind == ShardMessageKind::SOFT_UNLINK_FILE, "%s != %s", _kind, ShardMessageKind::SOFT_UNLINK_FILE);
    return std::get<9>(_data);
}
SoftUnlinkFileResp& ShardRespContainer::setSoftUnlinkFile() {
    _kind = ShardMessageKind::SOFT_UNLINK_FILE;
    auto& x = std::get<9>(_data);
    x.clear();
    return x;
}
const FileSpansResp& ShardRespContainer::getFileSpans() const {
    ALWAYS_ASSERT(_kind == ShardMessageKind::FILE_SPANS, "%s != %s", _kind, ShardMessageKind::FILE_SPANS);
    return std::get<10>(_data);
}
FileSpansResp& ShardRespContainer::setFileSpans() {
    _kind = ShardMessageKind::FILE_SPANS;
    auto& x = std::get<10>(_data);
    x.clear();
    return x;
}
const SameDirectoryRenameResp& ShardRespContainer::getSameDirectoryRename() const {
    ALWAYS_ASSERT(_kind == ShardMessageKind::SAME_DIRECTORY_RENAME, "%s != %s", _kind, ShardMessageKind::SAME_DIRECTORY_RENAME);
    return std::get<11>(_data);
}
SameDirectoryRenameResp& ShardRespContainer::setSameDirectoryRename() {
    _kind = ShardMessageKind::SAME_DIRECTORY_RENAME;
    auto& x = std::get<11>(_data);
    x.clear();
    return x;
}
const SetDirectoryInfoResp& ShardRespContainer::getSetDirectoryInfo() const {
    ALWAYS_ASSERT(_kind == ShardMessageKind::SET_DIRECTORY_INFO, "%s != %s", _kind, ShardMessageKind::SET_DIRECTORY_INFO);
    return std::get<12>(_data);
}
SetDirectoryInfoResp& ShardRespContainer::setSetDirectoryInfo() {
    _kind = ShardMessageKind::SET_DIRECTORY_INFO;
    auto& x = std::get<12>(_data);
    x.clear();
    return x;
}
const SnapshotLookupResp& ShardRespContainer::getSnapshotLookup() const {
    ALWAYS_ASSERT(_kind == ShardMessageKind::SNAPSHOT_LOOKUP, "%s != %s", _kind, ShardMessageKind::SNAPSHOT_LOOKUP);
    return std::get<13>(_data);
}
SnapshotLookupResp& ShardRespContainer::setSnapshotLookup() {
    _kind = ShardMessageKind::SNAPSHOT_LOOKUP;
    auto& x = std::get<13>(_data);
    x.clear();
    return x;
}
const VisitDirectoriesResp& ShardRespContainer::getVisitDirectories() const {
    ALWAYS_ASSERT(_kind == ShardMessageKind::VISIT_DIRECTORIES, "%s != %s", _kind, ShardMessageKind::VISIT_DIRECTORIES);
    return std::get<14>(_data);
}
VisitDirectoriesResp& ShardRespContainer::setVisitDirectories() {
    _kind = ShardMessageKind::VISIT_DIRECTORIES;
    auto& x = std::get<14>(_data);
    x.clear();
    return x;
}
const VisitFilesResp& ShardRespContainer::getVisitFiles() const {
    ALWAYS_ASSERT(_kind == ShardMessageKind::VISIT_FILES, "%s != %s", _kind, ShardMessageKind::VISIT_FILES);
    return std::get<15>(_data);
}
VisitFilesResp& ShardRespContainer::setVisitFiles() {
    _kind = ShardMessageKind::VISIT_FILES;
    auto& x = std::get<15>(_data);
    x.clear();
    return x;
}
const VisitTransientFilesResp& ShardRespContainer::getVisitTransientFiles() const {
    ALWAYS_ASSERT(_kind == ShardMessageKind::VISIT_TRANSIENT_FILES, "%s != %s", _kind, ShardMessageKind::VISIT_TRANSIENT_FILES);
    return std::get<16>(_data);
}
VisitTransientFilesResp& ShardRespContainer::setVisitTransientFiles() {
    _kind = ShardMessageKind::VISIT_TRANSIENT_FILES;
    auto& x = std::get<16>(_data);
    x.clear();
    return x;
}
const FullReadDirResp& ShardRespContainer::getFullReadDir() const {
    ALWAYS_ASSERT(_kind == ShardMessageKind::FULL_READ_DIR, "%s != %s", _kind, ShardMessageKind::FULL_READ_DIR);
    return std::get<17>(_data);
}
FullReadDirResp& ShardRespContainer::setFullReadDir() {
    _kind = ShardMessageKind::FULL_READ_DIR;
    auto& x = std::get<17>(_data);
    x.clear();
    return x;
}
const RemoveNonOwnedEdgeResp& ShardRespContainer::getRemoveNonOwnedEdge() const {
    ALWAYS_ASSERT(_kind == ShardMessageKind::REMOVE_NON_OWNED_EDGE, "%s != %s", _kind, ShardMessageKind::REMOVE_NON_OWNED_EDGE);
    return std::get<18>(_data);
}
RemoveNonOwnedEdgeResp& ShardRespContainer::setRemoveNonOwnedEdge() {
    _kind = ShardMessageKind::REMOVE_NON_OWNED_EDGE;
    auto& x = std::get<18>(_data);
    x.clear();
    return x;
}
const SameShardHardFileUnlinkResp& ShardRespContainer::getSameShardHardFileUnlink() const {
    ALWAYS_ASSERT(_kind == ShardMessageKind::SAME_SHARD_HARD_FILE_UNLINK, "%s != %s", _kind, ShardMessageKind::SAME_SHARD_HARD_FILE_UNLINK);
    return std::get<19>(_data);
}
SameShardHardFileUnlinkResp& ShardRespContainer::setSameShardHardFileUnlink() {
    _kind = ShardMessageKind::SAME_SHARD_HARD_FILE_UNLINK;
    auto& x = std::get<19>(_data);
    x.clear();
    return x;
}
const RemoveSpanInitiateResp& ShardRespContainer::getRemoveSpanInitiate() const {
    ALWAYS_ASSERT(_kind == ShardMessageKind::REMOVE_SPAN_INITIATE, "%s != %s", _kind, ShardMessageKind::REMOVE_SPAN_INITIATE);
    return std::get<20>(_data);
}
RemoveSpanInitiateResp& ShardRespContainer::setRemoveSpanInitiate() {
    _kind = ShardMessageKind::REMOVE_SPAN_INITIATE;
    auto& x = std::get<20>(_data);
    x.clear();
    return x;
}
const RemoveSpanCertifyResp& ShardRespContainer::getRemoveSpanCertify() const {
    ALWAYS_ASSERT(_kind == ShardMessageKind::REMOVE_SPAN_CERTIFY, "%s != %s", _kind, ShardMessageKind::REMOVE_SPAN_CERTIFY);
    return std::get<21>(_data);
}
RemoveSpanCertifyResp& ShardRespContainer::setRemoveSpanCertify() {
    _kind = ShardMessageKind::REMOVE_SPAN_CERTIFY;
    auto& x = std::get<21>(_data);
    x.clear();
    return x;
}
const SwapBlocksResp& ShardRespContainer::getSwapBlocks() const {
    ALWAYS_ASSERT(_kind == ShardMessageKind::SWAP_BLOCKS, "%s != %s", _kind, ShardMessageKind::SWAP_BLOCKS);
    return std::get<22>(_data);
}
SwapBlocksResp& ShardRespContainer::setSwapBlocks() {
    _kind = ShardMessageKind::SWAP_BLOCKS;
    auto& x = std::get<22>(_data);
    x.clear();
    return x;
}
const BlockServiceFilesResp& ShardRespContainer::getBlockServiceFiles() const {
    ALWAYS_ASSERT(_kind == ShardMessageKind::BLOCK_SERVICE_FILES, "%s != %s", _kind, ShardMessageKind::BLOCK_SERVICE_FILES);
    return std::get<23>(_data);
}
BlockServiceFilesResp& ShardRespContainer::setBlockServiceFiles() {
    _kind = ShardMessageKind::BLOCK_SERVICE_FILES;
    auto& x = std::get<23>(_data);
    x.clear();
    return x;
}
const RemoveInodeResp& ShardRespContainer::getRemoveInode() const {
    ALWAYS_ASSERT(_kind == ShardMessageKind::REMOVE_INODE, "%s != %s", _kind, ShardMessageKind::REMOVE_INODE);
    return std::get<24>(_data);
}
RemoveInodeResp& ShardRespContainer::setRemoveInode() {
    _kind = ShardMessageKind::REMOVE_INODE;
    auto& x = std::get<24>(_data);
    x.clear();
    return x;
}
const CreateDirectoryInodeResp& ShardRespContainer::getCreateDirectoryInode() const {
    ALWAYS_ASSERT(_kind == ShardMessageKind::CREATE_DIRECTORY_INODE, "%s != %s", _kind, ShardMessageKind::CREATE_DIRECTORY_INODE);
    return std::get<25>(_data);
}
CreateDirectoryInodeResp& ShardRespContainer::setCreateDirectoryInode() {
    _kind = ShardMessageKind::CREATE_DIRECTORY_INODE;
    auto& x = std::get<25>(_data);
    x.clear();
    return x;
}
const SetDirectoryOwnerResp& ShardRespContainer::getSetDirectoryOwner() const {
    ALWAYS_ASSERT(_kind == ShardMessageKind::SET_DIRECTORY_OWNER, "%s != %s", _kind, ShardMessageKind::SET_DIRECTORY_OWNER);
    return std::get<26>(_data);
}
SetDirectoryOwnerResp& ShardRespContainer::setSetDirectoryOwner() {
    _kind = ShardMessageKind::SET_DIRECTORY_OWNER;
    auto& x = std::get<26>(_data);
    x.clear();
    return x;
}
const RemoveDirectoryOwnerResp& ShardRespContainer::getRemoveDirectoryOwner() const {
    ALWAYS_ASSERT(_kind == ShardMessageKind::REMOVE_DIRECTORY_OWNER, "%s != %s", _kind, ShardMessageKind::REMOVE_DIRECTORY_OWNER);
    return std::get<27>(_data);
}
RemoveDirectoryOwnerResp& ShardRespContainer::setRemoveDirectoryOwner() {
    _kind = ShardMessageKind::REMOVE_DIRECTORY_OWNER;
    auto& x = std::get<27>(_data);
    x.clear();
    return x;
}
const CreateLockedCurrentEdgeResp& ShardRespContainer::getCreateLockedCurrentEdge() const {
    ALWAYS_ASSERT(_kind == ShardMessageKind::CREATE_LOCKED_CURRENT_EDGE, "%s != %s", _kind, ShardMessageKind::CREATE_LOCKED_CURRENT_EDGE);
    return std::get<28>(_data);
}
CreateLockedCurrentEdgeResp& ShardRespContainer::setCreateLockedCurrentEdge() {
    _kind = ShardMessageKind::CREATE_LOCKED_CURRENT_EDGE;
    auto& x = std::get<28>(_data);
    x.clear();
    return x;
}
const LockCurrentEdgeResp& ShardRespContainer::getLockCurrentEdge() const {
    ALWAYS_ASSERT(_kind == ShardMessageKind::LOCK_CURRENT_EDGE, "%s != %s", _kind, ShardMessageKind::LOCK_CURRENT_EDGE);
    return std::get<29>(_data);
}
LockCurrentEdgeResp& ShardRespContainer::setLockCurrentEdge() {
    _kind = ShardMessageKind::LOCK_CURRENT_EDGE;
    auto& x = std::get<29>(_data);
    x.clear();
    return x;
}
const UnlockCurrentEdgeResp& ShardRespContainer::getUnlockCurrentEdge() const {
    ALWAYS_ASSERT(_kind == ShardMessageKind::UNLOCK_CURRENT_EDGE, "%s != %s", _kind, ShardMessageKind::UNLOCK_CURRENT_EDGE);
    return std::get<30>(_data);
}
UnlockCurrentEdgeResp& ShardRespContainer::setUnlockCurrentEdge() {
    _kind = ShardMessageKind::UNLOCK_CURRENT_EDGE;
    auto& x = std::get<30>(_data);
    x.clear();
    return x;
}
const RemoveOwnedSnapshotFileEdgeResp& ShardRespContainer::getRemoveOwnedSnapshotFileEdge() const {
    ALWAYS_ASSERT(_kind == ShardMessageKind::REMOVE_OWNED_SNAPSHOT_FILE_EDGE, "%s != %s", _kind, ShardMessageKind::REMOVE_OWNED_SNAPSHOT_FILE_EDGE);
    return std::get<31>(_data);
}
RemoveOwnedSnapshotFileEdgeResp& ShardRespContainer::setRemoveOwnedSnapshotFileEdge() {
    _kind = ShardMessageKind::REMOVE_OWNED_SNAPSHOT_FILE_EDGE;
    auto& x = std::get<31>(_data);
    x.clear();
    return x;
}
const MakeFileTransientResp& ShardRespContainer::getMakeFileTransient() const {
    ALWAYS_ASSERT(_kind == ShardMessageKind::MAKE_FILE_TRANSIENT, "%s != %s", _kind, ShardMessageKind::MAKE_FILE_TRANSIENT);
    return std::get<32>(_data);
}
MakeFileTransientResp& ShardRespContainer::setMakeFileTransient() {
    _kind = ShardMessageKind::MAKE_FILE_TRANSIENT;
    auto& x = std::get<32>(_data);
    x.clear();
    return x;
}
size_t ShardRespContainer::packedSize() const {
    switch (_kind) {
    case ShardMessageKind::LOOKUP:
        return std::get<0>(_data).packedSize();
    case ShardMessageKind::STAT_FILE:
        return std::get<1>(_data).packedSize();
    case ShardMessageKind::STAT_TRANSIENT_FILE:
        return std::get<2>(_data).packedSize();
    case ShardMessageKind::STAT_DIRECTORY:
        return std::get<3>(_data).packedSize();
    case ShardMessageKind::READ_DIR:
        return std::get<4>(_data).packedSize();
    case ShardMessageKind::CONSTRUCT_FILE:
        return std::get<5>(_data).packedSize();
    case ShardMessageKind::ADD_SPAN_INITIATE:
        return std::get<6>(_data).packedSize();
    case ShardMessageKind::ADD_SPAN_CERTIFY:
        return std::get<7>(_data).packedSize();
    case ShardMessageKind::LINK_FILE:
        return std::get<8>(_data).packedSize();
    case ShardMessageKind::SOFT_UNLINK_FILE:
        return std::get<9>(_data).packedSize();
    case ShardMessageKind::FILE_SPANS:
        return std::get<10>(_data).packedSize();
    case ShardMessageKind::SAME_DIRECTORY_RENAME:
        return std::get<11>(_data).packedSize();
    case ShardMessageKind::SET_DIRECTORY_INFO:
        return std::get<12>(_data).packedSize();
    case ShardMessageKind::SNAPSHOT_LOOKUP:
        return std::get<13>(_data).packedSize();
    case ShardMessageKind::VISIT_DIRECTORIES:
        return std::get<14>(_data).packedSize();
    case ShardMessageKind::VISIT_FILES:
        return std::get<15>(_data).packedSize();
    case ShardMessageKind::VISIT_TRANSIENT_FILES:
        return std::get<16>(_data).packedSize();
    case ShardMessageKind::FULL_READ_DIR:
        return std::get<17>(_data).packedSize();
    case ShardMessageKind::REMOVE_NON_OWNED_EDGE:
        return std::get<18>(_data).packedSize();
    case ShardMessageKind::SAME_SHARD_HARD_FILE_UNLINK:
        return std::get<19>(_data).packedSize();
    case ShardMessageKind::REMOVE_SPAN_INITIATE:
        return std::get<20>(_data).packedSize();
    case ShardMessageKind::REMOVE_SPAN_CERTIFY:
        return std::get<21>(_data).packedSize();
    case ShardMessageKind::SWAP_BLOCKS:
        return std::get<22>(_data).packedSize();
    case ShardMessageKind::BLOCK_SERVICE_FILES:
        return std::get<23>(_data).packedSize();
    case ShardMessageKind::REMOVE_INODE:
        return std::get<24>(_data).packedSize();
    case ShardMessageKind::CREATE_DIRECTORY_INODE:
        return std::get<25>(_data).packedSize();
    case ShardMessageKind::SET_DIRECTORY_OWNER:
        return std::get<26>(_data).packedSize();
    case ShardMessageKind::REMOVE_DIRECTORY_OWNER:
        return std::get<27>(_data).packedSize();
    case ShardMessageKind::CREATE_LOCKED_CURRENT_EDGE:
        return std::get<28>(_data).packedSize();
    case ShardMessageKind::LOCK_CURRENT_EDGE:
        return std::get<29>(_data).packedSize();
    case ShardMessageKind::UNLOCK_CURRENT_EDGE:
        return std::get<30>(_data).packedSize();
    case ShardMessageKind::REMOVE_OWNED_SNAPSHOT_FILE_EDGE:
        return std::get<31>(_data).packedSize();
    case ShardMessageKind::MAKE_FILE_TRANSIENT:
        return std::get<32>(_data).packedSize();
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
    case ShardMessageKind::STAT_TRANSIENT_FILE:
        std::get<2>(_data).pack(buf);
        break;
    case ShardMessageKind::STAT_DIRECTORY:
        std::get<3>(_data).pack(buf);
        break;
    case ShardMessageKind::READ_DIR:
        std::get<4>(_data).pack(buf);
        break;
    case ShardMessageKind::CONSTRUCT_FILE:
        std::get<5>(_data).pack(buf);
        break;
    case ShardMessageKind::ADD_SPAN_INITIATE:
        std::get<6>(_data).pack(buf);
        break;
    case ShardMessageKind::ADD_SPAN_CERTIFY:
        std::get<7>(_data).pack(buf);
        break;
    case ShardMessageKind::LINK_FILE:
        std::get<8>(_data).pack(buf);
        break;
    case ShardMessageKind::SOFT_UNLINK_FILE:
        std::get<9>(_data).pack(buf);
        break;
    case ShardMessageKind::FILE_SPANS:
        std::get<10>(_data).pack(buf);
        break;
    case ShardMessageKind::SAME_DIRECTORY_RENAME:
        std::get<11>(_data).pack(buf);
        break;
    case ShardMessageKind::SET_DIRECTORY_INFO:
        std::get<12>(_data).pack(buf);
        break;
    case ShardMessageKind::SNAPSHOT_LOOKUP:
        std::get<13>(_data).pack(buf);
        break;
    case ShardMessageKind::VISIT_DIRECTORIES:
        std::get<14>(_data).pack(buf);
        break;
    case ShardMessageKind::VISIT_FILES:
        std::get<15>(_data).pack(buf);
        break;
    case ShardMessageKind::VISIT_TRANSIENT_FILES:
        std::get<16>(_data).pack(buf);
        break;
    case ShardMessageKind::FULL_READ_DIR:
        std::get<17>(_data).pack(buf);
        break;
    case ShardMessageKind::REMOVE_NON_OWNED_EDGE:
        std::get<18>(_data).pack(buf);
        break;
    case ShardMessageKind::SAME_SHARD_HARD_FILE_UNLINK:
        std::get<19>(_data).pack(buf);
        break;
    case ShardMessageKind::REMOVE_SPAN_INITIATE:
        std::get<20>(_data).pack(buf);
        break;
    case ShardMessageKind::REMOVE_SPAN_CERTIFY:
        std::get<21>(_data).pack(buf);
        break;
    case ShardMessageKind::SWAP_BLOCKS:
        std::get<22>(_data).pack(buf);
        break;
    case ShardMessageKind::BLOCK_SERVICE_FILES:
        std::get<23>(_data).pack(buf);
        break;
    case ShardMessageKind::REMOVE_INODE:
        std::get<24>(_data).pack(buf);
        break;
    case ShardMessageKind::CREATE_DIRECTORY_INODE:
        std::get<25>(_data).pack(buf);
        break;
    case ShardMessageKind::SET_DIRECTORY_OWNER:
        std::get<26>(_data).pack(buf);
        break;
    case ShardMessageKind::REMOVE_DIRECTORY_OWNER:
        std::get<27>(_data).pack(buf);
        break;
    case ShardMessageKind::CREATE_LOCKED_CURRENT_EDGE:
        std::get<28>(_data).pack(buf);
        break;
    case ShardMessageKind::LOCK_CURRENT_EDGE:
        std::get<29>(_data).pack(buf);
        break;
    case ShardMessageKind::UNLOCK_CURRENT_EDGE:
        std::get<30>(_data).pack(buf);
        break;
    case ShardMessageKind::REMOVE_OWNED_SNAPSHOT_FILE_EDGE:
        std::get<31>(_data).pack(buf);
        break;
    case ShardMessageKind::MAKE_FILE_TRANSIENT:
        std::get<32>(_data).pack(buf);
        break;
    default:
        throw EGGS_EXCEPTION("bad ShardMessageKind kind %s", _kind);
    }
}

void ShardRespContainer::unpack(BincodeBuf& buf, ShardMessageKind kind) {
    _kind = kind;
    switch (kind) {
    case ShardMessageKind::LOOKUP:
        std::get<0>(_data).unpack(buf);
        break;
    case ShardMessageKind::STAT_FILE:
        std::get<1>(_data).unpack(buf);
        break;
    case ShardMessageKind::STAT_TRANSIENT_FILE:
        std::get<2>(_data).unpack(buf);
        break;
    case ShardMessageKind::STAT_DIRECTORY:
        std::get<3>(_data).unpack(buf);
        break;
    case ShardMessageKind::READ_DIR:
        std::get<4>(_data).unpack(buf);
        break;
    case ShardMessageKind::CONSTRUCT_FILE:
        std::get<5>(_data).unpack(buf);
        break;
    case ShardMessageKind::ADD_SPAN_INITIATE:
        std::get<6>(_data).unpack(buf);
        break;
    case ShardMessageKind::ADD_SPAN_CERTIFY:
        std::get<7>(_data).unpack(buf);
        break;
    case ShardMessageKind::LINK_FILE:
        std::get<8>(_data).unpack(buf);
        break;
    case ShardMessageKind::SOFT_UNLINK_FILE:
        std::get<9>(_data).unpack(buf);
        break;
    case ShardMessageKind::FILE_SPANS:
        std::get<10>(_data).unpack(buf);
        break;
    case ShardMessageKind::SAME_DIRECTORY_RENAME:
        std::get<11>(_data).unpack(buf);
        break;
    case ShardMessageKind::SET_DIRECTORY_INFO:
        std::get<12>(_data).unpack(buf);
        break;
    case ShardMessageKind::SNAPSHOT_LOOKUP:
        std::get<13>(_data).unpack(buf);
        break;
    case ShardMessageKind::VISIT_DIRECTORIES:
        std::get<14>(_data).unpack(buf);
        break;
    case ShardMessageKind::VISIT_FILES:
        std::get<15>(_data).unpack(buf);
        break;
    case ShardMessageKind::VISIT_TRANSIENT_FILES:
        std::get<16>(_data).unpack(buf);
        break;
    case ShardMessageKind::FULL_READ_DIR:
        std::get<17>(_data).unpack(buf);
        break;
    case ShardMessageKind::REMOVE_NON_OWNED_EDGE:
        std::get<18>(_data).unpack(buf);
        break;
    case ShardMessageKind::SAME_SHARD_HARD_FILE_UNLINK:
        std::get<19>(_data).unpack(buf);
        break;
    case ShardMessageKind::REMOVE_SPAN_INITIATE:
        std::get<20>(_data).unpack(buf);
        break;
    case ShardMessageKind::REMOVE_SPAN_CERTIFY:
        std::get<21>(_data).unpack(buf);
        break;
    case ShardMessageKind::SWAP_BLOCKS:
        std::get<22>(_data).unpack(buf);
        break;
    case ShardMessageKind::BLOCK_SERVICE_FILES:
        std::get<23>(_data).unpack(buf);
        break;
    case ShardMessageKind::REMOVE_INODE:
        std::get<24>(_data).unpack(buf);
        break;
    case ShardMessageKind::CREATE_DIRECTORY_INODE:
        std::get<25>(_data).unpack(buf);
        break;
    case ShardMessageKind::SET_DIRECTORY_OWNER:
        std::get<26>(_data).unpack(buf);
        break;
    case ShardMessageKind::REMOVE_DIRECTORY_OWNER:
        std::get<27>(_data).unpack(buf);
        break;
    case ShardMessageKind::CREATE_LOCKED_CURRENT_EDGE:
        std::get<28>(_data).unpack(buf);
        break;
    case ShardMessageKind::LOCK_CURRENT_EDGE:
        std::get<29>(_data).unpack(buf);
        break;
    case ShardMessageKind::UNLOCK_CURRENT_EDGE:
        std::get<30>(_data).unpack(buf);
        break;
    case ShardMessageKind::REMOVE_OWNED_SNAPSHOT_FILE_EDGE:
        std::get<31>(_data).unpack(buf);
        break;
    case ShardMessageKind::MAKE_FILE_TRANSIENT:
        std::get<32>(_data).unpack(buf);
        break;
    default:
        throw BINCODE_EXCEPTION("bad ShardMessageKind kind %s", kind);
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
    case ShardMessageKind::STAT_TRANSIENT_FILE:
        out << x.getStatTransientFile();
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
    case ShardMessageKind::SET_DIRECTORY_INFO:
        out << x.getSetDirectoryInfo();
        break;
    case ShardMessageKind::SNAPSHOT_LOOKUP:
        out << x.getSnapshotLookup();
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
    case ShardMessageKind::FULL_READ_DIR:
        out << x.getFullReadDir();
        break;
    case ShardMessageKind::REMOVE_NON_OWNED_EDGE:
        out << x.getRemoveNonOwnedEdge();
        break;
    case ShardMessageKind::SAME_SHARD_HARD_FILE_UNLINK:
        out << x.getSameShardHardFileUnlink();
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

const MakeDirectoryReq& CDCReqContainer::getMakeDirectory() const {
    ALWAYS_ASSERT(_kind == CDCMessageKind::MAKE_DIRECTORY, "%s != %s", _kind, CDCMessageKind::MAKE_DIRECTORY);
    return std::get<0>(_data);
}
MakeDirectoryReq& CDCReqContainer::setMakeDirectory() {
    _kind = CDCMessageKind::MAKE_DIRECTORY;
    auto& x = std::get<0>(_data);
    x.clear();
    return x;
}
const RenameFileReq& CDCReqContainer::getRenameFile() const {
    ALWAYS_ASSERT(_kind == CDCMessageKind::RENAME_FILE, "%s != %s", _kind, CDCMessageKind::RENAME_FILE);
    return std::get<1>(_data);
}
RenameFileReq& CDCReqContainer::setRenameFile() {
    _kind = CDCMessageKind::RENAME_FILE;
    auto& x = std::get<1>(_data);
    x.clear();
    return x;
}
const SoftUnlinkDirectoryReq& CDCReqContainer::getSoftUnlinkDirectory() const {
    ALWAYS_ASSERT(_kind == CDCMessageKind::SOFT_UNLINK_DIRECTORY, "%s != %s", _kind, CDCMessageKind::SOFT_UNLINK_DIRECTORY);
    return std::get<2>(_data);
}
SoftUnlinkDirectoryReq& CDCReqContainer::setSoftUnlinkDirectory() {
    _kind = CDCMessageKind::SOFT_UNLINK_DIRECTORY;
    auto& x = std::get<2>(_data);
    x.clear();
    return x;
}
const RenameDirectoryReq& CDCReqContainer::getRenameDirectory() const {
    ALWAYS_ASSERT(_kind == CDCMessageKind::RENAME_DIRECTORY, "%s != %s", _kind, CDCMessageKind::RENAME_DIRECTORY);
    return std::get<3>(_data);
}
RenameDirectoryReq& CDCReqContainer::setRenameDirectory() {
    _kind = CDCMessageKind::RENAME_DIRECTORY;
    auto& x = std::get<3>(_data);
    x.clear();
    return x;
}
const HardUnlinkDirectoryReq& CDCReqContainer::getHardUnlinkDirectory() const {
    ALWAYS_ASSERT(_kind == CDCMessageKind::HARD_UNLINK_DIRECTORY, "%s != %s", _kind, CDCMessageKind::HARD_UNLINK_DIRECTORY);
    return std::get<4>(_data);
}
HardUnlinkDirectoryReq& CDCReqContainer::setHardUnlinkDirectory() {
    _kind = CDCMessageKind::HARD_UNLINK_DIRECTORY;
    auto& x = std::get<4>(_data);
    x.clear();
    return x;
}
const CrossShardHardUnlinkFileReq& CDCReqContainer::getCrossShardHardUnlinkFile() const {
    ALWAYS_ASSERT(_kind == CDCMessageKind::CROSS_SHARD_HARD_UNLINK_FILE, "%s != %s", _kind, CDCMessageKind::CROSS_SHARD_HARD_UNLINK_FILE);
    return std::get<5>(_data);
}
CrossShardHardUnlinkFileReq& CDCReqContainer::setCrossShardHardUnlinkFile() {
    _kind = CDCMessageKind::CROSS_SHARD_HARD_UNLINK_FILE;
    auto& x = std::get<5>(_data);
    x.clear();
    return x;
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
        std::get<0>(_data).unpack(buf);
        break;
    case CDCMessageKind::RENAME_FILE:
        std::get<1>(_data).unpack(buf);
        break;
    case CDCMessageKind::SOFT_UNLINK_DIRECTORY:
        std::get<2>(_data).unpack(buf);
        break;
    case CDCMessageKind::RENAME_DIRECTORY:
        std::get<3>(_data).unpack(buf);
        break;
    case CDCMessageKind::HARD_UNLINK_DIRECTORY:
        std::get<4>(_data).unpack(buf);
        break;
    case CDCMessageKind::CROSS_SHARD_HARD_UNLINK_FILE:
        std::get<5>(_data).unpack(buf);
        break;
    default:
        throw BINCODE_EXCEPTION("bad CDCMessageKind kind %s", kind);
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
    auto& x = std::get<0>(_data);
    x.clear();
    return x;
}
const RenameFileResp& CDCRespContainer::getRenameFile() const {
    ALWAYS_ASSERT(_kind == CDCMessageKind::RENAME_FILE, "%s != %s", _kind, CDCMessageKind::RENAME_FILE);
    return std::get<1>(_data);
}
RenameFileResp& CDCRespContainer::setRenameFile() {
    _kind = CDCMessageKind::RENAME_FILE;
    auto& x = std::get<1>(_data);
    x.clear();
    return x;
}
const SoftUnlinkDirectoryResp& CDCRespContainer::getSoftUnlinkDirectory() const {
    ALWAYS_ASSERT(_kind == CDCMessageKind::SOFT_UNLINK_DIRECTORY, "%s != %s", _kind, CDCMessageKind::SOFT_UNLINK_DIRECTORY);
    return std::get<2>(_data);
}
SoftUnlinkDirectoryResp& CDCRespContainer::setSoftUnlinkDirectory() {
    _kind = CDCMessageKind::SOFT_UNLINK_DIRECTORY;
    auto& x = std::get<2>(_data);
    x.clear();
    return x;
}
const RenameDirectoryResp& CDCRespContainer::getRenameDirectory() const {
    ALWAYS_ASSERT(_kind == CDCMessageKind::RENAME_DIRECTORY, "%s != %s", _kind, CDCMessageKind::RENAME_DIRECTORY);
    return std::get<3>(_data);
}
RenameDirectoryResp& CDCRespContainer::setRenameDirectory() {
    _kind = CDCMessageKind::RENAME_DIRECTORY;
    auto& x = std::get<3>(_data);
    x.clear();
    return x;
}
const HardUnlinkDirectoryResp& CDCRespContainer::getHardUnlinkDirectory() const {
    ALWAYS_ASSERT(_kind == CDCMessageKind::HARD_UNLINK_DIRECTORY, "%s != %s", _kind, CDCMessageKind::HARD_UNLINK_DIRECTORY);
    return std::get<4>(_data);
}
HardUnlinkDirectoryResp& CDCRespContainer::setHardUnlinkDirectory() {
    _kind = CDCMessageKind::HARD_UNLINK_DIRECTORY;
    auto& x = std::get<4>(_data);
    x.clear();
    return x;
}
const CrossShardHardUnlinkFileResp& CDCRespContainer::getCrossShardHardUnlinkFile() const {
    ALWAYS_ASSERT(_kind == CDCMessageKind::CROSS_SHARD_HARD_UNLINK_FILE, "%s != %s", _kind, CDCMessageKind::CROSS_SHARD_HARD_UNLINK_FILE);
    return std::get<5>(_data);
}
CrossShardHardUnlinkFileResp& CDCRespContainer::setCrossShardHardUnlinkFile() {
    _kind = CDCMessageKind::CROSS_SHARD_HARD_UNLINK_FILE;
    auto& x = std::get<5>(_data);
    x.clear();
    return x;
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
        std::get<0>(_data).unpack(buf);
        break;
    case CDCMessageKind::RENAME_FILE:
        std::get<1>(_data).unpack(buf);
        break;
    case CDCMessageKind::SOFT_UNLINK_DIRECTORY:
        std::get<2>(_data).unpack(buf);
        break;
    case CDCMessageKind::RENAME_DIRECTORY:
        std::get<3>(_data).unpack(buf);
        break;
    case CDCMessageKind::HARD_UNLINK_DIRECTORY:
        std::get<4>(_data).unpack(buf);
        break;
    case CDCMessageKind::CROSS_SHARD_HARD_UNLINK_FILE:
        std::get<5>(_data).unpack(buf);
        break;
    default:
        throw BINCODE_EXCEPTION("bad CDCMessageKind kind %s", kind);
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
    case ShardLogEntryKind::MAKE_FILE_TRANSIENT:
        out << "MAKE_FILE_TRANSIENT";
        break;
    case ShardLogEntryKind::REMOVE_SPAN_CERTIFY:
        out << "REMOVE_SPAN_CERTIFY";
        break;
    case ShardLogEntryKind::REMOVE_OWNED_SNAPSHOT_FILE_EDGE:
        out << "REMOVE_OWNED_SNAPSHOT_FILE_EDGE";
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
    out << "ConstructFileEntry(" << "Type=" << (int)x.type << ", " << "DeadlineTime=" << x.deadlineTime << ", " << "Note=" << x.note << ")";
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
    out << "LinkFileEntry(" << "FileId=" << x.fileId << ", " << "OwnerId=" << x.ownerId << ", " << "Name=" << x.name << ")";
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
    out << "SameDirectoryRenameEntry(" << "DirId=" << x.dirId << ", " << "TargetId=" << x.targetId << ", " << "OldName=" << x.oldName << ", " << "OldCreationTime=" << x.oldCreationTime << ", " << "NewName=" << x.newName << ")";
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
    out << "SoftUnlinkFileEntry(" << "OwnerId=" << x.ownerId << ", " << "FileId=" << x.fileId << ", " << "Name=" << x.name << ", " << "CreationTime=" << x.creationTime << ")";
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
}
void CreateLockedCurrentEdgeEntry::unpack(BincodeBuf& buf) {
    dirId.unpack(buf);
    buf.unpackBytes(name);
    targetId.unpack(buf);
}
void CreateLockedCurrentEdgeEntry::clear() {
    dirId = InodeId();
    name.clear();
    targetId = InodeId();
}
bool CreateLockedCurrentEdgeEntry::operator==(const CreateLockedCurrentEdgeEntry& rhs) const {
    if ((InodeId)this->dirId != (InodeId)rhs.dirId) { return false; };
    if (name != rhs.name) { return false; };
    if ((InodeId)this->targetId != (InodeId)rhs.targetId) { return false; };
    return true;
}
std::ostream& operator<<(std::ostream& out, const CreateLockedCurrentEdgeEntry& x) {
    out << "CreateLockedCurrentEdgeEntry(" << "DirId=" << x.dirId << ", " << "Name=" << x.name << ", " << "TargetId=" << x.targetId << ")";
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
    out << "UnlockCurrentEdgeEntry(" << "DirId=" << x.dirId << ", " << "Name=" << x.name << ", " << "CreationTime=" << x.creationTime << ", " << "TargetId=" << x.targetId << ", " << "WasMoved=" << x.wasMoved << ")";
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
    out << "LockCurrentEdgeEntry(" << "DirId=" << x.dirId << ", " << "Name=" << x.name << ", " << "CreationTime=" << x.creationTime << ", " << "TargetId=" << x.targetId << ")";
    return out;
}

void RemoveDirectoryOwnerEntry::pack(BincodeBuf& buf) const {
    dirId.pack(buf);
    buf.packBytes(info);
}
void RemoveDirectoryOwnerEntry::unpack(BincodeBuf& buf) {
    dirId.unpack(buf);
    buf.unpackBytes(info);
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
    out << "RemoveNonOwnedEdgeEntry(" << "DirId=" << x.dirId << ", " << "TargetId=" << x.targetId << ", " << "Name=" << x.name << ", " << "CreationTime=" << x.creationTime << ")";
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
    out << "SameShardHardFileUnlinkEntry(" << "OwnerId=" << x.ownerId << ", " << "TargetId=" << x.targetId << ", " << "Name=" << x.name << ", " << "CreationTime=" << x.creationTime << ")";
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
    buf.packList<EntryBlockService>(blockServices);
}
void UpdateBlockServicesEntry::unpack(BincodeBuf& buf) {
    buf.unpackList<EntryBlockService>(blockServices);
}
void UpdateBlockServicesEntry::clear() {
    blockServices.clear();
}
bool UpdateBlockServicesEntry::operator==(const UpdateBlockServicesEntry& rhs) const {
    if (blockServices != rhs.blockServices) { return false; };
    return true;
}
std::ostream& operator<<(std::ostream& out, const UpdateBlockServicesEntry& x) {
    out << "UpdateBlockServicesEntry(" << "BlockServices=" << x.blockServices << ")";
    return out;
}

void AddSpanInitiateEntry::pack(BincodeBuf& buf) const {
    fileId.pack(buf);
    buf.packScalar<uint64_t>(byteOffset);
    buf.packScalar<uint8_t>(storageClass);
    parity.pack(buf);
    buf.packFixedBytes<4>(crc32);
    buf.packScalar<uint32_t>(size);
    buf.packScalar<uint32_t>(blockSize);
    buf.packBytes(bodyBytes);
    buf.packList<EntryNewBlockInfo>(bodyBlocks);
}
void AddSpanInitiateEntry::unpack(BincodeBuf& buf) {
    fileId.unpack(buf);
    byteOffset = buf.unpackScalar<uint64_t>();
    storageClass = buf.unpackScalar<uint8_t>();
    parity.unpack(buf);
    buf.unpackFixedBytes<4>(crc32);
    size = buf.unpackScalar<uint32_t>();
    blockSize = buf.unpackScalar<uint32_t>();
    buf.unpackBytes(bodyBytes);
    buf.unpackList<EntryNewBlockInfo>(bodyBlocks);
}
void AddSpanInitiateEntry::clear() {
    fileId = InodeId();
    byteOffset = uint64_t(0);
    storageClass = uint8_t(0);
    parity = Parity();
    crc32.clear();
    size = uint32_t(0);
    blockSize = uint32_t(0);
    bodyBytes.clear();
    bodyBlocks.clear();
}
bool AddSpanInitiateEntry::operator==(const AddSpanInitiateEntry& rhs) const {
    if ((InodeId)this->fileId != (InodeId)rhs.fileId) { return false; };
    if ((uint64_t)this->byteOffset != (uint64_t)rhs.byteOffset) { return false; };
    if ((uint8_t)this->storageClass != (uint8_t)rhs.storageClass) { return false; };
    if ((Parity)this->parity != (Parity)rhs.parity) { return false; };
    if (crc32 != rhs.crc32) { return false; };
    if ((uint32_t)this->size != (uint32_t)rhs.size) { return false; };
    if ((uint32_t)this->blockSize != (uint32_t)rhs.blockSize) { return false; };
    if (bodyBytes != rhs.bodyBytes) { return false; };
    if (bodyBlocks != rhs.bodyBlocks) { return false; };
    return true;
}
std::ostream& operator<<(std::ostream& out, const AddSpanInitiateEntry& x) {
    out << "AddSpanInitiateEntry(" << "FileId=" << x.fileId << ", " << "ByteOffset=" << x.byteOffset << ", " << "StorageClass=" << (int)x.storageClass << ", " << "Parity=" << x.parity << ", " << "Crc32=" << x.crc32 << ", " << "Size=" << x.size << ", " << "BlockSize=" << x.blockSize << ", " << "BodyBytes=" << x.bodyBytes << ", " << "BodyBlocks=" << x.bodyBlocks << ")";
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
    out << "MakeFileTransientEntry(" << "Id=" << x.id << ", " << "Note=" << x.note << ")";
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
    out << "RemoveOwnedSnapshotFileEdgeEntry(" << "OwnerId=" << x.ownerId << ", " << "TargetId=" << x.targetId << ", " << "Name=" << x.name << ", " << "CreationTime=" << x.creationTime << ")";
    return out;
}

const ConstructFileEntry& ShardLogEntryContainer::getConstructFile() const {
    ALWAYS_ASSERT(_kind == ShardLogEntryKind::CONSTRUCT_FILE, "%s != %s", _kind, ShardLogEntryKind::CONSTRUCT_FILE);
    return std::get<0>(_data);
}
ConstructFileEntry& ShardLogEntryContainer::setConstructFile() {
    _kind = ShardLogEntryKind::CONSTRUCT_FILE;
    auto& x = std::get<0>(_data);
    x.clear();
    return x;
}
const LinkFileEntry& ShardLogEntryContainer::getLinkFile() const {
    ALWAYS_ASSERT(_kind == ShardLogEntryKind::LINK_FILE, "%s != %s", _kind, ShardLogEntryKind::LINK_FILE);
    return std::get<1>(_data);
}
LinkFileEntry& ShardLogEntryContainer::setLinkFile() {
    _kind = ShardLogEntryKind::LINK_FILE;
    auto& x = std::get<1>(_data);
    x.clear();
    return x;
}
const SameDirectoryRenameEntry& ShardLogEntryContainer::getSameDirectoryRename() const {
    ALWAYS_ASSERT(_kind == ShardLogEntryKind::SAME_DIRECTORY_RENAME, "%s != %s", _kind, ShardLogEntryKind::SAME_DIRECTORY_RENAME);
    return std::get<2>(_data);
}
SameDirectoryRenameEntry& ShardLogEntryContainer::setSameDirectoryRename() {
    _kind = ShardLogEntryKind::SAME_DIRECTORY_RENAME;
    auto& x = std::get<2>(_data);
    x.clear();
    return x;
}
const SoftUnlinkFileEntry& ShardLogEntryContainer::getSoftUnlinkFile() const {
    ALWAYS_ASSERT(_kind == ShardLogEntryKind::SOFT_UNLINK_FILE, "%s != %s", _kind, ShardLogEntryKind::SOFT_UNLINK_FILE);
    return std::get<3>(_data);
}
SoftUnlinkFileEntry& ShardLogEntryContainer::setSoftUnlinkFile() {
    _kind = ShardLogEntryKind::SOFT_UNLINK_FILE;
    auto& x = std::get<3>(_data);
    x.clear();
    return x;
}
const CreateDirectoryInodeEntry& ShardLogEntryContainer::getCreateDirectoryInode() const {
    ALWAYS_ASSERT(_kind == ShardLogEntryKind::CREATE_DIRECTORY_INODE, "%s != %s", _kind, ShardLogEntryKind::CREATE_DIRECTORY_INODE);
    return std::get<4>(_data);
}
CreateDirectoryInodeEntry& ShardLogEntryContainer::setCreateDirectoryInode() {
    _kind = ShardLogEntryKind::CREATE_DIRECTORY_INODE;
    auto& x = std::get<4>(_data);
    x.clear();
    return x;
}
const CreateLockedCurrentEdgeEntry& ShardLogEntryContainer::getCreateLockedCurrentEdge() const {
    ALWAYS_ASSERT(_kind == ShardLogEntryKind::CREATE_LOCKED_CURRENT_EDGE, "%s != %s", _kind, ShardLogEntryKind::CREATE_LOCKED_CURRENT_EDGE);
    return std::get<5>(_data);
}
CreateLockedCurrentEdgeEntry& ShardLogEntryContainer::setCreateLockedCurrentEdge() {
    _kind = ShardLogEntryKind::CREATE_LOCKED_CURRENT_EDGE;
    auto& x = std::get<5>(_data);
    x.clear();
    return x;
}
const UnlockCurrentEdgeEntry& ShardLogEntryContainer::getUnlockCurrentEdge() const {
    ALWAYS_ASSERT(_kind == ShardLogEntryKind::UNLOCK_CURRENT_EDGE, "%s != %s", _kind, ShardLogEntryKind::UNLOCK_CURRENT_EDGE);
    return std::get<6>(_data);
}
UnlockCurrentEdgeEntry& ShardLogEntryContainer::setUnlockCurrentEdge() {
    _kind = ShardLogEntryKind::UNLOCK_CURRENT_EDGE;
    auto& x = std::get<6>(_data);
    x.clear();
    return x;
}
const LockCurrentEdgeEntry& ShardLogEntryContainer::getLockCurrentEdge() const {
    ALWAYS_ASSERT(_kind == ShardLogEntryKind::LOCK_CURRENT_EDGE, "%s != %s", _kind, ShardLogEntryKind::LOCK_CURRENT_EDGE);
    return std::get<7>(_data);
}
LockCurrentEdgeEntry& ShardLogEntryContainer::setLockCurrentEdge() {
    _kind = ShardLogEntryKind::LOCK_CURRENT_EDGE;
    auto& x = std::get<7>(_data);
    x.clear();
    return x;
}
const RemoveDirectoryOwnerEntry& ShardLogEntryContainer::getRemoveDirectoryOwner() const {
    ALWAYS_ASSERT(_kind == ShardLogEntryKind::REMOVE_DIRECTORY_OWNER, "%s != %s", _kind, ShardLogEntryKind::REMOVE_DIRECTORY_OWNER);
    return std::get<8>(_data);
}
RemoveDirectoryOwnerEntry& ShardLogEntryContainer::setRemoveDirectoryOwner() {
    _kind = ShardLogEntryKind::REMOVE_DIRECTORY_OWNER;
    auto& x = std::get<8>(_data);
    x.clear();
    return x;
}
const RemoveInodeEntry& ShardLogEntryContainer::getRemoveInode() const {
    ALWAYS_ASSERT(_kind == ShardLogEntryKind::REMOVE_INODE, "%s != %s", _kind, ShardLogEntryKind::REMOVE_INODE);
    return std::get<9>(_data);
}
RemoveInodeEntry& ShardLogEntryContainer::setRemoveInode() {
    _kind = ShardLogEntryKind::REMOVE_INODE;
    auto& x = std::get<9>(_data);
    x.clear();
    return x;
}
const SetDirectoryOwnerEntry& ShardLogEntryContainer::getSetDirectoryOwner() const {
    ALWAYS_ASSERT(_kind == ShardLogEntryKind::SET_DIRECTORY_OWNER, "%s != %s", _kind, ShardLogEntryKind::SET_DIRECTORY_OWNER);
    return std::get<10>(_data);
}
SetDirectoryOwnerEntry& ShardLogEntryContainer::setSetDirectoryOwner() {
    _kind = ShardLogEntryKind::SET_DIRECTORY_OWNER;
    auto& x = std::get<10>(_data);
    x.clear();
    return x;
}
const SetDirectoryInfoEntry& ShardLogEntryContainer::getSetDirectoryInfo() const {
    ALWAYS_ASSERT(_kind == ShardLogEntryKind::SET_DIRECTORY_INFO, "%s != %s", _kind, ShardLogEntryKind::SET_DIRECTORY_INFO);
    return std::get<11>(_data);
}
SetDirectoryInfoEntry& ShardLogEntryContainer::setSetDirectoryInfo() {
    _kind = ShardLogEntryKind::SET_DIRECTORY_INFO;
    auto& x = std::get<11>(_data);
    x.clear();
    return x;
}
const RemoveNonOwnedEdgeEntry& ShardLogEntryContainer::getRemoveNonOwnedEdge() const {
    ALWAYS_ASSERT(_kind == ShardLogEntryKind::REMOVE_NON_OWNED_EDGE, "%s != %s", _kind, ShardLogEntryKind::REMOVE_NON_OWNED_EDGE);
    return std::get<12>(_data);
}
RemoveNonOwnedEdgeEntry& ShardLogEntryContainer::setRemoveNonOwnedEdge() {
    _kind = ShardLogEntryKind::REMOVE_NON_OWNED_EDGE;
    auto& x = std::get<12>(_data);
    x.clear();
    return x;
}
const SameShardHardFileUnlinkEntry& ShardLogEntryContainer::getSameShardHardFileUnlink() const {
    ALWAYS_ASSERT(_kind == ShardLogEntryKind::SAME_SHARD_HARD_FILE_UNLINK, "%s != %s", _kind, ShardLogEntryKind::SAME_SHARD_HARD_FILE_UNLINK);
    return std::get<13>(_data);
}
SameShardHardFileUnlinkEntry& ShardLogEntryContainer::setSameShardHardFileUnlink() {
    _kind = ShardLogEntryKind::SAME_SHARD_HARD_FILE_UNLINK;
    auto& x = std::get<13>(_data);
    x.clear();
    return x;
}
const RemoveSpanInitiateEntry& ShardLogEntryContainer::getRemoveSpanInitiate() const {
    ALWAYS_ASSERT(_kind == ShardLogEntryKind::REMOVE_SPAN_INITIATE, "%s != %s", _kind, ShardLogEntryKind::REMOVE_SPAN_INITIATE);
    return std::get<14>(_data);
}
RemoveSpanInitiateEntry& ShardLogEntryContainer::setRemoveSpanInitiate() {
    _kind = ShardLogEntryKind::REMOVE_SPAN_INITIATE;
    auto& x = std::get<14>(_data);
    x.clear();
    return x;
}
const UpdateBlockServicesEntry& ShardLogEntryContainer::getUpdateBlockServices() const {
    ALWAYS_ASSERT(_kind == ShardLogEntryKind::UPDATE_BLOCK_SERVICES, "%s != %s", _kind, ShardLogEntryKind::UPDATE_BLOCK_SERVICES);
    return std::get<15>(_data);
}
UpdateBlockServicesEntry& ShardLogEntryContainer::setUpdateBlockServices() {
    _kind = ShardLogEntryKind::UPDATE_BLOCK_SERVICES;
    auto& x = std::get<15>(_data);
    x.clear();
    return x;
}
const AddSpanInitiateEntry& ShardLogEntryContainer::getAddSpanInitiate() const {
    ALWAYS_ASSERT(_kind == ShardLogEntryKind::ADD_SPAN_INITIATE, "%s != %s", _kind, ShardLogEntryKind::ADD_SPAN_INITIATE);
    return std::get<16>(_data);
}
AddSpanInitiateEntry& ShardLogEntryContainer::setAddSpanInitiate() {
    _kind = ShardLogEntryKind::ADD_SPAN_INITIATE;
    auto& x = std::get<16>(_data);
    x.clear();
    return x;
}
const AddSpanCertifyEntry& ShardLogEntryContainer::getAddSpanCertify() const {
    ALWAYS_ASSERT(_kind == ShardLogEntryKind::ADD_SPAN_CERTIFY, "%s != %s", _kind, ShardLogEntryKind::ADD_SPAN_CERTIFY);
    return std::get<17>(_data);
}
AddSpanCertifyEntry& ShardLogEntryContainer::setAddSpanCertify() {
    _kind = ShardLogEntryKind::ADD_SPAN_CERTIFY;
    auto& x = std::get<17>(_data);
    x.clear();
    return x;
}
const MakeFileTransientEntry& ShardLogEntryContainer::getMakeFileTransient() const {
    ALWAYS_ASSERT(_kind == ShardLogEntryKind::MAKE_FILE_TRANSIENT, "%s != %s", _kind, ShardLogEntryKind::MAKE_FILE_TRANSIENT);
    return std::get<18>(_data);
}
MakeFileTransientEntry& ShardLogEntryContainer::setMakeFileTransient() {
    _kind = ShardLogEntryKind::MAKE_FILE_TRANSIENT;
    auto& x = std::get<18>(_data);
    x.clear();
    return x;
}
const RemoveSpanCertifyEntry& ShardLogEntryContainer::getRemoveSpanCertify() const {
    ALWAYS_ASSERT(_kind == ShardLogEntryKind::REMOVE_SPAN_CERTIFY, "%s != %s", _kind, ShardLogEntryKind::REMOVE_SPAN_CERTIFY);
    return std::get<19>(_data);
}
RemoveSpanCertifyEntry& ShardLogEntryContainer::setRemoveSpanCertify() {
    _kind = ShardLogEntryKind::REMOVE_SPAN_CERTIFY;
    auto& x = std::get<19>(_data);
    x.clear();
    return x;
}
const RemoveOwnedSnapshotFileEdgeEntry& ShardLogEntryContainer::getRemoveOwnedSnapshotFileEdge() const {
    ALWAYS_ASSERT(_kind == ShardLogEntryKind::REMOVE_OWNED_SNAPSHOT_FILE_EDGE, "%s != %s", _kind, ShardLogEntryKind::REMOVE_OWNED_SNAPSHOT_FILE_EDGE);
    return std::get<20>(_data);
}
RemoveOwnedSnapshotFileEdgeEntry& ShardLogEntryContainer::setRemoveOwnedSnapshotFileEdge() {
    _kind = ShardLogEntryKind::REMOVE_OWNED_SNAPSHOT_FILE_EDGE;
    auto& x = std::get<20>(_data);
    x.clear();
    return x;
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
    case ShardLogEntryKind::MAKE_FILE_TRANSIENT:
        return std::get<18>(_data).packedSize();
    case ShardLogEntryKind::REMOVE_SPAN_CERTIFY:
        return std::get<19>(_data).packedSize();
    case ShardLogEntryKind::REMOVE_OWNED_SNAPSHOT_FILE_EDGE:
        return std::get<20>(_data).packedSize();
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
    case ShardLogEntryKind::MAKE_FILE_TRANSIENT:
        std::get<18>(_data).pack(buf);
        break;
    case ShardLogEntryKind::REMOVE_SPAN_CERTIFY:
        std::get<19>(_data).pack(buf);
        break;
    case ShardLogEntryKind::REMOVE_OWNED_SNAPSHOT_FILE_EDGE:
        std::get<20>(_data).pack(buf);
        break;
    default:
        throw EGGS_EXCEPTION("bad ShardLogEntryKind kind %s", _kind);
    }
}

void ShardLogEntryContainer::unpack(BincodeBuf& buf, ShardLogEntryKind kind) {
    _kind = kind;
    switch (kind) {
    case ShardLogEntryKind::CONSTRUCT_FILE:
        std::get<0>(_data).unpack(buf);
        break;
    case ShardLogEntryKind::LINK_FILE:
        std::get<1>(_data).unpack(buf);
        break;
    case ShardLogEntryKind::SAME_DIRECTORY_RENAME:
        std::get<2>(_data).unpack(buf);
        break;
    case ShardLogEntryKind::SOFT_UNLINK_FILE:
        std::get<3>(_data).unpack(buf);
        break;
    case ShardLogEntryKind::CREATE_DIRECTORY_INODE:
        std::get<4>(_data).unpack(buf);
        break;
    case ShardLogEntryKind::CREATE_LOCKED_CURRENT_EDGE:
        std::get<5>(_data).unpack(buf);
        break;
    case ShardLogEntryKind::UNLOCK_CURRENT_EDGE:
        std::get<6>(_data).unpack(buf);
        break;
    case ShardLogEntryKind::LOCK_CURRENT_EDGE:
        std::get<7>(_data).unpack(buf);
        break;
    case ShardLogEntryKind::REMOVE_DIRECTORY_OWNER:
        std::get<8>(_data).unpack(buf);
        break;
    case ShardLogEntryKind::REMOVE_INODE:
        std::get<9>(_data).unpack(buf);
        break;
    case ShardLogEntryKind::SET_DIRECTORY_OWNER:
        std::get<10>(_data).unpack(buf);
        break;
    case ShardLogEntryKind::SET_DIRECTORY_INFO:
        std::get<11>(_data).unpack(buf);
        break;
    case ShardLogEntryKind::REMOVE_NON_OWNED_EDGE:
        std::get<12>(_data).unpack(buf);
        break;
    case ShardLogEntryKind::SAME_SHARD_HARD_FILE_UNLINK:
        std::get<13>(_data).unpack(buf);
        break;
    case ShardLogEntryKind::REMOVE_SPAN_INITIATE:
        std::get<14>(_data).unpack(buf);
        break;
    case ShardLogEntryKind::UPDATE_BLOCK_SERVICES:
        std::get<15>(_data).unpack(buf);
        break;
    case ShardLogEntryKind::ADD_SPAN_INITIATE:
        std::get<16>(_data).unpack(buf);
        break;
    case ShardLogEntryKind::ADD_SPAN_CERTIFY:
        std::get<17>(_data).unpack(buf);
        break;
    case ShardLogEntryKind::MAKE_FILE_TRANSIENT:
        std::get<18>(_data).unpack(buf);
        break;
    case ShardLogEntryKind::REMOVE_SPAN_CERTIFY:
        std::get<19>(_data).unpack(buf);
        break;
    case ShardLogEntryKind::REMOVE_OWNED_SNAPSHOT_FILE_EDGE:
        std::get<20>(_data).unpack(buf);
        break;
    default:
        throw BINCODE_EXCEPTION("bad ShardLogEntryKind kind %s", kind);
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
    case ShardLogEntryKind::MAKE_FILE_TRANSIENT:
        out << x.getMakeFileTransient();
        break;
    case ShardLogEntryKind::REMOVE_SPAN_CERTIFY:
        out << x.getRemoveSpanCertify();
        break;
    case ShardLogEntryKind::REMOVE_OWNED_SNAPSHOT_FILE_EDGE:
        out << x.getRemoveOwnedSnapshotFileEdge();
        break;
    default:
        throw EGGS_EXCEPTION("bad ShardLogEntryKind kind %s", x.kind());
    }
    return out;
}

