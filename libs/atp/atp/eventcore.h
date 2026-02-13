#pragma once

#include "common.h"
#include "posix_socket.h"

#include <functional>
#include <sys/epoll.h>

namespace Atp {

class IEventCore {
public:
    /* Flags */

    // Invoke callback immediately upon registration, irrespective of if
    // file descriptor is ready
    static constexpr int kInvokeImmediately = 0x01;

    // Start in suspended state
    static constexpr int kSuspend = 0x10;


    /* Methods */
    virtual ~IEventCore() = default;

    virtual void Run() = 0;

    // Always > 0
    using callback_ident_t = unsigned int;

    // returns 0 on failure, callbackData is passed to the callback as-is
    virtual callback_ident_t RegisterCallback(ISocket* socket, int flags,
            std::function<mseconds_t(void*)> callback, void* callbackData) = 0;

    // kInvokeImmediately *must* be set, returns 0 on failure
    virtual callback_ident_t RegisterCallback(int flags, 
            std::function<mseconds_t(void*)> callback, void* callbackData) = 0;

    virtual int SuspendCallback(callback_ident_t callbackIdentifier) = 0;
    virtual int ResumeCallback(callback_ident_t callbackIdentifier) = 0;

    virtual int DeleteCallback(callback_ident_t callbackIdentifier) = 0;
};

// EventCore will need locking - the main thread can call register (etc.) callback
// functions, whereas the atp thread is the epoll() loop
class EventCore final : public IEventCore {
public:
    EventCore() = default;

    void Run() override; // Houses the main epoll() loop

    // Simply use ISocket.GetFd() and use that in the epoll() loop
    // When using a fake ISocket, we will have to use a fake IEventCore which does not 
    // truly use an fd
    callback_ident_t RegisterCallback(ISocket* socket, int flags,
            std::function<mseconds_t(void*)> callback, void* callbackData) override;

    callback_ident_t RegisterCallback(int flags, 
            std::function<mseconds_t(void*)> callback, void* callbackData) override;

    int SuspendCallback(callback_ident_t callbackIdentifier) override;
    int ResumeCallback(callback_ident_t callbackIdentifier) override;

    int DeleteCallback(callback_ident_t callbackIdentifier) override;
};

}
