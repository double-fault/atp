#pragma once

#include "common.h"
#include "demux.h"
#include "eventcore.h"
#include "protocol.h"
#include "signalling.h"
#include "types.h"

#include <queue>
#include <stun/stun.h>

#include <expected>
#include <memory>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/epoll.h>

namespace Atp {

// I don't like the SocketImpl name, but I've spent 30 mins trying to cum up with
// a better name :skull:
class SocketImpl final {
private:
    SocketImpl() = default;
    std::expected<SocketImpl, Error> CloneToAccept(const struct sockaddr_in* peerAddress);

public:
    static std::expected<SocketImpl, Error> Create(EventCore* eventCore,
        ISignallingProvider* signallingProvider);

    SocketImpl(const SocketImpl&) = delete;
    SocketImpl& operator=(const SocketImpl&) = delete;

    SocketImpl(SocketImpl&&) = default;
    SocketImpl& operator=(SocketImpl&&) = default;

    ~SocketImpl(); // TODO: what do I clean in destructor, and what in explicit close funcn?

    int GetApplicationFd();
    void NetworkRecvCallback(const void* buffer, size_t length);

    Error Bind(const struct sockaddr_atp* addr);
    Error Listen(int backlog);
    Error Accept(struct sockaddr_atp* addr);
    Error Connect(const struct sockaddr_atp* addr);

    Error GetSockOpt(int level, int optname, void* optval, socklen_t* optlen);
    Error SetSockOpt(int level, int optname, const void* optval, socklen_t optlen);

private:
    State mState { State::CLOSED };
    EventCore* mEventCore {};

    // NOTE: Layering:
    // (1) Application
    // =    (Application FD) -- |
    // =            (ATP FD) -- | (Connected unix domain socketpair)
    // (2) ATP Protocol Stack
    // =     (Network FD)
    // (3) UDP/Kernel
    int mApplicationFd { -1 };
    int mAtpFd { -1 };
    int mNetworkFd { -1 };

    std::shared_ptr<Demux> mDemux {};

    std::shared_ptr<Stun::Client> mStunClient {};
    mseconds_t NatKeepAliveCallback(epoll_data_t data);
    EventCore::callback_ident_t mNatKeepAliveCallback {};

    mseconds_t PunchTransmitCallback(epoll_data_t data);
    EventCore::callback_ident_t mPunchTransmitCallback {};
    int mPunchPacketCounter {};

    /* Active sockets */

    void NetworkRecvCallback(void* buffer, size_t length);
    Demux::callback_ident_t mNetworkRecvCallback {};

    mseconds_t ApplicationRecvCallback(epoll_data_t data);
    EventCore::callback_ident_t mApplicationRecvCallback {};

    struct sockaddr_in mPeerAddress {};

    /* Signalling */

    mseconds_t SignallingRecvCallback(epoll_data_t data);

    ISignallingProvider* mSignallingProvider;
    int mSignallingSocket;
    std::unique_ptr<struct sockaddr_atp> mSignallingAddress {};
    EventCore::callback_ident_t mSignallingRecvCallback;

    /* Passive sockets */

    void WildcardRecvCallback(void* buffer, size_t length);
    Demux::callback_ident_t mWildcardRecvCallback;

    // At any point, the sum of sizes of the two queues <= backlog
    int mBacklog;
    std::queue<SocketImpl> mCompletedConnections {};
    std::queue<SocketImpl> mIncompleteConnections {};

    /* Stats */

    struct Stats {
        long mSocketsAccepted {};
        long mConnectionsRefused {};
    } mStats;
};

}
