#include <arpa/inet.h>
#include <netdb.h>
#include <sys/socket.h>
#include <unistd.h>

#include "stun.h"

#include <cstdint>
#include <cstring>
#include <iostream>
#include <random>

#define STUN_BINDING_REQUEST 0x0001
#define STUN_MAGIC_COOKIE 0x2112A442
#define STUN_ATTR_XOR_MAPPED_ADDRESS 0x0020

#pragma pack(push, 1)
struct StunHeader {
    uint16_t type;
    uint16_t length;
    uint32_t magic;
    uint8_t transaction_id[12];
};
#pragma pack(pop)

int main()
{
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        perror("socket");
        return 1;
    }

    sockaddr_in bind2;
    bzero(&bind2, sizeof(bind2));
    bind2.sin_family = AF_INET;
    bind2.sin_port = htons(6969);
    assert(!bind(sock, (struct sockaddr*)&bind2, sizeof(bind2)));

    sockaddr_in server {};
    server.sin_family = AF_INET;
    server.sin_port = htons(19302);
    inet_pton(AF_INET, "74.125.250.129", &server.sin_addr); // stun.l.google.com

    struct sockaddr_in answer;
    assert(stun::Query(sock, &server, &answer) == 0);

    std::cout << "Public IP: " << inet_ntoa(answer.sin_addr) << "\n";
    std::cout << "Public Port: " << ntohs(answer.sin_port) << "\n";

    /*
    StunHeader request{};
    request.type = htons(STUN_BINDING_REQUEST);
    request.length = htons(0);
    request.magic = htonl(STUN_MAGIC_COOKIE);

    std::random_device rd;
    for (int i = 0; i < 12; i++)
        request.transaction_id[i] = rd();

    if (sendto(sock, &request, sizeof(request), 0,
               (sockaddr*)&server, sizeof(server)) < 0) {
        perror("sendto");
        close(sock);
        return 1;
    }

    uint8_t buffer[512];
    sockaddr_in from{};
    socklen_t from_len = sizeof(from);

    ssize_t n = recvfrom(sock, buffer, sizeof(buffer), 0,
                         (sockaddr*)&from, &from_len);
    if (n < 0) {
        perror("recvfrom");
        close(sock);
        return 1;
    }

    // Skip STUN header
    size_t offset = sizeof(StunHeader);

    while (offset + 4 <= (size_t)n) {
        uint16_t attr_type = ntohs(*(uint16_t*)(buffer + offset));
        uint16_t attr_len  = ntohs(*(uint16_t*)(buffer + offset + 2));
        offset += 4;

        if (attr_type == STUN_ATTR_XOR_MAPPED_ADDRESS) {
            uint8_t family = buffer[offset + 1];
            uint16_t xport = ntohs(*(uint16_t*)(buffer + offset + 2));
            uint32_t xaddr = ntohl(*(uint32_t*)(buffer + offset + 4));

            uint16_t port = xport ^ (STUN_MAGIC_COOKIE >> 16);
            uint32_t addr = xaddr ^ STUN_MAGIC_COOKIE;

            in_addr ip{};
            ip.s_addr = htonl(addr);

            std::cout << "Public IP: " << inet_ntoa(ip) << "\n";
            std::cout << "Public Port: " << port << "\n";
            break;
        }

        offset += attr_len;
        if (attr_len % 4 != 0)
            offset += 4 - (attr_len % 4); // padding
    }
    */

    close(sock);
    return 0;
}
