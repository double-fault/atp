#include "context.h"
#include "common.h"
#include "eventcore.h"
#include "isignalling.h"
#include "types.h"

#include <netinet/in.h>
#include <strings.h>
#include <stun/stun.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <thread>

namespace Atp {

Context::Context(ISignallingProvider* signallingProvider)
    : mSignallingProvider { signallingProvider }
    , mEventCore()
{
    mSockets.reserve(Config::kMaxSocketCount);

    mEventLoopThread = std::thread(&EventCore::Run, &mEventCore);
}

int Context::Socket(int domain, int type, int protocol)
{
    if (domain != AF_INET)
        return +Error::AFNOSUPPORT;
    if (type != SOCK_STREAM || protocol != IPPROTO_ATP)
        return +Error::PROTONOSUPPORT;
    if (mSockets.size() == Config::kMaxSocketCount)
        return +Error::MAXSOCKETS;

    int returnCode = -1;

    errno = 0;
    int networkFd = socket(AF_INET, SOCK_DGRAM, 0);
    if (networkFd == -1)
        return +ErrnoToErrorCode(errno);

    int fds[2];
    errno = 0;
    int ret = socketpair(AF_UNIX, SOCK_STREAM, 0, fds);
    if (ret == -1) {
        close(networkFd);
        return +ErrnoToErrorCode(errno); // TODO: Add socketpair() errno vals to Error enum class
    }

    SocketData data, *ptr;
    data.mState = State::CLOSED;

    data.mApplicationFd = fds[0];
    data.mAtpFd = fds[1];
    data.mNetworkFd = networkFd;
    data.mStunClient = std::make_unique<Stun::Client>(networkFd);

    // Lets run queryall twice to be a bit extra sure about NAT type
    for (int i = 0; i < 2; i++) {
        if (data.mStunClient->QueryAllServers() == -1) {
            returnCode = +Error::NATQUERYFAILURE;
            goto clean;
        }
    }

    if (data.mStunClient->GetNatType() == Stun::NatType::kDependent) {
        returnCode = +Error::NATDEPENDENT;
        goto clean;
    }

    struct epoll_event epollNetwork;
    bzero(&epollNetwork, sizeof(epollNetwork));
    epollNetwork.data.fd = data.mNetworkFd;
    epollNetwork.events = 0;

    if ((data.mNatKeepAliveCallback = mEventCore.RegisterCallback(-1,
             &epollNetwork, EventCore::kInvokeImmediately | EventCore::kSuspend,
             [&](epoll_data_t data) -> mseconds_t {
                 return NatKeepAliveCallback(data);
             }))
        == -1) {
        returnCode = +Error::EVENTCORE;
        goto clean;
    }

    epollNetwork.events = EPOLLIN;
    if ((data.mNetworkRecvCallback = mEventCore.RegisterCallback(data.mNetworkFd,
             &epollNetwork, EventCore::kSuspend,
             [&](epoll_data_t data) -> mseconds_t {
                 return NetworkRecvCallback(data);
             }))
        == -1) {
        returnCode = +Error::EVENTCORE;
        goto clean;
    }

    struct epoll_event epollApplication;
    bzero(&epollApplication, sizeof(epollApplication));
    epollApplication.data.fd = data.mAtpFd;
    epollApplication.events = EPOLLIN;
    if ((data.mApplicationRecvCallback = mEventCore.RegisterCallback(data.mAtpFd,
             &epollApplication, EventCore::kSuspend,
             [&](epoll_data_t data) -> mseconds_t {
                 return ApplicationRecvCallback(data);
             }))
        == -1) {
        returnCode = +Error::EVENTCORE;
        goto clean;
    }

    if ((data.mSignallingSocket = mSignallingProvider->Socket()) < 0) {
        returnCode = +Error::SIGNALLINGPROVIDER;
        goto clean;
    }
    data.mSignallingRecvCallback = 0;

    mApplicationFds.insert(data.mApplicationFd);
    mSockets.push_back(std::move(data));
    ptr = &mSockets.back();
    // Careful not to use data after std::move'ing it
    mFdToSocket[fds[0]] = ptr;
    mFdToSocket[fds[1]] = ptr;
    mFdToSocket[networkFd] = ptr;

    THROW_IF(mEventCore.ResumeCallback(ptr->mNatKeepAliveCallback) != 0);
    THROW_IF(mEventCore.ResumeCallback(ptr->mNetworkRecvCallback) != 0);
    THROW_IF(mEventCore.ResumeCallback(ptr->mApplicationRecvCallback) != 0);

    return ptr->mApplicationFd;

clean:
    if (data.mNatKeepAliveCallback)
        mEventCore.DeleteCallback(data.mNatKeepAliveCallback);

    if (data.mNetworkRecvCallback)
        mEventCore.DeleteCallback(data.mNetworkRecvCallback);

    if (data.mApplicationRecvCallback)
        mEventCore.DeleteCallback(data.mApplicationRecvCallback);

    if (data.mSignallingRecvCallback)
        mEventCore.DeleteCallback(data.mSignallingRecvCallback);

    close(data.mApplicationFd);
    close(data.mAtpFd);
    close(data.mNetworkFd);
    return returnCode;
}

int Context::Bind(int appfd, const struct sockaddr_atp* addr)
{
    if (!mApplicationFds.contains(appfd))
        return +Error::BADFD;

    SocketData* data = mFdToSocket[appfd];
    if (data->mSignallingAddress != nullptr)
        return +Error::ALREADYSET;

    if (!mSignallingProvider->Bind(data->mSignallingSocket, addr))
        return +Error::SIGNALLINGPROVIDER;
    data->mSignallingAddress = std::make_unique<struct sockaddr_atp>(*addr);

    return 0;
}

int Context::Listen(int appfd, int backlog)
{
    if (!mApplicationFds.contains(appfd))
        return +Error::BADFD;
    
    SocketData* data = mFdToSocket[appfd];
    if (data->mSignallingAddress == nullptr)
        return +Error::NOTBOUND;
}

}
