#pragma once 

#include <cstddef>
#include <cstdint>
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

// TODO: Make most/all of these configurable using socket options
namespace Config {
    static constexpr mseconds_t kNatKeepAliveTimeout = 5000;
    static constexpr size_t kMaxSocketCount = 32;
    static constexpr size_t kMaxBacklog = 64;
    static constexpr mseconds_t kPunchInterval = 5000;
    static constexpr mseconds_t kPunchTimeout = 3 * 60 * 1000; 

    // HACK: Constant window size, for now. Need to implement properly later
    static constexpr uint16_t kConstantWindow = 4096;
};


}
