#pragma once

#include "eventcore.h"

namespace Atp {

// NOTE: Assumption is that no two sockets will Bind() the same atp address (sort
// of like no duplicate domain names - need to enforce the invariant, but that
// comes later).
// This means that there is a 1:1 relationship b/w atp address <-> ip:port.
// So, we can demultiplex solely based off ip:port.
class Demux {
public:
    // Any messages received which do not match a registered callback are silently 
    // dropped
    Demux(EventCore* eventCore, int networkFd);

    // Returns 0 on failure, all identifers should be positive
    using callback_ident_t = unsigned int;
    callback_ident_t RegisterCallback(struct sockaddr_in* sourceAddress,
        std::function<void(void* buffer, size_t length)> callback);

    callback_ident_t RegisterWildcardCallback(
        std::function<void(void* buffer, size_t length)> callback);

    int DeleteCallback(callback_ident_t callbackIdentifier);
};

}
