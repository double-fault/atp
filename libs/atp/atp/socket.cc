#include "socket.h"
#include "common.h"
#include "context.h"
#include "eventcore.h"
#include "nat_resolver.h"
#include "protocol.h"
#include "signalling.h"
#include "types.h"

#include <expected>
#include <fmt/format.h>
#include <memory>
#include <netinet/in.h>
#include <plog/Log.h>
#include <strings.h>
#include <sys/epoll.h>
#include <sys/socket.h>

namespace Atp {

Result<std::unique_ptr<AtpSocket>> AtpSocket::Create(
    IEventCore* eventCore,
    ISignallingProvider* signallingProvider,
    INatResolver* natResolver,
    ISocketFactory* socketFactory)
{
    Error returnCode = Error::UNKNOWN;

    /* Members are initialized in the same order as their declarations in the header */

    // Yes, using the new operator with a unique_ptr
    // But since we'd like to keep the constructor private and this is a factory
    // function, its fine
    std::unique_ptr<AtpSocket> newsock { new AtpSocket() };
    newsock->mState = State::CLOSED;
    newsock->mEventCore = eventCore;
    newsock->mNatResolver = natResolver;
    newsock->mSocketFactory = socketFactory;

    newsock->mApplicationSocket = socketFactory->Socket(AF_UNIX, SOCK_STREAM, 0);

    newsock->mAtpSocket = nullptr;
    newsock->mNetworkSocket = socketFactory->Socket(AF_INET, SOCK_DGRAM, 0);

    newsock->mDemux = std::make_shared<Demux>(newsock->mEventCore, newsock->mNetworkFd);

    newsock->mStunClient = std::make_shared<Stun::Client>(networkFd);

    // Lets run queryall twice to be a bit extra sure about NAT type
    for (int i = 0; i < 2; i++) {
        if (newsock->mStunClient->QueryAllServers() == -1) {
            returnCode = Error::NATQUERYFAILURE;
            goto clean;
        }
    }

    if (newsock->mStunClient->GetNatType() == Stun::NatType::kDependent) {
        returnCode = Error::NATDEPENDENT;
        goto clean;
    }

    struct epoll_event epollNat;
    bzero(&epollNat, sizeof(epollNat));
    // TODO: can use epoll_event.data to maintain RTO timer between calls?
    epollNat.events = 0;

    if ((newsock->mNatKeepAliveCallback = newsock->mEventCore->RegisterCallback(-1,
             &epollNat, EventCore::kInvokeImmediately | EventCore::kSuspend,
             [&](epoll_data_t data) -> mseconds_t {
                 return newsock->NatKeepAliveCallback(data);
             }))
        == 0) {
        returnCode = Error::EVENTCORE;
        goto clean;
    }

    newsock->mPunchThroughCallback = 0;
    newsock->mNetworkRecvCallback = 0;
    newsock->mApplicationRecvCallback = 0;

    bzero(&newsock->mPeerAddressIn, sizeof(newsock->mPeerAddressIn));

    newsock->mSignallingProvider = signallingProvider;
    if ((newsock->mSignallingSocket = newsock->mSignallingProvider->Socket()) < 0) {
        returnCode = Error::SIGNALLINGPROVIDER;
        goto clean;
    }
    newsock->mSignallingAddress = nullptr;
    newsock->mSignallingRecvCallback = 0;
    // newsock->mWildcardRecvCallback = 0;

    newsock->mBacklog = 0;

    THROW_IF(newsock->mEventCore->ResumeCallback(newsock->mNatKeepAliveCallback) != 0);

    return std::move(newsock);

clean:
    if (newsock->mNatKeepAliveCallback)
        newsock->mEventCore->DeleteCallback(newsock->mNatKeepAliveCallback);

    if (newsock->mApplicationRecvCallback)
        newsock->mEventCore->DeleteCallback(newsock->mApplicationRecvCallback);

    if (newsock->mSignallingRecvCallback)
        newsock->mEventCore->DeleteCallback(newsock->mSignallingRecvCallback);

    if (newsock->mPunchThroughCallback)
        newsock->mEventCore->DeleteCallback(newsock->mPunchThroughCallback);

    close(newsock->mApplicationFd);
    close(newsock->mAtpFd);
    close(newsock->mNetworkFd);
    return std::unexpected(returnCode);
}

Result<std::unique_ptr<AtpSocket>> AtpSocket::CloneForConnection(
    const struct sockaddr_atp* peerAddressAtp,
    const struct sockaddr_in* peerAddressIn)
{
    Error returnCode = Error::SUCCESS;

    std::unique_ptr<AtpSocket> newsock { new AtpSocket() };
    newsock->mState = State::PUNCH;
    newsock->mEventCore = mEventCore;
    newsock->mNatResolver = mNatResolver;
    newsock->mSocketFactory = mSocketFactory;

    newsock->mApplicationFd = -1;
    newsock->mAtpFd = -1;
    newsock->mNetworkFd = mNetworkFd;

    newsock->mDemux = mDemux;

    newsock->mStunClient = mStunClient;

    struct epoll_event epollNetwork;
    bzero(&epollNetwork, sizeof(epollNetwork));
    epollNetwork.events = 0;

    // Even though "child" sockets use the same UDP socket as the listen()ing "parent",
    // it is possible for the listening socket to be closed before a connection gets
    // established, hence the usually redundant keepalive in the "child" socket as well.
    if ((newsock->mNatKeepAliveCallback = newsock->mEventCore->RegisterCallback(-1,
             &epollNetwork, EventCore::kInvokeImmediately | EventCore::kSuspend,
             [&](epoll_data_t data) -> mseconds_t {
                 return newsock->NatKeepAliveCallback(data);
             }))
        == 0) {
        returnCode = Error::EVENTCORE;
        goto clean;
    }

    newsock->mPassiveOwner = this;

    struct epoll_event epollPunch;
    bzero(&epollPunch, sizeof(epollPunch));
    epollPunch.events = 0;
    if ((newsock->mPunchThroughCallback = newsock->mEventCore->RegisterCallback(-1,
             &epollPunch, EventCore::kInvokeImmediately | EventCore::kSuspend,
             [&](epoll_data_t data) -> mseconds_t {
                 return newsock->PunchThroughCallback(data);
             }))
        == 0) {
        returnCode = Error::EVENTCORE;
        goto clean;
    }

    if ((newsock->mNetworkRecvCallback = newsock->mDemux->RegisterCallback(peerAddressIn,
             [&](const void* buffer, size_t length) -> void {
                 NetworkRecvCallback(buffer, length);
             }))
        == 0) {
        returnCode = Error::DEMUX;
        goto clean;
    }

    newsock->mApplicationRecvCallback = 0;

    newsock->mPeerAddressAtp = *peerAddressAtp;
    newsock->mPeerAddressIn = *peerAddressIn;

    newsock->mSignallingRecvCallback = 0;
    newsock->mSignallingProvider = nullptr;

    newsock->mSequenceNumber = std::rand() % UINT32_MAX;

    THROW_IF(newsock->mEventCore->ResumeCallback(newsock->mNatKeepAliveCallback) != 0);
    THROW_IF(newsock->mEventCore->ResumeCallback(newsock->mPunchThroughCallback) != 0);

    return std::move(newsock);

clean:
    // BUG: Shouldn't I set the callback_ident_t vars to zero after deleting the callback?
    if (newsock->mNatKeepAliveCallback)
        newsock->mEventCore->DeleteCallback(newsock->mNatKeepAliveCallback);
    if (newsock->mPunchThroughCallback)
        newsock->mEventCore->DeleteCallback(newsock->mPunchThroughCallback);
    if (newsock->mNetworkRecvCallback)
        newsock->mDemux->DeleteCallback(newsock->mNetworkRecvCallback);

    return std::unexpected(returnCode);
}

// BUG: Locking required here, modifying data common to both threads
Result<AtpSocket*> AtpSocket::Accept(Context* context)
{
    if (mState != State::LISTEN)
        return std::unexpected(Error::INVAL);
    if (mCompletedConnections.empty())
        return std::unexpected(Error::WOULDBLOCK);

    std::unique_ptr<AtpSocket> socket = std::move(mCompletedConnections.front());
    mCompletedConnections.pop();

    // BUG: Isn't there a race condition here?
    // Context takes ownership of the ptr -> say the socket gets closed
    // and gets destroyed before the return -> the returned pointer is dangling
    // Possible solution: Hold master lock from Context before calling SocketImpl::Accept
    AtpSocket* sockptr = socket.get();
    context->TakeOwnership(std::move(socket));

    return sockptr;
}

Error AtpSocket::Connect(const struct sockaddr_atp* addr)
{
    if (mSignallingRecvCallback != 0)
        return Error::ALREADYSET;

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
        return Error::EVENTCORE;
    }

