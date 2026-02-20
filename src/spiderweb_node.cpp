#include <chrono>
#include <cstring>
#include <iostream>
#include <random>
#include <sstream>
#include <thread>
#include <mutex>
#include <map>
#include <string>

#include "spiderweb_node.h"

#include "heartbeat.h"
#include <google/protobuf/any.pb.h>
#include <google/protobuf/util/time_util.h>

// ---------- UUID generation ----------

#if __has_include(<uuid/uuid.h>)
#  include <uuid/uuid.h>
static std::string make_uuid16() {
    uuid_t u;
    uuid_generate(u);
    return std::string(reinterpret_cast<const char*>(u), 16);
}
#else
static std::string make_uuid16() {
    static std::mt19937_64 rng{std::random_device{}()};
    std::uniform_int_distribution<uint64_t> dist;
    uint64_t hi = dist(rng);
    uint64_t lo = dist(rng);
    std::string s(16, '\0');
    std::memcpy(s.data(),     &hi, 8);
    std::memcpy(s.data() + 8, &lo, 8);
    return s;
}
#endif

// ---------- SpiderwebNode ----------

SpiderwebNode::SpiderwebNode(const std::string& node_id,
                             const std::string& zmq_bind_addr,
                             const std::string& payload_mcast_addr,
                             int                payload_mcast_port,
                             const std::string& ctrl_mcast_addr,
                             int                ctrl_mcast_port)
    : node_id_(node_id)
    , zmq_bind_addr_(zmq_bind_addr)
    , payload_mcast_addr_(payload_mcast_addr)
    , payload_mcast_port_(payload_mcast_port)
    , ctrl_mcast_addr_(ctrl_mcast_addr)
    , ctrl_mcast_port_(ctrl_mcast_port)
{}

SpiderwebNode::~SpiderwebNode() {
    stop();
}

void SpiderwebNode::start() {
    // Initialise payload transport (send + receive).
    payload_transport_.init_sender(payload_mcast_addr_, payload_mcast_port_);
    payload_transport_.init_receiver(payload_mcast_addr_, payload_mcast_port_);

    // Initialise control transport (receive heartbeats).
    ctrl_transport_.init_sender(ctrl_mcast_addr_, ctrl_mcast_port_);
    ctrl_transport_.init_receiver(ctrl_mcast_addr_, ctrl_mcast_port_);

    // Start ZMQ fetch server.
    zmq_fetch_.start_server(zmq_bind_addr_,
        [this](const std::string& req_bytes) -> std::string {
            transport::FetchRequest req;
            if (!req.ParseFromString(req_bytes)) return {};
            auto envelopes = storage_.fetch(req.topic(), req.from(), req.to());
            transport::FetchResponse resp;
            for (auto& e : envelopes) {
                transport::Envelope env;
                if (env.ParseFromString(e))
                    resp.add_envelopes()->CopyFrom(env);
            }
            std::string out;
            resp.SerializeToString(&out);
            return out;
        });

    running_ = true;

    payload_transport_.start_recv(
        [this](const char* d, size_t n){ on_payload_recv(d, n); });
    ctrl_transport_.start_recv(
        [this](const char* d, size_t n){ on_ctrl_recv(d, n); });

    heartbeat_thread_ = std::thread([this]{ heartbeat_loop(); });
}

void SpiderwebNode::stop() {
    running_ = false;
    payload_transport_.stop_recv();
    ctrl_transport_.stop_recv();
    zmq_fetch_.stop_server();
    if (heartbeat_thread_.joinable()) heartbeat_thread_.join();
}

