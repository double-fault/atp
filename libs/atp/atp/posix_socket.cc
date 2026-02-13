#include "posix_socket.h"
#include "common.h"
#include <memory>
#include <sys/socket.h>

namespace Atp {

PosixSocket::PosixSocket(int fd)
    : mFd { fd }
{
}

PosixSocket::~PosixSocket()
{
    close(mFd);
}

ssize_t PosixSocket::SendTo(const void* buf, size_t len, int flags,
    const struct sockaddr* dest_addr, socklen_t addrlen)
{
    return ::sendto(mFd, buf, len, flags, dest_addr, addrlen);
}

ssize_t PosixSocket::RecvFrom(void* buf, size_t len,
    int flags, struct sockaddr* src_addr, socklen_t* addrlen)
{
    return ::recvfrom(mFd, buf, len, flags, src_addr, addrlen);
}

int PosixSocket::Dup2(const ISocket& oldSocket)
{
    return ::dup2(oldSocket.GetFd(), mFd);
}

int PosixSocket::GetFd() const
{
    return mFd;
}

std::unique_ptr<ISocket> PosixSocketFactory::Socket(int domain, int type, int protocol)
{
    int fd = ::socket(domain, type, protocol);
    THROW_IF(fd < 0);

    return std::make_unique<PosixSocket>(fd);
}

std::pair<std::unique_ptr<ISocket>, std::unique_ptr<ISocket>>
PosixSocketFactory::SocketPair(int domain, int type, int protocol)
{
    int fds[2];
    THROW_IF(socketpair(domain, type, protocol, fds) != 0);
    std::unique_ptr<ISocket> sock1 = std::make_unique<PosixSocket>(fds[0]);
    std::unique_ptr<ISocket> sock2 = std::make_unique<PosixSocket>(fds[1]);

    return std::make_pair(std::move(sock1), std::move(sock2));
}

}
