#include "stun.h"
#include <cassert>
#include <iostream>
#include <netinet/in.h>

namespace stun {

// TODO: Keep this simple POC.
// Need to add retries if packet is lost, error handling, etc.
int Query(int sockfd, const struct sockaddr_in* servAddr,
    struct sockaddr_in* reflexiveAddr)
{
    Header header;
    bzero(&header, sizeof(header));
    header.mType = htons(kRequest);
    header.mLength = htons(0);
    header.mMagicCookie = htonl(kMagicCookie);
    header.mTransactionId[0] = kTransactionId;

    assert(sendto(sockfd, &header, sizeof(header), 0,
               (const struct sockaddr*)servAddr, sizeof(struct sockaddr_in))
        == sizeof(header));

    char response[3000];
    struct sockaddr_in responseAddr;
    socklen_t responseAddrLen = sizeof(responseAddr);
    ssize_t ret = recvfrom(sockfd, &response, sizeof(response), 0,
        (struct sockaddr*)&responseAddr, &responseAddrLen);
    assert(ret > 0);
    assert(responseAddrLen == sizeof(responseAddr));

    Header responseHeader;
    char* ptr = response;

    Header* headerPtr = (Header*)ptr;
    responseHeader.mType = ntohs(headerPtr->mType);
    responseHeader.mLength = ntohs(headerPtr->mLength);
    responseHeader.mMagicCookie = ntohl(headerPtr->mMagicCookie);
    for (int i = 0; i < 6; i++) {
        responseHeader.mTransactionId[i] = headerPtr->mTransactionId[i];
    }

    assert(responseHeader.mType == kResponse);
    assert(responseHeader.mLength > 0);
    assert(responseHeader.mMagicCookie == kMagicCookie);
    assert(responseHeader.mTransactionId[0] == kTransactionId);

    ptr += sizeof(Header);
    while (responseHeader.mLength > 0) {
        Attribute* aptr = (Attribute*)ptr;
        Attribute* attribute = (Attribute*)malloc(sizeof(Attribute) + sizeof(uint32_t));

        attribute->mType = ntohs(aptr->mType);
        attribute->mLength = ntohs(aptr->mLength);

        ptr += sizeof(Attribute);
        if (attribute->mType != kXMapppedAddressType) {
            ptr += attribute->mLength;
            free(attribute);
            continue;
        }

        XMappedAddress* xptr = (XMappedAddress*)ptr;
        XMappedAddress xaddr;
        xaddr.mZero = xptr->mZero;
        xaddr.mFamily = xptr->mFamily;
        xaddr.mXPort = ntohs(xptr->mXPort);
        xaddr.mXAddress = ntohl(xptr->mXAddress);

        ptr += attribute->mLength;
        free(attribute);

        if (xaddr.mFamily != kIPv4Family) {
            continue;
        }

        xaddr.mXPort = xaddr.mXPort ^ (kMagicCookie >> 16);
        xaddr.mXAddress = xaddr.mXAddress ^ kMagicCookie;

        reflexiveAddr->sin_family = AF_INET;
        reflexiveAddr->sin_port = htons(xaddr.mXPort);
        reflexiveAddr->sin_addr.s_addr = htonl(xaddr.mXAddress);

        return 0;
    }
    return -1;
}

} // namespace stun
