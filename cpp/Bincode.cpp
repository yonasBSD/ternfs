#include "Bincode.hpp"

std::ostream& operator<<(std::ostream& out, const BincodeBytes& x) {
    out << "b\"";
    for (int i = 0; i < x.length; i++) {
        uint8_t ch = x.data[i];
        if (isprint(ch)) {
            out << ch;
        } else if (ch == 0) {
            out << "\\0";
        } else {
            const char cfill = out.fill();
            out << std::hex << std::setfill('0');
            out << "\\x" << std::setw(2) << ((int)ch);
            out << std::setfill(cfill) << std::dec;
        }
    }
    out << "\"";
    return out;
}

const char* BincodeException::what() const noexcept {
    return _msg.c_str();
}
