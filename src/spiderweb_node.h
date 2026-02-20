#pragma once

#include <string>
#include <map>
#include <mutex>
#include <thread>
#include <atomic>
#include <functional>

#include "udp_transport.h"
#include "storage.h"
#include "deduplicator.h"
#include "zmq_fetch.h"

// Forward-declared to avoid pulling in generated headers here.
namespace google { namespace protobuf { class Message; } }

class SpiderwebNode {
public:
    SpiderwebNode(const std::string& node_id,
                  const std::string& zmq_bind_addr,
                  const std::string& payload_mcast_addr,
                  int                payload_mcast_port,
                  const std::string& ctrl_mcast_addr,
                  int                ctrl_mcast_port);
    ~SpiderwebNode();

    void start();
    void stop();

    // Publish raw serialized bytes under topic.
    void publish(const std::string& topic, const std::string& payload_bytes);

    // Publish a protobuf message under topic; wraps it in google.protobuf.Any.
    template <typename T>
    void publishProto(const std::string& topic, const T& msg);

    // Return a snapshot of known peers: node_id -> zmq_addr.
    std::map<std::string, std::string> peers() const;

private:
    void on_payload_recv(const char* data, size_t len);
    void on_ctrl_recv(const char* data, size_t len);
    void heartbeat_loop();
    std::string gen_uuid16();

    std::string node_id_;
    std::string zmq_bind_addr_;
    std::string payload_mcast_addr_;
    int         payload_mcast_port_;
    std::string ctrl_mcast_addr_;
    int         ctrl_mcast_port_;

    UDPTransport   payload_transport_;
    UDPTransport   ctrl_transport_;
    Storage        storage_;
    Deduplicator   dedup_;
    ZMQFetch       zmq_fetch_;

    std::atomic<bool> running_{false};
    std::thread       heartbeat_thread_;

    // Sequence counter for outgoing messages per topic.
    mutable std::mutex seq_mutex_;
    std::map<std::string, uint64_t> out_seq_;

    // Peer map: node_id -> {zmq_addr, last_seq per topic}
    mutable std::mutex peers_mutex_;
    struct PeerInfo {
        std::string zmq_addr;
        std::map<std::string, uint64_t> last_seq;
    };
    std::map<std::string, PeerInfo> peer_map_;
};

// ---------- template implementation ----------

#include "transport.pb.h"
#include <google/protobuf/any.pb.h>

template <typename T>
void SpiderwebNode::publishProto(const std::string& topic, const T& msg) {
    google::protobuf::Any any;
    any.PackFrom(msg);
    std::string serialized;
    any.SerializeToString(&serialized);
    publish(topic, serialized);
}
