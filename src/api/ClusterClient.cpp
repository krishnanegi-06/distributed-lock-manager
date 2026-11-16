#include "dlm/api/ClusterClient.h"
#include "dlm/common/Logger.h"
#include "dlm/common/Platform.h"
#include "dlm/network/NetworkManager.h"
#include "dlm/sql/CommandParser.h"
#include <algorithm>
#include <sstream>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <csignal>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace dlm {

ClusterClient::ClusterClient(ClusterConfig config)
    : config_(std::move(config)) {
    std::vector<NodeId> ids;
    for (const auto& n : config_.nodes) ids.push_back(n.nodeId);
    shardRouter_.setNodes(std::move(ids));
    startTimeMs_ = nowEpochMs();
}

ClusterClient::~ClusterClient() {
    stop();
}

void ClusterClient::setEventCallback(EventBroadcastFn cb) {
    std::lock_guard<std::mutex> lock(eventCbMutex_);
    eventCb_ = std::move(cb);
}

void ClusterClient::emitEvent(const std::string& type, NodeId nodeId, TxnId txnId,
                              const nlohmann::json& payload) {
    nlohmann::json ev = {
        {"eventId", nextEventId_.fetch_add(1)},
        {"timestamp", nowEpochMs()},
        {"type", type},
        {"nodeId", nodeId},
        {"payload", payload}
    };
    if (txnId != 0) {
        ev["txnId"] = txnId;
    }

    EventBroadcastFn cb;
    {
        std::lock_guard<std::mutex> lock(eventCbMutex_);
        cb = eventCb_;
    }
    if (cb) {
        try {
            cb(ev);
        } catch (...) {}
    }
}

void ClusterClient::appendLog(const std::string& level, NodeId nodeId, const std::string& message) {
    std::lock_guard<std::mutex> lock(logsMutex_);
    systemLogs_.push_back({
        {"timestamp", nowEpochMs()},
        {"level", level},
        {"nodeId", nodeId},
        {"message", message}
    });
    while (systemLogs_.size() > 500) {
        systemLogs_.pop_front();
    }
}

void ClusterClient::start() {
    if (running_.exchange(true)) return;
    if (config_.superviseNodes) {
        startAllSupervisedNodes();
        std::this_thread::sleep_for(std::chrono::milliseconds(600));
    }
    pollThread_ = std::thread(&ClusterClient::pollLoop, this);
}

void ClusterClient::stop() {
    if (!running_.exchange(false)) return;
    pollSleepCv_.notify_all();
    if (pollThread_.joinable()) {
        pollThread_.join();
    }
    if (config_.superviseNodes) {
        stopAllSupervisedNodes();
    }
}

