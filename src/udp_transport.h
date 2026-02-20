#pragma once

#include <functional>
#include <string>
#include <thread>
#include <atomic>

using UdpRecvCallback = std::function<void(const char*, size_t)>;

class UDPTransport {
public:
    UDPTransport();
    ~UDPTransport();

    // Initialise a sending socket bound to any local port.
    bool init_sender(const std::string& mcast_addr, int port);

    // Initialise a receiving socket and join the multicast group.
    bool init_receiver(const std::string& mcast_addr, int port);

    // Send raw bytes via the sender socket.
    bool send(const char* data, size_t len);

    // Start background receive thread; invokes cb for every datagram.
    void start_recv(UdpRecvCallback cb);

    // Stop the background receive thread.
    void stop_recv();

private:
    std::string mcast_addr_;
    int         mcast_port_{0};
    int         send_fd_{-1};
    int         recv_fd_{-1};

    std::atomic<bool> running_{false};
    std::thread       recv_thread_;
};
