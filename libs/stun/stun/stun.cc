#include "stun.h"

#include <cerrno>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <ctime>
#include <fmt/format.h>
#include <iostream>
#include <netdb.h>
#include <netinet/in.h>
#include <plog/Log.h>
#include <poll.h>
#include <string.h>
#include <sys/socket.h>

namespace Stun {

bool IsStunMessage(const void* buffer, size_t length)
{
    if (length < sizeof(Header))
        return false;

    const char* cptr = (const char*)buffer;
    if (cptr[0] & 0xC0)
        return false;

    Header header;
    Header* hptr = (Header*)buffer;
    header.mMessageType = ntohs(hptr->mMessageType);
    header.mMessageLength = ntohs(hptr->mMessageLength);
    header.mMagicCookie = ntohl(hptr->mMagicCookie);

    if (header.mMagicCookie != kHeader::MagicCookie)
        return false;
    if (header.mMessageLength & 0x3 || header.mMessageLength != length - sizeof(Header))
        return false;

    return true;
}

MessageBuilder::MessageBuilder(uint16_t messageType)
{
    mMessage = malloc(sizeof(Header));

    Header* header = reinterpret_cast<Header*>(mMessage);
    header->mMessageType = htons(messageType);
    header->mMessageLength = htons(0);
    header->mMagicCookie = htonl(kHeader::MagicCookie);

    std::srand(std::time({}));
    for (int i = 0; i < 12; i++) {
        // big endian = little endian for uint8_t
        header->mTransactionId.mId[i] = std::rand() % (UINT8_MAX + 1);
    }

    mLength = sizeof(Header);
}

MessageBuilder::~MessageBuilder()
{
    free(mMessage);
}

const void* MessageBuilder::GetMessage() const
{
    return mMessage;
}

size_t MessageBuilder::GetMessageSize() const
{
    return mLength;
}

TransactionId MessageBuilder::GetTransactionId() const
{
    Header* header = (Header*)mMessage;
    TransactionId id;
    memcpy(&id, &header->mTransactionId, sizeof(id));
    return header->mTransactionId;
}

MessageReader::MessageReader(const void* buffer, size_t length)
    : mBuffer { buffer }
    , mBufferLength { length }
    , mCurrent { static_cast<const char*>(buffer) }
    , mCurrentLength { length }
{
    THROW_IF(mCurrentLength < sizeof(Header));
    mCurrent += sizeof(Header);
    mCurrentLength -= sizeof(Header);
}

const Header* MessageReader::GetHeader() const
{
    return static_cast<const Header*>(mBuffer);
}

const Attribute* MessageReader::Next()
{
    if (mCurrentLength == 0)
        return nullptr;

    THROW_IF(mCurrentLength < 2 * sizeof(uint16_t));

    Attribute* ptr = (Attribute*)mCurrent;
    mCurrentAttribute.mType = ntohs(ptr->mType);
    mCurrentAttribute.mLength = ntohs(ptr->mLength);
    mCurrentAttribute.mValue = (uint8_t*)(mCurrent + offsetof(Attribute, mValue));

    size_t totalAttributeLength = mCurrentAttribute.mLength;
    totalAttributeLength += (totalAttributeLength) % 4; // Length field does not include padding
    totalAttributeLength += sizeof(uint16_t) + sizeof(uint16_t); // mType, mLength

    THROW_IF(mCurrentLength < totalAttributeLength);
    mCurrent += totalAttributeLength;
    mCurrentLength -= totalAttributeLength;

    return &mCurrentAttribute;
}

int MessageReader::ParseXorMappedAddress(const Attribute* attribute,
    struct sockaddr* address, socklen_t* addressLength)
{
    if (attribute->mType != kAttribute::Required::XorMappedAddress)
        return -1;

    THROW_IF(attribute->mLength < sizeof(MappedAddressIPv4));
    THROW_IF(attribute->mValue == nullptr);
    MappedAddressIPv4* ptr = reinterpret_cast<MappedAddressIPv4*>(attribute->mValue);

    if (ptr->mZero != 0)
        return -1;
    // Currently only handling IPv4 everywhere
    if (ptr->mFamily != kAttribute::MappedAddress::IPv4)
        return -1;

    uint16_t port = ntohs(ptr->mPort) ^ (kHeader::MagicCookie >> 16);
    in_addr_t addr = ntohl(ptr->mAddr) ^ kHeader::MagicCookie;

