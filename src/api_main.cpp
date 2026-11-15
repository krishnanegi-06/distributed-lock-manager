// ---------------------------------------------------------------------------
// dlm_api - C++ HTTP REST + WebSocket Application API Gateway
//
// Usage:
//   dlm_api [--port 8080] [--config config/nodes.json] [--supervise]
//           [--node-bin ./dlm_node] [--data-dir data] [--log-dir logs]
// ---------------------------------------------------------------------------
#include "dlm/api/ApiServer.h"
#include "dlm/common/Logger.h"
#include <nlohmann/json.hpp>
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <thread>

namespace {
std::atomic<bool> g_stop{false};
void onSig(int) { g_stop.store(true); }

std::string getEnvStr(const char* name) {
#ifdef _WIN32
    char* buf = nullptr;
    size_t sz = 0;
    if (_dupenv_s(&buf, &sz, name) == 0 && buf != nullptr) {
        std::string v(buf);
        free(buf);
        return v;
    }
    return {};
#else
    const char* v = std::getenv(name);
    return v ? std::string(v) : std::string{};
#endif
}

std::vector<dlm::NodeEndpointConfig> parseNodesEnv(const std::string& raw) {
    std::vector<dlm::NodeEndpointConfig> out;
    std::istringstream iss(raw);
    std::string item;
    while (std::getline(iss, item, ',')) {
        std::istringstream parts(item);
        std::string idStr, host, portStr;
        if (std::getline(parts, idStr, ':') &&
            std::getline(parts, host, ':') &&
            std::getline(parts, portStr, ':')) {
            dlm::NodeEndpointConfig ep;
            ep.nodeId = static_cast<dlm::NodeId>(std::stoul(idStr));
            ep.host = host;
            ep.port = static_cast<uint16_t>(std::stoul(portStr));
            out.push_back(ep);
        }
    }
    return out;
}
} // namespace

int main(int argc, char* argv[]) {
    std::signal(SIGINT, onSig);
    std::signal(SIGTERM, onSig);

    dlm::ApiServerConfig cfg;
    std::string configPath;

    std::string envPort = getEnvStr("API_PORT");
    if (!envPort.empty()) cfg.port = static_cast<uint16_t>(std::stoul(envPort));

    std::string envData = getEnvStr("DLM_DATA_DIR");
    if (!envData.empty()) {
        cfg.cluster.dataDirectory = envData;
        cfg.authStorePath = envData + "/auth.json";
    }
    std::string envLog = getEnvStr("DLM_LOG_DIR");
    if (!envLog.empty()) cfg.cluster.logDirectory = envLog;

    std::string envSupervise = getEnvStr("DLM_SUPERVISE");
    if (envSupervise == "1" || envSupervise == "true") cfg.cluster.superviseNodes = true;

    std::string envNodeBin = getEnvStr("DLM_NODE_BIN");
    if (!envNodeBin.empty()) cfg.cluster.nodeBinaryPath = envNodeBin;

    std::string envCors = getEnvStr("DLM_CORS_ORIGINS");
    if (!envCors.empty()) {
        cfg.allowedOrigins.clear();
        std::istringstream iss(envCors);
        std::string orig;
        while (std::getline(iss, orig, ',')) {
            if (!orig.empty()) cfg.allowedOrigins.push_back(orig);
        }
    }

    for (int i = 1; i < argc; ++i) {
        std::string arg(argv[i]);
        if (arg == "--port" && i + 1 < argc) {
            cfg.port = static_cast<uint16_t>(std::stoul(argv[++i]));
        } else if (arg == "--host" && i + 1 < argc) {
            cfg.host = argv[++i];
        } else if (arg == "--config" && i + 1 < argc) {
            configPath = argv[++i];
        } else if (arg == "--supervise") {
            cfg.cluster.superviseNodes = true;
        } else if (arg == "--node-bin" && i + 1 < argc) {
            cfg.cluster.nodeBinaryPath = argv[++i];
        } else if (arg == "--data-dir" && i + 1 < argc) {
            cfg.cluster.dataDirectory = argv[++i];
            cfg.authStorePath = cfg.cluster.dataDirectory + "/auth.json";
        } else if (arg == "--log-dir" && i + 1 < argc) {
            cfg.cluster.logDirectory = argv[++i];
        }
    }

    if (!configPath.empty() && std::filesystem::exists(configPath)) {
        std::ifstream f(configPath);
        if (f.is_open()) {
            auto j = nlohmann::json::parse(f);
            if (j.contains("nodes") && j["nodes"].is_array()) {
                for (const auto& n : j["nodes"]) {
                    cfg.cluster.nodes.push_back({
                        n.value("nodeId", dlm::NodeId{1}),
                        n.value("host", std::string{"127.0.0.1"}),
                        n.value("port", uint16_t{9001})
                    });
                }
            }
        }
    }

    if (cfg.cluster.nodes.empty()) {
        std::string envNodes = getEnvStr("DLM_NODES");
        if (!envNodes.empty()) {
            cfg.cluster.nodes = parseNodesEnv(envNodes);
        }
    }

    if (cfg.cluster.nodes.empty()) {
        cfg.cluster.nodes = {
            {1, "127.0.0.1", 9001},
            {2, "127.0.0.1", 9002},
            {3, "127.0.0.1", 9003}
        };
    }

    std::filesystem::create_directories(cfg.cluster.dataDirectory);
    std::filesystem::create_directories(cfg.cluster.logDirectory);

    dlm::Logger::setLevel(dlm::LogLevel::INFO);
    dlm::Logger::setLogFile(cfg.cluster.logDirectory + "/dlm_api.log");

    dlm::ApiServer server(cfg);
    if (!server.start()) {
        dlm::Logger::error("Failed to start API server on port " + std::to_string(cfg.port));
        return 1;
    }

    dlm::Logger::info("dlm_api running. Press Ctrl+C (SIGINT/SIGTERM) to stop...");
    while (!g_stop.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    server.stop();
    dlm::Logger::info("dlm_api stopped.");
    return 0;
}
