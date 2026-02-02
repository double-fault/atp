#pragma once

#include <cstddef>
#include <netdb.h>
#include <string>
#include <sys/socket.h>
#include <sys/types.h>

#include "eventcore.h"
#include "types.h"

namespace Atp {

class ISignallingProvider {
protected:
    ISignallingProvider(EventCore* eventCore)
        : mEventCore { eventCore }
    {
    }

public:
    // Returns an eventfd which can be used with select/poll/epoll
    // I.e, the eventfd can be used to wait for readability. It is
    // assumed that the signal provider is always writable/a send()
    // can always be performed
    virtual int Socket() = 0;

    // NOTE: Every signalling socket MUST be bound to an address
    // If you want to assign an ephemeral address, do it in the user.
    virtual int Bind(int sigfd, const struct sockaddr_atp* addr) = 0;

    // Non-blocking
    virtual int Send(int sigfd, const void* buf, size_t len,
        const struct sockaddr_atp* dest)
        = 0;
    // Non-blocking
    virtual int Recv(int sigfd, void* buf, size_t len,
        struct sockaddr_atp* source)
        = 0;

    virtual ~ISignallingProvider() = default;

protected:
    // NOTE: Use the event core to register a callback for when the internal
    // socket used by the implementation is readable, i.e a msg has been recvd
    EventCore* mEventCore;
};

}
