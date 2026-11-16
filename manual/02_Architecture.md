# Chapter 2: System Architecture & Design

## 2.1 High-Level Architecture Overview

The **Distributed Lock Manager (DLM)** is structured as a peer-to-peer cluster of autonomous database server nodes. Each node runs identical software and contains a complete suite of locking, transaction management, networking, and consensus subsystems.

Below is the complete architectural layout of a 3-node DLM cluster:

```
┌───────────────────────────────────────────────────────────────────────────────────┐
│                                   DLM CLUSTER                                     │
│                                                                                   │
│  ┌──────────────────────┐  ┌──────────────────────┐  ┌──────────────────────┐   │
│  │     NODE 1 (:9001)   │  │     NODE 2 (:9002)   │  │     NODE 3 (:9003)   │   │
│  │                      │  │                      │  │                      │   │
│  │ ┌──────────────────┐ │  │ ┌──────────────────┐ │  │ ┌──────────────────┐ │   │
│  │ │   LockManager    │ │  │ │   LockManager    │ │  │ │   LockManager    │ │   │
│  │ │ (Mutex & CondVar)│ │  │ │ (Mutex & CondVar)│ │  │ │ (Mutex & CondVar)│ │   │
│  │ └────────┬─────────┘ │  │ └────────┬─────────┘ │  │ └────────┬─────────┘ │   │
│  │          │           │  │          │           │  │          │           │   │
│  │ ┌────────┴─────────┐ │  │ ┌────────┴─────────┐ │  │ ┌────────┴─────────┐ │   │
│  │ │TransactionManager│ │  │ │TransactionManager│ │  │ │TransactionManager│ │   │
│  │ │   (WAL Engine)   │ │  │ │   (WAL Engine)   │ │  │ │   (WAL Engine)   │ │   │
│  │ └────────┬─────────┘ │  │ └────────┬─────────┘ │  │ └────────┬─────────┘ │   │
│  │          │           │  │          │           │  │          │           │   │
│  │ ┌────────┴─────────┐ │  │ ┌────────┴─────────┐ │  │ ┌────────┴─────────┐ │   │
│  │ │ TwoPCCoordinator │ │  │ │ TwoPCCoordinator │ │  │ │ TwoPCCoordinator │ │   │
│  │ │  (Consensus 2PC) │ │  │ │  (Consensus 2PC) │ │  │ │  (Consensus 2PC) │ │   │
│  │ └────────┬─────────┘ │  │ └────────┬─────────┘ │  │ └────────┬─────────┘ │   │
│  │          │           │  │          │           │  │          │           │   │
│  │ ┌────────┴─────────┐ │  │ ┌────────┴─────────┐ │  │ ┌────────┴─────────┐ │   │
│  │ │ DeadlockDetector │ │  │ │ DeadlockDetector │ │  │ │ DeadlockDetector │ │   │
│  │ │  (Local+Global)  │ │  │ │  (Local+Global)  │ │  │ │  (Local+Global)  │ │   │
│  │ └────────┬─────────┘ │  │ └────────┬─────────┘ │  │ └────────┬─────────┘ │   │
│  │          │           │  │          │           │  │          │           │   │
│  │ ┌────────┴─────────┐ │  │ ┌────────┴─────────┐ │  │ ┌────────┴─────────┐ │   │
│  │ │ HeartbeatManager │ │  │ │ HeartbeatManager │ │  │ │ HeartbeatManager │ │   │
│  │ └────────┬─────────┘ │  │ └────────┬─────────┘ │  │ └────────┬─────────┘ │   │
│  │          │           │  │          │           │  │          │           │   │
│  │ ┌────────┴─────────┐ │  │ ┌────────┴─────────┐ │  │ ┌────────┴─────────┐ │   │
│  │ │  NetworkManager  │ │  │ │  NetworkManager  │ │  │ │  NetworkManager  │ │   │
│  │ └────────┬─────────┘ │  │ └────────┬─────────┘ │  │ └────────┬─────────┘ │   │
│  └──────────┼───────────┘  └──────────┼───────────┘  └──────────┼───────────┘   │
│             │                         │                         │               │
│             └─────────────────────────┼─────────────────────────┘               │
│                               TCP / IP Sockets                                  │
└───────────────────────────────────────────────────────────────────────────────────┘
```