    if (*addressLength < sizeof(struct sockaddr_in))
        return -1;
    *addressLength = sizeof(struct sockaddr_in);

    struct sockaddr_in* addressPtr = reinterpret_cast<struct sockaddr_in*>(address);
    bzero(addressPtr, sizeof(struct sockaddr_in));
    addressPtr->sin_family = AF_INET;
    addressPtr->sin_port = htons(port);
    addressPtr->sin_addr.s_addr = htonl(addr);

    return 0;
}

Client::Client(int sockfd)
    : Client(sockfd, kDefaultServers, kDefaultTimeout)
{
}

Client::Client(int sockfd, Timeout timeout)
    : Client(sockfd, kDefaultServers, timeout)
{
}

Client::Client(int sockfd, const std::vector<Endpoint>& servers, Timeout timeout)
    : mSockfd { sockfd }
    , mServers { servers }
    , mNatType { NatType::kUnknown }
    , mTimeout { timeout }
{
    std::srand(std::time({}));

    bzero(&mReflexiveAddress, sizeof(mReflexiveAddress));
    // To represent an empty sockaddr_in structure
    mReflexiveAddress.sin_addr.s_addr = htonl(INADDR_ANY);

    // RFC 5389 7.2.1
    mStunTtlMs = 0;
    uint64_t rto = mTimeout.mTimeoutMs;
    for (int i = 0; i < mTimeout.mMaxRetransmissions; i++) {
        mStunTtlMs += rto;
        rto *= 2;
    }
    mStunTtlMs += mTimeout.mTimeoutMs * mTimeout.mFinalTimeoutMultiplier;
}

int Client::QueryAllServers()
{
    if (mNatType == NatType::kDependent) {
        PLOG_WARNING << "Nat type is Dependent, skipping Stun::Client::QueryAllServers";
        return -1;
    }

    std::vector<struct sockaddr*> servers;
    std::vector<struct addrinfo*> addrinfos;

    struct addrinfo hints;
    bzero(&hints, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;

    for (auto& endpoint : mServers) {
        struct addrinfo* res;
        int ret = getaddrinfo(endpoint.mHostname.c_str(), endpoint.mPort.c_str(),
            &hints, &res);

        if (ret != 0) {
            PLOG_WARNING << fmt::format("Stun::Client::QueryAllServers getaddrinfo failed for "
                                        "{}:{}, {}",
                endpoint.mHostname, endpoint.mPort, gai_strerror(ret));
            continue;
        }

        // In the case of multiple retries being required, it is not realistic to
        // query all the addresses returned by getaddrinfo (say multihomed server)
        // So, we simply pick only the first address
        THROW_IF(res == nullptr);
        THROW_IF(res->ai_family != AF_INET);
        THROW_IF(res->ai_socktype != SOCK_DGRAM);
        servers.emplace_back(res->ai_addr);
        addrinfos.emplace_back(res);
    }

    size_t nServers { servers.size() };
    if (!nServers) {
        PLOG_WARNING << "Stun::Client::QueryAllServers - failed to resolve all servers!";
        return -1;
    }

    // RFC 5389 states that RTO estimate should be cached and reused for 10 minutes.
    // We do not cache the RTO altogether to simply avoid the complexity involved in
    // maintaining the RTO for each server separately.

    uint32_t rto = static_cast<uint32_t>(mTimeout.mTimeoutMs);
    std::vector<bool> successfulResponsesFrom(nServers, false);
    int successfulServerCount = 0;
    std::vector<std::pair<TransactionId, int>> requests; // transaction id -> server index

    for (int i = 0; i < nServers; i++) {
        TransactionId id;
        if (SendRequest(servers[i], sizeof(struct sockaddr_in), &id) == 0)
            requests.push_back({ id, i });
    }

    struct pollfd fd;
    fd.fd = mSockfd;
    fd.events = POLLIN;

    for (int i = 1; i <= mTimeout.mMaxRetransmissions && successfulServerCount < nServers; i++) {
        bool isFinalTimeout = (i == mTimeout.mMaxRetransmissions);

        if (isFinalTimeout)
            rto = mTimeout.mTimeoutMs * mTimeout.mFinalTimeoutMultiplier;

        errno = 0;
        int pollret = poll(&fd, 1, static_cast<int>(rto));
        rto *= 2;

        if (pollret == -1) {
            if (errno == EINTR)
                continue;
            else {
                PLOG_WARNING << "Stun::Client::QueryAllServers poll failed - " << strerror(errno);
                goto error;
            }
        }

        if (pollret > 0) {
            char buffer[kMtu];
            errno = 0;
            ssize_t ret;
            while ((ret = recv(mSockfd, buffer, sizeof(buffer), MSG_DONTWAIT)) > 0) {
                if (!IsStunMessage(buffer, ret))
                    continue;

                TransactionId id;
                if (ProcessResponse(buffer, ret, &id) == 0) {
                    int serverIndex = -1;
                    for (auto& [transactionId, index] : requests)
                        if (id == transactionId)
                            serverIndex = index;

                    if (serverIndex != -1 && successfulResponsesFrom[serverIndex] == false) {
                        successfulServerCount++;
                        successfulResponsesFrom[serverIndex] = true;
                    }
                }
            }

            if (ret == -1 && errno != EAGAIN && errno != EWOULDBLOCK) {
                PLOG_WARNING << "Stun::Client::QueryAllServers recv failed - " << strerror(errno);
                goto error;
            }
        }

        if (!isFinalTimeout) {
            for (int j = 0; j < nServers; j++) {
                TransactionId id;
                if (successfulResponsesFrom[j] == false
                    && SendRequest(servers[j], sizeof(struct sockaddr_in), &id) == 0)
                    requests.push_back({ id, j });
            }
        }
    }

    if (!successfulServerCount) {
        PLOG_WARNING << "Stun::Client::QueryAllServers failure to transact with all STUN servers";
        goto error;
    }

    for (auto& ai : addrinfos)
        freeaddrinfo(ai);
    return 0;

error:
    for (auto& ai : addrinfos)
        freeaddrinfo(ai);
    return -1;
}

NatType Client::GetNatType() const
{
    return mNatType;
}

int Client::GetReflexiveAddress(struct sockaddr* reflexiveAddress,
    socklen_t* reflexiveAddressLength) const
{
    if (mReflexiveAddress.sin_addr.s_addr == htonl(INADDR_ANY))
        return -1;
    if (*reflexiveAddressLength < sizeof(struct sockaddr_in))
        return -1;

    *reflexiveAddressLength = sizeof(struct sockaddr_in);
    memcpy(reflexiveAddress, &mReflexiveAddress, sizeof(mReflexiveAddress));
    return 0;
}

void Client::InvalidateReflexiveAddress()
{
    mNatType = NatType::kUnknown;
    bzero(&mReflexiveAddress, sizeof(mReflexiveAddress));
    mReflexiveAddress.sin_addr.s_addr = htonl(INADDR_ANY);
}

int Client::NatKeepAliveSend()
{
    const Endpoint& endpoint { mServers[std::rand() % mServers.size()] };
    struct addrinfo hints;
    bzero(&hints, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;

    struct addrinfo* res;
    int ret = getaddrinfo(endpoint.mHostname.c_str(), endpoint.mPort.c_str(),
        &hints, &res);

    if (ret != 0) {
        PLOG_WARNING << fmt::format("Stun::Client::NatKeepAliveSend getaddrinfo failed for "
                                    "{}:{}, {}",
            endpoint.mHostname, endpoint.mPort, gai_strerror(ret));
        return -1;
    }

    THROW_IF(res == nullptr);
    THROW_IF(res->ai_family != AF_INET);
    THROW_IF(res->ai_socktype != SOCK_DGRAM);

    struct sockaddr* serverAddress = res->ai_addr;
    if (SendRequest(serverAddress, sizeof(struct sockaddr_in), nullptr) == -1) {
        freeaddrinfo(res);
        return -1;
    }

    freeaddrinfo(res);
    return 0;
}

int Client::NatKeepAliveReceive(const void* message, size_t length)
{
    THROW_IF(!IsStunMessage(message, length));
    if (!ProcessResponse(message, length, nullptr))
        return -1;
    return 0;
}

int Client::SendRequest(const struct sockaddr* serverAddress, socklen_t serverAddressLength,
    TransactionId* transactionId)
{
    PurgeStaleTransactions();

    MessageBuilder builder(kHeader::MessageType::Request);

    TransactionId id = builder.GetTransactionId();
    const void* message = builder.GetMessage();
    size_t length = builder.GetMessageSize();

    errno = 0;
    ssize_t ret = sendto(mSockfd, message, length, MSG_DONTWAIT,
        serverAddress, serverAddressLength);

    if (ret == -1) {
        PLOG_WARNING << "Stun::Client::SendRequest sendto failed - " << strerror(errno);
        return -1;
    }

    if (ret < length) {
        PLOG_WARNING << fmt::format("Stun::Client::SendRequest sendto incomplete send - {} of {} bytes",
            ret, length);
        return -1;
    }

    AddNewTransaction(id);
    return 0;
}

int Client::ProcessResponse(const void* message, size_t length,
    TransactionId* transactionId)
{
    PurgeStaleTransactions();

    if (!IsStunMessage(message, length))
        return -1;

    MessageReader reader(message, length);
    const Header* header = reader.GetHeader();

    if (!EraseTransactionIfExists(header->mTransactionId)) {
        PLOG_INFO << "Received a packet with an unknown transaction id, too much network congestion?";
        return -1;
    }

    if (mNatType == NatType::kDependent)
        // No point of further processing
        return 0;

    const Attribute* attribute;
    std::set<uint16_t> attributesProcessed;
    int ret = -1;
    while ((attribute = reader.Next()) != nullptr) {
        uint16_t type = attribute->mType;
        // RFC 5389 section 15
        if (attributesProcessed.find(type) != attributesProcessed.end())
            continue;
        attributesProcessed.insert(type);

        using namespace kAttribute;
        switch (type) {
        case Required::XorMappedAddress: {
            struct sockaddr_in reflexiveAddress;
            socklen_t addressLength = sizeof(reflexiveAddress);
            if (reader.ParseXorMappedAddress(attribute, (struct sockaddr*)&reflexiveAddress,
                    &addressLength)
                == 0) {
                ret = 0;
                THROW_IF(addressLength != sizeof(reflexiveAddress));

                if (mNatType == NatType::kUnknown
                    && mReflexiveAddress.sin_addr.s_addr == htonl(INADDR_ANY)) {
                    // Very first STUN response
                    mReflexiveAddress = reflexiveAddress;
                } else if (mReflexiveAddress.sin_family != reflexiveAddress.sin_family
                    || mReflexiveAddress.sin_port != reflexiveAddress.sin_port
                    || mReflexiveAddress.sin_addr.s_addr != reflexiveAddress.sin_addr.s_addr) {
                    // Address mismatch
                    mNatType = NatType::kDependent;
                    bzero(&mReflexiveAddress, sizeof(mReflexiveAddress));
                    mReflexiveAddress.sin_addr.s_addr = htonl(INADDR_ANY);
                } else {
                    mNatType = NatType::kIndependent;
                }
            }
            break;
        }
        case Required::ErrorCode: {
            PLOG_ERROR << fmt::format(
                "Stun::Client::ProcessResponse error-code attribute received");
            break;
        }
        case Required::MappedAddress:
        case Required::Username:
        case Required::MessageIntegrity:
        case Required::UnknownAttributes:
        case Required::Realm:
        case Required::Nonce:
        case Optional::Software:
        case Optional::AlternateServer:
        case Optional::Fingerprint: {
            PLOG_INFO << fmt::format(
                "Stun::Client::ProcessResponse known-but-unexpected attribute of type {},"
                "ignoring",
                type);
            break;
        }
        default:
            if (type >= RequiredMin && type <= RequiredMax) {
                PLOG_WARNING << fmt::format(
                    "Stun::Client::ProcessResponse received unknown comprehension-required "
                    "attribute {}",
                    type);
                return -1;
            } else if (type >= OptionalMin && type <= OptionalMax) {
                PLOG_INFO << fmt::format(
                    "Stun::Client::ProcessResponse ignoring unknown comprehension-optional"
                    "attribute {}",
                    type);
            } else {
                THROW("Stun::Client::ProcessResponse malformed attribute type");
            }
        }
    }
    return ret;
}

uint64_t Client::GetTimeMs() const
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

void Client::AddNewTransaction(TransactionId id)
{
    mOngoingTransactions.insert({ .mSendTimeMs = GetTimeMs(),
        .mTransactionId = id });
}

bool Client::EraseTransactionIfExists(TransactionId id)
{
    for (auto& transaction : mOngoingTransactions) {
        if (transaction.mTransactionId == id) {
            mOngoingTransactions.erase(transaction);
            return true;
        }
    }
    return false;
}

void Client::PurgeStaleTransactions()
{
    uint64_t time = GetTimeMs();
    while (!mOngoingTransactions.empty()
        && (time - mOngoingTransactions.begin()->mSendTimeMs) > mStunTtlMs) {
        mOngoingTransactions.erase(mOngoingTransactions.begin());
    }
}

}
