#include "Bincode.hpp"

std::ostream& operator<<(std::ostream& out, const BincodeBytesRef& x) {
    out << "[";
    uint8_t len = x.size();
    const uint8_t* data = (const uint8_t*)x.data();
    for (int i = 0; i < len; i++) {
        if (i > 0) {
            out << " ";
        }
        out << (int)data[i];
    }
    out << "]";
    return out;
    /*
    out << "b\"";
    uint8_t len = x.size();
    const uint8_t* data = (const uint8_t*)x.data();
    for (int i = 0; i < len; i++) {
        uint8_t ch = data[i];
        if (isprint(ch)) {
            out << ch;
        } else if (ch == 0) {
            out << "\\0";
        } else {
            const char cfill = out.fill();
            out << std::hex << std::setfill('0');
            out << "\\x" << std::setw(2) << (int)ch;
            out << std::setfill(cfill) << std::dec;
        }
    }
    out << "\"";
    return out;
    */
}

std::ostream& operator<<(std::ostream& out, const BincodeBytes& x) {
    return out << x.ref();
}

const char* BincodeException::what() const noexcept {
    return _msg.c_str();
}
