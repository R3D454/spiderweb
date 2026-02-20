#include "heartbeat.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

bool Heartbeat::send_heartbeat(const std::string& ctrl_mcast_addr,
                                int ctrl_port,
                                const std::string& serialized_bytes) {
    int fd = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) return false;

    unsigned char ttl = 32;
    ::setsockopt(fd, IPPROTO_IP, IP_MULTICAST_TTL, &ttl, sizeof(ttl));

    sockaddr_in dest{};
    dest.sin_family      = AF_INET;
    dest.sin_port        = htons(static_cast<uint16_t>(ctrl_port));
    dest.sin_addr.s_addr = ::inet_addr(ctrl_mcast_addr.c_str());

    ssize_t sent = ::sendto(fd, serialized_bytes.data(),
                            serialized_bytes.size(), 0,
                            reinterpret_cast<sockaddr*>(&dest), sizeof(dest));
    ::close(fd);
    return sent == static_cast<ssize_t>(serialized_bytes.size());
}
