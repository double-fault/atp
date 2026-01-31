
//
// Dealing with no hairpin support NATs?
//
// If I'm spawning a thread, then some amount of locking would be required?
// Any way to still keep calls like fork() working?
// Can have multiple connections at a given time (imagine server accepting multiple conn.)
// Need continuous keepalive messages.
// Flow control, retransmissions, no congestion control pls.
//

// (KEEPALIVE)
// From point of atp init (or constructor), NAT type needs to be checked and continuous
// keepalive messages should be sent to STUN server to ensure NAT binding is fixed.
//
// What happens if network connection is changed (for eg. wifi -> mobile data)?
// For now, just drop the connection; recovering the connection will increase code complexity.
//
// Once a connection is established, continuous keep alive messages also need to be sent
// through the connection to ensure NAT bindings for incoming packets in peer NATs.
// Should skip keepalive messages to STUN server while a connection is established, although
// this is extra complexity. Since keepalive messages will likely be sparse, this logic
// can be skipped for simplicity.

#pragma once

#include "eventcore.h"
#include "isignalling.h"
#include "protocol.h"
#include "common.h"

#include <stun/stun.h>

#include <map>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <thread>

namespace Atp {

// NOTE: It would be nice to integrate ATP into the kernel somehow (kernel module?)
// So you could do something like domain = AF_INET, socktype = SOCK_STREAM, protocol = ATP
// man socket(2) states that normally protocol is 0 as only one protocol exists for a
// (family, socktype) combo, would be nice to add a second protocol lol
// The current interface is a bit hacky but ok for first iteration lol, you need to
// call ctx.funcn for like half the things, and direct read/write/close on the atpfd
// as the interface b/w Context and Engine is a unix domain socket


// BUG: Imagine context functions are being called from multiple application threads - 
// we would need locking in all the ctx functions when modifying data structures etc.
class Context final {
public:
    Context(ISignallingProvider* signallingProvider);
    ~Context() = default; // TODO: Any cleanup required?

    int Socket(int domain, int type, int protocol); // Only supporting AF_INET rn

    // Passive sockets: Socket() -> Bind() -> Listen() -> Accept()
    // Active sockets: Socket() -> (optional: Bind()) -> Connect()
    int Bind(int appfd, const struct sockaddr_atp* addr);
    int Listen(int appfd, int backlog);
    // HACK: Always non-blocking, for now.
    // Note that a listening socket becomes readable when a connection is available,
    // so we can implement that with atpfd (the unix domain socket) somehow?
    int Accept(int appfd, struct sockaddr_atp* addr);

    int Connect(int appfd, const struct sockaddr_atp* addr);

    int GetSockOpt(int appfd, int level, int optname, void* optval, socklen_t* optlen);
    int SetSockOpt(int appfd, int level, int optname, const void* optval, socklen_t optlen);
    // TODO: fcntl? (Stevens chapter 7)

    /* Deleted Functions */

    int GetAddrInfo(...) = delete;
    int GetNameInfo(...) = delete;

    // Call the glibc functions directly on the fdYou should use Microsoft OFFICE 365  to view the powerpoint slides of the lectures. Each student can get access to an account on OFFICE 365. Please contact Computer Center for the formalities. 
    ssize_t Read(...) = delete;
    ssize_t Write(...) = delete;
    int Close(...) = delete;
    int Shutdown(...) = delete;

private:
    struct SocketData {
        // NOTE: Layering:
        // (1) Application
        // =    (Application FD) -- |
        // =            (ATP FD) -- | (Connected unix domain socketpair)
        // (2) ATP Protocol Stack
        // =     (Network FD)
        // (3) UDP/Kernel
        int mApplicationFd;
        int mAtpFd;
        int mNetworkFd;

        std::unique_ptr<Stun::Client> mStunClient{};
        mseconds_t mNatKeepAliveTimeout;
        EventCore::callback_ident_t mNatKeepAliveCallback;

        EventCore::callback_ident_t mApplicationRecvCallback;
        EventCore::callback_ident_t mNetworkRecvCallback;

        int mSignallingSocket;
        std::unique_ptr<struct sockaddr_atp> mSignallingAddress{};
        EventCore::callback_ident_t mSignallingRecvCallback;
    };

    // TODO: Allow setting custom keepalive frequency/time gap through 
    // setsockopt.
    mseconds_t NatKeepAliveCallback(epoll_data_t data);

    mseconds_t ApplicationRecvCallback(epoll_data_t data);
    mseconds_t NetworkRecvCallback(epoll_data_t data);

    ISignallingProvider* mSignallingProvider;
    EventCore mEventCore;
    std::thread mEventLoopThread;

    std::vector<SocketData> mSockets;
    std::unordered_map<int, SocketData*> mFdToSocket;
    std::set<int> mApplicationFds;
};

}