---

## 2.2 Seven-Module Breakdown

The codebase is organized into seven modular subsystems within the `dlm` namespace:

| Module | Key Header | Key Source | Primary Role |
| :--- | :--- | :--- | :--- |
| **1. Common** | `Types.h`, `Logger.h` | `Logger.cpp` | Global types (`TxnId`, `NodeId`, `ResourceId`), enums (`LockMode`, `TxnState`), and thread-safe console/file logging. |
| **2. Lock Manager** | `LockManager.h` | `LockManager.cpp` | In-memory lock table, shared/exclusive compatibility checking, condition variable wait queues, and local Wait-For Graph edge extraction. |
| **3. Network Layer** | `Message.h`, `NetworkManager.h`, `HeartbeatManager.h` | `Message.cpp`, `NetworkManager.cpp`, `HeartbeatManager.cpp` | TCP socket server/client abstraction, pipe-delimited message serialization/deserialization, async dispatcher, and failure detection. |
| **4. Coordinator** | `TwoPCCoordinator.h` | `TwoPCCoordinator.cpp` | Drives the Two-Phase Commit (2PC) protocol across distributed transaction participants (`PREPARE` $\to$ `VOTE` $\to$ `COMMIT`/`ABORT`). |
| **5. Deadlock Detection**| `DeadlockDetector.h` | `DeadlockDetector.cpp` | Detects circular wait conditions using DFS-based cycle finding with 3-color vertex marking. Selects victims based on transaction seniority. |
| **6. Transaction System**| `Transaction.h`, `TransactionLog.h`, `TransactionManager.h`| `Transaction.cpp`, `TransactionLog.cpp`, `TransactionManager.cpp` | Manages transaction lifecycle (`ACTIVE`, `PREPARING`, `COMMITTED`, `ABORTED`), locks tracking, and persistent Write-Ahead Logging (WAL). |
| **7. Node Orchestration**| `Node.h` | `Node.cpp`, `node_main.cpp` | Top-level facade that instantiates all subsystems, connects network callbacks, schedules background threads, and exposes the public API. |

---

## 2.3 Module Interaction & Architecture Stack

Within any given node, components interact through well-defined clean C++ interfaces:

```
[ Application / Client Requests ]
               │
               ▼
   ┌──────────────────────┐
   │      dlm::Node       │ ── Top-level coordinator & facade
   └──────────┬───────────┘
              │
    ┌─────────┼──────────────────────────┬────────────────────────┐
    │         │                          │                        │
    ▼         ▼                          ▼                        ▼
┌─────────┐ ┌────────────────────┐ ┌──────────────────┐ ┌───────────────────┐
│ LockMgr │ │ TransactionManager │ │ TwoPCCoordinator │ │ DeadlockDetector  │
│         │ ├────────────────────┤ │                  │ │                   │
│         │ │  TransactionLog    │ │                  │ │                   │
└────┬────┘ └─────────┬──────────┘ └────────┬─────────┘ └─────────┬─────────┘
     │                │                     │                     │
     └────────────────┴──────────┬──────────┴─────────────────────┘
                                 │
                                 ▼
                     ┌───────────────────────┐
                     │    NetworkManager     │ ◄─── HeartbeatManager
                     └──────────┬────────────┘
                                │
                          [ TCP Socket ]
```

### Call Flow Responsibilities:
1. When a client calls `node.beginTransaction()`, `Node` delegates to `TransactionManager`, which generates a monotonic `TxnId` and writes `TXN_BEGIN` to `TransactionLog`.
2. When the client calls `node.lockResource(txnId, res, mode)`, `TransactionManager` calls `LockManager::acquireLock()`. If the lock cannot be granted immediately, the calling thread blocks on a condition variable.
3. When `node.commitTransaction(txnId)` is called:
   * If local: `TransactionManager` marks the transaction committed, flushes `TXN_COMMIT` to the WAL, and calls `LockManager::releaseAllLocks()`.
   * If distributed (multiple nodes involved): `Node` engages `TwoPCCoordinator::coordinateCommit()`, broadcasting `PREPARE` over `NetworkManager` and waiting for all participants to vote before issuing `GLOBAL_COMMIT`.

---

## 2.4 End-to-End Data Flow Diagrams

