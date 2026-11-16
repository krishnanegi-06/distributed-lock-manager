#include "dlm/node/Node.h"
#include "dlm/common/Logger.h"
#include "dlm/common/Platform.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <filesystem>

namespace dlm {

Node::Node(const NodeConfig& config)
    : config_(config) {
    // 1. Storage Engine (data/node<id>/{database,wal,checkpoints})
    storageEngine_.initialize(config_.dataDirectory, config_.nodeId);

    // 2. Deterministic FNV-1a Shard Router
    std::vector<NodeId> clusterNodes = {config_.nodeId};
    for (const auto& p : config_.peers) {
        clusterNodes.push_back(p.id);
    }
    shardRouter_.setNodes(std::move(clusterNodes));

    // 3. Network
    networkManager_ = std::make_unique<NetworkManager>(config_.nodeId, config_.port);
    for (const auto& p : config_.peers) {
        networkManager_->addPeer(p.id, p.host, p.port);
    }

    // 4. Heartbeat
    heartbeatManager_ = std::make_unique<HeartbeatManager>(
        config_.nodeId, *networkManager_,
        config_.heartbeatInterval, config_.heartbeatTimeout);
    heartbeatManager_->setFailureCallback(
        [this](NodeId failed) { onNodeFailure(failed); });
    heartbeatManager_->setRecoveryCallback(
        [this](NodeId recovered) { onNodeRecovered(recovered); });

    // 5. TransactionManager (backed by StorageEngine + durable WAL)
    std::string walPath = storageEngine_.getWalDir() + "/wal.log";
    txnManager_ = std::make_unique<TransactionManager>(
        lockManager_, walPath, config_.nodeId, &storageEngine_);
    txnManager_->setLockTimeout(config_.lockTimeout);
    txnManager_->setEventCallback([this](const std::string& type, TxnId tid, const std::string& detail) {
        emitEvent(type, tid, detail);
    });

    // 6. Durable 2PC Coordinator
    std::string decisionPath = storageEngine_.getWalDir() + "/2pc_decisions.log";
    coordinator_ = std::make_unique<TwoPCCoordinator>(
        config_.nodeId, *networkManager_, std::chrono::milliseconds(5000),
        txnManager_->getTransactionLog(), decisionPath);
    coordinator_->registerParticipantHandlers(
        [this](TxnId t) { return txnManager_->prepareTransaction(t); },
        [this](TxnId t) { txnManager_->commitPrepared(t); },
        [this](TxnId t) { txnManager_->abortPrepared(t); });
    coordinator_->setEventCallback([this](const std::string& type, TxnId tid, const std::string& detail) {
        emitEvent(type, tid, detail);
    });

    // 7. Deadlock Detector
    deadlockDetector_ = std::make_unique<DeadlockDetector>();
}

Node::~Node() {
    stop();
}

void Node::setEventCallback(EventCallback cb) {
    std::lock_guard<std::mutex> lock(eventCbMutex_);
    eventCb_ = std::move(cb);
}

void Node::emitEvent(const std::string& type, TxnId txnId, const std::string& detail) {
    EventCallback cb;
    {
        std::lock_guard<std::mutex> lock(eventCbMutex_);
        cb = eventCb_;
    }
    if (cb) {
        try {
            cb(type, config_.nodeId, txnId, detail);
        } catch (...) {}
    }
}

void Node::start() {
    if (running_.load()) return;

    Logger::info("Node " + std::to_string(config_.nodeId) + " starting on port "
                 + std::to_string(config_.port));

    // Step 1: Crash recovery from WAL & StorageEngine checkpoints before accepting traffic
    emitEvent("recovery.started", 0, "Recovering WAL and storage");
    RecoveryReport rec = txnManager_->recoverFromWal();
    emitEvent("recovery.completed", 0,
              "Redone=" + std::to_string(rec.redoneTransactions) +
              " Undone=" + std::to_string(rec.undoneTransactions) +
              " InDoubt=" + std::to_string(rec.inDoubtTransactions));

    // Step 2: Setup handlers & start network/heartbeats
    setupMessageHandlers();
    if (!networkManager_->start()) {
        Logger::error("Node " + std::to_string(config_.nodeId) + " failed to bind network port.");
        return;
    }
    heartbeatManager_->start();

    // Step 3: Retry any unresolved coordinator decisions and query in-doubt participant txns
    coordinator_->recoverCoordinatorDecisions();
    resolveInDoubtTransactions();

    // Step 4: Periodic local + global deadlock detection
    deadlockDetector_->startPeriodicDetection(
        config_.deadlockCheckInterval,
        [this]() { return lockManager_.getWaitForEdges(); },
        [this]() { return collectGlobalEdges(); },
        [this](TxnId victim) { handleDeadlockVictim(victim); },
        [this](const DeadlockCycle& dc) {
            emitEvent("deadlock.detected", dc.victim,
                      "Cycle size=" + std::to_string(dc.cycle.size()) +
                      " victim=" + std::to_string(dc.victim));
        });

    running_.store(true);
    emitEvent("node.status_changed", 0, "HEALTHY");
    Logger::info("Node " + std::to_string(config_.nodeId) + " started.");
}

void Node::stop() {
    if (!running_.exchange(false)) return;
    Logger::info("Node " + std::to_string(config_.nodeId) + " stopping...");

    deadlockDetector_->stopPeriodicDetection();
    heartbeatManager_->stop();
    networkManager_->stop();

    emitEvent("node.status_changed", 0, "SHUTDOWN");
    Logger::info("Node " + std::to_string(config_.nodeId) + " stopped.");
}

// ------------------------------------------------------- public transaction API

TxnId Node::beginTransaction(IsolationLevel iso) {
    return txnManager_->beginTransaction(iso);
}

NodeId Node::getKeyOwner(const std::string& key) const {
    return shardRouter_.getOwnerNode(key);
}

std::vector<StoredRecord> Node::scanLocal(const std::string& prefix,
                                          size_t limit,
                                          size_t offset) const {
    return storageEngine_.scan(prefix, limit, offset);
}

bool Node::lockResource(TxnId txnId, const ResourceId& resourceId,
                        LockMode mode, OpStatus* outStatus) {
    NodeId owner = shardRouter_.getOwnerNode(resourceId);
    if (owner == 0 || owner == config_.nodeId) {
        return txnManager_->acquireLock(txnId, resourceId, mode, outStatus);
    }

    // Remote shard owner
    NodeHealthState hs = heartbeatManager_->getNodeState(owner);
    if (hs == NodeHealthState::FAILED || hs == NodeHealthState::SHUTDOWN) {
        if (outStatus) *outStatus = OpStatus::NODE_UNAVAILABLE;
        return false;
    }

    Message req;
    req.type = MessageType::LOCK_REQUEST;
    req.senderId = config_.nodeId;
    req.txnId = txnId;
    req.resourceId = resourceId;
    req.lockMode = mode;

    Message resp;
    if (!networkManager_->sendRequest(owner, req, resp, config_.lockTimeout + std::chrono::milliseconds(1000))) {
        if (outStatus) *outStatus = OpStatus::NODE_UNAVAILABLE;
        return false;
    }

    if (resp.success) {
        txnManager_->addParticipant(txnId, owner);
        {
            std::lock_guard<std::mutex> lk(remoteLocksMutex_);
            remoteLocks_[txnId].emplace_back(owner, resourceId);
        }
        if (Transaction* t = txnManager_->getTransaction(txnId)) {
            t->addOperation(TxnOperation{
                TxnOperation::OpType::LOCK_ACQUIRE, resourceId, mode, "", "", false, owner
            });
        }
        if (outStatus) *outStatus = OpStatus::OK;
        return true;
    }

    if (outStatus) {
        *outStatus = (resp.status != OpStatus::OK) ? resp.status : OpStatus::LOCK_TIMEOUT;
    }
    return false;
}

TxnOpResult Node::get(TxnId txnId, const std::string& key) {
    TxnOpResult res;
    if (key.empty()) {
        res.status = OpStatus::INVALID_ARGUMENT;
        res.error = "Key cannot be empty";
        return res;
    }

    Transaction* txn = txnManager_->getTransaction(txnId);
    if (!txn || txn->getState() != TxnState::ACTIVE) {
        res.status = OpStatus::INVALID_STATE;
        res.error = "Transaction is not ACTIVE";
        return res;
    }

    NodeId owner = shardRouter_.getOwnerNode(key);
    res.ownerNode = owner;

    if (owner == 0 || owner == config_.nodeId) {
        std::string val;
        bool found = false;
        OpStatus st = txnManager_->readKey(txnId, key, val, found);
        res.status = st;
        res.ok = (st == OpStatus::OK);
        res.found = found;
        res.value = std::move(val);
        if (!res.ok) res.error = to_string(st);
        return res;
    }

    NodeHealthState hs = heartbeatManager_->getNodeState(owner);
    if (hs == NodeHealthState::FAILED || hs == NodeHealthState::SHUTDOWN) {
        res.status = OpStatus::NODE_UNAVAILABLE;
        res.error = "Shard owner node " + std::to_string(owner) + " is unavailable";
        return res;
    }

    Message req;
    req.type = MessageType::KV_GET_REQ;
    req.senderId = config_.nodeId;
    req.txnId = txnId;
    req.key = key;
    req.resourceId = key;

    Message resp;
    if (!networkManager_->sendRequest(owner, req, resp, config_.lockTimeout + std::chrono::milliseconds(1000))) {
        res.status = OpStatus::NODE_UNAVAILABLE;
        res.error = "Failed to reach shard owner node " + std::to_string(owner);
        return res;
    }

    res.ok = resp.success;
    res.status = resp.status;
    res.found = resp.found;
    res.value = resp.value;
    res.error = resp.error;
    if (res.ok) {
        txnManager_->addParticipant(txnId, owner);
        if (Transaction* t = txnManager_->getTransaction(txnId)) {
            t->addOperation(TxnOperation{
                TxnOperation::OpType::READ, key, LockMode::SHARED,
                res.found ? res.value : "", "", res.found, owner
            });
        }
    }
    return res;
}

TxnOpResult Node::put(TxnId txnId, const std::string& key, const std::string& value) {
    TxnOpResult res;
    if (key.empty()) {
        res.status = OpStatus::INVALID_ARGUMENT;
        res.error = "Key cannot be empty";
        return res;
    }

    Transaction* txn = txnManager_->getTransaction(txnId);
    if (!txn || txn->getState() != TxnState::ACTIVE) {
        res.status = OpStatus::INVALID_STATE;
        res.error = "Transaction is not ACTIVE";
        return res;
    }

    NodeId owner = shardRouter_.getOwnerNode(key);
    res.ownerNode = owner;

    if (owner == 0 || owner == config_.nodeId) {
        OpStatus st = txnManager_->writeKey(txnId, key, value);
        res.status = st;
        res.ok = (st == OpStatus::OK);
        if (!res.ok) res.error = to_string(st);
        return res;
    }

    NodeHealthState hs = heartbeatManager_->getNodeState(owner);
    if (hs == NodeHealthState::FAILED || hs == NodeHealthState::SHUTDOWN) {
        res.status = OpStatus::NODE_UNAVAILABLE;
        res.error = "Shard owner node " + std::to_string(owner) + " is unavailable";
        return res;
    }

    Message req;
    req.type = MessageType::KV_PUT_REQ;
    req.senderId = config_.nodeId;
    req.txnId = txnId;
    req.key = key;
    req.resourceId = key;
    req.value = value;

    Message resp;
    if (!networkManager_->sendRequest(owner, req, resp, config_.lockTimeout + std::chrono::milliseconds(1000))) {
        res.status = OpStatus::NODE_UNAVAILABLE;
        res.error = "Failed to reach shard owner node " + std::to_string(owner);
        return res;
    }

    res.ok = resp.success;
    res.status = resp.status;
    res.error = resp.error;
    if (res.ok) {
        txnManager_->addParticipant(txnId, owner);
        if (Transaction* t = txnManager_->getTransaction(txnId)) {
            t->addOperation(TxnOperation{
                TxnOperation::OpType::WRITE, key, LockMode::EXCLUSIVE,
                value, "", false, owner
            });
        }
    }
    return res;
}

TxnOpResult Node::remove(TxnId txnId, const std::string& key) {
    TxnOpResult res;
    if (key.empty()) {
        res.status = OpStatus::INVALID_ARGUMENT;
        res.error = "Key cannot be empty";
        return res;
    }

    Transaction* txn = txnManager_->getTransaction(txnId);
    if (!txn || txn->getState() != TxnState::ACTIVE) {
        res.status = OpStatus::INVALID_STATE;
        res.error = "Transaction is not ACTIVE";
        return res;
    }

    NodeId owner = shardRouter_.getOwnerNode(key);
    res.ownerNode = owner;

    if (owner == 0 || owner == config_.nodeId) {
        OpStatus st = txnManager_->deleteKey(txnId, key);
        res.status = st;
        res.ok = (st == OpStatus::OK);
        if (!res.ok) res.error = to_string(st);
        return res;
    }

    NodeHealthState hs = heartbeatManager_->getNodeState(owner);
    if (hs == NodeHealthState::FAILED || hs == NodeHealthState::SHUTDOWN) {
        res.status = OpStatus::NODE_UNAVAILABLE;
        res.error = "Shard owner node " + std::to_string(owner) + " is unavailable";
        return res;
    }

    Message req;
    req.type = MessageType::KV_DELETE_REQ;
    req.senderId = config_.nodeId;
    req.txnId = txnId;
    req.key = key;
    req.resourceId = key;

    Message resp;
    if (!networkManager_->sendRequest(owner, req, resp, config_.lockTimeout + std::chrono::milliseconds(1000))) {
        res.status = OpStatus::NODE_UNAVAILABLE;
        res.error = "Failed to reach shard owner node " + std::to_string(owner);
        return res;
    }

    res.ok = resp.success;
    res.status = resp.status;
    res.error = resp.error;
    if (res.ok) {
        txnManager_->addParticipant(txnId, owner);
        if (Transaction* t = txnManager_->getTransaction(txnId)) {
            t->addOperation(TxnOperation{
                TxnOperation::OpType::ERASE, key, LockMode::EXCLUSIVE,
                "", "", false, owner
            });
        }
    }
    return res;
}

bool Node::commitTransaction(TxnId txnId) {
    Transaction* txn = txnManager_->getTransaction(txnId);
    if (!txn) return false;

    TxnState cur = txn->getState();
    if (cur == TxnState::COMMITTED) return true;
    if (cur == TxnState::ABORTED) return false;

    if (txn->isDistributed()) {
        std::vector<NodeId> parts(txn->getParticipantNodes().begin(),
                                  txn->getParticipantNodes().end());
        bool ok = coordinator_->coordinateCommit(txnId, parts);
        {
            std::lock_guard<std::mutex> lk(remoteLocksMutex_);
            remoteLocks_.erase(txnId);
        }
        return ok;
    }

    return txnManager_->commitTransaction(txnId);
}

bool Node::abortTransaction(TxnId txnId, const std::string& reason) {
    Transaction* txn = txnManager_->getTransaction(txnId);
    if (!txn) return false;

    if (txn->isDistributed()) {
        std::vector<NodeId> parts(txn->getParticipantNodes().begin(),
                                  txn->getParticipantNodes().end());
        coordinator_->coordinateAbort(txnId, parts);
        {
            std::lock_guard<std::mutex> lk(remoteLocksMutex_);
            remoteLocks_.erase(txnId);
        }
        return true;
    }

    return txnManager_->abortTransaction(txnId, reason);
}

// ------------------------------------------------------- message handlers

void Node::setupMessageHandlers() {
    auto& net = *networkManager_;

    // Lock operations
    net.registerHandler(MessageType::LOCK_REQUEST,
        [this](const Message& m) { onLockRequest(m); });
    net.registerHandler(MessageType::LOCK_RELEASE,
        [this](const Message& m) { onLockRelease(m); });
    net.registerHandler(MessageType::LOCK_RELEASE_ALL,
        [this](const Message& m) { onLockReleaseAll(m); });

    // Distributed KV operations
    net.registerHandler(MessageType::KV_GET_REQ,
        [this](const Message& m) { onKvGetReq(m); });
    net.registerHandler(MessageType::KV_PUT_REQ,
        [this](const Message& m) { onKvPutReq(m); });
    net.registerHandler(MessageType::KV_DELETE_REQ,
        [this](const Message& m) { onKvDeleteReq(m); });
    net.registerHandler(MessageType::KV_SCAN_REQ,
        [this](const Message& m) { onKvScanReq(m); });

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
    net.registerHandler(MessageType::COMMIT_ACK,
        [this](const Message& m) { coordinator_->handleMessage(m); });
    net.registerHandler(MessageType::ABORT_ACK,
        [this](const Message& m) { coordinator_->handleMessage(m); });
    net.registerHandler(MessageType::TXN_STATUS_REQ,
        [this](const Message& m) { coordinator_->handleMessage(m); });

    // Deadlock
    net.registerHandler(MessageType::WFG_REQUEST,
        [this](const Message& m) { onWfgRequest(m); });
    net.registerHandler(MessageType::ABORT_TXN,
        [this](const Message& m) {
            Logger::warn("Received remote ABORT_TXN for txn " + std::to_string(m.txnId));
            abortTransaction(m.txnId, m.error.empty() ? "Distributed deadlock victim" : m.error);
        });
    net.registerHandler(MessageType::WFG_RESPONSE,
        [this](const Message& m) {
            std::lock_guard<std::mutex> lk(wfgMutex_);
            if (m.requestId == wfgRoundId_) {
                wfgRespondedPeers_.insert(m.senderId);
                for (const auto& e : m.waitEdges) {
                    pendingWfgEdges_.push_back(e);
                }
                wfgCv_.notify_all();
            }
        });

    // Control RPC (synchronous reply on caller socket)
    net.registerRpcHandler(MessageType::CTRL_RPC_REQ,
        [this](const Message& m) { return onCtrlRpcReq(m); });
}

void Node::onLockRequest(const Message& msg) {
    txnManager_->ensureParticipantTransaction(msg.txnId, msg.senderId);
    OpStatus st = OpStatus::OK;
    bool granted = txnManager_->acquireLock(msg.txnId, msg.resourceId, msg.lockMode, &st);

    Message resp;
    resp.type       = MessageType::LOCK_RESPONSE;
    resp.senderId   = config_.nodeId;
    resp.requestId  = msg.requestId;
    resp.txnId      = msg.txnId;
    resp.resourceId = msg.resourceId;
    resp.success    = granted;
    resp.status     = granted ? OpStatus::OK : st;
    networkManager_->sendMessage(msg.senderId, resp, std::chrono::milliseconds(2000));
}

void Node::onLockRelease(const Message& msg) {
    txnManager_->releaseLock(msg.txnId, msg.resourceId);
}

void Node::onLockReleaseAll(const Message& msg) {
    lockManager_.releaseAllLocks(msg.txnId);
}

void Node::onKvGetReq(const Message& msg) {
    txnManager_->ensureParticipantTransaction(msg.txnId, msg.senderId);
    std::string val;
    bool found = false;
    OpStatus st = txnManager_->readKey(msg.txnId, msg.key, val, found);

    Message resp;
    resp.type      = MessageType::KV_GET_RESP;
    resp.senderId  = config_.nodeId;
    resp.requestId = msg.requestId;
    resp.txnId     = msg.txnId;
    resp.key       = msg.key;
    resp.success   = (st == OpStatus::OK);
    resp.status    = st;
    resp.found     = found;
    resp.value     = std::move(val);
    if (!resp.success) resp.error = to_string(st);
    networkManager_->sendMessage(msg.senderId, resp, std::chrono::milliseconds(2000));
}

void Node::onKvPutReq(const Message& msg) {
    txnManager_->ensureParticipantTransaction(msg.txnId, msg.senderId);
    OpStatus st = txnManager_->writeKey(msg.txnId, msg.key, msg.value);

    Message resp;
    resp.type      = MessageType::KV_PUT_RESP;
    resp.senderId  = config_.nodeId;
    resp.requestId = msg.requestId;
    resp.txnId     = msg.txnId;
    resp.key       = msg.key;
    resp.success   = (st == OpStatus::OK);
    resp.status    = st;
    if (!resp.success) resp.error = to_string(st);
    networkManager_->sendMessage(msg.senderId, resp, std::chrono::milliseconds(2000));
}

void Node::onKvDeleteReq(const Message& msg) {
    txnManager_->ensureParticipantTransaction(msg.txnId, msg.senderId);
    OpStatus st = txnManager_->deleteKey(msg.txnId, msg.key);

    Message resp;
    resp.type      = MessageType::KV_DELETE_RESP;
    resp.senderId  = config_.nodeId;
    resp.requestId = msg.requestId;
    resp.txnId     = msg.txnId;
    resp.key       = msg.key;
    resp.success   = (st == OpStatus::OK);
    resp.status    = st;
    if (!resp.success) resp.error = to_string(st);
    networkManager_->sendMessage(msg.senderId, resp, std::chrono::milliseconds(2000));
}

void Node::onKvScanReq(const Message& msg) {
    auto records = storageEngine_.scan(msg.key, 1000, 0);
    Message resp;
    resp.type      = MessageType::KV_SCAN_RESP;
    resp.senderId  = config_.nodeId;
    resp.requestId = msg.requestId;
    resp.success   = true;
    resp.status    = OpStatus::OK;
    for (const auto& r : records) {
        resp.kvPairs.emplace_back(r.key, r.value);
    }
    networkManager_->sendMessage(msg.senderId, resp, std::chrono::milliseconds(2000));
}

void Node::onWfgRequest(const Message& msg) {
    auto edges = lockManager_.getWaitForEdges();
    Message resp;
    resp.type      = MessageType::WFG_RESPONSE;
    resp.senderId  = config_.nodeId;
    resp.requestId = msg.requestId;
    resp.waitEdges = std::move(edges);
    networkManager_->sendMessage(msg.senderId, resp, std::chrono::milliseconds(1500));
}

void Node::onNodeFailure(NodeId failedNode) {
    Logger::warn("Node " + std::to_string(failedNode) + " heartbeat timed out (FAILED).");
    emitEvent("node.status_changed", 0,
              "Peer " + std::to_string(failedNode) + " FAILED");

    // Abort local ACTIVE transactions waiting on or coordinated by the failed node,
    // WITHOUT releasing PREPARED (in-doubt) 2PC locks!
    auto txns = txnManager_->getTransactionsSnapshot();
    for (const auto& t : txns) {
        if (t.state == TxnState::ACTIVE || t.state == TxnState::WAITING) {
            if (t.coordinatorNode == failedNode && t.coordinatorNode != config_.nodeId) {
                txnManager_->abortTransaction(t.id, "Coordinator node " + std::to_string(failedNode) + " failed before prepare");
            }
        }
    }
}

void Node::onNodeRecovered(NodeId recoveredNode) {
    Logger::info("Node " + std::to_string(recoveredNode) + " recovered (HEALTHY).");
    emitEvent("node.status_changed", 0,
              "Peer " + std::to_string(recoveredNode) + " HEALTHY");
    resolveInDoubtTransactions();
}

size_t Node::resolveInDoubtTransactions() {
    size_t resolved = 0;
    auto txns = txnManager_->getTransactionsSnapshot();
    for (const auto& t : txns) {
        if (t.state == TxnState::PREPARED && t.coordinatorNode != 0 &&
            t.coordinatorNode != config_.nodeId) {
            if (coordinator_->queryCoordinatorStatus(t.id, t.coordinatorNode)) {
                resolved++;
            }
        }
    }
    return resolved;
}

std::vector<WaitEdge> Node::collectGlobalEdges() {
    auto alivePeers = heartbeatManager_->getAliveNodes();
    if (alivePeers.empty()) {
        return lockManager_.getWaitForEdges();
    }

    uint64_t roundId = 0;
    {
        std::lock_guard<std::mutex> lk(wfgMutex_);
        roundId = ++wfgRoundId_;
        pendingWfgEdges_.clear();
        wfgRespondedPeers_.clear();
    }

    Message req;
    req.type      = MessageType::WFG_REQUEST;
    req.senderId  = config_.nodeId;
    req.requestId = roundId;

    size_t expected = 0;
    for (NodeId pid : alivePeers) {
        if (networkManager_->sendMessage(pid, req, std::chrono::milliseconds(500))) {
            expected++;
        }
    }

    if (expected > 0) {
        std::unique_lock<std::mutex> lk(wfgMutex_);
        wfgCv_.wait_for(lk, std::chrono::milliseconds(600), [&]() {
            return wfgRespondedPeers_.size() >= expected || !running_.load();
        });
    }

    std::lock_guard<std::mutex> lk(wfgMutex_);
    auto localEdges = lockManager_.getWaitForEdges();
    std::vector<WaitEdge> all;
    all.reserve(localEdges.size() + pendingWfgEdges_.size());
    all.insert(all.end(), localEdges.begin(), localEdges.end());
    all.insert(all.end(), pendingWfgEdges_.begin(), pendingWfgEdges_.end());
    return all;
}

void Node::handleDeadlockVictim(TxnId victim) {
    Logger::warn("Deadlock victim selected: txn " + std::to_string(victim));
    NodeId origin = txnOriginNode(victim);
    if (origin == 0 || origin == config_.nodeId) {
        abortTransaction(victim, "Deadlock victim");
    } else {
        // Notify originating coordinator node to abort distributed victim
        Message abortMsg;
        abortMsg.type = MessageType::ABORT_TXN;
        abortMsg.senderId = config_.nodeId;
        abortMsg.txnId = victim;
        abortMsg.error = "Global deadlock victim selected by node " + std::to_string(config_.nodeId);
        networkManager_->sendMessage(origin, abortMsg, std::chrono::milliseconds(1500));
        // Also abort any local participant locks for victim
        txnManager_->abortTransaction(victim, "Global deadlock victim");
    }
    emitEvent("deadlock.resolved", victim, "Victim aborted");
}

std::string Node::exportStateJson() const {
    nlohmann::json j;
    j["nodeId"] = config_.nodeId;
    j["port"] = config_.port;
    j["running"] = running_.load();

    auto sstats = storageEngine_.getStats();
    j["storage"] = {
        {"keyCount", sstats.keyCount},
        {"checkpointLsn", sstats.checkpointLsn},
        {"lastSyncEpochMs", sstats.lastSyncEpochMs},
        {"recoveredCleanly", sstats.recoveredCleanly}
    };

    nlohmann::json kvArr = nlohmann::json::array();
    for (const auto& rec : storageEngine_.scan("", 2000, 0)) {
        kvArr.push_back({
            {"key", rec.key},
            {"value", rec.value},
            {"isJson", rec.isJson},
            {"version", rec.version},
            {"updatedAtMs", rec.updatedAtMs},
            {"ownerNode", config_.nodeId}
        });
    }
    j["records"] = kvArr;

    nlohmann::json locksArr = nlohmann::json::array();
    for (const auto& l : lockManager_.getLockTableSnapshot()) {
        nlohmann::json waitersArr = nlohmann::json::array();
        for (const auto& [wtid, wmode] : l.waiters) {
            waitersArr.push_back({{"txnId", wtid}, {"mode", to_string(wmode)}});
        }
        locksArr.push_back({
            {"resourceId", l.resourceId},
            {"mode", to_string(l.mode)},
            {"owners", l.owners},
            {"waiters", waitersArr},
            {"nodeId", config_.nodeId}
        });
    }
    j["locks"] = locksArr;

    nlohmann::json wfgArr = nlohmann::json::array();
    for (const auto& e : lockManager_.getWaitForEdges()) {
        wfgArr.push_back({
            {"waitingTxn", e.waitingTxn},
            {"holdingTxn", e.holdingTxn},
            {"resourceId", e.resourceId},
            {"nodeId", config_.nodeId}
        });
    }
    j["waitEdges"] = wfgArr;

    nlohmann::json txnsArr = nlohmann::json::array();
    for (const auto& t : txnManager_->getTransactionsSnapshot()) {
        nlohmann::json opsArr = nlohmann::json::array();
        for (const auto& op : t.operations) {
            opsArr.push_back({
                {"type", static_cast<int>(op.type)},
                {"resourceId", op.resourceId},
                {"lockMode", to_string(op.lockMode)},
                {"data", op.data},
                {"ownerNode", op.ownerNode}
            });
        }
        txnsArr.push_back({
            {"id", t.id},
            {"coordinatorNode", t.coordinatorNode},
            {"state", to_string(t.state)},
            {"isolation", t.isolation == IsolationLevel::SERIALIZABLE ? "SERIALIZABLE" : "READ_COMMITTED"},
            {"startEpochMs", t.startEpochMs},
            {"endEpochMs", t.endEpochMs},
            {"abortReason", t.abortReason},
            {"lockedResources", t.lockedResources},
            {"participants", t.participants},
            {"operations", opsArr}
        });
    }
    j["transactions"] = txnsArr;

    nlohmann::json peersArr = nlohmann::json::array();
    for (const auto& p : heartbeatManager_->getPeerHealthSnapshot()) {
        peersArr.push_back({
            {"nodeId", p.nodeId},
            {"state", to_string(p.state)},
            {"lastHeartbeatEpochMs", p.lastHeartbeatEpochMs},
            {"missedBeats", p.missedBeats}
        });
    }
    j["peers"] = peersArr;

    nlohmann::json twoPcArr = nlohmann::json::array();
    for (const auto& tp : coordinator_->getSnapshot()) {
        nlohmann::json votesObj = nlohmann::json::object();
        for (const auto& [nid, v] : tp.votes) votesObj[std::to_string(nid)] = v;
        nlohmann::json acksObj = nlohmann::json::object();
        for (const auto& [nid, a] : tp.acks) acksObj[std::to_string(nid)] = a;
        twoPcArr.push_back({
            {"txnId", tp.txnId},
            {"role", to_string(tp.role)},
            {"state", to_string(tp.state)},
            {"coordinatorNode", tp.coordinatorNode},
            {"participants", tp.participants},
            {"votes", votesObj},
            {"acks", acksObj},
            {"decisionDurable", tp.decisionDurable},
            {"updatedAtMs", tp.updatedAtMs}
        });
    }
    j["twoPc"] = twoPcArr;

    nlohmann::json ddArr = nlohmann::json::array();
    for (const auto& dc : deadlockDetector_->getHistory()) {
        ddArr.push_back({
            {"cycle", dc.cycle},
            {"victim", dc.victim},
            {"detectedAtMs", dc.detectedAtMs},
            {"resolved", dc.resolved}
        });
    }
    j["deadlocks"] = ddArr;
    j["deadlockCount"] = deadlockDetector_->getTotalDetectedCount();

    nlohmann::json walArr = nlohmann::json::array();
    for (const auto& w : txnManager_->getWalEntries(200)) {
        walArr.push_back({
            {"lsn", w.lsn},
            {"type", to_string(w.type)},
            {"txnId", w.txnId},
            {"nodeId", w.nodeId},
            {"resourceId", w.resourceId},
            {"data", w.data},
            {"prevData", w.prevData},
            {"prevExisted", w.prevExisted},
            {"participants", w.participants},
            {"timestamp", w.timestamp}
        });
    }
    j["wal"] = walArr;

    auto rec = txnManager_->getLastRecoveryReport();
    j["recovery"] = {
        {"executed", rec.executed},
        {"truncatedCorruptTail", rec.truncatedCorruptTail},
        {"checkpointLsn", rec.checkpointLsn},
        {"walRecordsScanned", rec.walRecordsScanned},
        {"redoneTransactions", rec.redoneTransactions},
        {"undoneTransactions", rec.undoneTransactions},
        {"inDoubtTransactions", rec.inDoubtTransactions},
        {"inDoubtTxnIds", rec.inDoubtTxnIds},
        {"recoveredAtMs", rec.recoveredAtMs}
    };

    return j.dump();
}

Message Node::onCtrlRpcReq(const Message& msg) {
    Message resp;
    resp.type = MessageType::CTRL_RPC_RESP;
    resp.senderId = config_.nodeId;
    resp.requestId = msg.requestId;

    try {
        auto req = nlohmann::json::parse(msg.value.empty() ? "{}" : msg.value);
        std::string cmd = req.value("cmd", "state");

        if (cmd == "state") {
            resp.success = true;
            resp.value = exportStateJson();
        } else if (cmd == "ping") {
            resp.success = true;
            resp.value = nlohmann::json({{"nodeId", config_.nodeId}, {"running", running_.load()}}).dump();
        } else if (cmd == "begin") {
            std::string isoStr = req.value("isolation", "SERIALIZABLE");
            auto iso = (isoStr == "READ_COMMITTED") ? IsolationLevel::READ_COMMITTED
                                                    : IsolationLevel::SERIALIZABLE;
            TxnId tid = beginTransaction(iso);
            resp.success = true;
            resp.txnId = tid;
            resp.value = nlohmann::json({{"txnId", tid}}).dump();
        } else if (cmd == "get") {
            TxnId tid = req.at("txnId").get<TxnId>();
            std::string key = req.at("key").get<std::string>();
            auto r = get(tid, key);
            resp.success = r.ok;
            resp.status = r.status;
            resp.found = r.found;
            resp.value = nlohmann::json({
                {"ok", r.ok},
                {"status", to_string(r.status)},
                {"found", r.found},
                {"value", r.value},
                {"ownerNode", r.ownerNode},
                {"error", r.error}
            }).dump();
        } else if (cmd == "put") {
            TxnId tid = req.at("txnId").get<TxnId>();
            std::string key = req.at("key").get<std::string>();
            std::string val = req.at("value").get<std::string>();
            auto r = put(tid, key, val);
            resp.success = r.ok;
            resp.status = r.status;
            resp.value = nlohmann::json({
                {"ok", r.ok},
                {"status", to_string(r.status)},
                {"ownerNode", r.ownerNode},
                {"error", r.error}
            }).dump();
        } else if (cmd == "delete") {
            TxnId tid = req.at("txnId").get<TxnId>();
            std::string key = req.at("key").get<std::string>();
            auto r = remove(tid, key);
            resp.success = r.ok;
            resp.status = r.status;
            resp.value = nlohmann::json({
                {"ok", r.ok},
                {"status", to_string(r.status)},
                {"ownerNode", r.ownerNode},
                {"error", r.error}
            }).dump();
        } else if (cmd == "commit") {
            TxnId tid = req.at("txnId").get<TxnId>();
            bool ok = commitTransaction(tid);
            resp.success = ok;
            resp.value = nlohmann::json({{"ok", ok}}).dump();
        } else if (cmd == "abort") {
            TxnId tid = req.at("txnId").get<TxnId>();
            std::string reason = req.value("reason", "Client abort");
            bool ok = abortTransaction(tid, reason);
            resp.success = ok;
            resp.value = nlohmann::json({{"ok", ok}}).dump();
        } else if (cmd == "checkpoint") {
            bool ok = txnManager_->createCheckpoint();
            resp.success = ok;
            resp.value = nlohmann::json({{"ok", ok}}).dump();
        } else {
            resp.success = false;
            resp.error = "Unknown CTRL_RPC command: " + cmd;
        }
    } catch (const std::exception& e) {
        resp.success = false;
        resp.error = e.what();
    }

    return resp;
}

} // namespace dlm
