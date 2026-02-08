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
#include <netinet/in.h>
#include <sys/epoll.h>
#include <sys/socket.h>

namespace Atp {

class Context;

// I don't like the SocketImpl name, but I've spent 30 mins trying to cum up with
// a better name :skull:
// HACK: Keep thinking about locking.
class SocketImpl final {
private:
    SocketImpl() = default;
    Result<std::unique_ptr<SocketImpl>> CloneForConnection(
        const struct sockaddr_atp* peerAddressAtp,
        const struct sockaddr_in* peerAddrIn);

public:
    static Result<std::unique_ptr<SocketImpl>> Create(
        EventCore* EventCore,
        ISignallingProvider* signallingProvider);

    SocketImpl(const SocketImpl&) = delete;
    SocketImpl& operator=(const SocketImpl&) = delete;

    SocketImpl(SocketImpl&&) = default;
    SocketImpl& operator=(SocketImpl&&) = default;

    ~SocketImpl(); // TODO: what do I clean in destructor, and what in explicit close funcn?

    int GetApplicationFd();
    const struct sockaddr_atp* GetPeerAddress();

    Error Bind(const struct sockaddr_atp* addr);
    Error Listen(int backlog);
    Result<SocketImpl*> Accept(Context* context);
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
    // TODO: connection keepalive callback - merge with Nat keepalive
    // to have a single keepalive callback?
    mseconds_t NatKeepAliveCallback(epoll_data_t data);
    EventCore::callback_ident_t mNatKeepAliveCallback {};

    /* Active sockets */

    SocketImpl* mPassiveOwner {}; // the passive socket owning this one, if any

    // Punching from the server side is done in the new cloned socket object,
    // so not in the passive listening socket object
    mseconds_t PunchThroughCallback(epoll_data_t data);
    EventCore::callback_ident_t mPunchThroughCallback {};
    int mPunchPacketCounter {};

    void SetupSocketpair(SocketImpl* socket);

    void NetworkRecvCallback(const void* buffer, size_t length);
    Demux::callback_ident_t mNetworkRecvCallback {};

    void NetworkRecvPunch(const struct atp_hdr* header,
        const void* payload, size_t length);
    void NetworkRecvThru(const struct atp_hdr* header,
        const void* payload, size_t length);
    void NetworkRecvEstablished(const struct atp_hdr* header,
        const void* payload, size_t length);

    mseconds_t ApplicationRecvCallback(epoll_data_t data);
    EventCore::callback_ident_t mApplicationRecvCallback {};

    struct sockaddr_atp mPeerAddressAtp {};
    struct sockaddr_in mPeerAddressIn {};
    // helpers
    void SendDatagram(const void* datagram, size_t length);
    void SendControlDatagram(union atp_control control);

    /* Signalling */

    mseconds_t SignallingRecvCallback(epoll_data_t data);
    EventCore::callback_ident_t mSignallingRecvCallback {};
    
    void SignallingRecvRequest(const struct signal* request, const struct sockaddr_atp* source);
    void SignallingRecvResponse(const struct signal* response, const struct sockaddr_atp* source);

    ISignallingProvider* mSignallingProvider {};
    int mSignallingSocket { -1 };
    std::unique_ptr<struct sockaddr_atp> mSignallingAddress {};

    /* Passive sockets */

    void WildcardRecvCallback(const void* buffer, size_t length);
    // Demux::callback_ident_t mWildcardRecvCallback {};

    // At any point, the sum of sizes of both should be <= backlog
    int mBacklog {};
    std::queue<std::unique_ptr<SocketImpl>> mCompletedConnections {};
    std::vector<std::unique_ptr<SocketImpl>> mIncompleteConnections {};

    void ConnectionEstablished(SocketImpl* socket);
    void ConnectionClosed(SocketImpl* socket);

    /* Protocol State */
    uint32_t mSequenceNumber {};
    uint32_t mAckNumber {};

    /* Stats */

    struct Stats {
        long mSocketsAccepted {};
        long mConnectionsRefused {};
    } mStats;
};

}