### Scenario A: Local Lock Acquisition & Commit

```
Client             Node          TxnManager         LockManager          WAL Log
  │                 │                │                   │                  │
  ├─── beginTxn() ─►│                │                   │                  │
  │                 ├─── beginTxn() ─►                   │                  │
  │                 │                ├─ logBegin(t1) ──────────────────────►│
  │                 │◄── return t1 ──┤                   │                  │
  │◄── return t1 ───┤                │                   │                  │
  │                 │                │                   │                  │
  ├── lock(row1) ──►│                │                   │                  │
  │                 ├── acquireLock ─►                   │                  │
  │                 │                ├── acquireLock ───►│                  │
  │                 │                │                   │ (Check table)    │
  │                 │                │                   │ (Grant lock)     │
  │                 │                │◄── true (granted) ┤                  │
  │                 │                ├─ logOp(row1) ───────────────────────►│
  │                 │◄── true ───────┤                   │                  │
  │◄── true ────────┤                │                   │                  │
  │                 │                │                   │                  │
  ├── commit(t1) ──►│                │                   │                  │
  │                 ├── commitTxn ──►│                   │                  │
  │                 │                ├── releaseAll ────►│ (Release lock)   │
  │                 │                │                   │ (cv_.notify_all) │
  │                 │                ├─ logCommit(t1) ─────────────────────►│
  │                 │◄── true ───────┤                   │                  │
  │◄── true ────────┤                │                   │                  │
```

---

## 2.5 Threading Model

To deliver responsive concurrency, each DLM node executes across multiple concurrent OS threads:

```
┌────────────────────────────────────────────────────────────────────────┐
│                        NODE THREADING MODEL                            │
├───────────────────────┬────────────────────────────────────────────────┤
│ Thread Name           │ Function & Responsibility                      │
├───────────────────────┼────────────────────────────────────────────────┤
│ **Main / User Thread**│ Drives application logic, begins/commits txns. │
│ **Accept Thread**     │ Dedicated socket accept loop (`acceptLoop()`). │
│ **Client Threads**    │ Per-connection worker thread (`handleClient()`)│
│                       │ reading length-prefixed messages from peers.   │
│ **Heartbeat Thread**  │ Background worker (`heartbeatLoop()`) firing   │
│                       │ pings every 1000ms and pruning timed-out nodes.│
│ **Deadlock Detector** │ Periodic background thread running every 2000ms│
│                       │ querying WFG edges and selecting abort victims.│
└───────────────────────┴────────────────────────────────────────────────┘
```

### Concurrency Synchronization Guards:
* `LockManager::mutex_`: Protects `lockTable_`, `waiters_`, and `txnLocks_`.
* `NetworkManager::handlerMutex_`: Protects the message dispatcher table.
* `NetworkManager::peerMutex_`: Protects peer routing tables.
* `HeartbeatManager::livenessMutex_`: Synchronizes the `lastSeen_` timestamp map.
* `TransactionLog::mutex_`: Serializes physical disk appends to the log file.
* `TwoPCCoordinator::CoordTxn::mtx`: Protects per-transaction voting tables during 2PC rounds.

---

## 2.6 Network Communication Model

The network layer uses a **Connection-Oriented Client-Server TCP Model**:
1. Every node listens on a dedicated TCP server port (e.g., Node 1 on `9001`, Node 2 on `9002`, Node 3 on `9003`).
2. When Node $A$ needs to send a message to Node $B$, `NetworkManager::sendMessage()` opens an ephemeral TCP socket to Node $B$'s port, transmits the length-prefixed message, flushes the stream, and closes the connection.
3. Node $B$'s `acceptLoop()` accepts the connection and spawns a thread running `handleClient()` to deserialize the payload and execute the registered callback function.

```
Node 1 (Sender)                                            Node 2 (Receiver)
  │                                                               │
  │ socket()                                                      │
  │ connect(127.0.0.1:9002) ─────────────────────────► accept()   │
  │                                                               │ (Spawns worker)
  │ [4-byte big-endian length] ──────────────────────► recvAll()  │
  │ [Pipe-delimited serialized payload] ─────────────► recvAll()  │
  │ closesocket()                                                 │
  │                                                               │ Message::deserialize()
  │                                                               │ Dispatch to Handler
  │                                                               │ closesocket()
```
