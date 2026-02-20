#include "zmq_fetch.h"

#include <zmq.hpp>
#include <iostream>

ZMQFetch::ZMQFetch() = default;

ZMQFetch::~ZMQFetch() {
    stop_server();
}

void ZMQFetch::start_server(const std::string& zmq_bind_addr,
                             ZmqServerHandler handler) {
    bind_addr_ = zmq_bind_addr;
    running_   = true;
    server_thread_ = std::thread([this, handler = std::move(handler)]() {
        zmq::context_t ctx(1);
        zmq::socket_t  sock(ctx, zmq::socket_type::rep);
        sock.bind(bind_addr_);
        sock.set(zmq::sockopt::rcvtimeo, 200); // 200 ms poll interval
        while (running_) {
            zmq::message_t req;
            auto result = sock.recv(req, zmq::recv_flags::none);
            if (!result) continue; // timeout
            std::string req_str(static_cast<char*>(req.data()), req.size());
            std::string resp_str = handler(req_str);
            zmq::message_t rep(resp_str.size());
            std::memcpy(rep.data(), resp_str.data(), resp_str.size());
            sock.send(rep, zmq::send_flags::none);
        }
    });
}

void ZMQFetch::stop_server() {
    running_ = false;
    if (server_thread_.joinable()) server_thread_.join();
}

std::string ZMQFetch::fetch_from(const std::string& zmq_addr,
                                  const std::string& serialized_request) {
    try {
        zmq::context_t ctx(1);
        zmq::socket_t  sock(ctx, zmq::socket_type::req);
        sock.set(zmq::sockopt::rcvtimeo, 2000); // 2 s timeout
        sock.connect(zmq_addr);

        zmq::message_t req(serialized_request.size());
        std::memcpy(req.data(), serialized_request.data(),
                    serialized_request.size());
        sock.send(req, zmq::send_flags::none);

        zmq::message_t rep;
        auto result = sock.recv(rep, zmq::recv_flags::none);
        if (!result) return {};
        return {static_cast<char*>(rep.data()), rep.size()};
    } catch (const zmq::error_t& e) {
        std::cerr << "[ZMQFetch] error: " << e.what() << '\n';
        return {};
    }
}
