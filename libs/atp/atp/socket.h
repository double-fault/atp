#pragma once

#include "eventcore.h"
#include "isignalling.h"
#include "protocol.h"
#include "types.h"
#include "demux.h"

#include <queue>
#include <stun/stun.h>

#include <expected>
#include <memory>
#include <sys/epoll.h>

namespace Atp {

// I don't like the SocketImpl name, but I've spent 30 mins trying to cum up with
// a better name :skull:
class SocketImpl final {
private:
    SocketImpl() = default;

public:
    static std::expected<SocketImpl, Error> Create(EventCore* eventCore,
        ISignallingProvider* signallingProvider);

    int GetApplicationFd();
    void NetworkRecvCallback(const void* buffer, size_t length);

    Error Bind(const struct sockaddr_atp* addr);
    Error Listen(int backlog);
    Error Accept(struct sockaddr_atp* addr);
    Error Connect(const struct sockaddr_atp* addr);

    Error GetSockOpt(int level, int optname, void* optval, socklen_t* optlen);
    Error SetSockOpt(int level, int optname, const void* optval, socklen_t optlen);

private:
    // TODO: Allow setting custom keepalive frequency/time gap through
    // setsockopt.
    mseconds_t NatKeepAliveCallback(epoll_data_t data);

    mseconds_t ApplicationRecvCallback(epoll_data_t data);

    void NetworkRecvCallback(void* buffer, size_t length);
    mseconds_t SignallingRecvCallback(epoll_data_t data);

    EventCore* mEventCore;
    ISignallingProvider* mSignallingProvider;

    State mState;

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

    std::shared_ptr<Demux> mDemux {};

    std::unique_ptr<Stun::Client> mStunClient {};
    mseconds_t mNatKeepAliveTimeout;
    EventCore::callback_ident_t mNatKeepAliveCallback;

    EventCore::callback_ident_t mApplicationRecvCallback;

    int mSignallingSocket;
    std::unique_ptr<struct sockaddr_atp> mSignallingAddress {};
    EventCore::callback_ident_t mSignallingRecvCallback;

    // At any point, the sum of sizes of the two queues <= backlog
    int mBacklog;
    std::queue<SocketImpl> mCompletedConnections;
    std::queue<SocketImpl> mIncompleteConnections;

    struct Stats {
        long mSocketsAccepted{};
        long mConnectionsRefused{};
    } mStats;
};

}
