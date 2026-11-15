#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include "dlm/api/AuthManager.h"
#include "dlm/api/ClusterClient.h"
#include "dlm/api/HttpWsServer.h"

namespace dlm {

struct ApiServerConfig {
    std::string host = "0.0.0.0";
    uint16_t    port = 8080;
    std::vector<std::string> allowedOrigins = {
        "http://localhost:5173",
        "http://127.0.0.1:5173",
        "http://localhost:3000",
        "http://127.0.0.1:3000",
        "http://localhost:8080"
    };
    std::string   authStorePath = "data/auth.json";
    ClusterConfig cluster;
};

class ApiServer {
public:
    explicit ApiServer(ApiServerConfig config);
    ~ApiServer();

    bool start();
    void stop();
    bool isRunning() const;
    uint16_t getPort() const { return config_.port; }

    ClusterClient& getClusterClient() { return cluster_; }
    AuthManager&   getAuthManager()   { return auth_; }

private:
    HttpResponse handleRequest(const HttpRequest& req);
    bool authenticateRequest(const HttpRequest& req, AuthSession& outSession) const;
    static std::string extractToken(const HttpRequest& req);
    static HttpResponse jsonResp(int status, const nlohmann::json& j);
    static HttpResponse errResp(int status, const std::string& code, const std::string& message);

    ApiServerConfig config_;
    AuthManager     auth_;
    ClusterClient   cluster_;
    HttpWsServer    http_;
};

} // namespace dlm
