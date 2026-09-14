#include "dlm/node/Node.h"
#include "dlm/common/Logger.h"
#include <algorithm>

namespace dlm {

// ------------------------------------------------------------------ lifecycle

Node::Node(const NodeConfig& config)
    : config_(config)
{
    // Network
    networkManager_ = std::make_unique<NetworkManager>(config_.nodeId,
                                                       config_.port);
    for (auto& p : config_.peers)
        networkManager_->addPeer(p.id, p.host, p.port);

    // Heartbeat
    heartbeatManager_ = std::make_unique<HeartbeatManager>(
        config_.nodeId, *networkManager_);
    heartbeatManager_->setFailureCallback(
        [this](NodeId failed) { onNodeFailure(failed); });

    // Transactions (with WAL)
    std::string walPath;
    if (!config_.logDirectory.empty())
        walPath = config_.logDirectory + "/node_"
                  + std::to_string(config_.nodeId) + "_wal.log";
    txnManager_ = std::make_unique<TransactionManager>(lockManager_, walPath);

    // 2PC
    coordinator_ = std::make_unique<TwoPCCoordinator>(config_.nodeId,
                                                       *networkManager_);
    coordinator_->registerParticipantHandlers(
        [this](TxnId t) { return txnManager_->prepareTransaction(t); },
        [this](TxnId t) { txnManager_->commitPrepared(t); },
        [this](TxnId t) { txnManager_->abortPrepared(t); });

    // Deadlock detector
    deadlockDetector_ = std::make_unique<DeadlockDetector>();
}

Node::~Node() { stop(); }

void Node::start() {
    if (running_.load()) return;

    Logger::info("Node " + std::to_string(config_.nodeId) + " starting on port "
                 + std::to_string(config_.port));

    setupMessageHandlers();
    networkManager_->start();
    heartbeatManager_->start();

    // Periodic deadlock detection every 2 seconds
    deadlockDetector_->startPeriodicDetection(
        std::chrono::milliseconds(2000),
        /* getLocalEdges */
        [this]() { return lockManager_.getWaitForEdges(); },
        /* getGlobalEdges */
        [this]() { return collectGlobalEdges(); },
        /* onAbort */
        [this](TxnId victim) {
            Logger::warn("Deadlock victim: txn " + std::to_string(victim));
            txnManager_->abortTransaction(victim);
        });

    running_.store(true);
    Logger::info("Node " + std::to_string(config_.nodeId) + " started.");
}

void Node::stop() {
    if (!running_.load()) return;
    Logger::info("Node " + std::to_string(config_.nodeId) + " stopping...");

    deadlockDetector_->stopPeriodicDetection();
    heartbeatManager_->stop();
    networkManager_->stop();

    running_.store(false);
    Logger::info("Node " + std::to_string(config_.nodeId) + " stopped.");
}

// ------------------------------------------------------- public transaction API

TxnId Node::beginTransaction(IsolationLevel iso) {
    return txnManager_->beginTransaction(iso);
}

bool Node::lockResource(TxnId txnId, const ResourceId& resourceId,
                         LockMode mode) {
    return txnManager_->acquireLock(txnId, resourceId, mode);
}

bool Node::commitTransaction(TxnId txnId) {
    Transaction* txn = txnManager_->getTransaction(txnId);
    if (!txn) return false;

    if (txn->isDistributed()) {
        // Use 2PC for distributed transactions
        std::vector<NodeId> parts(txn->getParticipantNodes().begin(),
                                  txn->getParticipantNodes().end());
        bool ok = coordinator_->coordinateCommit(txnId, parts);
        if (ok) {
            txnManager_->commitTransaction(txnId);
        } else {
            txnManager_->abortTransaction(txnId);
        }
        return ok;
    }
    return txnManager_->commitTransaction(txnId);
}

bool Node::abortTransaction(TxnId txnId) {
    return txnManager_->abortTransaction(txnId);
}

// ------------------------------------------------------- message handlers

void Node::setupMessageHandlers() {
    auto& net = *networkManager_;

    // Lock operations
    net.registerHandler(MessageType::LOCK_REQUEST,
        [this](const Message& m) { onLockRequest(m); });
    net.registerHandler(MessageType::LOCK_RELEASE,
        [this](const Message& m) { onLockRelease(m); });

    // 2PC messages
    net.registerHandler(MessageType::PREPARE,
        [this](const Message& m) { coordinator_->handleMessage(m); });
    net.registerHandler(MessageType::VOTE_YES,
        [this](const Message& m) { coordinator_->handleMessage(m); });
    net.registerHandler(MessageType::VOTE_NO,
        [this](const Message& m) { coordinator_->handleMessage(m); });
    net.registerHandler(MessageType::GLOBAL_COMMIT,
        [this](const Message& m) { coordinator_->handleMessage(m); });
    net.registerHandler(MessageType::GLOBAL_ABORT,
        [this](const Message& m) { coordinator_->handleMessage(m); });

    // Deadlock
    net.registerHandler(MessageType::WFG_REQUEST,
        [this](const Message& m) { onWfgRequest(m); });
    net.registerHandler(MessageType::ABORT_TXN,
        [this](const Message& m) {
            Logger::warn("Received remote ABORT for txn "
                         + std::to_string(m.txnId));
            txnManager_->abortTransaction(m.txnId);
        });

    // Heartbeat (handled by HeartbeatManager internally via its own
    // registration, but we also wire WFG_RESPONSE here)
    net.registerHandler(MessageType::WFG_RESPONSE,
        [this](const Message& m) {
            // Handled asynchronously by collectGlobalEdges()
            std::lock_guard<std::mutex> lk(wfgMutex_);
            for (auto& e : m.waitEdges)
                pendingWfgEdges_.push_back(e);
            wfgCv_.notify_all();
        });
}

void Node::onLockRequest(const Message& msg) {
    bool granted = lockManager_.acquireLock(
        msg.txnId, msg.resourceId, msg.lockMode,
        std::chrono::milliseconds(5000));

    Message resp;
    resp.type       = MessageType::LOCK_RESPONSE;
    resp.senderId   = config_.nodeId;
    resp.txnId      = msg.txnId;
    resp.resourceId = msg.resourceId;
    resp.success    = granted;
    networkManager_->sendMessage(msg.senderId, resp);
}

void Node::onLockRelease(const Message& msg) {
    lockManager_.releaseLock(msg.txnId, msg.resourceId);
}

void Node::onWfgRequest(const Message& msg) {
    auto edges = lockManager_.getWaitForEdges();

    Message resp;
    resp.type      = MessageType::WFG_RESPONSE;
    resp.senderId  = config_.nodeId;
    resp.waitEdges = edges;
    networkManager_->sendMessage(msg.senderId, resp);
}

void Node::onNodeFailure(NodeId failedNode) {
    Logger::error("Node " + std::to_string(failedNode)
                  + " detected as FAILED — cleaning up locks.");
    // In a full implementation we would identify all transactions
    // originating from that node and abort them.  For this demo we
    // log the event.
}

std::vector<WaitEdge> Node::collectGlobalEdges() {
    // Send WFG_REQUEST to every peer and wait for responses
    {
        std::lock_guard<std::mutex> lk(wfgMutex_);
        pendingWfgEdges_.clear();
    }

    Message req;
    req.type     = MessageType::WFG_REQUEST;
    req.senderId = config_.nodeId;
    networkManager_->broadcast(req);

    // Wait up to 1 second for responses
    auto peers = networkManager_->getPeerIds();
    if (peers.empty()) return {};

    {
        std::unique_lock<std::mutex> lk(wfgMutex_);
        wfgCv_.wait_for(lk, std::chrono::milliseconds(1000));
    }

    std::lock_guard<std::mutex> lk(wfgMutex_);

    // Merge with local edges
    auto localEdges = lockManager_.getWaitForEdges();
    std::vector<WaitEdge> all;
    all.reserve(localEdges.size() + pendingWfgEdges_.size());
    all.insert(all.end(), localEdges.begin(), localEdges.end());
    all.insert(all.end(), pendingWfgEdges_.begin(), pendingWfgEdges_.end());
    return all;
}

} // namespace dlm
