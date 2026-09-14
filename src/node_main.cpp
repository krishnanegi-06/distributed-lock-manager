// ---------------------------------------------------------------------------
// dlm_node – Start a single DLM node from the command line.
//
// Usage:  dlm_node <node_id> <port> [peer_id:host:port ...]
// Example:  dlm_node 1 9001 2:127.0.0.1:9002 3:127.0.0.1:9003
// ---------------------------------------------------------------------------
#include "dlm/node/Node.h"
#include "dlm/common/Logger.h"
#include <iostream>
#include <string>
#include <sstream>
#include <filesystem>

int main(int argc, char* argv[]) {
    if (argc < 3) {
        std::cerr << "Usage: dlm_node <node_id> <port> [peer_id:host:port ...]\n";
        return 1;
    }

    dlm::NodeConfig cfg;
    cfg.nodeId = static_cast<dlm::NodeId>(std::stoul(argv[1]));
    cfg.port   = static_cast<uint16_t>(std::stoul(argv[2]));
    cfg.logDirectory = "logs";

    // Parse optional peers
    for (int i = 3; i < argc; ++i) {
        std::string arg(argv[i]);
        // Format: id:host:port
        std::istringstream ss(arg);
        std::string idStr, host, portStr;
        if (std::getline(ss, idStr, ':') &&
            std::getline(ss, host, ':') &&
            std::getline(ss, portStr, ':')) {
            dlm::NodeConfig::PeerEntry p;
            p.id   = static_cast<dlm::NodeId>(std::stoul(idStr));
            p.host = host;
            p.port = static_cast<uint16_t>(std::stoul(portStr));
            cfg.peers.push_back(p);
        } else {
            std::cerr << "Invalid peer format: " << arg << " (expected id:host:port)\n";
        }
    }

    std::filesystem::create_directories(cfg.logDirectory);

    dlm::Logger::setLevel(dlm::LogLevel::INFO);
    dlm::Logger::info("Starting DLM node " + std::to_string(cfg.nodeId)
                      + " on port " + std::to_string(cfg.port)
                      + " with " + std::to_string(cfg.peers.size()) + " peer(s)");

    dlm::Node node(cfg);
    node.start();

    dlm::Logger::info("Node running. Press ENTER to stop...");
    std::cin.get();

    node.stop();
    dlm::Logger::info("Node stopped.");
    return 0;
}