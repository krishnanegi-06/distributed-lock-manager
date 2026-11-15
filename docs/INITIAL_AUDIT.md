# Initial Repository Audit

**Date:** 2026-11-10  
**Repository:** `distributed-lock-manager` (branch `krishna`)  
**Audited Commit Baseline:** `282fb36` + local working-tree edits  

---

## 1. Original Build and Test Results (Baseline)

### 1.1 Environment Observed
- **OS:** Windows
- **Build System:** CMake `4.0.0`, Visual Studio 17 2022 BuildTools (`x64`), MSBuild `17.13.19`
- **Node.js / npm:** `v22.15.0` / `10.9.2`
- **Python:** `3.12.10`
- **Docker:** Not available on PATH in current Windows environment
- **Boost / vcpkg:** Not installed locally

### 1.2 Baseline Build (`cmake --build build --config Debug`)
- **Result:** Exited with code `0`
- **Artifacts Built:**
  - `build/Debug/dlm_core.lib`
  - `build/Debug/dlm_node.exe`
  - `build/Debug/dlm_simulation.exe`
  - `build/Debug/dlm_tests.exe`

### 1.3 Baseline Test Execution (`ctest --test-dir build -C Debug` & `dlm_tests.exe`)
- **CTest Result:** `1/1 Test #1: unit_tests ... Passed (1.25 sec)`
- **`dlm_tests.exe` Result:** `17 passed, 0 failed, 17 total`
  - LockManager (6): `shared_lock_compatibility`, `exclusive_lock_blocks`, `release_lock`, `release_all_locks`, `wait_for_edges`, `shared_exclusive_conflict`
  - DeadlockDetector (4): `no_deadlock`, `simple_deadlock`, `three_way_deadlock`, `empty_edges`
  - Transaction (1): `transaction_lifecycle`
  - TransactionManager (3): `txn_manager_begin_commit`, `txn_manager_abort`, `txn_manager_prepare`
  - TransactionLog (1): `transaction_log`
  - Message (2): `message_roundtrip`, `message_empty_fields`

### 1.4 Baseline Simulation (`dlm_simulation.exe`)
- **Result:** Exited with code `0` across 5 scenarios (runtime ~17 seconds).
- **Critical Observation:** Scenario 4 (`Multi-Node Communication`) only starts 3 nodes and runs independent local locks (`Node1 T6 -> row_1`, `Node2 T2 -> row_2`, `Node3 T1 -> row_3`)—no remote lock requests, no cross-node transactions, and no 2PC execution actually occur.

---

## 2. Module-by-Module Inventory & Defect Analysis

### 2.1 `Types.h` (`include/dlm/common/Types.h`)
- **Implemented:** `NodeId` (`uint32_t`), `TxnId` (`uint32_t`), `ResourceId` (`std::string`), `LockMode`, `LockGranularity`, `TxnState`, `IsolationLevel`.
- **Defects / Missing:**
  - `TxnId` is a 32-bit counter (`std::atomic<TxnId> nextTxnId_{1}`) independently incremented on each node. In `dlm_simulation`, Node 1, Node 2, and Node 3 all generate `TxnId = 1, 2, ...`, causing global transaction ID collisions in distributed WFGs, 2PC, and logs.
  - `TxnState` lacks `WAITING` and `PREPARED` (only has `ACTIVE`, `PREPARING`, `COMMITTED`, `ABORTED`).
  - No valid state transition validation exists.

### 2.2 `LockManager` (`include/dlm/lock_manager/LockManager.h`, `src/lock_manager/LockManager.cpp`)
- **Implemented:** Local mutex + `condition_variable` lock table (`lockTable_`, `waiters_`, `txnLocks_`), shared/exclusive compatibility, FIFO wait queue conflict check, `releaseLock`, `releaseAllLocks`, `checkLock`, `getWaitForEdges`.
- **Defects:**
  - `using namespace std;` in public header `LockManager.h` pollutes global/namespace scope for all includers.
  - **Re-entrant lock / upgrade hazard:** If `txnId` already holds `SHARED` and requests `SHARED` again while an `EXCLUSIVE` waiter is queued, `acquireLock` appends `txnId` behind the `EXCLUSIVE` waiter and blocks itself instead of recognizing it already holds a compatible lock. If `txnId` holds `SHARED` and requests `EXCLUSIVE` while another waiter is queued ahead, a circular wait (`waiter -> txnId` and `txnId -> waiter`) can form without appearing in `getWaitForEdges()`.
  - **Cancellation:** No explicit `cancelWait(TxnId)` beyond `releaseAllLocks(TxnId)`.
  - **Observability:** No read-only snapshot API (`getLockTableSnapshot()`, `getWaitingLocks()`) for monitoring/API exposure.

