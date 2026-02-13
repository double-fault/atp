#pragma once

#include <string>
#include <set>
#include <vector>

#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>

// Keep sending stun packets to keep NAT binding alive??
// Is there a need to multiplex STUN packets with normal application packets?
//
// "If the agent is sending a request, it SHOULD add a SOFTWARE attribute
//   to the request."
//
//   Check b4 sending if packet size < MTU
//
// Instead of libs/stun, make some sort of libs/common which also does stuff like path MTU probing?
//
// "a client SHOULD
//   space new transactions to a server by RTO and SHOULD limit itself to
//   ten outstanding transactions to the same server."
//
//  Karn algorithm for RTO/RTT? RFC2988?
//
// Handle ICMP messages??

// What is the interface? A class? Keepalive behavior? multiplexing?
// Lets say there is a P2P mesh, then you want multiple sockets in a single program -> multiple stun clients -> should be a class.
// Some methods can be static, like to check if a packet is a STUN msg, to query a given stun server, etc.
// IPv6 support? Later please.
// Two separate classes? One for basic stun communication, one for keepalive, comparing results from multiple stun servers, etc.

// Reference: RFC 5389
namespace Stun {

#define likely(x) __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)

#define THROW(message) throw std::runtime_error(#message)

#define THROW_IF(condition)                       \
    do {                                          \
        if (unlikely(condition))                  \
            throw std::runtime_error(#condition); \
    } while (false)

namespace kHeader {
    namespace MessageType {
        inline constexpr uint16_t Request { 0x0001 };
        inline constexpr uint16_t Indication { 0x0011 };
        inline constexpr uint16_t Response { 0x0101 };
        inline constexpr uint16_t Error { 0x0111 };
    }
    inline constexpr uint32_t MagicCookie { 0x2112A442 };
}

namespace kAttribute {
    // RFC 5389 18.2: Comprehension required attributes from 0x0000 - 0x7FFF
    //                Comprehension optional attributes from 0x8000 - 0xFFFF
    inline constexpr uint16_t RequiredMin { 0x0000 };
    inline constexpr uint16_t RequiredMax { 0x7FFF };
    inline constexpr uint16_t OptionalMin { 0x8000 };
    inline constexpr uint16_t OptionalMax { 0xFFFF };
    namespace Required {
        inline constexpr uint16_t MappedAddress { 0x0001 };
        inline constexpr uint16_t Username { 0x0006 };
        inline constexpr uint16_t MessageIntegrity { 0x0008 };
        inline constexpr uint16_t ErrorCode { 0x0009 };
        inline constexpr uint16_t UnknownAttributes { 0x000A };
        inline constexpr uint16_t Realm { 0x0014 };
        inline constexpr uint16_t Nonce { 0x0015 };
        inline constexpr uint16_t XorMappedAddress { 0x0020 };
    }
    namespace Optional {
        inline constexpr uint16_t Software { 0x8022 };
        inline constexpr uint16_t AlternateServer { 0x8023 };
        inline constexpr uint16_t Fingerprint { 0x8028 };
    }
    namespace MappedAddress {
        inline constexpr uint8_t IPv4 { 0x01 };
        inline constexpr uint8_t IPv6 { 0x02 };
    }
}

// All structs are maintained in host byte order when in memory

struct __attribute__((packed)) TransactionId {
    uint8_t mId[12];

    bool operator==(const TransactionId&) const = default;
    bool operator<(const TransactionId& other) const {
        for (int i = 0; i < 12; i++) {
            if (mId[i] != other.mId[i])
                return mId[i] < other.mId[i];
        }
        return false;
    }
};

struct __attribute__((packed)) Header {
    uint16_t mMessageType;
    uint16_t mMessageLength;
    uint32_t mMagicCookie;
    TransactionId mTransactionId;
};

// RFC 5389: "Each STUN attribute MUST end on a 32-bit boundary"
struct __attribute__((packed)) Attribute {
    uint16_t mType;
    uint16_t mLength;
    uint8_t* mValue; // Whether Attribute owns the memory allocated
                     // to mValue is left upto the user
};

struct __attribute__((packed)) MappedAddressIPv4 {
    uint8_t mZero;
    uint8_t mFamily;
    uint16_t mPort;
    in_addr_t mAddr;
};

bool IsStunMessage(const void* buffer, size_t length);

class MessageBuilder final {
public:
    MessageBuilder(uint16_t messageType);
    ~MessageBuilder();

    // Function not implemented, because well, it is not needed for now.
    int AddAttribute(const Attribute& attribute) = delete;

    const void* GetMessage() const;
    size_t GetMessageSize() const;
    TransactionId GetTransactionId() const;

private:
    void* mMessage;
    size_t mLength;
};

// Assumes a well-formed STUN message, can throw otherwise
class MessageReader final {
public:
    MessageReader(const void* buffer, size_t length);
    ~MessageReader() = default;

    const Header* GetHeader() const;
    const Attribute* Next();

    int ParseXorMappedAddress(const Attribute* attribute,
        struct sockaddr* address, socklen_t* addressLength);

private:
    const void* const mBuffer;
    const size_t mBufferLength;
    const char* mCurrent; // char* for easy pointer arithmetic
    size_t mCurrentLength;

    Attribute mCurrentAttribute;
};

struct Endpoint {
    std::string mHostname;
    std::string mPort;
};

enum class NatType {
    kUnknown = 0,
    kIndependent,
    kDependent
};

class Client final {
public:
    struct Timeout {
        uint64_t mTimeoutMs;
        uint64_t mMaxRetransmissions;
        uint64_t mFinalTimeoutMultiplier;
    };

    Client(int sockfd);
    Client(int sockfd, Timeout timeout);
    Client(int sockfd, const std::vector<Endpoint>& servers, Timeout timeout);
    ~Client() = default;

    int QueryAllServers();
    NatType GetNatType() const;
    int GetReflexiveAddress(struct sockaddr* reflexiveAddress,
        socklen_t* reflexiveAddressLength) const;

    void InvalidateReflexiveAddress();

    // Using these, you can multiplex keepalive messages with other data
    int NatKeepAliveSend();
    int NatKeepAliveReceive(const void* message, size_t length);

private:
    struct OngoingTransaction {
        uint64_t mSendTimeMs;
        TransactionId mTransactionId;

        bool operator<(const OngoingTransaction& other) const
        {
            if (mSendTimeMs != other.mSendTimeMs)
                return mSendTimeMs < other.mSendTimeMs;
            return mTransactionId < other.mTransactionId;
        }
    };

    inline static const std::vector<Endpoint> kDefaultServers {
        { "stun.l.google.com", "19302" },
        { "stun.freeswitch.org", "3478" },
        { "stun.voip.blackberry.com", "3478" }
    };

    inline static const Timeout kDefaultTimeout {
        .mTimeoutMs = 500,
        .mMaxRetransmissions = 7,
        .mFinalTimeoutMultiplier = 16
    };

    // Path MTU is unknown, so 576 is the safe value
    inline static const uint32_t kMtu { 576 };

    int SendRequest(const struct sockaddr* serverAddress,
        socklen_t serverAddressLength, TransactionId* transactionId);

    // ProcessResponse rejects a message if the transaction id does not
    // match the id of any of the live transactions, sent from SendRequest
    int ProcessResponse(const void* message, size_t length,
        TransactionId* transactionId);

    uint64_t GetTimeMs() const;
    void AddNewTransaction(TransactionId id);
    bool EraseTransactionIfExists(TransactionId id);
    void PurgeStaleTransactions();

    int mSockfd;
    const std::vector<Endpoint> mServers;
    NatType mNatType;

    Timeout mTimeout; // RTO = Retransmission TimeOut
    uint64_t mStunTtlMs;
    std::set<OngoingTransaction> mOngoingTransactions;

    // Current code only support IPv4, although I have tried to make
    // the function signatures protocol-independent
    struct sockaddr_in mReflexiveAddress;
};

}
