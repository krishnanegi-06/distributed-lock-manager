#pragma once
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>
#include <nlohmann/json.hpp>
#include "dlm/common/Types.h"
#include "dlm/storage/ShardRouter.h"

namespace dlm {

struct NodeEndpointConfig {
    NodeId      nodeId = 1;
    std::string host   = "127.0.0.1";
    uint16_t    port   = 9001;
};

struct ClusterConfig {
    std::vector<NodeEndpointConfig> nodes;
    std::string nodeBinaryPath;
    std::string dataDirectory = "data";
    std::string logDirectory  = "logs";
    bool        superviseNodes = false;
};

struct ApiMetrics {
    uint64_t totalRequests = 0;
    uint64_t totalErrors   = 0;
    uint64_t totalTxnsStarted = 0;
    uint64_t totalTxnsCommitted = 0;
    uint64_t totalTxnsAborted = 0;
    double   avgTxnLatencyMs = 0.0;
    double   throughputTxnsPerSec = 0.0;
    uint64_t uptimeMs = 0;
};

/// Communicates with the 3 independent database nodes over TCP CTRL_RPC_REQ,
/// optionally supervises child node processes, tracks metrics, and emits
/// real-time events whenever backend state changes.
class ClusterClient {
public:
    using EventBroadcastFn = std::function<void(const nlohmann::json& event)>;

    explicit ClusterClient(ClusterConfig config);
    ~ClusterClient();

    void start();
    void stop();

    void setEventCallback(EventBroadcastFn cb);

    /// Spawn all configured node processes (only when superviseNodes == true).
    bool startAllSupervisedNodes();
    void stopAllSupervisedNodes();

    /// Crash / restart a specific node.
    bool crashNode(NodeId nodeId, std::string& outError);
    bool restartNode(NodeId nodeId, std::string& outError);

    /// Aggregate snapshot from all nodes (used by /api/cluster and sub-endpoints).
    nlohmann::json getClusterSnapshot();

    /// Transactional operations routed to the coordinator node (`coordinatorNodeId`, default 1 or first healthy node).
    nlohmann::json beginTransaction(NodeId coordinatorNodeId, const std::string& isolation);
    nlohmann::json readKey(TxnId txnId, const std::string& key);
    nlohmann::json writeKey(TxnId txnId, const std::string& key, const std::string& value);
    nlohmann::json deleteKey(TxnId txnId, const std::string& key);
    nlohmann::json commitTransaction(TxnId txnId);
    nlohmann::json abortTransaction(TxnId txnId, const std::string& reason);

    /// Key lookup & shard ownership
    NodeId getKeyOwner(const std::string& key) const;
    nlohmann::json getKeyValue(const std::string& key);
    nlohmann::json listAllKeys(const std::string& prefix, NodeId filterNode);
    nlohmann::json triggerCheckpoint(NodeId nodeId);

    /// SQL-like command console execution
    nlohmann::json executeSql(const std::string& script,
                              NodeId preferredNodeId,
                              uint64_t activeTxnId = 0);

    /// Observability helpers
    nlohmann::json getMetricsJson();
    nlohmann::json getSystemLogsJson(size_t limit = 200);
    void recordHttpMetric(bool isError, double latencyMs);
    void appendLog(const std::string& level, NodeId nodeId, const std::string& message);

private:
    bool sendCtrlRpc(NodeId nodeId, const nlohmann::json& req, nlohmann::json& outResp,
                     std::chrono::milliseconds timeout = std::chrono::milliseconds(5000));
    NodeId pickHealthyNode(NodeId preferred = 0) const;
    bool spawnNodeProcess(const NodeEndpointConfig& ep);
    void killNodeProcess(NodeId nodeId, bool abruptCrash);
    void pollLoop();
    void emitEvent(const std::string& type, NodeId nodeId, TxnId txnId, const nlohmann::json& payload);
    void diffAndEmitEvents(const nlohmann::json& prev, const nlohmann::json& curr);

    ClusterConfig    config_;
    ShardRouter      shardRouter_;
    std::atomic<bool> running_{false};
    std::thread      pollThread_;
    std::mutex       pollSleepMutex_;
    std::condition_variable pollSleepCv_;

    EventBroadcastFn eventCb_;
    std::mutex       eventCbMutex_;
    std::atomic<uint64_t> nextEventId_{1};

    mutable std::mutex stateMutex_;
    nlohmann::json     lastSnapshot_;
    std::unordered_map<NodeId, bool> crashedNodes_;

    // Track which coordinator node owns active TxnIds
    mutable std::mutex txnCoordMutex_;
    std::unordered_map<TxnId, NodeId> txnCoordinatorMap_;

    // Metrics
    mutable std::mutex metricsMutex_;
    uint64_t startTimeMs_ = 0;
    uint64_t httpRequests_ = 0;
    uint64_t httpErrors_ = 0;
    uint64_t txnsStarted_ = 0;
    uint64_t txnsCommitted_ = 0;
    uint64_t txnsAborted_ = 0;
    double   txnLatencySumMs_ = 0.0;
    uint64_t txnLatencyCount_ = 0;

    // Recent API/cluster event logs
    mutable std::mutex logsMutex_;
    std::deque<nlohmann::json> systemLogs_;

    // Supervised child process handles
    struct ChildProc {
#ifdef _WIN32
        void* hProcess = nullptr;
        unsigned long pid = 0;
#else
        int pid = -1;
#endif
    };
    std::mutex procMutex_;
    std::unordered_map<NodeId, ChildProc> childProcs_;
};

} // namespace dlm
