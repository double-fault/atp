#pragma once

#include "common.h"

#include <netinet/in.h>

namespace Atp {

class INatResolver {
public:
    enum class NatType {
        kUnknown = 0,
        kIndependent,
        kDependent
    };

    virtual ~INatResolver() = default;

    // On failure, *both* reflexiveAddress is nullptr and returned type is Unknown
    // *Must* be non-blocking, or not too long a call
    virtual NatType Resolve(int sockfd, struct sockaddr_in* reflexiveAddress) = 0;
};

class StunClient final : public INatResolver  {
public:
    NatType Resolve(int sockfd, struct sockaddr_in* reflexiveAddress) override;
};

}