    struct sockaddr_in reflexiveAddress;
    socklen_t reflexiveLength = sizeof(reflexiveAddress);
    THROW_IF(mStunClient->GetReflexiveAddress((struct sockaddr*)&reflexiveAddress,
                 &reflexiveLength)
        != 0);
    THROW_IF(reflexiveAddress.sin_family != AF_INET);

    struct signal request;
    bzero(&request, sizeof(request));
    request.magic = kSignalMagic;
    request.request = 1;
    request.addr_family = AF_INET;
    request.addr_port = reflexiveAddress.sin_port;
    request.addr_ipv4 = reflexiveAddress.sin_addr.s_addr;

    size_t bufferLength;
    const void* buffer = BuildSignal(&request, &bufferLength);
    if (mSignallingProvider->Send(mSignallingSocket, buffer, bufferLength,
            addr)
        < 0) {
        returnCode = Error::SIGNALLINGPROVIDER;
        goto clean;
    }

    return Error::SUCCESS;

clean:
    if (mSignallingRecvCallback)
        mEventCore->DeleteCallback(mSignallingRecvCallback);
    return returnCode;
}

AtpSocket::~AtpSocket()
{
    close(mApplicationFd);
    close(mAtpFd);
    close(mNetworkFd);

    if (mNatKeepAliveCallback)
        mEventCore->DeleteCallback(mNatKeepAliveCallback);
    if (mPunchThroughCallback)
        mEventCore->DeleteCallback(mPunchThroughCallback);
    if (mNetworkRecvCallback)
        mDemux->DeleteCallback(mNetworkRecvCallback);
    if (mApplicationRecvCallback)
        mEventCore->DeleteCallback(mApplicationRecvCallback);
    if (mSignallingRecvCallback)
        mEventCore->DeleteCallback(mSignallingRecvCallback);
    // if (mWildcardRecvCallback)
    //    mEventCore->DeleteCallback(mWildcardRecvCallback);
}

