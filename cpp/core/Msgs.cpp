#include <unordered_map>

#include "Msgs.hpp"

std::ostream& operator<<(std::ostream& out, ShardId shard) {
    out << int(shard.u8);
    return out;
}

std::ostream& operator<<(std::ostream& out, InodeId id) {
    const char cfill = out.fill();
    out << "0x" << std::setfill('0') << std::setw(16) << std::hex << id.u64;
    out << std::dec << std::setfill(cfill);
    return out;
}

std::ostream& operator<<(std::ostream& out, InodeIdExtra id) {
    out << "[" << (id.extra() ? 'X' : ' ') << "]" << id.id();
    return out;
}

std::ostream& operator<<(std::ostream& out, Parity parity) {
    if (parity.u8 == 0) {
        return out << "Parity(0)";
    } else {
        out << "Parity(" << (int)parity.dataBlocks() << ", " << (int)parity.parityBlocks() << ")";
    }
    return out;
}

static const std::unordered_map<std::string, uint8_t> STORAGE_CLASSES_BY_NAME = {
    {"HDD", 2},
    {"FLASH", 3},
};

uint8_t storageClassByName(const char* name) {
    return STORAGE_CLASSES_BY_NAME.at(name);
}
