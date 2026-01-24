#pragma once

#include <cstdint>
#include <sys/socket.h>
#include <strings.h>
#include <arpa/inet.h>

// Reference: RFC 5389

namespace stun {

const uint32_t kMagicCookie = 0x2112A442;
const uint16_t kRequest = 0x0001;
const uint16_t kResponse = 0x0101;
const uint8_t kTransactionId = 0x69;
const uint16_t kXMapppedAddressType = 0x0020;
const uint8_t kIPv4Family = 0x01;

int Query(int sockfd, const struct sockaddr_in* servAddr,
    struct sockaddr_in* reflexiveAddr);

struct __attribute__((__packed__)) Header {
    uint16_t mType;
    uint16_t mLength;
    uint32_t mMagicCookie;
    uint8_t mTransactionId[12];
};

struct __attribute__((__packed__)) Attribute {
    uint16_t mType;
    uint16_t mLength;
    uint32_t mValue[]; // Flexible Array Member (FAM), compiler extension
                       // As usual, cpp did not standardize cool things from C
};

struct __attribute__((__packed__)) XMappedAddress {
    uint8_t mZero;
    uint8_t mFamily;
    uint16_t mXPort;
    uint32_t mXAddress;
};

}