bool ClusterClient::spawnNodeProcess(const NodeEndpointConfig& ep) {
    if (config_.nodeBinaryPath.empty()) return false;

    std::vector<std::string> peerArgs;
    for (const auto& other : config_.nodes) {
        if (other.nodeId != ep.nodeId) {
            peerArgs.push_back(std::to_string(other.nodeId) + ":" + other.host + ":" + std::to_string(other.port));
        }
    }

#ifdef _WIN32
    std::ostringstream cmd;
    cmd << "\"" << config_.nodeBinaryPath << "\" "
        << ep.nodeId << " " << ep.port
        << " --data-dir \"" << config_.dataDirectory << "\""
        << " --log-dir \"" << config_.logDirectory << "\""
        << " --daemon";
    for (const auto& pa : peerArgs) cmd << " " << pa;

    std::string cmdStr = cmd.str();
    STARTUPINFOA si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    std::vector<char> mutableCmd(cmdStr.begin(), cmdStr.end());
    mutableCmd.push_back('\0');

    if (!CreateProcessA(nullptr, mutableCmd.data(), nullptr, nullptr, FALSE,
                        CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
        return false;
    }
    CloseHandle(pi.hThread);
    std::lock_guard<std::mutex> lock(procMutex_);
    childProcs_[ep.nodeId] = ChildProc{pi.hProcess, pi.dwProcessId};
    return true;
#else
    pid_t pid = fork();
    if (pid < 0) return false;
    if (pid == 0) {
        std::vector<std::string> args = {
            config_.nodeBinaryPath,
            std::to_string(ep.nodeId),
            std::to_string(ep.port),
            "--data-dir", config_.dataDirectory,
            "--log-dir", config_.logDirectory,
            "--daemon"
        };
        for (const auto& pa : peerArgs) args.push_back(pa);
        std::vector<char*> cargs;
        for (auto& s : args) cargs.push_back(const_cast<char*>(s.c_str()));
        cargs.push_back(nullptr);
        execvp(config_.nodeBinaryPath.c_str(), cargs.data());
        _exit(127);
    }
    std::lock_guard<std::mutex> lock(procMutex_);
    childProcs_[ep.nodeId] = ChildProc{pid};
    return true;
#endif
}

void ClusterClient::killNodeProcess(NodeId nodeId, bool abruptCrash) {
    ChildProc cp;
    {
        std::lock_guard<std::mutex> lock(procMutex_);
        auto it = childProcs_.find(nodeId);
        if (it == childProcs_.end()) return;
        cp = it->second;
        childProcs_.erase(it);
    }
#ifdef _WIN32
    if (cp.hProcess) {
        HANDLE h = static_cast<HANDLE>(cp.hProcess);
        if (abruptCrash) {
            TerminateProcess(h, 9);
        } else {
            TerminateProcess(h, 0);
        }
        WaitForSingleObject(h, 3000);
        CloseHandle(h);
    }
#else
    if (cp.pid > 0) {
        ::kill(cp.pid, abruptCrash ? SIGKILL : SIGTERM);
        int status = 0;
        waitpid(cp.pid, &status, 0);
    }
#endif
}

bool ClusterClient::startAllSupervisedNodes() {
    bool allOk = true;
    for (const auto& ep : config_.nodes) {
        if (!spawnNodeProcess(ep)) {
            allOk = false;
            appendLog("ERROR", ep.nodeId, "Failed to spawn supervised node process");
        } else {
            appendLog("INFO", ep.nodeId, "Spawned supervised node process on port " + std::to_string(ep.port));
        }
    }
    return allOk;
}

void ClusterClient::stopAllSupervisedNodes() {
    for (const auto& ep : config_.nodes) {
        killNodeProcess(ep.nodeId, false);
    }
}

bool ClusterClient::sendCtrlRpc(NodeId nodeId, const nlohmann::json& req,
                                nlohmann::json& outResp,
                                std::chrono::milliseconds timeout) {
    const NodeEndpointConfig* target = nullptr;
    for (const auto& n : config_.nodes) {
        if (n.nodeId == nodeId) {
            target = &n;
            break;
        }
    }
    if (!target) return false;

    {
        std::lock_guard<std::mutex> lock(stateMutex_);
        if (crashedNodes_[nodeId]) return false;
    }

    Message m;
    m.type = MessageType::CTRL_RPC_REQ;
    m.senderId = 0;
    m.requestId = 1;
    m.value = req.dump();

    Message resp;
    if (!NetworkManager::requestEndpoint(target->host, target->port, m, resp, timeout)) {
        return false;
    }
    try {
        outResp = nlohmann::json::parse(resp.value.empty() ? "{}" : resp.value);
        return resp.success;
    } catch (...) {
        return false;
    }
}

bool ClusterClient::crashNode(NodeId nodeId, std::string& outError) {
    const NodeEndpointConfig* target = nullptr;
    for (const auto& n : config_.nodes) {
        if (n.nodeId == nodeId) { target = &n; break; }
    }
    if (!target) {
        outError = "Unknown nodeId";
        return false;
    }

    if (config_.superviseNodes) {
        killNodeProcess(nodeId, true);
    } else {
        std::lock_guard<std::mutex> lock(stateMutex_);
        crashedNodes_[nodeId] = true;
    }
    appendLog("WARN", nodeId, "Simulated node CRASH executed");
    emitEvent("node.status_changed", nodeId, 0, {{"status", "FAILED"}, {"reason", "Simulated crash"}});
    return true;
}

bool ClusterClient::restartNode(NodeId nodeId, std::string& outError) {
    const NodeEndpointConfig* target = nullptr;
    for (const auto& n : config_.nodes) {
        if (n.nodeId == nodeId) { target = &n; break; }
    }
    if (!target) {
        outError = "Unknown nodeId";
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(stateMutex_);
        crashedNodes_[nodeId] = false;
    }

    if (config_.superviseNodes) {
        killNodeProcess(nodeId, false);
        if (!spawnNodeProcess(*target)) {
            outError = "Failed to spawn node process";
            return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }

    appendLog("INFO", nodeId, "Node restarted and WAL recovery triggered");
    emitEvent("recovery.completed", nodeId, 0, {{"nodeId", nodeId}});
    emitEvent("node.status_changed", nodeId, 0, {{"status", "HEALTHY"}});
    return true;
}

NodeId ClusterClient::getKeyOwner(const std::string& key) const {
    return shardRouter_.getOwnerNode(key);
}

NodeId ClusterClient::pickHealthyNode(NodeId preferred) const {
    if (preferred != 0) {
        std::lock_guard<std::mutex> lock(stateMutex_);
        auto it = crashedNodes_.find(preferred);
        if (it == crashedNodes_.end() || !it->second) return preferred;
    }
    for (const auto& n : config_.nodes) {
        std::lock_guard<std::mutex> lock(stateMutex_);
        auto it = crashedNodes_.find(n.nodeId);
        if (it == crashedNodes_.end() || !it->second) return n.nodeId;
    }
    return config_.nodes.empty() ? 1 : config_.nodes.front().nodeId;
}

nlohmann::json ClusterClient::getClusterSnapshot() {
    nlohmann::json nodesArr = nlohmann::json::array();
    nlohmann::json allRecords = nlohmann::json::array();
    nlohmann::json allLocks = nlohmann::json::array();
    nlohmann::json allWaitEdges = nlohmann::json::array();
    nlohmann::json allTxns = nlohmann::json::array();
    nlohmann::json allTwoPc = nlohmann::json::array();
    nlohmann::json allDeadlocks = nlohmann::json::array();
    nlohmann::json allWal = nlohmann::json::array();
    nlohmann::json allRecovery = nlohmann::json::array();

    int healthyCount = 0;
    int failedCount = 0;
    uint64_t totalDeadlocks = 0;

    for (const auto& ep : config_.nodes) {
        nlohmann::json nodeState;
        bool reachable = sendCtrlRpc(ep.nodeId, {{"cmd", "state"}}, nodeState,
                                     std::chrono::milliseconds(1500));
        if (!reachable) {
            failedCount++;
            nodesArr.push_back({
                {"nodeId", ep.nodeId},
                {"port", ep.port},
                {"status", "FAILED"},
                {"reachable", false},
                {"keyCount", 0},
                {"activeLocks", 0},
                {"activeTxns", 0}
            });
            continue;
        }

        healthyCount++;
        auto recs = nodeState.value("records", nlohmann::json::array());
        auto locks = nodeState.value("locks", nlohmann::json::array());
        auto edges = nodeState.value("waitEdges", nlohmann::json::array());
        auto txns = nodeState.value("transactions", nlohmann::json::array());
        auto twoPc = nodeState.value("twoPc", nlohmann::json::array());
        auto deadlocks = nodeState.value("deadlocks", nlohmann::json::array());
        auto wal = nodeState.value("wal", nlohmann::json::array());
        auto recov = nodeState.value("recovery", nlohmann::json::object());
        recov["nodeId"] = ep.nodeId;
        allRecovery.push_back(recov);

        size_t activeTxnCount = 0;
        for (auto& t : txns) {
            std::string st = t.value("state", "");
            if (st == "ACTIVE" || st == "WAITING" || st == "PREPARING" || st == "PREPARED") {
                activeTxnCount++;
            }
            allTxns.push_back(t);
        }

        for (auto& r : recs) allRecords.push_back(r);
        for (auto& l : locks) allLocks.push_back(l);
        for (auto& e : edges) allWaitEdges.push_back(e);
        for (auto& tp : twoPc) allTwoPc.push_back(tp);
        for (auto& dd : deadlocks) allDeadlocks.push_back(dd);
        for (auto& w : wal) allWal.push_back(w);
        totalDeadlocks += nodeState.value("deadlockCount", uint64_t{0});

        nodesArr.push_back({
            {"nodeId", ep.nodeId},
            {"port", ep.port},
            {"status", "HEALTHY"},
            {"reachable", true},
            {"storage", nodeState.value("storage", nlohmann::json::object())},
            {"keyCount", recs.size()},
            {"activeLocks", locks.size()},
            {"activeTxns", activeTxnCount},
            {"peers", nodeState.value("peers", nlohmann::json::array())}
        });
    }

    // Deduplicate transactions across coordinator/participants by TxnId (prefer coordinator's view)
    std::unordered_map<TxnId, nlohmann::json> mergedTxns;
    for (const auto& t : allTxns) {
        TxnId tid = t.value("id", uint64_t{0});
        NodeId coord = t.value("coordinatorNode", NodeId{0});
        auto it = mergedTxns.find(tid);
        if (it == mergedTxns.end()) {
            mergedTxns[tid] = t;
        } else {
            // Keep entry that has more operations or participants
            if (t.value("operations", nlohmann::json::array()).size() >
                it->second.value("operations", nlohmann::json::array()).size()) {
                mergedTxns[tid] = t;
            }
        }
        if (coord != 0) {
            std::lock_guard<std::mutex> tk(txnCoordMutex_);
            txnCoordinatorMap_[tid] = coord;
        }
    }
    nlohmann::json dedupTxns = nlohmann::json::array();
    for (auto& [id, t] : mergedTxns) dedupTxns.push_back(std::move(t));
    std::sort(dedupTxns.begin(), dedupTxns.end(), [](const nlohmann::json& a, const nlohmann::json& b) {
        return a.value("startEpochMs", uint64_t{0}) > b.value("startEpochMs", uint64_t{0});
    });

    nlohmann::json snap = {
        {"timestamp", nowEpochMs()},
        {"totalNodes", config_.nodes.size()},
        {"healthyNodes", healthyCount},
        {"failedNodes", failedCount},
        {"nodes", nodesArr},
        {"records", allRecords},
        {"locks", allLocks},
        {"waitEdges", allWaitEdges},
        {"transactions", dedupTxns},
        {"twoPc", allTwoPc},
        {"deadlocks", allDeadlocks},
        {"deadlockCount", totalDeadlocks},
        {"wal", allWal},
        {"recovery", allRecovery},
        {"metrics", getMetricsJson()}
    };

    {
        std::lock_guard<std::mutex> lock(stateMutex_);
        lastSnapshot_ = snap;
    }
    return snap;
}

nlohmann::json ClusterClient::beginTransaction(NodeId coordinatorNodeId, const std::string& isolation) {
    NodeId target = pickHealthyNode(coordinatorNodeId);
    nlohmann::json resp;
    if (!sendCtrlRpc(target, {{"cmd", "begin"}, {"isolation", isolation}}, resp)) {
        return {{"ok", false}, {"error", "Target node " + std::to_string(target) + " is unavailable"}};
    }
    TxnId tid = resp.value("txnId", uint64_t{0});
    {
        std::lock_guard<std::mutex> lock(txnCoordMutex_);
        txnCoordinatorMap_[tid] = target;
    }
    {
        std::lock_guard<std::mutex> mk(metricsMutex_);
        txnsStarted_++;
    }
    appendLog("INFO", target, "BEGIN transaction " + std::to_string(tid));
    emitEvent("transaction.created", target, tid, {{"isolation", isolation}});
    return {{"ok", true}, {"txnId", tid}, {"coordinatorNode", target}, {"state", "ACTIVE"}};
}

nlohmann::json ClusterClient::readKey(TxnId txnId, const std::string& key) {
    NodeId coord = txnOriginNode(txnId);
    if (coord == 0) {
        std::lock_guard<std::mutex> lock(txnCoordMutex_);
        auto it = txnCoordinatorMap_.find(txnId);
        coord = (it != txnCoordinatorMap_.end()) ? it->second : pickHealthyNode(0);
    }
    nlohmann::json resp;
    if (!sendCtrlRpc(coord, {{"cmd", "get"}, {"txnId", txnId}, {"key", key}}, resp,
                     std::chrono::milliseconds(7000))) {
        return {{"ok", false}, {"status", "NODE_UNAVAILABLE"}, {"error", "Coordinator node unavailable"}};
    }
    return resp;
}

nlohmann::json ClusterClient::writeKey(TxnId txnId, const std::string& key, const std::string& value) {
    NodeId coord = txnOriginNode(txnId);
    if (coord == 0) {
        std::lock_guard<std::mutex> lock(txnCoordMutex_);
        auto it = txnCoordinatorMap_.find(txnId);
        coord = (it != txnCoordinatorMap_.end()) ? it->second : pickHealthyNode(0);
    }
    nlohmann::json resp;
    if (!sendCtrlRpc(coord, {{"cmd", "put"}, {"txnId", txnId}, {"key", key}, {"value", value}}, resp,
                     std::chrono::milliseconds(7000))) {
        return {{"ok", false}, {"status", "NODE_UNAVAILABLE"}, {"error", "Coordinator node unavailable"}};
    }
    return resp;
}

nlohmann::json ClusterClient::deleteKey(TxnId txnId, const std::string& key) {
    NodeId coord = txnOriginNode(txnId);
    if (coord == 0) {
        std::lock_guard<std::mutex> lock(txnCoordMutex_);
        auto it = txnCoordinatorMap_.find(txnId);
        coord = (it != txnCoordinatorMap_.end()) ? it->second : pickHealthyNode(0);
    }
    nlohmann::json resp;
    if (!sendCtrlRpc(coord, {{"cmd", "delete"}, {"txnId", txnId}, {"key", key}}, resp,
                     std::chrono::milliseconds(7000))) {
        return {{"ok", false}, {"status", "NODE_UNAVAILABLE"}, {"error", "Coordinator node unavailable"}};
    }
    return resp;
}

nlohmann::json ClusterClient::commitTransaction(TxnId txnId) {
    NodeId coord = txnOriginNode(txnId);
    if (coord == 0) {
        std::lock_guard<std::mutex> lock(txnCoordMutex_);
        auto it = txnCoordinatorMap_.find(txnId);
        coord = (it != txnCoordinatorMap_.end()) ? it->second : pickHealthyNode(0);
    }
    uint64_t t0 = nowEpochMs();
    nlohmann::json resp;
    bool rpcOk = sendCtrlRpc(coord, {{"cmd", "commit"}, {"txnId", txnId}}, resp,
                             std::chrono::milliseconds(8000));
    uint64_t elapsed = nowEpochMs() - t0;
    bool committed = rpcOk && resp.value("ok", false);
    {
        std::lock_guard<std::mutex> mk(metricsMutex_);
        if (committed) txnsCommitted_++;
        else txnsAborted_++;
        txnLatencySumMs_ += static_cast<double>(elapsed);
        txnLatencyCount_++;
    }
    if (committed) {
        appendLog("INFO", coord, "COMMIT transaction " + std::to_string(txnId));
        emitEvent("transaction.committed", coord, txnId, {{"latencyMs", elapsed}});
    } else {
        appendLog("WARN", coord, "COMMIT failed for transaction " + std::to_string(txnId));
    }
    return {{"ok", committed}, {"txnId", txnId}, {"state", committed ? "COMMITTED" : "ABORTED"}};
}

nlohmann::json ClusterClient::abortTransaction(TxnId txnId, const std::string& reason) {
    NodeId coord = txnOriginNode(txnId);
    if (coord == 0) {
        std::lock_guard<std::mutex> lock(txnCoordMutex_);
        auto it = txnCoordinatorMap_.find(txnId);
        coord = (it != txnCoordinatorMap_.end()) ? it->second : pickHealthyNode(0);
    }
    nlohmann::json resp;
    bool rpcOk = sendCtrlRpc(coord, {{"cmd", "abort"}, {"txnId", txnId}, {"reason", reason}}, resp,
                             std::chrono::milliseconds(5000));
    bool aborted = rpcOk && resp.value("ok", false);
    {
        std::lock_guard<std::mutex> mk(metricsMutex_);
        if (aborted) txnsAborted_++;
    }
    if (aborted) {
        appendLog("WARN", coord, "ABORT transaction " + std::to_string(txnId) + " (" + reason + ")");
        emitEvent("transaction.aborted", coord, txnId, {{"reason", reason}});
    }
    return {{"ok", aborted}, {"txnId", txnId}, {"state", "ABORTED"}};
}

nlohmann::json ClusterClient::getKeyValue(const std::string& key) {
    NodeId owner = getKeyOwner(key);
    auto beg = beginTransaction(owner, "SERIALIZABLE");
    if (!beg.value("ok", false)) {
        return {{"ok", false}, {"ownerNode", owner}, {"error", beg.value("error", "Shard owner unavailable")}};
    }
    TxnId tid = beg["txnId"].get<TxnId>();
    auto r = readKey(tid, key);
    commitTransaction(tid);
    r["ownerNode"] = owner;
    return r;
}

nlohmann::json ClusterClient::listAllKeys(const std::string& prefix, NodeId filterNode) {
    auto snap = getClusterSnapshot();
    nlohmann::json filtered = nlohmann::json::array();
    for (const auto& rec : snap["records"]) {
        std::string k = rec.value("key", "");
        NodeId owner = rec.value("ownerNode", NodeId{0});
        if (!prefix.empty() && k.compare(0, prefix.size(), prefix) != 0) continue;
        if (filterNode != 0 && owner != filterNode) continue;
        filtered.push_back(rec);
    }
    return {{"ok", true}, {"keys", filtered}, {"count", filtered.size()}};
}

nlohmann::json ClusterClient::triggerCheckpoint(NodeId nodeId) {
    nlohmann::json resp;
    bool ok = sendCtrlRpc(nodeId, {{"cmd", "checkpoint"}}, resp, std::chrono::milliseconds(5000));
    if (ok && resp.value("ok", false)) {
        appendLog("INFO", nodeId, "Checkpoint and WAL compaction completed");
        return {{"ok", true}, {"nodeId", nodeId}};
    }
    return {{"ok", false}, {"nodeId", nodeId}, {"error", "Checkpoint failed or node unavailable"}};
}

nlohmann::json ClusterClient::executeSql(const std::string& script,
                                         NodeId preferredNodeId,
                                         uint64_t activeTxnId) {
    auto parsed = CommandParser::parse(script);
    if (!parsed.ok) {
        return {
            {"ok", false},
            {"error", parsed.error},
            {"line", parsed.errorLine},
            {"column", parsed.errorColumn}
        };
    }

    TxnId currentTxn = activeTxnId;
    nlohmann::json results = nlohmann::json::array();
    bool allOk = true;

    for (const auto& cmd : parsed.commands) {
        nlohmann::json step;
        switch (cmd.type) {
            case SqlCommandType::BEGIN: {
                std::string iso = (cmd.isolation == IsolationLevel::READ_COMMITTED)
                                  ? "READ_COMMITTED" : "SERIALIZABLE";
                auto r = beginTransaction(preferredNodeId, iso);
                if (r.value("ok", false)) {
                    currentTxn = r["txnId"].get<TxnId>();
                } else {
                    allOk = false;
                }
                step = {{"command", "BEGIN"}, {"result", r}};
                break;
            }
            case SqlCommandType::PUT: {
                bool autoTxn = (currentTxn == 0);
                if (autoTxn) {
                    auto b = beginTransaction(preferredNodeId, "SERIALIZABLE");
                    if (!b.value("ok", false)) {
                        allOk = false;
                        step = {{"command", "PUT"}, {"key", cmd.key}, {"result", b}};
                        break;
                    }
                    currentTxn = b["txnId"].get<TxnId>();
                }
                auto r = writeKey(currentTxn, cmd.key, cmd.value);
                if (!r.value("ok", false)) allOk = false;
                if (autoTxn) {
                    if (r.value("ok", false)) commitTransaction(currentTxn);
                    else abortTransaction(currentTxn, "Auto-txn PUT failed");
                    currentTxn = 0;
                }
                step = {{"command", "PUT"}, {"key", cmd.key}, {"ownerNode", getKeyOwner(cmd.key)}, {"result", r}};
                break;
            }
            case SqlCommandType::GET: {
                bool autoTxn = (currentTxn == 0);
                if (autoTxn) {
                    auto b = beginTransaction(preferredNodeId, "SERIALIZABLE");
                    if (!b.value("ok", false)) {
                        allOk = false;
                        step = {{"command", "GET"}, {"key", cmd.key}, {"result", b}};
                        break;
                    }
                    currentTxn = b["txnId"].get<TxnId>();
                }
                auto r = readKey(currentTxn, cmd.key);
                if (!r.value("ok", false)) allOk = false;
                if (autoTxn) {
                    commitTransaction(currentTxn);
                    currentTxn = 0;
                }
                step = {{"command", "GET"}, {"key", cmd.key}, {"ownerNode", getKeyOwner(cmd.key)}, {"result", r}};
                break;
            }
            case SqlCommandType::DEL: {
                bool autoTxn = (currentTxn == 0);
                if (autoTxn) {
                    auto b = beginTransaction(preferredNodeId, "SERIALIZABLE");
                    if (!b.value("ok", false)) {
                        allOk = false;
                        step = {{"command", "DELETE"}, {"key", cmd.key}, {"result", b}};
                        break;
                    }
                    currentTxn = b["txnId"].get<TxnId>();
                }
                auto r = deleteKey(currentTxn, cmd.key);
                if (!r.value("ok", false)) allOk = false;
                if (autoTxn) {
                    if (r.value("ok", false)) commitTransaction(currentTxn);
                    else abortTransaction(currentTxn, "Auto-txn DELETE failed");
                    currentTxn = 0;
                }
                step = {{"command", "DELETE"}, {"key", cmd.key}, {"ownerNode", getKeyOwner(cmd.key)}, {"result", r}};
                break;
            }
            case SqlCommandType::EXISTS: {
                bool autoTxn = (currentTxn == 0);
                if (autoTxn) {
                    auto b = beginTransaction(preferredNodeId, "SERIALIZABLE");
                    currentTxn = b.value("txnId", uint64_t{0});
                }
                auto r = readKey(currentTxn, cmd.key);
                if (autoTxn && currentTxn != 0) {
                    commitTransaction(currentTxn);
                    currentTxn = 0;
                }
                step = {{"command", "EXISTS"}, {"key", cmd.key}, {"exists", r.value("found", false)}, {"result", r}};
                break;
            }
            case SqlCommandType::SCAN: {
                auto r = listAllKeys(cmd.key, 0);
                step = {{"command", "SCAN"}, {"prefix", cmd.key}, {"result", r}};
                break;
            }
            case SqlCommandType::COMMIT: {
                if (currentTxn == 0) {
                    allOk = false;
                    step = {{"command", "COMMIT"}, {"result", {{"ok", false}, {"error", "No active transaction"}}}};
                } else {
                    auto r = commitTransaction(currentTxn);
                    if (!r.value("ok", false)) allOk = false;
                    currentTxn = 0;
                    step = {{"command", "COMMIT"}, {"result", r}};
                }
                break;
            }
            case SqlCommandType::ABORT: {
                if (currentTxn == 0) {
                    allOk = false;
                    step = {{"command", "ABORT"}, {"result", {{"ok", false}, {"error", "No active transaction"}}}};
                } else {
                    auto r = abortTransaction(currentTxn, "SQL console ABORT");
                    currentTxn = 0;
                    step = {{"command", "ABORT"}, {"result", r}};
                }
                break;
            }
            case SqlCommandType::SHOW_LOCKS: {
                auto snap = getClusterSnapshot();
                step = {{"command", "SHOW LOCKS"}, {"result", {{"ok", true}, {"locks", snap["locks"]}}}};
                break;
            }
            case SqlCommandType::SHOW_TRANSACTIONS: {
                auto snap = getClusterSnapshot();
                step = {{"command", "SHOW TRANSACTIONS"}, {"result", {{"ok", true}, {"transactions", snap["transactions"]}}}};
                break;
            }
            case SqlCommandType::SHOW_NODES: {
                auto snap = getClusterSnapshot();
                step = {{"command", "SHOW NODES"}, {"result", {{"ok", true}, {"nodes", snap["nodes"]}}}};
                break;
            }
        }
        results.push_back(std::move(step));
        if (!allOk) break;
    }

    return {
        {"ok", allOk},
        {"activeTxnId", currentTxn},
        {"results", results}
    };
}

void ClusterClient::recordHttpMetric(bool isError, double latencyMs) {
    (void)latencyMs;
    std::lock_guard<std::mutex> lock(metricsMutex_);
    httpRequests_++;
    if (isError) httpErrors_++;
}

nlohmann::json ClusterClient::getMetricsJson() {
    std::lock_guard<std::mutex> lock(metricsMutex_);
    uint64_t upMs = nowEpochMs() - startTimeMs_;
    double upSec = std::max(1.0, static_cast<double>(upMs) / 1000.0);
    double avgLat = txnLatencyCount_ > 0 ? (txnLatencySumMs_ / static_cast<double>(txnLatencyCount_)) : 0.0;
    double tput = static_cast<double>(txnsCommitted_) / upSec;
    return {
        {"uptimeMs", upMs},
        {"httpRequests", httpRequests_},
        {"httpErrors", httpErrors_},
        {"txnsStarted", txnsStarted_},
        {"txnsCommitted", txnsCommitted_},
        {"txnsAborted", txnsAborted_},
        {"avgTxnLatencyMs", avgLat},
        {"throughputTxnsPerSec", tput}
    };
}

nlohmann::json ClusterClient::getSystemLogsJson(size_t limit) {
    std::lock_guard<std::mutex> lock(logsMutex_);
    nlohmann::json arr = nlohmann::json::array();
    size_t start = systemLogs_.size() > limit ? (systemLogs_.size() - limit) : 0;
    for (size_t i = start; i < systemLogs_.size(); ++i) {
        arr.push_back(systemLogs_[i]);
    }
    return arr;
}

void ClusterClient::diffAndEmitEvents(const nlohmann::json& prev, const nlohmann::json& curr) {
    if (prev.is_null() || !prev.is_object()) return;

    // Node status diffs
    std::unordered_map<NodeId, std::string> prevNodes;
    for (const auto& n : prev.value("nodes", nlohmann::json::array())) {
        prevNodes[n.value("nodeId", NodeId{0})] = n.value("status", "");
    }
    for (const auto& n : curr.value("nodes", nlohmann::json::array())) {
        NodeId nid = n.value("nodeId", NodeId{0});
        std::string st = n.value("status", "");
        auto it = prevNodes.find(nid);
        if (it != prevNodes.end() && it->second != st) {
            emitEvent("node.status_changed", nid, 0, {{"status", st}, {"previous", it->second}});
        }
    }

    // Deadlock count diffs
    uint64_t prevDd = prev.value("deadlockCount", uint64_t{0});
    uint64_t currDd = curr.value("deadlockCount", uint64_t{0});
    if (currDd > prevDd) {
        emitEvent("deadlock.detected", 0, 0, {{"count", currDd}});
    }

    // WAL count diffs
    size_t prevWal = prev.value("wal", nlohmann::json::array()).size();
    size_t currWal = curr.value("wal", nlohmann::json::array()).size();
    if (currWal > prevWal) {
        emitEvent("wal.record_written", 0, 0, {{"records", currWal}});
    }
}

void ClusterClient::pollLoop() {
    nlohmann::json prevSnap;
    while (running_.load()) {
        nlohmann::json currSnap = getClusterSnapshot();
        diffAndEmitEvents(prevSnap, currSnap);
        prevSnap = std::move(currSnap);

        std::unique_lock<std::mutex> lk(pollSleepMutex_);
        pollSleepCv_.wait_for(lk, std::chrono::milliseconds(800), [this]() { return !running_.load(); });
    }
}

} // namespace dlm
