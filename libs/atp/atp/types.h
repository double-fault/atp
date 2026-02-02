#pragma once

#include <sys/socket.h>
#include <type_traits>

namespace Atp {

enum {
    IPPROTO_ATP = 111
};

struct sockaddr_atp {
    sa_family_t sa_family;
    char hostname[16];
    char service[14];
};

enum class Error : int {
    SUCCESS = 0,
    UNKNOWN = -1,
    ACCESS = -2, 
    AFNOSUPPORT = -3,
    INVAL = -4, 
    MFILE = -5, 
    NFILE = -6, 
    NOMEM = -7,
    PROTONOSUPPORT = -8,
    MAXSOCKETS = -9,
    NATQUERYFAILURE = -10,
    NATDEPENDENT = -11,
    EVENTCORE = -12,
    SIGNALLINGPROVIDER = -13,
    BADFD = -14,
    ALREADYSET = -15, 
    NOTBOUND = -16,
};

// https://www.learncpp.com/cpp-tutorial/scoped-enumerations-enum-classes/#operatorplus
template <typename T>
constexpr auto operator+(T a) noexcept
{
    return static_cast<std::underlying_type_t<T>>(a);
}

const char* Strerror(Error err);
Error ErrnoToErrorCode(int errnum); // Converts errno value to error codes

}
