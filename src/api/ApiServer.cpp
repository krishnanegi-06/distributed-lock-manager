#include "dlm/api/ApiServer.h"
#include "dlm/common/Logger.h"
#include "dlm/common/Platform.h"
#include <nlohmann/json.hpp>
#include <sstream>

namespace dlm {

namespace {

std::vector<std::string> splitPath(const std::string& path) {
    std::vector<std::string> segs;
    std::istringstream iss(path);
    std::string part;
    while (std::getline(iss, part, '/')) {
        if (!part.empty()) segs.push_back(part);
    }
    return segs;
}

bool parseU64(const std::string& s, uint64_t& out) {
    if (s.empty()) return false;
    for (char c : s) {
        if (c < '0' || c > '9') return false;
    }
    try {
        out = std::stoull(s);
        return true;
    } catch (...) {
        return false;
    }
}

bool isValidKey(const std::string& k) {
    return !k.empty() && k.size() <= 256;
}

} // namespace

ApiServer::ApiServer(ApiServerConfig config)
    : config_(std::move(config)),
      auth_(config_.authStorePath),
      cluster_(config_.cluster),
      http_(config_.host, config_.port, config_.allowedOrigins) {
    auth_.initialize();

    cluster_.setEventCallback([this](const nlohmann::json& ev) {
        http_.broadcastWsText(ev.dump());
    });

    http_.setWsAuthHandler([this](const HttpRequest& req) {
        AuthSession sess;
        return authenticateRequest(req, sess);
    });

    http_.setRequestHandler([this](const HttpRequest& req) {
        return handleRequest(req);
    });
}

ApiServer::~ApiServer() {
    stop();
}

bool ApiServer::start() {
    cluster_.start();
    if (!http_.start()) {
        cluster_.stop();
        return false;
    }
    Logger::info("API Server listening on " + config_.host + ":" + std::to_string(config_.port));
    return true;
}

void ApiServer::stop() {
    http_.stop();
    cluster_.stop();
}

bool ApiServer::isRunning() const {
    return http_.isRunning();
}

HttpResponse ApiServer::jsonResp(int status, const nlohmann::json& j) {
    HttpResponse r;
    r.status = status;
    r.contentType = "application/json";
    r.body = j.dump();
    return r;
}

HttpResponse ApiServer::errResp(int status, const std::string& code, const std::string& message) {
    return jsonResp(status, {
        {"ok", false},
        {"error", {{"code", code}, {"message", message}}}
    });
}

std::string ApiServer::extractToken(const HttpRequest& req) {
    auto it = req.headers.find("authorization");
    if (it != req.headers.end()) {
        const std::string& val = it->second;
        if (val.size() > 7 && (val.rfind("Bearer ", 0) == 0 || val.rfind("bearer ", 0) == 0)) {
            return val.substr(7);
        }
    }
    auto qit = req.queryParams.find("token");
    if (qit != req.queryParams.end()) {
        return qit->second;
    }
    return {};
}

bool ApiServer::authenticateRequest(const HttpRequest& req, AuthSession& outSession) const {
    std::string tok = extractToken(req);
    if (tok.empty()) return false;
    return auth_.validateToken(tok, outSession);
}

HttpResponse ApiServer::handleRequest(const HttpRequest& req) {
    uint64_t t0 = nowEpochMs();
    auto segs = splitPath(req.path);

    auto finish = [&](HttpResponse resp) {
        uint64_t lat = nowEpochMs() - t0;
        cluster_.recordHttpMetric(resp.status >= 400, static_cast<double>(lat));
        resp.extraHeaders["X-Request-Id"] = req.requestId;
        return resp;
    };

    // ---------------- Public Endpoints ----------------
    if (req.method == "GET" && req.path == "/api/health") {
        return finish(jsonResp(200, {
            {"ok", true},
            {"status", "UP"},
            {"timestamp", nowEpochMs()},
            {"setupComplete", auth_.isSetupComplete()}
        }));
    }

    if (req.method == "GET" && req.path == "/api/auth/status") {
        return finish(jsonResp(200, {
            {"ok", true},
            {"setupComplete", auth_.isSetupComplete()}
        }));
    }

    if (req.method == "POST" && req.path == "/api/auth/setup") {
        if (!http_.checkRateLimit(req.clientIp, "auth_setup", 10)) {
            return finish(errResp(429, "RATE_LIMITED", "Too many setup attempts"));
        }
        try {
            auto body = nlohmann::json::parse(req.body.empty() ? "{}" : req.body);
            std::string user = body.value("username", "");
            std::string pass = body.value("password", "");
            std::string err;
            if (!auth_.setupInitialAdmin(user, pass, err)) {
                return finish(errResp(400, "SETUP_FAILED", err));
            }
            std::string token;
            std::string loginErr;
            auth_.login(user, pass, token, loginErr);
            return finish(jsonResp(201, {
                {"ok", true},
                {"token", token},
                {"user", {{"username", user}, {"role", "admin"}}}
            }));
        } catch (...) {
            return finish(errResp(400, "BAD_JSON", "Invalid JSON body"));
        }
    }

    if (req.method == "POST" && req.path == "/api/auth/login") {
        if (!http_.checkRateLimit(req.clientIp, "auth_login", 20)) {
            return finish(errResp(429, "RATE_LIMITED", "Too many login attempts"));
        }
        try {
            auto body = nlohmann::json::parse(req.body.empty() ? "{}" : req.body);
            std::string user = body.value("username", "");
            std::string pass = body.value("password", "");
            std::string token, err;
            if (!auth_.login(user, pass, token, err)) {
                return finish(errResp(401, "UNAUTHORIZED", err));
            }
            return finish(jsonResp(200, {
                {"ok", true},
                {"token", token},
                {"user", {{"username", user}, {"role", "admin"}}}
            }));
        } catch (...) {
            return finish(errResp(400, "BAD_JSON", "Invalid JSON body"));
        }
    }

    // ---------------- Protected Endpoints ----------------
    AuthSession session;
    if (!authenticateRequest(req, session)) {
        return finish(errResp(401, "UNAUTHORIZED", "Valid authentication token required"));
    }

    if (req.method == "POST" && req.path == "/api/auth/logout") {
        auth_.logout(extractToken(req));
        return finish(jsonResp(200, {{"ok", true}}));
    }

    if (req.method == "GET" && req.path == "/api/auth/me") {
        return finish(jsonResp(200, {
            {"ok", true},
            {"user", {
                {"username", session.username},
                {"role", session.role},
                {"expiresAtMs", session.expiresAtMs}
            }}
        }));
    }

    // Cluster & Nodes
    if (req.method == "GET" && (req.path == "/api/cluster" || req.path == "/api/snapshot")) {
        return finish(jsonResp(200, cluster_.getClusterSnapshot()));
    }

    if (req.method == "GET" && req.path == "/api/nodes") {
        auto snap = cluster_.getClusterSnapshot();
        return finish(jsonResp(200, {{"ok", true}, {"nodes", snap["nodes"]}}));
    }

    if (segs.size() == 3 && segs[0] == "api" && segs[1] == "nodes" && req.method == "GET") {
        uint64_t nid = 0;
        if (!parseU64(segs[2], nid)) return finish(errResp(400, "INVALID_NODE", "Invalid node ID"));
        auto snap = cluster_.getClusterSnapshot();
        for (const auto& n : snap["nodes"]) {
            if (n.value("nodeId", uint64_t{0}) == nid) {
                return finish(jsonResp(200, {{"ok", true}, {"node", n}}));
            }
        }
        return finish(errResp(404, "NOT_FOUND", "Node not found"));
    }

    if (segs.size() == 4 && segs[0] == "api" && segs[1] == "nodes" && req.method == "POST") {
        if (!http_.checkRateLimit(req.clientIp, "node_admin", 30)) {
            return finish(errResp(429, "RATE_LIMITED", "Admin operation rate limit exceeded"));
        }
        uint64_t nid = 0;
        if (!parseU64(segs[2], nid)) return finish(errResp(400, "INVALID_NODE", "Invalid node ID"));
        std::string err;
        if (segs[3] == "crash") {
            if (!cluster_.crashNode(static_cast<NodeId>(nid), err)) {
                return finish(errResp(400, "CRASH_FAILED", err));
            }
            return finish(jsonResp(200, {{"ok", true}, {"nodeId", nid}, {"action", "crash"}}));
        }
        if (segs[3] == "restart") {
            if (!cluster_.restartNode(static_cast<NodeId>(nid), err)) {
                return finish(errResp(400, "RESTART_FAILED", err));
            }
            return finish(jsonResp(200, {{"ok", true}, {"nodeId", nid}, {"action", "restart"}}));
        }
    }

    // Transactions
    if (req.method == "GET" && req.path == "/api/transactions") {
        auto snap = cluster_.getClusterSnapshot();
        return finish(jsonResp(200, {{"ok", true}, {"transactions", snap["transactions"]}}));
    }

    if (req.method == "POST" && req.path == "/api/transactions") {
        try {
            auto body = nlohmann::json::parse(req.body.empty() ? "{}" : req.body);
            NodeId nid = body.value("nodeId", NodeId{1});
            std::string iso = body.value("isolation", "SERIALIZABLE");
            if (iso != "SERIALIZABLE" && iso != "READ_COMMITTED") {
                return finish(errResp(400, "INVALID_ISOLATION", "Isolation must be SERIALIZABLE or READ_COMMITTED"));
            }
            auto r = cluster_.beginTransaction(nid, iso);
            return finish(jsonResp(r.value("ok", false) ? 201 : 503, r));
        } catch (...) {
            return finish(errResp(400, "BAD_JSON", "Invalid JSON body"));
        }
    }

    if (segs.size() == 3 && segs[0] == "api" && segs[1] == "transactions" && req.method == "GET") {
        uint64_t tid = 0;
        if (!parseU64(segs[2], tid)) return finish(errResp(400, "INVALID_TXN", "Invalid transaction ID"));
        auto snap = cluster_.getClusterSnapshot();
        for (const auto& t : snap["transactions"]) {
            if (t.value("id", uint64_t{0}) == tid) {
                return finish(jsonResp(200, {{"ok", true}, {"transaction", t}}));
            }
        }
        return finish(errResp(404, "NOT_FOUND", "Transaction not found"));
    }

    if (segs.size() == 4 && segs[0] == "api" && segs[1] == "transactions" && req.method == "POST") {
        uint64_t tid = 0;
        if (!parseU64(segs[2], tid)) return finish(errResp(400, "INVALID_TXN", "Invalid transaction ID"));
        const std::string& action = segs[3];
        try {
            auto body = nlohmann::json::parse(req.body.empty() ? "{}" : req.body);
            if (action == "read") {
                std::string key = body.value("key", "");
                if (!isValidKey(key)) return finish(errResp(400, "INVALID_KEY", "Key must be 1..256 chars"));
                auto r = cluster_.readKey(tid, key);
                return finish(jsonResp(r.value("ok", false) ? 200 : 409, r));
            }
            if (action == "write") {
                std::string key = body.value("key", "");
                if (!isValidKey(key)) return finish(errResp(400, "INVALID_KEY", "Key must be 1..256 chars"));
                if (!body.contains("value")) return finish(errResp(400, "MISSING_VALUE", "Field 'value' is required"));
                std::string val = body["value"].is_string() ? body["value"].get<std::string>() : body["value"].dump();
                if (val.size() > 512 * 1024) return finish(errResp(413, "VALUE_TOO_LARGE", "Value exceeds 512 KiB"));
                auto r = cluster_.writeKey(tid, key, val);
                return finish(jsonResp(r.value("ok", false) ? 200 : 409, r));
            }
            if (action == "delete") {
                std::string key = body.value("key", "");
                if (!isValidKey(key)) return finish(errResp(400, "INVALID_KEY", "Key must be 1..256 chars"));
                auto r = cluster_.deleteKey(tid, key);
                return finish(jsonResp(r.value("ok", false) ? 200 : 409, r));
            }
            if (action == "commit") {
                auto r = cluster_.commitTransaction(tid);
                return finish(jsonResp(r.value("ok", false) ? 200 : 409, r));
            }
            if (action == "abort") {
                std::string reason = body.value("reason", "Client requested abort");
                auto r = cluster_.abortTransaction(tid, reason);
                return finish(jsonResp(r.value("ok", false) ? 200 : 409, r));
            }
        } catch (...) {
            return finish(errResp(400, "BAD_JSON", "Invalid JSON body"));
        }
    }

    // Database Explorer
    if (req.method == "GET" && req.path == "/api/database/keys") {
        std::string prefix = req.queryParams.count("prefix") ? req.queryParams.at("prefix") : "";
        NodeId filterNode = 0;
        if (req.queryParams.count("nodeId")) {
            uint64_t nid = 0;
            if (parseU64(req.queryParams.at("nodeId"), nid)) filterNode = static_cast<NodeId>(nid);
        }
        return finish(jsonResp(200, cluster_.listAllKeys(prefix, filterNode)));
    }

    if (segs.size() == 4 && segs[0] == "api" && segs[1] == "database" && segs[2] == "keys" && req.method == "GET") {
        const std::string& key = segs[3];
        if (!isValidKey(key)) return finish(errResp(400, "INVALID_KEY", "Invalid key"));
        auto r = cluster_.getKeyValue(key);
        return finish(jsonResp(r.value("ok", false) ? 200 : 503, r));
    }

    if (segs.size() == 5 && segs[0] == "api" && segs[1] == "database" && segs[2] == "keys" &&
        segs[4] == "owner" && req.method == "GET") {
        const std::string& key = segs[3];
        if (!isValidKey(key)) return finish(errResp(400, "INVALID_KEY", "Invalid key"));
        return finish(jsonResp(200, {
            {"ok", true},
            {"key", key},
            {"ownerNode", cluster_.getKeyOwner(key)}
        }));
    }

    if (req.method == "POST" && req.path == "/api/database/checkpoint") {
        try {
            auto body = nlohmann::json::parse(req.body.empty() ? "{}" : req.body);
            NodeId nid = body.value("nodeId", NodeId{1});
            auto r = cluster_.triggerCheckpoint(nid);
            return finish(jsonResp(r.value("ok", false) ? 200 : 503, r));
        } catch (...) {
            return finish(errResp(400, "BAD_JSON", "Invalid JSON body"));
        }
    }

    // SQL-like Command Console
    if (req.method == "POST" && req.path == "/api/sql") {
        try {
            auto body = nlohmann::json::parse(req.body.empty() ? "{}" : req.body);
            std::string script = body.value("command", "");
            if (script.empty() || script.size() > 32768) {
                return finish(errResp(400, "INVALID_COMMAND", "Command must be 1..32768 characters"));
            }
            NodeId nid = body.value("nodeId", NodeId{1});
            uint64_t activeTxn = body.value("activeTxnId", uint64_t{0});
            auto r = cluster_.executeSql(script, nid, activeTxn);
            return finish(jsonResp(r.value("ok", false) ? 200 : 400, r));
        } catch (...) {
            return finish(errResp(400, "BAD_JSON", "Invalid JSON body"));
        }
    }

    // Locks & Deadlocks
    if (req.method == "GET" && req.path == "/api/locks") {
        auto snap = cluster_.getClusterSnapshot();
        return finish(jsonResp(200, {{"ok", true}, {"locks", snap["locks"]}}));
    }

    if (req.method == "GET" && req.path == "/api/locks/waiting") {
        auto snap = cluster_.getClusterSnapshot();
        nlohmann::json waiting = nlohmann::json::array();
        for (const auto& l : snap["locks"]) {
            if (!l.value("waiters", nlohmann::json::array()).empty()) {
                waiting.push_back(l);
            }
        }
        return finish(jsonResp(200, {{"ok", true}, {"waiting", waiting}, {"waitEdges", snap["waitEdges"]}}));
    }

    if (req.method == "GET" && req.path == "/api/deadlocks") {
        auto snap = cluster_.getClusterSnapshot();
        return finish(jsonResp(200, {
            {"ok", true},
            {"deadlocks", snap["deadlocks"]},
            {"deadlockCount", snap["deadlockCount"]},
            {"waitEdges", snap["waitEdges"]}
        }));
    }

    if (req.method == "GET" && req.path == "/api/two-pc") {
        auto snap = cluster_.getClusterSnapshot();
        return finish(jsonResp(200, {{"ok", true}, {"twoPc", snap["twoPc"]}}));
    }

    // Logs, WAL, Recovery, Metrics
    if (req.method == "GET" && req.path == "/api/logs") {
        return finish(jsonResp(200, {{"ok", true}, {"logs", cluster_.getSystemLogsJson(300)}}));
    }

    if (req.method == "GET" && req.path == "/api/wal") {
        auto snap = cluster_.getClusterSnapshot();
        return finish(jsonResp(200, {{"ok", true}, {"wal", snap["wal"]}}));
    }

    if (req.method == "GET" && req.path == "/api/recovery") {
        auto snap = cluster_.getClusterSnapshot();
        return finish(jsonResp(200, {{"ok", true}, {"recovery", snap["recovery"]}}));
    }

    if (req.method == "GET" && req.path == "/api/metrics") {
        return finish(jsonResp(200, {{"ok", true}, {"metrics", cluster_.getMetricsJson()}}));
    }

    return finish(errResp(404, "NOT_FOUND", "Endpoint not found"));
}

} // namespace dlm