Error AtpSocket::Bind(const struct sockaddr_atp* addr)
{
    if (mSignallingAddress != nullptr)
        return Error::ALREADYSET;

    if (!mSignallingProvider->Bind(mSignallingSocket, addr))
        return Error::SIGNALLINGPROVIDER;
    mSignallingAddress = std::make_unique<struct sockaddr_atp>(*addr);

    return Error::SUCCESS;
}

Error AtpSocket::Listen(int backlog)
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

    /*
    if ((mWildcardRecvCallback = mDemux->RegisterWildcardCallback(
             [&](const void* buffer, size_t length) -> void {
                 WildcardRecvCallback(buffer, length);
             }))
        == 0) {
        returnCode = Error::DEMUX;
        goto clean;
    }*/

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

    // if (mWildcardRecvCallback)
    //    mDemux->DeleteCallback(mWildcardRecvCallback);

    return returnCode;
}

void AtpSocket::SetupSocketpair(AtpSocket* socket)
{
    int fds[2];
    THROW_IF(socketpair(AF_UNIX, SOCK_STREAM, 0, fds) != 0);

    if (socket->mApplicationFd == -1) {
        socket->mApplicationFd = fds[0];
        socket->mAtpFd = fds[1];
    } else {
        // BUG: If errno is EINTR we should retry, not throw, but this would be pretty rare
        THROW_IF(dup2(fds[0], socket->mApplicationFd) < 0);
        close(fds[0]);
        socket->mAtpFd = fds[1];
    }

    struct epoll_event epollApplication;
    bzero(&epollApplication, sizeof(epollApplication));
    epollApplication.events = EPOLLIN;
    // Not needed, when even is epoll_data_t needed ffs?
    // epollApplication.data.fd = socket->mAtpFd;

    THROW_IF((socket->mApplicationRecvCallback = socket->mEventCore->RegisterCallback(
                  socket->mAtpFd, &epollApplication, 0,
                  [&](epoll_data_t data) -> mseconds_t {
                      return socket->ApplicationRecvCallback(data);
                  }))
        != 0);
    //   return Error::EVENTCORE;
}