### 2.3 `DeadlockDetector` (`include/dlm/deadlock/DeadlockDetector.h`, `src/deadlock/DeadlockDetector.cpp`)
- **Implemented:** 3-color DFS cycle detection on `WaitEdge` list, youngest-victim selection (`max(TxnId)`), periodic background thread (`startPeriodicDetection`).
- **Defects:**
  - `Node::start()` runs both `getLocalEdges()` AND `getGlobalEdges()` every 2 seconds on **every node**, calling `txnManager_->abortTransaction(victim)` locally even if `victim` originated on a remote node. `ABORT_TXN` handler exists in `Node::setupMessageHandlers()`, but `ABORT_TXN` is **never sent** anywhere in the codebase.
  - `Node::collectGlobalEdges()` calls `wfgCv_.wait_for(lk, 1000ms)` **without a predicate**: it wakes immediately on the first peer response (or spurious wakeup) and ignores remaining peers.
  - `DeadlockDetector::stopPeriodicDetection()` waits up to the full sleep interval (`2000ms`) because `std::this_thread::sleep_for` is uninterruptible by `stop()`.
  - Uses local 32-bit `TxnId`, so cross-node WFG edges collide when nodes share `TxnId = 1`.
  - No cycle/history tracking or retry/backoff policy.

### 2.4 `NetworkManager` & `Message` (`src/network/Message.cpp`, `NetworkManager.cpp`)
- **Implemented:** Length-prefixed TCP socket server/client (`ws2_32` on Windows, POSIX sockets declared in header), `Message` binary/text framing, handler dispatch by `MessageType`.
- **Defects:**
  - **Unescaped delimiter serialization:** `Message::serialize()` uses `|`, `,`, `;`, `[`, `]` without escaping. Any key/value containing these characters breaks deserialization; `std::stoi`/`std::stoul` can throw uncaught exceptions on malformed packets.
  - **No `requestId` or response correlation:** `Node::onLockRequest` sends `LOCK_RESPONSE`, but `Node` never registers a handler for `LOCK_RESPONSE`, and there is no synchronous/async RPC request-response correlation.
  - **Resource/Thread leaks:** `clientThreads_` appends a new `std::thread` on every accepted connection and never reaps finished threads until `stop()`.
  - **Shutdown hang risk:** `NetworkManager::stop()` closes `serverSocket_` but does not close active `clientSock` handles, so `handleClient` threads blocked in `recvAll` can stall shutdown.
  - **Missing bounds & timeouts:** `handleClient` reads arbitrary 32-bit `len` and allocates `std::string buf(len, '\0')` with no `MAX_MESSAGE_SIZE` check; sockets have no `SO_RCVTIMEO` / `SO_SNDTIMEO` or connect timeouts.
  - **Non-Windows compilation gap:** `NetworkManager.cpp` includes `<iostream>` and `#pragma comment(lib, "ws2_32.lib")` unconditionally without `#ifdef _WIN32` guards around `WSAStartup`, `WSACleanup`, `closesocket`, `SOCKET_ERROR`, or POSIX `<sys/socket.h>`, `<arpa/inet.h>`, `<unistd.h>` includes—it will **fail to compile on Linux/Docker** as written!

### 2.5 `HeartbeatManager` (`src/network/HeartbeatManager.cpp`)
- **Implemented:** Periodic `HEARTBEAT` broadcast, `HEARTBEAT_ACK` reply, timeout tracking (`lastSeen_`), failure callback.
- **Defects:**
  - Only tracks peers that have sent at least one heartbeat; a peer that is down from startup is never added to `lastSeen_` and never triggers failure detection or health status reporting.
  - Once `timeout_` expires, `lastSeen_` erases the node and treats it as permanently failed until a new heartbeat arrives; there is no `HEALTHY` / `SUSPECTED` / `FAILED` / `SHUTDOWN` state machine.
  - `stop()` sleeps for the full `interval_` (`1000ms`) due to uninterruptible `sleep_for`.
  - `setFailureCallback` has no mutex protection relative to `heartbeatLoop()`.

### 2.6 `TwoPCCoordinator` (`src/coordinator/TwoPCCoordinator.cpp`)
- **Implemented:** `coordinateCommit()` sending `PREPARE`, waiting for `VOTE_YES`/`VOTE_NO` with `voteTimeout_`, sending `GLOBAL_COMMIT` / `GLOBAL_ABORT`, participant callbacks.
- **Defects:**
  - **Dead / Unreachable in `Node`:** `Node::lockResource` never registers remote participants (`addParticipantNode` is never called in `src/`), so `txn->isDistributed()` is always `false` and `TwoPCCoordinator` is never invoked in live cluster runs.
  - **Non-durable decisions:** `TwoPCCoordinator::logDecision()` only calls `Logger::info(...)`—decisions are **not written to disk**, violating 2PC durability.
  - **No participant vote persistence or in-doubt recovery:** Participants do not persist `PREPARED` state or recover unresolved transactions after crash/restart.
  - **Self-participation bug:** If `participants` included the coordinator itself, coordinator would send TCP `PREPARE` to itself (`sendMessage` only checks `peers_`, which excludes self) and wait until `voteTimeout_` expires!
  - **No duplicate/late message handling or retransmission** for `PREPARE`, `GLOBAL_COMMIT`, `GLOBAL_ABORT`.

