#pragma once

#include <cstddef>
#include <cstdint>
#include <sys/types.h>
namespace Atp {

inline constexpr uint8_t kMagic = 0x69;

// TODO: Make keepalive some sort of relay to detect UDP buffer overflowing?
// Then need to implement congestion control :nausea:

/*
 * CONNECTION ESTABLISHMENT
 * - State::LISTEN is only relevant to signalling.
 * - When getting a peer ip:port from signalling, state moves to PUNCH and PUNCH packets
 *   are continuously sent to peer.
 * - Upon receiving peer PUNCH packets, state moves to THRU and a THRU packet is sent to peer
 *   for every PUNCH/THRU packet received.
 * - Upon receiving a THRU packet, state moves to established. Both NATs have been successfully
 *   punched through!
 *
 * CONNECTION TERMINATION
 * - Identical to TCP
 */

enum class State {
    // NOTE: Need to use RTO (Retransmission TimeOut) for all packets, including control ones
    // TODO: How to interleave keepalive messages during the connection termination process?
    INVALID = 0,

    CLOSED,
    LISTEN,

    // Connection Establishment
    PUNCH,
    THRU,

    ESTABLISHED,

    // Passive Close
    CLOSE_WAIT,
    LAST_ACK,

    // Active Close
    FIN_WAIT_1,
    FIN_WAIT_2,
    CLOSING,
    TIME_WAIT,
};

// ISSUE: Lets say a server is servicing multiple clients. All packets come to the same udp socket.
// How to demultiplex?
struct __attribute__((packed)) atp_hdr {
    // byte-stream numbers, same as TCP
    uint32_t seq_num;
    uint32_t ack_num;
    // Probably some UB here, or at least compiler-extension dependent
    // But whatever, anonymous unions/structs are sexy
    union {
        uint8_t control;
        struct __attribute__((packed)) {
            unsigned punch : 1;
            unsigned thru : 1;
            unsigned ack : 1;
            unsigned rst : 1;
            unsigned fin : 1;
            // A keepalive message is ACKed with the seq num 
            // of the keepalive msg, not plus one
            unsigned kpalive : 1;
            unsigned res6 : 1;
            unsigned res7 : 1;
        };
    };
    uint8_t magic; // Is 8 bit magic insufficient?
    uint16_t window;
};

static_assert(sizeof(struct atp_hdr) == 12);

// BUG: Following two functions have UB (see protocol.cc)
int BuildDatagram(const struct atp_hdr* header, const void* payload, size_t payloadSize,
    void* datagram, size_t* datagramLength);
int ReadDatagram(const void* datagram, size_t datagramLength, struct atp_hdr* header,
    void* payload, size_t* payloadSize);

}
