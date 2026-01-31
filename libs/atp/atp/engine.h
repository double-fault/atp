#pragma once

#include "eventcore.h"
#include "isignalling.h"
#include "protocol.h"

namespace Atp {

// (CONTEXT <-> ENGINE COMMUNICATION)
// Lets use Unix domain sockets to communicate between Context and Engine?
// They solve shit ton of issues ig, can also apply backpressure using
// setsockopt or something ig.
// Can easily epoll() on unix domain sockets (in both engine and application)
// Also allows for half-close/shutdown().
//
// Application <-> Context <-> Unix domain socket < --|
//   Udp socket <-> Engine <-> Unix domain socket < --|
//
// Use socketpair() to generate unix domain socket pair.

// (ACCEPT)
// How will Accept() work?
// The peer which wants to connect to a server uses the signalling provider to send
// a signal along with its address. The server gets this signal, then starts sending
// UDP packets to the client. The client which has looked up the public address using
// getaddrinfo() will also start transmitting UDP packets, and hopefully a hole will
// be punched.
// At this point, the connection has been established and the connection is in some sort
// of "pending" state till the application calls ctx.Accept()
//
// The packets should be continuously sent for some period (say few mins) before giving up.
// If it succeeds, how will Engine communicate back to Context the client address and
// the success value?
// Solution for now is to simply make ctx.Accept() non-blocking always.
// If blocking behavior is needed, an eventfd (or smth similar) can be used to signal
// from Engine -> Context, where ctx.Accept() sleeps on this eventfd, and then when woken
// up calls engine.Accept() which successfully returns a connection.

// (LOCKING, FALSE SHARING)
// Accept() removes an address from the queue of "pending connections".
// Code like setaddrinfo() runs in the main application thread and uses the signalling
// provider.
// What amount (if any) locking is required? Any way to avoid locking?
// False sharing/cache invalidation will also happen ig, although probably that
// does not matter in the grand scheme of things/speed anyway.
//
// Lets say the application itself spawns two threads and calls Accept() or setaddrinfo()
// or some other function in both threads. Then even if Engine did not have a separate thread,
// some amount of locking would probably be required(?).

class Engine final {
public:
    // NOTE: Constructor should spawn a thread? Which runs some internal Run()/EventLoop() function
    // Run() should be like your event loop which runs epoll() or some variant
    Engine(ISignallingProvider* signallingProvider);
    ~Engine();

    int Socket(int domain, int type, int protocol);
    int Accept(int sockfd, struct sockaddr_atp* addr);
    int Connect(int sockfd, const struct sockaddr_atp* addr);
    int Listen(int sockfd, int backlog);
    int Bind(int sockfd, const struct sockaddr_atp* addr);

    int GetSockOpt(int sockfd, int level, int optname, void* optval, socklen_t* optlen);
    int SetSockOpt(int sockfd, int level, int optname, const void* optval, socklen_t optlen);

private:
    ISignallingProvider* mSignallingProvider;
    EventCore mEventCore;
};

}