mseconds_t AtpSocket::SignallingRecvCallback(epoll_data_t data)
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

    if (sig.request)
        SignallingRecvRequest(&sig, &peerAddressAtp);
    else if (sig.response)
        SignallingRecvResponse(&sig, &peerAddressAtp);
    else
        PLOG_ERROR << "Corrupted signal received";

    return -1;
}

void AtpSocket::SignallingRecvRequest(const struct signal* request,
    const struct sockaddr_atp* source)
{
    if (mState != State::LISTEN) {
        PLOG_ERROR << "Active socket receiving signalling request, ignoring";
        return;
    }

    struct sockaddr_in peerAddressIn;
    bzero(&peerAddressIn, sizeof(peerAddressIn));
    peerAddressIn.sin_family = request->addr_family;
    peerAddressIn.sin_port = request->addr_port;
    peerAddressIn.sin_addr.s_addr = request->addr_ipv4;

    if (mCompletedConnections.size() + mIncompleteConnections.size() > mBacklog) {
        PLOG_WARNING << fmt::format("Cannot accept more connections, backlog={}",
            mBacklog);
        return;
    }

    Result<std::unique_ptr<AtpSocket>> newsock = CloneForConnection(source,
        &peerAddressIn);
    if (!newsock) {
        PLOG_ERROR << Strerror(newsock.error());
        return;
    }

    mIncompleteConnections.push_back(std::move(*newsock));

    // Send a signal back to the client with our public ip, port
    struct signal response;
    bzero(&response, sizeof(response));

    struct sockaddr_in reflexiveAddress;
    socklen_t reflexiveLength = sizeof(struct sockaddr_in);
    THROW_IF(mStunClient->GetReflexiveAddress((struct sockaddr*)&reflexiveAddress,
                 &reflexiveLength)
        != 0);
    THROW_IF(reflexiveAddress.sin_family != AF_INET);

    response.magic = kSignalMagic;
    response.response = 1;
    response.addr_family = AF_INET;
    response.addr_port = reflexiveAddress.sin_port;
    response.addr_ipv4 = reflexiveAddress.sin_addr.s_addr;

    size_t sendbufLength;
    const void* sendbuf = BuildSignal(&response, &sendbufLength);
    THROW_IF(sendbuf == nullptr);

    if (mSignallingProvider->Send(mSignallingSocket, sendbuf, sendbufLength,
            source)
        != 0) {
        PLOG_ERROR << "Failed to sending signalling response";
        return;
    }
    return;
}

void AtpSocket::SignallingRecvResponse(const struct signal* response,
    const struct sockaddr_atp* source)
{
    if (mState != State::CLOSED) {
        PLOG_WARNING << "Received signalling response when state is not CLOSED, ignoring";
        return;
    }

    mPeerAddressAtp = *source;

    THROW_IF(response->addr_family != AF_INET);
    mPeerAddressIn.sin_family = response->addr_family;
    mPeerAddressIn.sin_port = response->addr_port;
    mPeerAddressIn.sin_addr.s_addr = response->addr_ipv4;

    struct epoll_event epollPunch;
    bzero(&epollPunch, sizeof(epollPunch));
    epollPunch.events = 0;
    if ((mPunchThroughCallback = mEventCore->RegisterCallback(-1,
             &epollPunch, EventCore::kInvokeImmediately | EventCore::kSuspend,
             [&](epoll_data_t data) -> mseconds_t {
                 return PunchThroughCallback(data);
             }))
        == 0) {
        goto clean;
    }

    if ((mNetworkRecvCallback = mDemux->RegisterCallback(&mPeerAddressIn,
             [&](const void* buffer, size_t length) -> void {
                 NetworkRecvCallback(buffer, length);
             }))
        == 0) {
        goto clean;
    }

    mState = State::PUNCH;

    THROW_IF(mEventCore->ResumeCallback(mPunchThroughCallback) != 0);
    return;

clean:
    if (mPunchThroughCallback)
        mEventCore->DeleteCallback(mPunchThroughCallback);
    if (mNetworkRecvCallback)
        mDemux->DeleteCallback(mNetworkRecvCallback);
}

