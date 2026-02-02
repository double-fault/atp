#pragma once 

#include <cstddef>
#include <stdexcept>

namespace Atp {

#define likely(x) __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)

#define THROW(message) throw std::runtime_error(#message)

#define THROW_IF(condition)                       \
    do {                                          \
        if (unlikely(condition))                  \
            throw std::runtime_error(#condition); \
    } while (false)

using mseconds_t = int;

namespace Config {
    static constexpr mseconds_t kNatKeepAliveTimeout = 5000;
    static constexpr size_t kMaxSocketCount = 32;
};


}
