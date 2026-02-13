#pragma once

#include "common.h"
#include "protocol.h"
#include "types.h"

#include <span>
#include <vector>

namespace Atp {

/*
 * Notes for implementation when I do next time around:
 *
 * Use cumulative acks.
 * Still need to figure out easy way to estimate RTT.
 * Steven's method of adding timestamp to packet which is echo'ed back is cool, figure out
 * how to extend that to cumulative acknowledgments.
 * Maybe do add a timestamp to every packet, and based off the ACK, use that timestamp..
 * Like 2 packets were sent and 1 ACK was received - the ACK carries the timestamp of the
 * last packet processed? And then from there we can easily estimate.
 * Issue: what about duplicate ACKs?
 * Soln?: on retransmit set timestamp to invalid. So timestamp is echo'd back and
 * RTT is updated only if original packet made it and original ACK also made it back.
 *
 *
 * Solution to registering callbacks, testability of all classes, etc.: instead of directly
 * using glibc syscalls like socket(), we use an interface say ISocket, and then implement
 * a PosixSocket : public ISocket. How to pass a factory function to SocketImpl?
 *
 * Create a ISocketFactory, PosixSocketFactory : public ISocketFactory, and ISocketFactory
 * has a factory function virtual std::unique_ptr<ISocket> create(...) = 0;
 *
 * We pass in ISocketFactory& (or raw pointer?) to SocketImpl constructor, and then it uses
 * the factory function to create ISockets and then uses the ISocket functions instead of
 * raw calls.
 *
 * This makes SocketImpl testable. We can now also pass in ISockets to ProtocolEngine?
 *
 * NO. Lets not pass ISocket to ProtocolEngine (but implementing ISocket is still needed for
 * testing purposes).
 *
 * ProtocolEngine = takes in data from application in byte stream form, from network in
 * packet form. Produces data to be sent to application in stream form, produces
 * datagrams to be sent to network in packet form.
 *
 * There should be some sort of Run() (or maybe 2 functions one for application, network each)
 * which returns mseconds_t and also somehow returns the data to be sent to application/network
 * if there is any.
 *
 */

/*
 * ProtocolEngine implements the following functions:
 *
 * (1) Read from Application byte stream -> IngestStream()
 *              Network Segments to Send <- PeekStream()
 *
 * (2)                   Read Network Segment -> IngestSegment()
 *     Write bytes to application byte stream <- PeekStream()
 *
 * ProtocolEngine, SocketImpl both use a common definition of Segment and are closely linked.
 * As such, I'm not making an IProtocolEngine.. although it does feel like there could be
 * an interface here. Also reduces the need of a factory class etc.
 */

class ProtocolEngine {
public:
    mseconds_t Run();

    // returns number of bytes ingested
    unsigned int IngestStream(std::vector<std::byte>&& blob);

    // returns 0 on failure, else size of *payload* in bytes
    unsigned int IngestSegment(Segment&& segment);

    std::span<std::byte> PeekStream();
    void AdvanceStream(size_t length);

    std::span<Segment> PeekSegments();
    void PopSegments(size_t count);
};

}
