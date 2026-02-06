#pragma once

#include <cstddef>
#include <netdb.h>
#include <string>
#include <sys/socket.h>
#include <sys/types.h>

#include "eventcore.h"
#include "types.h"

namespace Atp {

// Signalling implementation *must* be reliable
class ISignallingProvider {
protected:
    ISignallingProvider(EventCore* eventCore)
        : mEventCore { eventCore }
    {
    }

public:
    // Returns an eventfd (EFD_SEMAPHORE) which can be used with select/poll/epoll
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
    virtual int Recv(int sigfd, void* buf, size_t* len,
        struct sockaddr_atp* source)
        = 0;

    virtual ~ISignallingProvider() = default;

protected:
    // NOTE: Use the event core to register a callback for when the internal
    // socket used by the implementation is readable, i.e a msg has been recvd
    EventCore* mEventCore;
};

inline constexpr uint16_t kSignalMagic = 0xF6F9;

struct __attribute__((packed)) signal {
    uint16_t magic;
    uint8_t zero;
    union {
        uint8_t type;
        struct __attribute__((packed)) {
            unsigned request : 1;
            unsigned response : 1;
            unsigned res2 : 1;
            unsigned res3 : 1;
            unsigned res4 : 1;
            unsigned res5 : 1;
            unsigned res6 : 1;
            unsigned res7 : 1;
        };
    };
    uint16_t addr_family;
    uint16_t addr_port;
    uint32_t addr_ipv4;
};

static_assert(sizeof(signal) == 12);

int IsSignal(const void* buffer, size_t bufferLength);
int BuildSignal(const struct signal* sig, void* buffer, size_t* bufferLength);
int ReadSignal(const void* buffer, size_t bufferLength, struct signal* sig);

}
