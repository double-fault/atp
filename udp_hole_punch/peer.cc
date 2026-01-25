#include "stun.h"

#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <iostream>
#include <netinet/in.h>
#include <cassert>
#include <sys/socket.h>
#include <unistd.h>
#include <strings.h>

void foo(int sockfd, struct sockaddr_in* peer_addr)
{
    puts("in foo");
    int counter = 0;
    char buf_to_send[40];

    int i = 0;

    for (;;) {
        sprintf(buf_to_send, "hi sankul %d\n", counter++);

        sendto(sockfd, buf_to_send, strlen(buf_to_send), 0, 
                (struct sockaddr*)peer_addr, sizeof(struct sockaddr_in));

        char recv_buf[100];
        struct sockaddr_in from;
        socklen_t from_len = sizeof(from);

        errno = 0;
        ssize_t ret = recvfrom(sockfd, recv_buf, sizeof(recv_buf), 
                MSG_DONTWAIT, (struct sockaddr*)&from, &from_len);

        usleep(1000);

        if (ret == -1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
//                puts("blocked");

                if (i == 1000) {
                    puts("blocked");
                    i = 0;
                } else i++;
                continue;
            }
            perror("recvfrom");
        }

        recv_buf[ret] = '\0';
        printf("%s\n", recv_buf);
    }
}

int main()
{
    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    assert(sockfd > 0);

    sockaddr_in bind_addr;
    bzero(&bind_addr, sizeof(bind_addr));
    bind_addr.sin_family = AF_INET;
    bind_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    bind_addr.sin_port = htons(6969);
    assert(!bind(sockfd, (struct sockaddr*)&bind_addr, sizeof(bind_addr)));

    sockaddr_in server {};
    server.sin_family = AF_INET;
    server.sin_port = htons(19302);
    inet_pton(AF_INET, "74.125.250.129", &server.sin_addr); // stun.l.google.com

    struct sockaddr_in answer;
    assert(stun::Query(sockfd, &server, &answer) == 0);

    std::cout << "Public IP: " << inet_ntoa(answer.sin_addr) << "\n";
    std::cout << "Public Port: " << ntohs(answer.sin_port) << "\n";

    struct sockaddr_in peer_addr;
    bzero(&peer_addr, sizeof(peer_addr));

    char server_paddr[300];
    std::cout << "Enter peer ip: ";
    scanf("%s", server_paddr);
    uint16_t server_port;
    std::cout << "Enter peer port: ";
    scanf("%hd", &server_port);

    assert(1 == inet_pton(AF_INET, server_paddr, &peer_addr.sin_addr));
    peer_addr.sin_port = htons(server_port);
    peer_addr.sin_family = AF_INET;

    char x;
    std::cout << "Enter any character to start udp hole punching!";
    scanf(" %c", &x);
    foo(sockfd, &peer_addr);

    return 0;
}
