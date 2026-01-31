#pragma once 

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

}
