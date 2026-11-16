#pragma once
#include <atomic>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include "dlm/common/Types.h"
#include "dlm/coordinator/TwoPCCoordinator.h"
#include "dlm/deadlock/DeadlockDetector.h"
#include "dlm/lock_manager/LockManager.h"
#include "dlm/network/HeartbeatManager.h"
#include "dlm/network/NetworkManager.h"
#include "dlm/storage/ShardRouter.h"
#include "dlm/storage/StorageEngine.h"
#include "dlm/transaction/TransactionManager.h"

namespace dlm {

/// Configuration for one DLM database node.
struct NodeConfig {
    NodeId   nodeId = 1;
    uint16_t port   = 9001;

    struct PeerEntry {
        NodeId      id = 0;
        std::string host;
        uint16_t    port = 0;
    };
    std::vector<PeerEntry> peers;

    std::string logDirectory  = "logs";
    std::string dataDirectory = "data";
    std::chrono::milliseconds deadlockCheckInterval{1000};
    std::chrono::milliseconds heartbeatInterval{1000};
    std::chrono::milliseconds heartbeatTimeout{4000};
    std::chrono::milliseconds lockTimeout{5000};
};

struct TxnOpResult {
    bool        ok = false;
    OpStatus    status = OpStatus::OK;
    bool        found = false;
    std::string value;
    NodeId      ownerNode = 0;
    std::string error;
};

/// A single autonomous node in the distributed transactional KV database cluster.
class Node {
public:
    using EventCallback = std::function<void(const std::string& eventType,
                                             NodeId nodeId,
                                             TxnId txnId,
                                             const std::string& detail)>;

    explicit Node(const NodeConfig& config);
    ~Node();

    void start();
    void stop();

    void setEventCallback(EventCallback cb);

    // ---------- public transaction & KV API ----------
    TxnId       beginTransaction(IsolationLevel iso = IsolationLevel::SERIALIZABLE);
    bool        lockResource(TxnId txnId, const ResourceId& resourceId, LockMode mode,
                             OpStatus* outStatus = nullptr);
    TxnOpResult get(TxnId txnId, const std::string& key);
    TxnOpResult put(TxnId txnId, const std::string& key, const std::string& value);
    TxnOpResult remove(TxnId txnId, const std::string& key);
    bool        commitTransaction(TxnId txnId);
    bool        abortTransaction(TxnId txnId, const std::string& reason = "");

    // Direct local storage read/scan helpers (committed data only)
    std::vector<StoredRecord> scanLocal(const std::string& prefix = "",
                                        size_t limit = 1000,
                                        size_t offset = 0) const;
    NodeId getKeyOwner(const std::string& key) const;

    // ---------- accessors ----------
    NodeId getId()       const { return config_.nodeId; }
    uint16_t getPort()   const { return config_.port; }
    bool   isRunning()   const { return running_.load(); }
    const NodeConfig& getConfig() const { return config_; }

    LockManager&        getLockManager()        { return lockManager_; }
    NetworkManager&     getNetworkManager()     { return *networkManager_; }
    HeartbeatManager&   getHeartbeatManager()   { return *heartbeatManager_; }
    TwoPCCoordinator&   getTwoPCCoordinator()   { return *coordinator_; }
    DeadlockDetector&   getDeadlockDetector()   { return *deadlockDetector_; }
    TransactionManager& getTransactionManager() { return *txnManager_; }
    StorageEngine&      getStorageEngine()      { return storageEngine_; }
    const ShardRouter&  getShardRouter()  const { return shardRouter_; }

    /// Resolve any in-doubt PREPARED transactions by querying their coordinator.
    size_t resolveInDoubtTransactions();

    /// Export full JSON state snapshot (for observability / CTRL_RPC_REQ).
    std::string exportStateJson() const;

private:
    void setupMessageHandlers();
    void emitEvent(const std::string& type, TxnId txnId, const std::string& detail);

    // Inbound message handlers
    void onLockRequest(const Message& msg);
    void onLockRelease(const Message& msg);
    void onLockReleaseAll(const Message& msg);
    void onKvGetReq(const Message& msg);
    void onKvPutReq(const Message& msg);
    void onKvDeleteReq(const Message& msg);
    void onKvScanReq(const Message& msg);
    void onWfgRequest(const Message& msg);
    Message onCtrlRpcReq(const Message& msg);
    void onNodeFailure(NodeId failedNode);
    void onNodeRecovered(NodeId recoveredNode);

    // Helpers
    std::vector<WaitEdge> collectGlobalEdges();
    void handleDeadlockVictim(TxnId victim);

    NodeConfig        config_;
    std::atomic<bool> running_{false};
    EventCallback     eventCb_;
    std::mutex        eventCbMutex_;

    // WFG response collection for global deadlock detection
    std::mutex              wfgMutex_;
    std::condition_variable wfgCv_;
    uint64_t                wfgRoundId_ = 0;
    std::unordered_set<NodeId> wfgRespondedPeers_;
    std::vector<WaitEdge>   pendingWfgEdges_;

    // Track remote locks acquired by locally coordinated transactions
    std::mutex remoteLocksMutex_;
    std::unordered_map<TxnId, std::vector<std::pair<NodeId, ResourceId>>> remoteLocks_;

    // Sub-systems (destruction order is reverse of declaration)
    StorageEngine                        storageEngine_;
    ShardRouter                          shardRouter_;
    LockManager                          lockManager_;
    std::unique_ptr<NetworkManager>      networkManager_;
    std::unique_ptr<HeartbeatManager>    heartbeatManager_;
    std::unique_ptr<TransactionManager>  txnManager_;
    std::unique_ptr<TwoPCCoordinator>    coordinator_;
    std::unique_ptr<DeadlockDetector>    deadlockDetector_;
};

} // namespace dlm