void AtpSocket::SendControlDatagram(union atp_control control)
{
    struct atp_hdr header;
    bzero(&header, sizeof(header));
    header.seq_num = mSequenceNumber;
    header.ack_num = mAckNumber;
    header.c = control;
    header.magic = kAtpMagic;
    header.window = Config::kConstantWindow;

    size_t datagramLength;
    const void* datagram = BuildDatagram(&header, nullptr, 0, &datagramLength);
    THROW_IF(datagram == nullptr);
    SendDatagram(datagram, datagramLength);
}

mseconds_t AtpSocket::PunchThroughCallback(epoll_data_t data)
{
    if (mState != State::PUNCH || mState != State::THRU)
        return -1;

    if (++mPunchPacketCounter > (Config::kPunchTimeout / Config::kPunchInterval)) {
        // too many tries already, failed to establish connection
        mState = State::CLOSED;
        if (mPassiveOwner)
            mPassiveOwner->ConnectionClosed(this);
        return -1;
    }

    union atp_control control {};
    if (mState == State::PUNCH)
        control.punch = 1;
    else if (mState == State::THRU)
        control.thru = 1;

    SendControlDatagram(control);
    return Config::kPunchInterval;
}

void AtpSocket::NetworkRecvCallback(const void* buffer, size_t length)
{
    if (!IsAtpDatagram(buffer, length)) {
        PLOG_WARNING << "Received datagram which is not an ATP message!";
        return;
    }

    struct atp_hdr header;
    char payload[kAtpPayloadMaxLimit];
    size_t payloadSize = sizeof(payload);
    if (-1 == ReadDatagram(buffer, length, &header, payload, &payloadSize)) {
        PLOG_WARNING << "Failed to parse ATP datagram";
        return;
    }

    switch (mState) {
    case State::PUNCH:
        NetworkRecvPunch(&header, payload, payloadSize);
        break;
    case State::THRU:
        NetworkRecvThru(&header, payload, payloadSize);
        break;
    case State::ESTABLISHED:
        NetworkRecvEstablished(&header, payload, payloadSize);
        break;
    default:
        PLOG_INFO << "Received datagram with no handler for current state";
        break;
    }
}

void AtpSocket::NetworkRecvPunch(const struct atp_hdr* header,
    const void* payload, size_t length)
{
    if (header->c.punch) {
        mState = State::THRU;
    } else if (header->c.thru) {
        // In this case the socket never entered the THRU state
        // However, the socket will still transmit a THRU packet
        // for every THRU it receives, even in the established state
        mState = State::ESTABLISHED;
        SetupSocketpair(this);
        if (mPassiveOwner)
            mPassiveOwner->ConnectionEstablished(this);
    } else {
        PLOG_WARNING << fmt::format("Received a non punch/thru packet while in State::PUNCH,"
                                    "header.control={}",
            header->c.control);
        return;
    }
}

void AtpSocket::NetworkRecvThru(const struct atp_hdr* header,
    const void* payload, size_t length)
{
    if (header->c.punch) {
        ; // No change
    } else if (header->c.thru || header->c.data) {
        // It is possible for the peer to be in an established state while we are still
        // in State::THRU.
        // In such a scenario we simply ignore the payload and let it get re-transmitted.
        mState = State::ESTABLISHED;
        SetupSocketpair(this);
        if (mPassiveOwner)
            mPassiveOwner->ConnectionEstablished(this);
    } else {
        PLOG_WARNING << fmt::format("Received unhandled packet type while in State::THRU,"
                                    "header.control={}",
            header->c.control);
        return;
    }
}

void AtpSocket::NetworkRecvEstablished(const struct atp_hdr* header,
    const void* payload, size_t length)
{
    if (header->c.punch) {
        PLOG_WARNING << "Received PUNCH packet while in State::ESTABLISHED";
        return;
    } else if (header->c.thru) {
        SendControlDatagram(atp_control { .thru = 1 });
        return;
    }
}

void AtpSocket::ConnectionEstablished(AtpSocket* socket)
{
    auto it = mIncompleteConnections.begin();
    while (it != mIncompleteConnections.end() && it->get() != socket)
        it++;

    if (it != mIncompleteConnections.end()) {
        mCompletedConnections.push(std::move(*it));
        mIncompleteConnections.erase(it);
    }
}

void AtpSocket::ConnectionClosed(AtpSocket* socket)
{
    auto it = mIncompleteConnections.begin();
    while (it != mIncompleteConnections.end() && it->get() != socket)
        it++;

    if (it != mIncompleteConnections.end())
        mIncompleteConnections.erase(it);
}

}
