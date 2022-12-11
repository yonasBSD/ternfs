#include "ShardDBData.hpp"

std::ostream& operator<<(std::ostream& out, SpanState state) {
    switch (state) {
    case SpanState::CLEAN:
        out << "CLEAN";
        break;
    case SpanState::DIRTY:
        out << "DIRTY";
        break;
    case SpanState::CONDEMNED:
        out << "CONDEMNED";
        break;
    default:
        out << "SpanState(" << ((int)state) << ")";
        break;
    }
    return out;
}

std::ostream& operator<<(std::ostream& out, const EdgeKey& edgeKey) {
    out << "EdgeKey(dirId=" << edgeKey.dirId() << ", current=" << (int)edgeKey.current() << ", nameHash=" << edgeKey.nameHash() << ", name=" << edgeKey.name();
    if (!edgeKey.current()) {
        out << ", creationTime=" << edgeKey.creationTime();
    }
    out << ")";
    return out;
}

void blockServicesToValue(const BincodeList<EntryBlockService>& entries, std::string& buf) {
    buf.resize(entries.packedSize());
    BincodeBuf bbuf(buf);
    bbuf.packList(entries);
    ALWAYS_ASSERT(bbuf.remaining() == 0);
}

void blockServicesFromValue(const rocksdb::Slice& value, BincodeList<EntryBlockService>& entries) {
    BincodeBuf bbuf((char*)value.data(), value.size());
    bbuf.unpackList(entries);
    ALWAYS_ASSERT(bbuf.remaining() == 0);
}