#pragma once

#include <memory>
#include <sys/socket.h>

namespace Atp {

class ISocket {
public:
    virtual ~ISocket() = default;

    virtual ssize_t SendTo(const void* buf, size_t len, int flags,
        const struct sockaddr* dest_addr, socklen_t addrlen)
        = 0;

    virtual ssize_t RecvFrom(void* buf, size_t len,
        int flags, struct sockaddr* src_addr, socklen_t* addrlen)
        = 0;

    virtual int Dup2(const ISocket& oldSocket) = 0;

    virtual int GetFd() const = 0;
};

class ISocketFactory {
public:
    virtual ~ISocketFactory() = default;

    virtual std::unique_ptr<ISocket> Socket(int domain, int type, int protocol) = 0;

    virtual std::pair<std::unique_ptr<ISocket>, std::unique_ptr<ISocket>>
    SocketPair(int domain, int type, int protocol) = 0;
};

class PosixSocket final : public ISocket {
public:
    PosixSocket(int fd);
    ~PosixSocket() override;

    ssize_t SendTo(const void* buf, size_t len, int flags,
        const struct sockaddr* dest_addr, socklen_t addrlen) override;

    ssize_t RecvFrom(void* buf, size_t len,
        int flags, struct sockaddr* src_addr, socklen_t* addrlen) override;

    int Dup2(const ISocket& oldSocket) override;

    int GetFd() const override;


private:
    int mFd;
};

class PosixSocketFactory final : public ISocketFactory {
public:
    std::unique_ptr<ISocket> Socket(int domain, int type, int protocol) override;

    std::pair<std::unique_ptr<ISocket>, std::unique_ptr<ISocket>>
    SocketPair(int domain, int type, int protocol) override;
};

}
