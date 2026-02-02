#pragma once

#include "eventcore.h"

namespace Atp {

class Demux {
public:
    Demux(EventCore* eventCore, int networkFd,
        std::function<void(void* buffer, size_t length)> wildcardCallback);

    using callback_ident_t = unsigned int;
    callback_ident_t RegisterCallback(struct sockaddr_in* sourceAddress,
        std::function<void(void* buffer, size_t length)> callback);

    int DeleteCallback(callback_ident_t callbackIdentifier);
};

}