### 2.7 `TransactionManager` & `TransactionLog` (`src/transaction/*.cpp`)
- **Implemented:** Transaction map (`transactions_`), `beginTransaction`, `acquireLock`, `releaseLock`, `commitTransaction`, `abortTransaction`, `prepareTransaction`, `commitPrepared`, `abortPrepared`, append-only text WAL (`TransactionLog`).
- **Defects:**
  - **No Storage Engine:** There is no key-value store, no `PUT`/`GET`/`DELETE`/`SCAN`, no sharding, and no write buffering. `TransactionLog::logOperation` logs empty strings (`""`).
  - **Invalid state transitions:** `abortTransaction` does not check if a transaction is already `COMMITTED`; calling `abortTransaction` on a committed txn overwrites its state to `ABORTED` and writes `TXN_ABORT` to WAL.
  - **No crash recovery:** `TransactionLog::readLog()` exists and is tested in `test_all.cpp`, but is **never called** by `TransactionManager` or `Node` on startup. No undo/redo, no checkpoints, no checksums, no OS-level `FlushFileBuffers` / `fsync`.
  - **Non-idempotent completion:** Repeated `commitTransaction` / `abortTransaction` returns `false` rather than cleanly handling idempotent retries, and `rollbackTransaction` only releases locks without undoing data changes.

### 2.8 `Node` (`src/node/Node.cpp`, `src/node_main.cpp`)
- **Implemented:** Wires `NetworkManager`, `HeartbeatManager`, `TransactionManager`, `TwoPCCoordinator`, `DeadlockDetector`; standalone `dlm_node.exe` CLI.
- **Defects:**
  - `Node::lockResource` only calls local `txnManager_->acquireLock`—no shard ownership calculation, no remote `LOCK_REQUEST` RPC, no participant registration.
  - `Node::onNodeFailure` is a stub that only logs an error message.
  - `dlm_node.exe` blocks on `std::cin.get()`, making headless/background/Docker operation fragile without signal handling (`SIGINT`/`SIGTERM`).
  - All 3 nodes in `dlm_simulation.exe` share `logDirectory = "logs"`, and `Logger::setLogFile` is not configured per node.

### 2.9 Frontend (`index.html`, `styles.css`, `app.js`)
- **Critical Architectural Defect:** `app.js` is a 100% standalone, client-side JavaScript simulation (`DLMClusterDashboard`) with its own `this.nodes`, `this.transactions`, `this.lockTable`, `this.wfgEdges`, and `setTimeout`-faked 2PC animations.
- There is **no HTTP API**, **no WebSocket server**, and **no communication** with the C++ backend.

---

## 3. Feature Status Matrix (Baseline)

| Feature | Status | Notes |
| :--- | :--- | :--- |
| Local LockManager (S/X, queue) | Implemented (with upgrade/re-entry bugs) | Tested locally (6 tests) |
| DeadlockDetector (DFS + youngest victim) | Implemented (local only in practice) | Tested unit-only (4 tests); global WFG wait buggy |
| TransactionManager & Transaction | Partially Implemented | Local locks only; invalid state transitions possible |
| TransactionLog (WAL) | Partially Implemented | Text pipe log; no checksums, no fsync, no recovery |
| Key-Value Storage & Sharding | **Not Implemented** | Missing entirely |
| Remote Locking & Shard Routing | **Not Implemented** | `LOCK_RESPONSE` unhandled; `addParticipantNode` unused |
| Two-Phase Commit (2PC) | Partially Implemented | Non-durable `logDecision`, untested with `Node` |
| NetworkManager (TCP) | Partially Implemented | Windows-only, no timeouts/size bounds, thread leak |
| HeartbeatManager | Partially Implemented | No state machine, no startup peer tracking |
| HTTP REST + WebSocket API | **Not Implemented** | Missing entirely |
| SQL-like Command Console | **Not Implemented** | Missing entirely |
| Security / Auth | **Not Implemented** | Missing entirely |
| React + TypeScript Frontend | **Not Implemented** | Only standalone simulated `app.js` exists |
| Docker & Windows Cluster Scripts | **Not Implemented** | Only `launch_dashboard.bat`/`.ps1` for `index.html` |
