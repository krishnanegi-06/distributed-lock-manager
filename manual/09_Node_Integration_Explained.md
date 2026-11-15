# Chapter 9: Node Integration & Orchestration

The **`Node` class** (`include/dlm/node/Node.h` and `src/node/Node.cpp`) serves as the master controller and orchestration hub of the Distributed Lock Manager. It instantiates, wires, and manages the lifecycles of all 6 underlying subsystems into a cohesive cluster node.

---

## 1. Node Configuration (`NodeConfig`)

Before starting a node, its identity and cluster membership must be defined:

```cpp
struct PeerConfig {
    NodeId      id;
    std::string host;
    int         port;
};

struct NodeConfig {
    NodeId                  nodeId;
    int                     port;
    std::vector<PeerConfig> peers;
    std::string             logDirectory; // Path to WAL log storage
};
```

---

## 2. Subsystem Ownership & Dependency Wiring

In the constructor of `Node`, all subsystems are initialized with their respective dependencies:

```cpp
Node::Node(const NodeConfig& config) : config_(config) {
    // 1. Network Subsystem
    networkManager_ = std::make_unique<NetworkManager>(config_.nodeId, config_.port);
    for (auto& p : config_.peers) {
        networkManager_->addPeer(p.id, p.host, p.port);
    }

    // 2. Heartbeat Subsystem
    heartbeatManager_ = std::make_unique<HeartbeatManager>(config_.nodeId, *networkManager_);
    heartbeatManager_->setFailureCallback([this](NodeId failed) { 
        onNodeFailure(failed); 
    });

    // 3. Transaction Subsystem with WAL
    std::string walPath = config_.logDirectory + "/node_" + std::to_string(config_.nodeId) + "_wal.log";
    txnManager_ = std::make_unique<TransactionManager>(lockManager_, walPath);

    // 4. Two-Phase Commit Coordinator
    coordinator_ = std::make_unique<TwoPCCoordinator>(config_.nodeId, *networkManager_);
    coordinator_->registerParticipantHandlers(
        [this](TxnId t) { return txnManager_->prepareTransaction(t); },
        [this](TxnId t) { txnManager_->commitPrepared(t); },
        [this](TxnId t) { txnManager_->abortPrepared(t); }
    );

    // 5. Deadlock Detection Subsystem
    deadlockDetector_ = std::make_unique<DeadlockDetector>();
}
```

---

## 3. Node Lifecycle: `start()` and `stop()`

### Startup (`start()`)
When `Node::start()` is called:
1. `setupMessageHandlers()`: Registers callback handlers for all 12 network message types.
2. `networkManager_->start()`: Binds the TCP listening socket and begins accepting peer connections.
3. `heartbeatManager_->start()`: Launches the background thread broadcasting heartbeats every 500ms.
4. `deadlockDetector_->startPeriodicDetection()`: Spawns the periodic deadlock thread (every 2 seconds) that collects local and global WFG edges and executes DFS cycle detection.

### Shutdown (`stop()`)
Performs a graceful, orderly teardown:
1. Stops the deadlock detection thread.
2. Stops the heartbeat broadcasting and monitoring threads.
3. Closes listening and connected sockets in `NetworkManager`.
4. Flushes any pending Write-Ahead Log entries to disk.

---

## 4. Message Routing & Dispatching

The `setupMessageHandlers()` function routes incoming network messages to their respective subsystem:

| Message Type | Target Subsystem / Handler | Action Taken |
| :--- | :--- | :--- |
| `LOCK_REQUEST` | `onLockRequest(msg)` | Acquires lock on local `LockManager` and sends `LOCK_RESPONSE`. |
| `LOCK_RELEASE` | `onLockRelease(msg)` | Releases lock on local `LockManager`. |
| `PREPARE` | `coordinator_->handleMessage()` | Calls `txnManager_->prepareTransaction()`, replies `VOTE_YES`/`NO`. |
| `VOTE_YES / NO` | `coordinator_->handleMessage()` | Increments vote counter in `TwoPCCoordinator`. |
| `GLOBAL_COMMIT` | `coordinator_->handleMessage()` | Commits prepared transaction and releases locks. |
| `GLOBAL_ABORT` | `coordinator_->handleMessage()` | Aborts prepared transaction and releases locks. |
| `WFG_REQUEST` | `onWfgRequest(msg)` | Queries `LockManager::getWaitForEdges()` and returns `WFG_RESPONSE`. |
| `WFG_RESPONSE` | `collectGlobalEdges()` | Appends remote node's edges to the global edge collection list. |
| `ABORT_TXN` | `txnManager_->abortTransaction()` | Forcibly aborts a local transaction selected as a global victim. |

---

## 5. Global Deadlock Aggregation (`collectGlobalEdges()`)

To detect deadlocks that cross physical node boundaries:
1. Node broadcasts `WFG_REQUEST` to all peer nodes over TCP.
2. Waits up to 1000ms for incoming `WFG_RESPONSE` messages.
3. Merges the local edges (`lockManager_.getWaitForEdges()`) with the received remote edges:
   $$E_{\text{merged}} = E_{\text{local}} \cup \bigcup_{p \in \text{peers}} E_p$$
4. Returns the unified edge list to the `DeadlockDetector` for DFS cycle detection.
