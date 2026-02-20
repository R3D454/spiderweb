#include "udp_transport.h"

#include <arpa/inet.h>
#include <net/if.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstring>
#include <stdexcept>

static constexpr size_t RECV_BUF = 65536;

UDPTransport::UDPTransport() = default;

UDPTransport::~UDPTransport() {
    stop_recv();
    if (send_fd_ >= 0) ::close(send_fd_);
    if (recv_fd_ >= 0) ::close(recv_fd_);
}

bool UDPTransport::init_sender(const std::string& mcast_addr, int port) {
    mcast_addr_ = mcast_addr;
    mcast_port_ = port;

    send_fd_ = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (send_fd_ < 0) return false;

    // Set TTL for multicast packets.
    unsigned char ttl = 32;
    if (::setsockopt(send_fd_, IPPROTO_IP, IP_MULTICAST_TTL,
                     &ttl, sizeof(ttl)) < 0) {
        ::close(send_fd_); send_fd_ = -1;
        return false;
    }
    return true;
}

bool UDPTransport::init_receiver(const std::string& mcast_addr, int port) {
    mcast_addr_ = mcast_addr;
    mcast_port_ = port;

    recv_fd_ = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (recv_fd_ < 0) return false;

    int reuse = 1;
    ::setsockopt(recv_fd_, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
#ifdef SO_REUSEPORT
    ::setsockopt(recv_fd_, SOL_SOCKET, SO_REUSEPORT, &reuse, sizeof(reuse));
#endif

    sockaddr_in addr{};
    addr.sin_family      = AF_INET;
    addr.sin_port        = htons(static_cast<uint16_t>(port));
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    if (::bind(recv_fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        ::close(recv_fd_); recv_fd_ = -1;
        return false;
    }

    ip_mreq mreq{};
    mreq.imr_multiaddr.s_addr = ::inet_addr(mcast_addr.c_str());
    mreq.imr_interface.s_addr = htonl(INADDR_ANY);
    if (::setsockopt(recv_fd_, IPPROTO_IP, IP_ADD_MEMBERSHIP,
                     &mreq, sizeof(mreq)) < 0) {
        ::close(recv_fd_); recv_fd_ = -1;
        return false;
    }
    return true;
}

bool UDPTransport::send(const char* data, size_t len) {
    if (send_fd_ < 0) return false;

    sockaddr_in dest{};
    dest.sin_family      = AF_INET;
    dest.sin_port        = htons(static_cast<uint16_t>(mcast_port_));
    dest.sin_addr.s_addr = ::inet_addr(mcast_addr_.c_str());

    ssize_t sent = ::sendto(send_fd_, data, len, 0,
                            reinterpret_cast<sockaddr*>(&dest), sizeof(dest));
    return sent == static_cast<ssize_t>(len);
}

void UDPTransport::start_recv(UdpRecvCallback cb) {
    if (recv_fd_ < 0) return;
    running_ = true;
    recv_thread_ = std::thread([this, cb = std::move(cb)]() {
        std::vector<char> buf(RECV_BUF);
        while (running_) {
            // Use a short timeout so we can check running_.
            timeval tv{0, 100000}; // 100 ms
            fd_set fds;
            FD_ZERO(&fds);
            FD_SET(recv_fd_, &fds);
            int r = ::select(recv_fd_ + 1, &fds, nullptr, nullptr, &tv);
            if (r > 0 && FD_ISSET(recv_fd_, &fds)) {
                ssize_t n = ::recv(recv_fd_, buf.data(), buf.size(), 0);
                if (n > 0) cb(buf.data(), static_cast<size_t>(n));
            }
        }
    });
}

void UDPTransport::stop_recv() {
    running_ = false;
    if (recv_thread_.joinable()) recv_thread_.join();
}
