#pragma once

#include "Common.hpp"

#define ON_BLOCK_EXIT_INNER(line) SCOPEGUARD ## line
#define ON_BLOCK_EXIT UNUSED const auto & ON_BLOCK_EXIT_INNER(__LINE__) = makeScopeGuard 


template<typename L>
class ScopeGuard {
public:
    explicit ScopeGuard(L &&l) :
        _committed(false),
        _rollback(std::forward<L>(l)) {
    }

    // This is important, the class can be moved, this means
    // we can return by value and even if the copy is not elided
    // then we will only execute the commit once.
    ScopeGuard(ScopeGuard &&rhs) :
        _committed(rhs._committed),
        _rollback(std::move(rhs._rollback)) {
        rhs._committed = true;
    }

    ~ScopeGuard() {
        if (!_committed) {
            _rollback();
        }
    }

    void commit() noexcept {
        _committed = true;
    }

private:
    bool _committed;
    L _rollback;
};


template<typename L>
ScopeGuard<L> makeScopeGuard(L &&r) {
    return ScopeGuard<L>(std::forward<L>(r));
}
