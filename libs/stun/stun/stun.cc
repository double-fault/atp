#include "stun.h"

#include <cstdint>
#include <ctime>
#include <plog/Log.h>
#include <sys/socket.h>
#include <fmt/format.h>

namespace Stun {

bool IsStunMessage(const void* buffer, size_t length)
{
    if (length < sizeof(Header))
        return false;

    const char* cptr = (const char*)buffer;
    if (cptr[0] != 0 || cptr[1] != 0)
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

    Header* header = (Header*)mMessage;
    header->mMessageType = htons(messageType);
    header->mMessageLength = htons(0);
    header->mMagicCookie = htonl(kHeader::MagicCookie);

    std::srand(std::time({}));
    for (int i = 0; i < 12; i++) {
        // big endian = little endian for uint8_t
        header->mTransactionId[i] = std::rand() % UINT8_MAX;
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

uint8_t* MessageBuilder::GetTransactionId() const
{
    Header* header = (Header*)mMessage;
    return header->mTransactionId;
}

MessageReader::MessageReader(const void* buffer, size_t length)
    : mBuffer { buffer }
    , mBufferLength { length }
    , mCurrent { static_cast<const char*>(buffer) }
    , mCurrentLength { length }
{
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

    Attribute* ptr = (Attribute*)mCurrent;
    mCurrentAttribute.mType = ntohs(ptr->mType);
    mCurrentAttribute.mLength = ntohs(ptr->mLength);
    mCurrentAttribute.mValue = ptr->mValue;

    size_t totalAttributeLength = mCurrentAttribute.mLength;
    totalAttributeLength += (totalAttributeLength) % 4; // Length field does not include padding
    totalAttributeLength += sizeof(uint16_t) + sizeof(uint16_t); // mType, mLength

    mCurrent += totalAttributeLength;
    mCurrentLength -= totalAttributeLength;

    return &mCurrentAttribute;
}

int MessageReader::ParseXorMappedAddress(const Attribute* attribute,
    struct sockaddr* address, socklen_t* addressLength)
{
    if (attribute->mType != kAttribute::Required::XorMappedAddress)
        return -1;

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
    : Client(sockfd, kDefaultServers)
{
}

Client::Client(int sockfd, const std::vector<Endpoint>& servers)
    : mSockfd { sockfd }
    , mServers { servers }
    , mNatType { NatType::kUnknown }
{
    bzero(&mReflexiveAddress, sizeof(mReflexiveAddress));
}

int Client::QueryAllServers(struct sockaddr* reflexiveAddress, socklen_t* reflexiveAddressLength) 
{
    if (mNatType == NatType::kIndependent)
        return -1;
}

}
