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
