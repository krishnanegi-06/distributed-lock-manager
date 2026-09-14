#pragma once
#include <memory>
#include <string>
#include <vector>
#include <atomic>
#include "dlm/common/Types.h"
#include "dlm/lock_manager/LockManager.h"
#include "dlm/network/NetworkManager.h"
#include "dlm/network/HeartbeatManager.h"
#include "dlm/coordinator/TwoPCCoordinator.h"
#include "dlm/deadlock/DeadlockDetector.h"
#include "dlm/transaction/TransactionManager.h"

namespace dlm {

/// Configuration for one DLM node.
struct NodeConfig {
    NodeId   nodeId;
    uint16_t port;

    struct PeerEntry {
        NodeId      id;
        std::string host;
        uint16_t    port;
    };
    std::vector<PeerEntry> peers;

    std::string logDirectory = "logs";
};

/// A single node in the distributed lock manager cluster.
/// Owns every subsystem and wires them together.
class Node {
public:
    explicit Node(const NodeConfig& config);
    ~Node();

    void start();
    void stop();

    // ---------- public transaction API ----------
    TxnId beginTransaction(
        IsolationLevel iso = IsolationLevel::SERIALIZABLE);
    bool  lockResource(TxnId txnId, const ResourceId& resourceId,
                       LockMode mode);
    bool  commitTransaction(TxnId txnId);
    bool  abortTransaction(TxnId txnId);

    // ---------- accessors ----------
    NodeId getId()       const { return config_.nodeId; }
    bool   isRunning()   const { return running_.load(); }

    LockManager&        getLockManager()        { return lockManager_; }
    NetworkManager&     getNetworkManager()     { return *networkManager_; }
    TransactionManager& getTransactionManager() { return *txnManager_; }

private:
    void setupMessageHandlers();

    // Inbound message handlers
    void onLockRequest(const Message& msg);
    void onLockRelease(const Message& msg);
    void onWfgRequest(const Message& msg);
    void onNodeFailure(NodeId failedNode);

    // Helpers
    std::vector<WaitEdge> collectGlobalEdges();

    NodeConfig        config_;
    std::atomic<bool> running_{false};

    // WFG response collection for global deadlock detection
    std::mutex              wfgMutex_;
    std::condition_variable wfgCv_;
    std::vector<WaitEdge>   pendingWfgEdges_;

    // Sub-systems (order matters for destruction)
    LockManager                          lockManager_;
    std::unique_ptr<NetworkManager>      networkManager_;
    std::unique_ptr<HeartbeatManager>    heartbeatManager_;
    std::unique_ptr<TwoPCCoordinator>    coordinator_;
    std::unique_ptr<DeadlockDetector>    deadlockDetector_;
    std::unique_ptr<TransactionManager>  txnManager_;
};

} // namespace dlm
