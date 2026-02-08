#pragma once

#include "common.h"

#include <functional>
#include <sys/epoll.h>

namespace Atp {

// EventCore will need locking - the main thread can call register (etc.) callback 
// functions, whereas the atp thread is the epoll() loop
class EventCore {
public:
    // Invoke callback immediately upon registration, irrespective of if 
    // file descriptor is ready
    static constexpr int kInvokeImmediately = 0x01;

    // Start in suspended state
    static constexpr int kSuspend = 0x10;

    EventCore();

    void Run(); // Houses the main epoll() loop

    // Always > 0
    using callback_ident_t = unsigned int;

    // if fd = -1, flags must have kInvokeImmediately
    // returns 0 on failure, as all callback identifiers must be positive
    callback_ident_t RegisterCallback(int fd, struct epoll_event* event, int flags,
            std::function<mseconds_t(epoll_data_t)> callback);
    // TODO: ^^ should we pass epoll_data_t to the callback? It seems useless, so far.

    int SuspendCallback(callback_ident_t callbackIdentifier);
    int ResumeCallback(callback_ident_t callbackIdentifier);

    int DeleteCallback(callback_ident_t callbackIdentifier);

};

}
