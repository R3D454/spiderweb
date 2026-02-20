#pragma once

#include <functional>
#include <string>
#include <thread>
#include <atomic>

// Handler signature for the ZMQ server: receives a serialised FetchRequest,
// returns a serialised FetchResponse.
using ZmqServerHandler =
    std::function<std::string(const std::string& serialized_req)>;

class ZMQFetch {
public:
    ZMQFetch();
    ~ZMQFetch();

    // Start a ZMQ REP server on zmq_bind_addr (e.g. "tcp://*:5555").
    // handler is called in the server thread for each request.
    void start_server(const std::string& zmq_bind_addr,
                      ZmqServerHandler handler);

    // Stop the server thread.
    void stop_server();

    // Synchronous fetch from a peer's ZMQ REP endpoint.
    // Returns the raw serialised FetchResponse, or empty on error.
    std::string fetch_from(const std::string& zmq_addr,
                           const std::string& serialized_request);

private:
    std::atomic<bool> running_{false};
    std::thread       server_thread_;
    std::string       bind_addr_;
};
