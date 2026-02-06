#include "socket.h"
#include "common.h"
#include "eventcore.h"
#include "protocol.h"
#include "signalling.h"
#include "types.h"

#include <memory>
#include <strings.h>
#include <sys/epoll.h>

namespace Atp {

std::expected<SocketImpl, Error> SocketImpl::Create(EventCore* eventCore,
    ISignallingProvider* signallingProvider)
{
    Error returnCode = Error::UNKNOWN;

    errno = 0;
    int networkFd = socket(AF_INET, SOCK_DGRAM, 0);
    if (networkFd == -1)
        return std::unexpected(ErrnoToErrorCode(errno));

    /* Members are initialized in the same order as their declarations in the header */

    SocketImpl newsock;
    newsock.mState = State::CLOSED;
    newsock.mEventCore = eventCore;

    errno = 0;
    newsock.mApplicationFd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (newsock.mApplicationFd == -1) {
        close(networkFd);
        return std::unexpected(ErrnoToErrorCode(errno));
    }

    newsock.mAtpFd = 0;
    newsock.mNetworkFd = networkFd;

    newsock.mDemux = std::make_shared<Demux>(newsock.mEventCore, newsock.mNetworkFd);

    newsock.mStunClient = std::make_shared<Stun::Client>(networkFd);

    // Lets run queryall twice to be a bit extra sure about NAT type
    for (int i = 0; i < 2; i++) {
        if (newsock.mStunClient->QueryAllServers() == -1) {
            returnCode = Error::NATQUERYFAILURE;
            goto clean;
        }
    }

    if (newsock.mStunClient->GetNatType() == Stun::NatType::kDependent) {
        returnCode = Error::NATDEPENDENT;
        goto clean;
    }

    struct epoll_event epollNat;
    bzero(&epollNat, sizeof(epollNat));
    // TODO: can use epoll_event.data to maintain RTO timer between calls?
    epollNat.events = 0;

    if ((newsock.mNatKeepAliveCallback = newsock.mEventCore->RegisterCallback(-1,
             &epollNat, EventCore::kInvokeImmediately | EventCore::kSuspend,
             [&](epoll_data_t data) -> mseconds_t {
                 return newsock.NatKeepAliveCallback(data);
             }))
        == 0) {
        returnCode = Error::EVENTCORE;
        goto clean;
    }

    newsock.mPunchTransmitCallback = 0;
    newsock.mNetworkRecvCallback = 0;
    newsock.mApplicationRecvCallback = 0;

    bzero(&newsock.mPeerAddress, sizeof(newsock.mPeerAddress));

    newsock.mSignallingProvider = signallingProvider;
    if ((newsock.mSignallingSocket = newsock.mSignallingProvider->Socket()) < 0) {
        returnCode = Error::SIGNALLINGPROVIDER;
        goto clean;
    }
    newsock.mSignallingAddress = nullptr;
    newsock.mSignallingRecvCallback = 0;
    newsock.mWildcardRecvCallback = 0;

    newsock.mBacklog = 0;

    THROW_IF(newsock.mEventCore->ResumeCallback(newsock.mNatKeepAliveCallback) != 0);

    return std::move(newsock);

clean:
    if (newsock.mNatKeepAliveCallback)
        newsock.mEventCore->DeleteCallback(newsock.mNatKeepAliveCallback);

    if (newsock.mApplicationRecvCallback)
        newsock.mEventCore->DeleteCallback(newsock.mApplicationRecvCallback);

    if (newsock.mSignallingRecvCallback)
        newsock.mEventCore->DeleteCallback(newsock.mSignallingRecvCallback);

    if (newsock.mPunchTransmitCallback)
        newsock.mEventCore->DeleteCallback(newsock.mPunchTransmitCallback);

    close(newsock.mApplicationFd);
    close(newsock.mAtpFd);
    close(newsock.mNetworkFd);
    return std::unexpected(returnCode);
}

std::expected<SocketImpl, Error> SocketImpl::CloneToAccept(const struct sockaddr_in* peerAddress)
{
    Error returnCode = Error::SUCCESS;

    SocketImpl newsock;
    newsock.mState = State::PUNCH;
    newsock.mEventCore = mEventCore;

    newsock.mApplicationFd = -1;
    newsock.mAtpFd = -1;
    newsock.mNetworkFd = mNetworkFd;

    newsock.mDemux = mDemux;

    newsock.mStunClient = mStunClient;

    struct epoll_event epollNetwork;
    bzero(&epollNetwork, sizeof(epollNetwork));
    epollNetwork.events = 0;

    if ((newsock.mNatKeepAliveCallback = newsock.mEventCore->RegisterCallback(-1,
             &epollNetwork, EventCore::kInvokeImmediately | EventCore::kSuspend,
             [&](epoll_data_t data) -> mseconds_t {
                 return newsock.NatKeepAliveCallback(data);
             }))
        == 0) {
        returnCode = Error::EVENTCORE;
        goto clean;
    }

    struct epoll_event epollPunch;
    bzero(&epollPunch, sizeof(epollPunch));
    epollPunch.events = 0;

clean:
    if (newsock.mNatKeepAliveCallback)
        newsock.mEventCore->DeleteCallback(newsock.mNatKeepAliveCallback);

    return std::unexpected(returnCode);
}

Error SocketImpl::Bind(const struct sockaddr_atp* addr)
{
    if (mSignallingAddress != nullptr)
        return Error::ALREADYSET;

    if (!mSignallingProvider->Bind(mSignallingSocket, addr))
        return Error::SIGNALLINGPROVIDER;
    mSignallingAddress = std::make_unique<struct sockaddr_atp>(*addr);

    return Error::SUCCESS;
}

Error SocketImpl::Listen(int backlog)
{
    if (mState != State::CLOSED)
        return Error::ALREADYSET;
    if (mSignallingAddress == nullptr)
        return Error::NOTBOUND;
    if (backlog <= 0 || backlog > Config::kMaxBacklog)
        return Error::INVAL;
    THROW_IF(mSignallingRecvCallback != 0);

    Error returnCode = Error::UNKNOWN;

    struct epoll_event epollSignalling;
    bzero(&epollSignalling, sizeof(epollSignalling));
    epollSignalling.data.fd = mSignallingSocket;
    epollSignalling.events = EPOLLIN;
    if ((mSignallingRecvCallback = mEventCore->RegisterCallback(mSignallingSocket,
             &epollSignalling, EventCore::kSuspend,
             [&](epoll_data_t data) -> mseconds_t {
                 return SignallingRecvCallback(data);
             }))
        == 0) {
        returnCode = Error::EVENTCORE;
        goto clean;
    }

    if ((mWildcardRecvCallback = mDemux->RegisterWildcardCallback(
             [&](void* buffer, size_t length) -> void {
                 WildcardRecvCallback(buffer, length);
             }))
        == 0) {
        returnCode = Error::DEMUX;
        goto clean;
    }

    mBacklog = backlog;

    // Lets flush any pending messages
    while (mSignallingProvider->Recv(mSignallingSocket, nullptr, nullptr, nullptr) >= 0)
        ;

    mState = State::LISTEN;

    THROW_IF(mEventCore->ResumeCallback(mSignallingRecvCallback) < 0);

    return Error::SUCCESS;

clean:
    if (mSignallingRecvCallback)
        mEventCore->DeleteCallback(mSignallingRecvCallback);

    if (mWildcardRecvCallback)
        mDemux->DeleteCallback(mWildcardRecvCallback);

    return returnCode;
}

mseconds_t SocketImpl::SignallingRecvCallback(epoll_data_t data)
{
    THROW_IF(mState != State::LISTEN);

    struct sockaddr_atp peerAddressAtp;

    size_t length = 1024;
    char buffer[length];
    if (mSignallingProvider->Recv(mSignallingSocket, buffer, &length, &peerAddressAtp) < 0)
        return -1;
    THROW_IF(length > sizeof(buffer)); // Signal was truncated
    THROW_IF(!IsSignal(buffer, length));

    struct signal sig;
    THROW_IF(ReadSignal(buffer, length, &sig) < 0);
    THROW_IF(sig.request == 0);
    THROW_IF(sig.addr_family != AF_INET);

    struct sockaddr_in peerAddressIn;
    bzero(&peerAddressIn, sizeof(peerAddressIn));
    peerAddressIn.sin_family = sig.addr_family;
    peerAddressIn.sin_port = sig.addr_port;
    peerAddressIn.sin_addr.s_addr = sig.addr_ipv4;

    SocketImpl newsocket;
    // TODO: incomplete
}

mseconds_t SocketImpl::PunchTransmitCallback(epoll_data_t data)
{
}

}
