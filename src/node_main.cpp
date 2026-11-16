// ---------------------------------------------------------------------------
// dlm_node - Start an autonomous Distributed Transactional KV Database Node.
//
// Usage:
//   dlm_node <node_id> <port> [--data-dir <path>] [--log-dir <path>]
//            [--daemon] [peer_id:host:port ...]
// ---------------------------------------------------------------------------
#include "dlm/node/Node.h"
#include "dlm/common/Logger.h"
#include <atomic>
#include <chrono>
#include <csignal>
#include <filesystem>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>

namespace {
std::atomic<bool> g_stopRequested{false};

void signalHandler(int) {
    g_stopRequested.store(true);
}
} // namespace

int main(int argc, char* argv[]) {
    if (argc < 3) {
        std::cerr << "Usage: dlm_node <node_id> <port> [--data-dir <dir>] [--log-dir <dir>] [--daemon] [peer_id:host:port ...]\n";
        return 1;
    }

    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);

    dlm::NodeConfig cfg;
    cfg.nodeId = static_cast<dlm::NodeId>(std::stoul(argv[1]));
    cfg.port   = static_cast<uint16_t>(std::stoul(argv[2]));
    cfg.dataDirectory = "data";
    cfg.logDirectory  = "logs";

    bool daemonMode = false;

    for (int i = 3; i < argc; ++i) {
        std::string arg(argv[i]);
        if (arg == "--data-dir" && i + 1 < argc) {
            cfg.dataDirectory = argv[++i];
        } else if (arg == "--log-dir" && i + 1 < argc) {
            cfg.logDirectory = argv[++i];
        } else if (arg == "--daemon") {
            daemonMode = true;
        } else {
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
                std::cerr << "Invalid argument or peer format: " << arg
                          << " (expected id:host:port)\n";
            }
        }
    }

    std::filesystem::create_directories(cfg.logDirectory);
    std::filesystem::create_directories(cfg.dataDirectory);

    dlm::Logger::setLevel(dlm::LogLevel::INFO);
    dlm::Logger::setLogFile(cfg.logDirectory + "/node_" + std::to_string(cfg.nodeId) + ".log");
    dlm::Logger::info("Starting DLM DB node " + std::to_string(cfg.nodeId)
                      + " on port " + std::to_string(cfg.port)
                      + " dataDir=" + cfg.dataDirectory
                      + " peers=" + std::to_string(cfg.peers.size()));

    dlm::Node node(cfg);
    node.start();

    if (!node.isRunning()) {
        dlm::Logger::error("Node failed to start.");
        return 1;
    }

    if (daemonMode) {
        dlm::Logger::info("Node running in daemon mode (awaiting SIGINT/SIGTERM)...");
        while (!g_stopRequested.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }
    } else {
        dlm::Logger::info("Node running. Send SIGINT/SIGTERM or press ENTER to stop...");
        std::thread inputThread([&]() {
            std::cin.get();
            g_stopRequested.store(true);
        });
        inputThread.detach();
        while (!g_stopRequested.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }
    }

    node.stop();
    dlm::Logger::info("Node stopped cleanly.");
    return 0;
}
