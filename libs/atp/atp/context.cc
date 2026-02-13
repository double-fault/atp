#include "context.h"
#include "common.h"
#include "eventcore.h"
#include "signalling.h"
#include "socket.h"
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

    std::expected<AtpSocket, Error> socket = AtpSocket::Create(&mEventCore, mSignallingProvider);
    if (!socket) 
        return +socket.error();

    int fd = socket->GetApplicationFd();
    mApplicationFds.insert(fd);
    mSockets[fd] = std::move(*socket);

    return fd;
}

int Context::Bind(int appfd, const struct sockaddr_atp* addr)
{
    if (!mApplicationFds.contains(appfd))
        return +Error::BADFD;

    Error ret = mSockets[appfd].Bind(addr);

    return +ret;
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
