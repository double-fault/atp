#include "nat_resolver.h"

#include <stun/stun.h>
#include <netinet/in.h>

namespace Atp {

INatResolver::NatType StunClient::Resolve(int sockfd, struct sockaddr_in* reflexiveAddress)
{
    Stun::Client client(sockfd, {
            .mTimeoutMs = 50,
            .mMaxRetransmissions = 20,
            .mFinalTimeoutMultiplier = 2
            });

    if (client.QueryAllServers() < 0) {
        reflexiveAddress = nullptr;
        return INatResolver::NatType::kUnknown;
    }

    socklen_t length = sizeof(struct sockaddr_in);
    if (client.GetReflexiveAddress((struct sockaddr*)reflexiveAddress, &length) < 0)
        reflexiveAddress = nullptr;
    THROW_IF(reflexiveAddress->sin_family != AF_INET);

    Stun::NatType type = client.GetNatType();
    INatResolver::NatType ret = INatResolver::NatType::kUnknown;

    if (type == Stun::NatType::kDependent)
        ret = INatResolver::NatType::kDependent;
    else if (type == Stun::NatType::kIndependent)
        ret = INatResolver::NatType::kIndependent;

    return ret;
}

}
