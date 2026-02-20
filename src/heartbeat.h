#pragma once

#include <string>

class Heartbeat {
public:
    // Send a serialised Heartbeat message to the control multicast group.
    // ctrl_mcast_addr is the multicast address, ctrl_port is the UDP port.
    // serialized_bytes is the already-serialised transport::Heartbeat proto.
    static bool send_heartbeat(const std::string& ctrl_mcast_addr,
                               int ctrl_port,
                               const std::string& serialized_bytes);
};