void SpiderwebNode::publish(const std::string& topic,
                            const std::string& payload_bytes) {
    transport::Envelope env;
    env.set_topic(topic);

    {
        std::lock_guard<std::mutex> lock(seq_mutex_);
        env.set_seq(++out_seq_[topic]);
    }

    env.set_uuid(gen_uuid16());

    *env.mutable_ts() =
        google::protobuf::util::TimeUtil::GetCurrentTime();

    // Store payload_bytes as-is inside a google.protobuf.Any with empty
    // type_url so subscribers can unpack it if they know the type.
    google::protobuf::Any any;
    any.set_value(payload_bytes);
    *env.mutable_payload() = std::move(any);

    std::string serialized;
    env.SerializeToString(&serialized);

    payload_transport_.send(serialized.data(), serialized.size());
    storage_.append(topic, env.seq(), serialized);
}

void SpiderwebNode::on_payload_recv(const char* data, size_t len) {
    transport::Envelope env;
    if (!env.ParseFromArray(data, static_cast<int>(len))) return;

    if (dedup_.is_duplicate_and_mark(env.uuid())) return;

    // Capture the last known seq BEFORE appending so we can detect gaps.
    uint64_t prev_last = storage_.last_seq(env.topic());
    storage_.append(env.topic(), env.seq(), std::string(data, len));

    // Gap detection: check if any sequences were skipped.
    if (env.seq() > prev_last + 1) {
        uint64_t from = prev_last + 1;
        uint64_t to   = env.seq() - 1;

        // Build FetchRequest.
        transport::FetchRequest req;
        req.set_topic(env.topic());
        req.set_from(from);
        req.set_to(to);
        std::string req_bytes;
        req.SerializeToString(&req_bytes);

        // Try each known peer.
        std::map<std::string, PeerInfo> snapshot;
        {
            std::lock_guard<std::mutex> lock(peers_mutex_);
            snapshot = peer_map_;
        }
        for (auto& [peer_id, info] : snapshot) {
            auto it = info.last_seq.find(env.topic());
            if (it == info.last_seq.end() || it->second < to) continue;

            std::string resp_bytes = zmq_fetch_.fetch_from(info.zmq_addr, req_bytes);
            if (resp_bytes.empty()) continue;

            transport::FetchResponse resp;
            if (!resp.ParseFromString(resp_bytes)) continue;
            for (auto& fetched : resp.envelopes()) {
                if (dedup_.is_duplicate_and_mark(fetched.uuid())) continue;
                std::string s;
                fetched.SerializeToString(&s);
                storage_.append(fetched.topic(), fetched.seq(), s);
            }
            break;
        }
    }
}

void SpiderwebNode::on_ctrl_recv(const char* data, size_t len) {
    transport::Heartbeat hb;
    if (!hb.ParseFromArray(data, static_cast<int>(len))) return;
    if (hb.node_id() == node_id_) return; // ignore our own heartbeats

    std::lock_guard<std::mutex> lock(peers_mutex_);
    auto& info    = peer_map_[hb.node_id()];
    info.zmq_addr = hb.zmq_addr();
    for (auto& [topic, seq] : hb.last_seq()) {
        info.last_seq[topic] = seq;
    }
}

void SpiderwebNode::heartbeat_loop() {
    while (running_) {
        transport::Heartbeat hb;
        hb.set_node_id(node_id_);
        hb.set_zmq_addr(zmq_bind_addr_);

        // Snapshot last_seq per topic from storage.
        // (We iterate out_seq_ as a proxy for topics we've published.)
        {
            std::lock_guard<std::mutex> lock(seq_mutex_);
            for (auto& [topic, _] : out_seq_) {
                (*hb.mutable_last_seq())[topic] = storage_.last_seq(topic);
            }
        }

        std::string serialized;
        hb.SerializeToString(&serialized);
        Heartbeat::send_heartbeat(ctrl_mcast_addr_, ctrl_mcast_port_, serialized);

        for (int i = 0; i < 20 && running_; ++i)
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}

std::string SpiderwebNode::gen_uuid16() {
    return make_uuid16();
}

std::map<std::string, std::string> SpiderwebNode::peers() const {
    std::lock_guard<std::mutex> lock(peers_mutex_);
    std::map<std::string, std::string> result;
    for (auto& [id, info] : peer_map_)
        result[id] = info.zmq_addr;
    return result;
}
