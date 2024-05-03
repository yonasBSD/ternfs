#include "Msgs.hpp"

#include <unordered_map>

#include <netinet/in.h>

void IpPort::toSockAddrIn(struct sockaddr_in& out) const {
    out.sin_family = AF_INET;
    memcpy(&out.sin_addr.s_addr, ip.data.data(), ip.data.size());
    out.sin_port = htons(port);
}

IpPort IpPort::fromSockAddrIn(const struct sockaddr_in& in){
    IpPort addr;
    memcpy(addr.ip.data.data(), &in.sin_addr.s_addr, addr.ip.data.size());
    addr.port = ntohs(in.sin_port);
    return addr;
}

std::ostream& operator<<(std::ostream& out, const IpPort& addr) {
    return out << (int) addr.ip.data[0] << '.' << (int) addr.ip.data[1] << '.' << (int) addr.ip.data[2] << '.' << (int) addr.ip.data[3] << ':' << addr.port;
}

std::ostream& operator<<(std::ostream& out, const AddrsInfo& addrs) {
    out << "AddrsInfo(addr1=" << addrs[0] << ", addr2=" << addrs[1] << ")";
    return out;
}

std::ostream& operator<<(std::ostream& out, ShardId shard) {
    return out << int(shard.u8);
}

std::ostream& operator<<(std::ostream& out, ReplicaId replica) {
    return out << int(replica.u8);
}

std::ostream& operator<<(std::ostream& out, ShardReplicaId shrid) {
    return out << std::setw(3) << std::setfill('0') << shrid.shardId() << ":" << shrid.replicaId();
}

std::ostream& operator<<(std::ostream& out, InodeId id) {
    const char cfill = out.fill();
    out << "0x" << std::setfill('0') << std::setw(16) << std::hex << id.u64;
    out << std::dec << std::setfill(cfill);
    return out;
}

std::ostream& operator<<(std::ostream& out, InodeIdExtra id) {
    return out << "[" << (id.extra() ? 'X' : ' ') << "]" << id.id();
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

std::ostream& operator<<(std::ostream& out, Crc crc) {
    char buf[9];
    sprintf(buf, "%08x", crc.u32);
    return out << buf;
}

std::ostream& operator<<(std::ostream& out, BlockServiceId id) {
    char buf[19];
    sprintf(buf, "0x%016lx", id.u64);
    return out << buf;
}

std::ostream& operator<<(std::ostream& out, LogIdx idx) {
    out << idx.u64;
    return out;
}

std::ostream& operator<<(std::ostream& out, LeaderToken token) {
    return out << token.idx() << ":" << token.replica();
}
