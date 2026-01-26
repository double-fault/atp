#include <arpa/inet.h>
#include <stun/stun.h>
#include <sys/socket.h>
#include <plog/Log.h>
#include <plog/Formatters/TxtFormatter.h>
#include <plog/Initializers/ConsoleInitializer.h>

int main(int argc, char** argv) 
{
    plog::init<plog::TxtFormatter>(plog::info, plog::streamStdOut);

    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd == -1) {
        perror("socket");
        exit(0);
    }

    // binding test to verify port
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(6969);
    assert(!bind(sockfd, (struct sockaddr*)&addr, sizeof(addr)));

    Stun::Client stunClient(sockfd);
    if (stunClient.QueryAllServers() != 0) {
        PLOG_ERROR << "Stun queries failed";
        exit(0);
    }

    Stun::NatType natType = stunClient.GetNatType();
    std::cout << "Nat Type: ";
    if (natType == Stun::NatType::kUnknown)
        std::cout << "Unknown\n";
    else if (natType == Stun::NatType::kDependent)
        std::cout << "Dependent\n";
    else if (natType == Stun::NatType::kIndependent)
        std::cout << "Independent\n";

    struct sockaddr_in answer;
    socklen_t len = sizeof(answer);
    assert(stunClient.GetReflexiveAddress((struct sockaddr*)&answer, &len) == 0);
    assert(len == sizeof(answer));

    std::cout << "Public IP: " << inet_ntoa(answer.sin_addr) << "\n";
    std::cout << "Public Port: " << ntohs(answer.sin_port) << "\n";

    return 0;
}
