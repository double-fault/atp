#include "socket.h"
#include "common.h"
#include "eventcore.h"
#include "isignalling.h"
#include "types.h"

#include <memory>
#include <strings.h>

namespace Atp {

std::expected<SocketImpl, Error> SocketImpl::Create(EventCore* eventCore,
    ISignallingProvider* signallingProvider)
{
    Error returnCode = Error::UNKNOWN;

    errno = 0;
    int networkFd = socket(AF_INET, SOCK_DGRAM, 0);
    if (networkFd == -1)
        return std::unexpected(ErrnoToErrorCode(errno));

    int fds[2];
    errno = 0;
    int ret = socketpair(AF_UNIX, SOCK_STREAM, 0, fds);
    if (ret == -1) {
        close(networkFd);

        // TODO: Add socketpair() errno vals to Error enum class
        return std::unexpected(ErrnoToErrorCode(errno));
    }

    SocketImpl socket;
    socket.mEventCore = eventCore;
    socket.mSignallingProvider = signallingProvider;

    socket.mState = State::CLOSED;

    socket.mApplicationFd = fds[0];
    socket.mAtpFd = fds[1];
    socket.mNetworkFd = networkFd;

    socket.mDemux = std::make_shared<Demux>(socket.mEventCore, socket.mNetworkFd,
        [&](void* buffer, size_t length) -> void {
            socket.NetworkRecvCallback(buffer, length);
        });

    socket.mStunClient = std::make_unique<Stun::Client>(networkFd);

    // Lets run queryall twice to be a bit extra sure about NAT type
    for (int i = 0; i < 2; i++) {
        if (socket.mStunClient->QueryAllServers() == -1) {
            returnCode = Error::NATQUERYFAILURE;
            goto clean;
        }
    }

    if (socket.mStunClient->GetNatType() == Stun::NatType::kDependent) {
        returnCode = Error::NATDEPENDENT;
        goto clean;
    }

    struct epoll_event epollNetwork;
    bzero(&epollNetwork, sizeof(epollNetwork));
    epollNetwork.data.fd = socket.mNetworkFd;
    epollNetwork.events = 0;

    if ((socket.mNatKeepAliveCallback = socket.mEventCore->RegisterCallback(-1,
             &epollNetwork, EventCore::kInvokeImmediately | EventCore::kSuspend,
             [&](epoll_data_t data) -> mseconds_t {
                 return socket.NatKeepAliveCallback(data);
             }))
        == -1) {
        returnCode = Error::EVENTCORE;
        goto clean;
    }

    struct epoll_event epollApplication;
    bzero(&epollApplication, sizeof(epollApplication));
    epollApplication.data.fd = socket.mAtpFd;
    epollApplication.events = EPOLLIN;
    if ((socket.mApplicationRecvCallback = socket.mEventCore->RegisterCallback(socket.mAtpFd,
             &epollApplication, EventCore::kSuspend,
             [&](epoll_data_t data) -> mseconds_t {
                 return socket.ApplicationRecvCallback(data);
             }))
        == -1) {
        returnCode = Error::EVENTCORE;
        goto clean;
    }

    if ((socket.mSignallingSocket = socket.mSignallingProvider->Socket()) < 0) {
        returnCode = Error::SIGNALLINGPROVIDER;
        goto clean;
    }
    socket.mSignallingRecvCallback = 0;

    THROW_IF(socket.mEventCore->ResumeCallback(socket.mNatKeepAliveCallback) != 0);
    THROW_IF(socket.mEventCore->ResumeCallback(socket.mApplicationRecvCallback) != 0);

    return std::move(socket);

clean:
    if (socket.mNatKeepAliveCallback)
        socket.mEventCore->DeleteCallback(socket.mNatKeepAliveCallback);

    if (socket.mApplicationRecvCallback)
        socket.mEventCore->DeleteCallback(socket.mApplicationRecvCallback);

    if (socket.mSignallingRecvCallback)
        socket.mEventCore->DeleteCallback(socket.mSignallingRecvCallback);

    close(socket.mApplicationFd);
    close(socket.mAtpFd);
    close(socket.mNetworkFd);
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
        < 0) {
        returnCode = Error::EVENTCORE;
        goto clean;
    }

    mBacklog = backlog;

    // Lets flush any pending messages
    while (mSignallingProvider->RecvResponse(mSignallingSocket, nullptr, nullptr) >= 0)
        ;

    mState = State::LISTEN;

    THROW_IF(mEventCore->ResumeCallback(mSignallingRecvCallback) < 0);
    
    return Error::SUCCESS;

clean:
    if (mSignallingRecvCallback)
        mEventCore->DeleteCallback(mSignallingRecvCallback);

    return returnCode;
}

}
