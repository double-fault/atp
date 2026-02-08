#include "protocol.h"
#include "common.h"

#include <cstring>
#include <netinet/in.h>

namespace Atp {

int BuildDatagram(const struct atp_hdr* header, const void* payload, size_t payloadSize,
    void* datagram, size_t* datagramLength)
{
    if (*datagramLength < sizeof(struct atp_hdr) + payloadSize)
        return -1;

    THROW_IF(header->magic != kAtpMagic);

    char* ptr = static_cast<char*>(datagram);
    // BUG: This method of casting the pointer to a struct* is UB apparently
    // Ask chatgpt (strict aliasing etc, which apparently gets enabled with -O3)
    struct atp_hdr* hptr = static_cast<struct atp_hdr*>(datagram);

    hptr->seq_num = htonl(header->seq_num);
    hptr->ack_num = htonl(header->ack_num);
    hptr->c = header->c;
    hptr->magic = header->magic;
    hptr->window = htons(header->magic);

    // PERF: memory copy :(
    memcpy(ptr + sizeof(atp_hdr), payload, payloadSize);
    *datagramLength = sizeof(struct atp_hdr) + payloadSize;

    return 0;
}

int ReadDatagram(const void* datagram, size_t datagramLength, struct atp_hdr* header,
    void* payload, size_t* payloadSize)
{
    if (datagramLength < sizeof(struct atp_hdr))
        return -1;
    if (*payloadSize < datagramLength - sizeof(struct atp_hdr))
        return -1;

    const char* ptr = static_cast<const char*>(datagram);
    // BUG: This method of casting the pointer to a struct* is UB apparently
    // Ask chatgpt (strict aliasing etc, which apparently gets enabled with -O3)
    const struct atp_hdr* hptr = static_cast<const struct atp_hdr*>(datagram);

    header->seq_num = ntohl(hptr->seq_num);
    header->ack_num = ntohl(hptr->ack_num);
    header->c = hptr->c;
    header->magic = hptr->magic;
    header->window = ntohs(hptr->window);

    THROW_IF(header->magic != kAtpMagic);

    // PERF: memory copy :(
    *payloadSize = datagramLength - sizeof(struct atp_hdr);
    memcpy(payload, ptr + sizeof(atp_hdr), *payloadSize);

    return 0;
}

}
