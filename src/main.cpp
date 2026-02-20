#include "spiderweb_node.h"

#include <iostream>
#include <sstream>
#include <string>

static void usage(const char* prog) {
    std::cerr << "Usage: " << prog
              << " <node_id> <zmq_bind_addr> <payload_mcast_addr>"
                 " <payload_mcast_port> <ctrl_mcast_addr> <ctrl_mcast_port>\n"
              << "\nExample:\n"
              << "  " << prog
              << " node1 tcp://*:5555 239.0.0.1 5000 239.0.0.2 5001\n";
}

int main(int argc, char* argv[]) {
    if (argc != 7) { usage(argv[0]); return 1; }

    const std::string node_id           = argv[1];
    const std::string zmq_bind_addr     = argv[2];
    const std::string payload_mcast     = argv[3];
    const int         payload_port      = std::stoi(argv[4]);
    const std::string ctrl_mcast        = argv[5];
    const int         ctrl_port         = std::stoi(argv[6]);

    SpiderwebNode node(node_id, zmq_bind_addr,
                       payload_mcast, payload_port,
                       ctrl_mcast, ctrl_port);
    node.start();

    std::cout << "[spiderweb] Node '" << node_id << "' started.\n"
              << "Commands: publish <topic> <text>  |  peers  |  quit\n";

    std::string line;
    while (std::getline(std::cin, line)) {
        if (line.empty()) continue;
        std::istringstream iss(line);
        std::string cmd;
        iss >> cmd;

        if (cmd == "quit" || cmd == "exit") {
            break;
        } else if (cmd == "peers") {
            auto p = node.peers();
            if (p.empty()) {
                std::cout << "(no peers yet)\n";
            } else {
                for (auto& [id, addr] : p)
                    std::cout << "  " << id << "  ->  " << addr << '\n';
            }
        } else if (cmd == "publish") {
            std::string topic, text;
            iss >> topic;
            std::getline(iss >> std::ws, text);
            if (topic.empty()) {
                std::cerr << "Usage: publish <topic> <text>\n";
                continue;
            }
            node.publish(topic, text);
            std::cout << "[publish] topic=" << topic
                      << " payload=\"" << text << "\"\n";
        } else {
            std::cerr << "Unknown command: " << cmd << '\n';
        }
    }

    node.stop();
    return 0;
}
