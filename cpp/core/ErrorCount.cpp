#include "ErrorCount.hpp"

void ErrorCount::reset() {
    for (int i = 0; i < count.size(); i++) {
        count[i].store(0);
    }
}
