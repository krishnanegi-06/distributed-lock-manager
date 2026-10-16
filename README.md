# Distributed Lock Manager for a Multi-Node Database Cluster

A full-featured **Distributed Lock Manager (DLM)** that enables multiple database server processes to coordinate read/write access to shared resources (rows/tables) across a cluster — the backbone of distributed transactions.

## Table of Contents

- [Overview](#overview)
- [Architecture](#architecture)
- [Features](#features)
- [OS & DBMS Concepts](#os--dbms-concepts)
- [Project Structure](#project-structure)
- [Building](#building)
- [Running](#running)
- [Testing](#testing)
- [Team Contributions](#team-contributions)

---

## Overview

Single-node locking is well understood (OS-level mutexes/semaphores), but **multi-node locking** — where processes on different machines must agree on who holds a lock — is what makes distributed databases like CockroachDB, Spanner, and TiDB fundamentally hard.

This project builds a scaled-down but fully functional distributed lock manager that demonstrates:

- **Lock coordination over TCP/IP** — nodes communicate via custom RPC
- **Local and global deadlock detection** — wait-for graph cycle detection
- **Two-Phase Commit (2PC)** — distributed transaction atomicity
- **Heartbeat-based failure detection** — node liveness monitoring
- **Transaction management with Write-Ahead Logging** — durability and recovery

## Architecture

```
┌──────────────────────────────────────────────────────────┐
│                    DLM Cluster                           │
│                                                          │
│  ┌─────────────┐  ┌─────────────┐  ┌─────────────┐     │
│  │   Node 1     │  │   Node 2     │  │   Node 3     │   │
│  │  ┌─────────┐ │  │  ┌─────────┐ │  │  ┌─────────┐ │  │
│  │  │Lock Mgr │ │  │  │Lock Mgr │ │  │  │Lock Mgr │ │  │
│  │  └────┬────┘ │  │  └────┬────┘ │  │  └────┬────┘ │  │
│  │  ┌────┴────┐ │  │  ┌────┴────┐ │  │  ┌────┴────┐ │  │
│  │  │Txn Mgr  │ │  │  │Txn Mgr  │ │  │  │Txn Mgr  │ │  │
│  │  └─────────┘ │  │  └─────────┘ │  │  └─────────┘ │  │
│  │  ┌─────────┐ │  │  ┌─────────┐ │  │  ┌─────────┐ │  │
│  │  │2PC Coord│ │  │  │2PC Coord│ │  │  │2PC Coord│ │  │
│  │  └─────────┘ │  │  └─────────┘ │  │  └─────────┘ │  │
│  │  ┌─────────┐ │  │  ┌─────────┐ │  │  ┌─────────┐ │  │
│  │  │Deadlock │ │  │  │Deadlock │ │  │  │Deadlock │ │  │
│  │  │Detector │ │  │  │Detector │ │  │  │Detector │ │  │
│  │  └─────────┘ │  │  └─────────┘ │  │  └─────────┘ │  │
│  └──────┬───────┘  └──────┬───────┘  └──────┬───────┘  │
│         │    TCP/IP       │                  │           │
│         └────────────────┼──────────────────┘           │
│                          │                               │
│              ┌───────────┴──────────┐                   │
│              │  NetworkManager      │                   │
│              │  (TCP Sockets)       │                   │
│              │  HeartbeatManager    │                   │
│              └──────────────────────┘                   │
└──────────────────────────────────────────────────────────┘
```

### Component Interaction

```
Transaction API (begin/lock/commit/abort)
         │
         ▼
    ┌─────────┐
    │  Node    │ ── orchestrates all sub-systems
    └────┬────┘
         │
    ┌────┼───────────────┬──────────────┬──────────────┐
    ▼    ▼               ▼              ▼              ▼
LockMgr  TxnMgr    2PC Coord     Deadlock Det    Heartbeat
(mutex/   (WAL,      (PREPARE→     (local WFG +   (periodic
condvar)  lifecycle)  VOTE→COMMIT)  global merge)  liveness)
    │         │           │              │              │
    └─────────┴───────────┴──────────────┴──────────────┘
                          │
                    NetworkManager
                    (TCP sockets)
```

## Features

| Feature | Description |
|---------|-------------|
| **Lock Server** | Grants/releases SHARED and EXCLUSIVE locks over TCP |
| **Lock Compatibility** | Shared locks coexist; exclusive locks are mutually exclusive |
| **Wait Queues** | Transactions queue up when locks are contended |
| **Local Deadlock Detection** | DFS-based cycle detection on per-node wait-for graph |
| **Global Deadlock Detection** | Merges wait-for graphs from all nodes to detect cross-node deadlocks |
| **Victim Selection** | Aborts the youngest transaction in a cycle (highest TxnId) |
| **Two-Phase Commit** | Coordinator sends PREPARE → collects VOTEs → COMMIT/ABORT |
| **Transaction Manager** | Full lifecycle: BEGIN → LOCK → COMMIT/ABORT with rollback |
| **Write-Ahead Log** | Append-only WAL for transaction durability and crash recovery |
| **Heartbeat** | Periodic heartbeats with timeout-based node failure detection |
| **Failure Handling** | Detects node failures and cleans up orphaned locks |
| **Isolation Levels** | READ_COMMITTED and SERIALIZABLE support |

## OS & DBMS Concepts

### OS Concepts Used

| Concept | Implementation |
|---------|---------------|
| **Inter-Process Communication** | TCP sockets for node-to-node RPC |
| **Process Synchronization** | `std::mutex` + `std::condition_variable` for local locking |
| **Deadlock Detection** | Wait-for graph (scheduling concept) with DFS cycle detection |
| **Timeout/Heartbeat** | Periodic liveness checks analogous to OS process health monitoring |
| **Concurrency** | Multi-threaded server, lock contention handling, concurrent transactions |

### DBMS Concepts Used

| Concept | Implementation |
|---------|---------------|
| **Distributed Transactions** | Cross-node transactions with 2PC coordination |
| **Two-Phase Commit** | PREPARE → VOTE → COMMIT/ABORT protocol |
| **Lock Granularity** | Row-level locking with resource ID abstraction |
| **Transaction Logs** | Write-Ahead Log (WAL) for durability |
| **ACID Properties** | Atomicity (2PC), Consistency (locks), Isolation (lock modes), Durability (WAL) |
| **Isolation Levels** | READ_COMMITTED and SERIALIZABLE via lock duration policies |

### OS ↔ DBMS Interaction

1. **Local → Distributed Escalation**: Each node's local lock manager uses OS-level `mutex`/`condvar`. When cross-node contention occurs, requests are escalated over the network to the distributed lock manager.

2. **Wait-For Graph Merging**: Each node maintains a local WFG (an OS scheduling concept). The global deadlock detector merges these into a unified graph (a DBMS concept) to detect cross-node cycles and decide transaction aborts.

## Project Structure

```
distribute_lock_manager/
├── CMakeLists.txt                          # Build configuration
├── README.md                               # This file
├── include/dlm/
│   ├── common/
│   │   ├── Types.h                         # Shared types (TxnId, NodeId, enums)
│   │   └── Logger.h                        # Thread-safe logger with levels
│   ├── lock_manager/
│   │   └── LockManager.h                   # Local lock manager
│   ├── network/
│   │   ├── Message.h                       # Wire protocol message types
│   │   ├── NetworkManager.h                # TCP server/client
│   │   └── HeartbeatManager.h              # Node liveness detection
│   ├── coordinator/
│   │   └── TwoPCCoordinator.h              # Two-Phase Commit protocol
│   ├── deadlock/
│   │   └── DeadlockDetector.h              # Local + global deadlock detection
│   ├── transaction/
│   │   ├── Transaction.h                   # Transaction data class
│   │   ├── TransactionLog.h                # Write-Ahead Log
│   │   └── TransactionManager.h            # Transaction lifecycle
│   └── node/
│       └── Node.h                          # Top-level node orchestrator
├── src/
│   ├── common/Logger.cpp
│   ├── lock_manager/LockManager.cpp
│   ├── network/Message.cpp
│   ├── network/NetworkManager.cpp
│   ├── network/HeartbeatManager.cpp
│   ├── coordinator/TwoPCCoordinator.cpp
│   ├── deadlock/DeadlockDetector.cpp
│   ├── transaction/Transaction.cpp
│   ├── transaction/TransactionLog.cpp
│   ├── transaction/TransactionManager.cpp
│   ├── node/Node.cpp
│   ├── node_main.cpp                       # Single-node CLI launcher
│   └── simulation_main.cpp                 # Multi-node simulation
├── tests/
│   └── test_all.cpp                        # Unit & integration tests
└── logs/                                   # Runtime log output
```

## Building

### Prerequisites

- **CMake** 3.16+
- **C++17** compiler (MSVC 2019+, GCC 8+, or Clang 10+)
- **Windows**: Winsock2 (included with Windows SDK)

### Build Steps

```bash
# Configure
mkdir build && cd build
cmake .. -G "Visual Studio 17 2022" -A x64

# Build
cmake --build . --config Debug

# Or on Linux/macOS:
mkdir build && cd build
cmake ..
make -j$(nproc)
```

### Build Outputs

| Executable | Description |
|-----------|-------------|
| `dlm_node` | Start a single DLM node (CLI) |
| `dlm_simulation` | Run multi-node simulation with 5 scenarios |
| `dlm_tests` | Run unit and integration tests |

## Running

### Multi-Node Simulation (Recommended)

Launches 3 nodes in-process and runs all demo scenarios:

```bash
./build/Debug/dlm_simulation.exe
```

**Scenarios demonstrated:**
1. Shared/Exclusive lock compatibility
2. Local deadlock detection and victim abort
3. Transaction commit with lock release verification
4. Multi-node lock acquisition across 3 nodes
5. Wait-for graph edge visualization (OS↔DBMS interaction)

### Individual Node (Multi-Process)

Start nodes in separate terminals:

```bash
# Terminal 1
./build/Debug/dlm_node.exe 1 9001 2:127.0.0.1:9002 3:127.0.0.1:9003

# Terminal 2
./build/Debug/dlm_node.exe 2 9002 1:127.0.0.1:9001 3:127.0.0.1:9003

# Terminal 3
./build/Debug/dlm_node.exe 3 9003 1:127.0.0.1:9001 2:127.0.0.1:9002
```

## Testing

```bash
# Run all tests
./build/Debug/dlm_tests.exe

# Or via CTest
cd build && ctest -C Debug --verbose
```

### Test Coverage

| Module | Tests | Description |
|--------|-------|-------------|
| LockManager | 6 | Shared/exclusive compatibility, blocking, release, WFG edges |
| DeadlockDetector | 4 | No-cycle, simple cycle, 3-way cycle, empty input |
| Transaction | 1 | Lifecycle, resource tracking, distributed awareness |
| TransactionManager | 3 | Begin/commit, abort/rollback, 2PC prepare |
| TransactionLog | 1 | WAL write/read roundtrip |
| Message | 2 | Serialization roundtrip, empty fields |

## Team Contributions

| Member | Role | Key Responsibilities |
|--------|------|---------------------|
| **Member 1** | Technical Lead / Core Architect | Core architecture, LockManager, NetworkManager, 2PC Coordinator, Global Deadlock Detection, Node Integration, Code Review |
| **Member 2** | Backend / Systems Developer | TransactionManager, Transaction Log/WAL, Local Deadlock Detection, HeartbeatManager, Isolation Levels |
| **Member 3** | Simulation & Integration Developer | Node class, Multi-node simulation runner, CLI launcher, Enhanced Logger |
| **Member 4** | QA / Documentation / DevOps | Unit tests, Integration tests, README, Architecture docs, Build system, .gitignore |

---

## License

MIT License — see [LICENSE](LICENSE) for details.
