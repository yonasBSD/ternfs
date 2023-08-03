#include "Loop.hpp"

void* runLoop(void* server) {
    ((Loop*)server)->run();
    return nullptr;
}
