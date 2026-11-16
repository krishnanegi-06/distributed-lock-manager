# Distributed Lock Manager (DLM) — Student Project Manual

Welcome to the comprehensive, student-friendly engineering and theory manual for the **Distributed Lock Manager (DLM)** project.

This manual is written specifically for computer science and software engineering students studying **Operating Systems (OS)**, **Database Management Systems (DBMS)**, and **Distributed Systems**. It bridges academic theory—such as mutual exclusion, wait-for graphs, two-phase commit, and write-ahead logging—with real-world, production-quality C++17 systems programming.

---

## Master Table of Contents

| Chapter | Title | Focus & Core Concepts |
| :--- | :--- | :--- |
| **[Chapter 1](01_Introduction.md)** | **System Overview & Concepts** | What is a DLM, real-world databases (Spanner, CockroachDB, TiDB), OS & DBMS theory intersection. |
| **[Chapter 2](02_Architecture.md)** | **System Architecture & Design** | 7-module decomposition, data flow diagrams, threading model, and network architecture. |
| **[Chapter 3](03_Build_and_Run.md)** | **Build & Execution Guide** | CMake configuration, MSVC/C++17 builds, running tests, simulations, and troubleshooting. |
| **[Chapter 4](04_LockManager_Explained.md)** | **Lock Manager Deep Dive** | Shared vs. Exclusive locks, wait queues, mutexes, condition variables, and step-by-step code walkthrough. |
| **[Chapter 5](05_Network_Layer_Explained.md)** | **Network & IPC Layer** | TCP sockets, wire framing, custom binary/pipe serialization, RPC dispatch, and HeartbeatManager. |
| **[Chapter 6](06_Deadlock_Detection_Explained.md)** | **Deadlock Detection & Resolution** | Wait-For Graphs (WFG), DFS cycle detection (White/Gray/Black coloring), and victim abort policy. |
| **[Chapter 7](07_Transaction_System_Explained.md)** | **Transaction Management & WAL** | Transaction state machine, ACID guarantees, Write-Ahead Logging (WAL), and isolation levels. |
| **[Chapter 8](08_TwoPhaseCommit_Explained.md)** | **Two-Phase Commit (2PC) Protocol** | Distributed consensus, PREPARE/COMMIT protocol, coordinator state machine, and failure handling. |
| **[Chapter 9](09_Node_Integration_Explained.md)** | **Node Integration & Orchestration** | How the `Node` class glues all 6 subsystems into an autonomous, fault-tolerant cluster member. |
| **[Chapter 10](10_Simulation_Walkthrough.md)** | **Simulation Walkthrough** | Line-by-line breakdown of the 5 simulation scenarios, log outputs, and verification points. |
| **[Chapter 11](11_Testing_Guide.md)** | **Testing Guide & Test Suite** | Verification of all 17 unit and integration tests, testing methodology, and adding new tests. |
| **[Chapter 12](12_Terminal_Commands_Reference.md)** | **Terminal Commands Reference** | Complete cheat-sheet for PowerShell, CMake, CTest, Git, and WAL log examination. |
| **[Chapter 13](13_OS_DBMS_Concepts_Mapping.md)** | **OS & DBMS Theory Mapping** | Master matrix cross-referencing textbook theory to project headers, source files, and functions. |
| **[Chapter 14](14_Glossary.md)** | **Comprehensive Glossary** | Alphabetical dictionary of distributed systems, OS concurrency, and database terms. |

---

## Suggested Learning Paths

### Path A: The Systems & Concurrency Student (Focus: OS & Multithreading)
If you are currently taking an **Operating Systems** course or want to master low-level concurrency:
1. Start with [Chapter 1: Introduction](01_Introduction.md) for the foundational motivation.
2. Read [Chapter 4: Lock Manager](04_LockManager_Explained.md) to understand mutexes, condition variables, and wait queues.
3. Read [Chapter 6: Deadlock Detection](06_Deadlock_Detection_Explained.md) to study graph algorithms and resource allocation cycles.
4. Review [Chapter 13: OS & DBMS Concepts Mapping](13_OS_DBMS_Concepts_Mapping.md) to review exam-ready mappings.

### Path B: The Database & Distributed Systems Student (Focus: DBMS & Networks)
If you are studying **Database Systems**, **Storage Engines**, or **Cloud Systems**:
1. Review [Chapter 2: Architecture](02_Architecture.md) to visualize the multi-node cluster layout.
2. Study [Chapter 7: Transaction System & WAL](07_Transaction_System_Explained.md) for ACID, isolation levels, and write-ahead logging.
3. Master [Chapter 8: Two-Phase Commit](08_TwoPhaseCommit_Explained.md) to understand distributed transaction atomicity.
4. Read [Chapter 5: Network Layer](05_Network_Layer_Explained.md) to see how RPCs coordinate distributed state over TCP.

### Path C: The Hands-On Developer (Focus: Compiling, Debugging & Extending)
If you want to immediately build, run, and experiment with the code:
1. Follow [Chapter 3: Build & Run Guide](03_Build_and_Run.md) to compile with CMake and Visual Studio.
2. Run the unit tests via [Chapter 11: Testing Guide](11_Testing_Guide.md).
3. Execute the simulation scenarios in [Chapter 10: Simulation Walkthrough](10_Simulation_Walkthrough.md).
4. Use [Chapter 12: Terminal Commands Reference](12_Terminal_Commands_Reference.md) whenever you need quick CLI recipes.

---

## Repository Quick Reference

```
distribute_lock_manager/
├── CMakeLists.txt              # Unified CMake build script
├── README.md                   # Project overview & high-level summary
├── index.html                  # Visual Cluster Dashboard UI
├── styles.css                  # Dashboard modern styles
├── app.js                      # WFG canvas renderer & state engine
├── launch_dashboard.bat        # 1-Click Windows dashboard launcher
├── manual/                     # This student manual (15 markdown files)
├── include/dlm/                # Public C++ interface headers
│   ├── common/                 # Types.h, Logger.h
│   ├── lock_manager/           # LockManager.h
│   ├── network/                # Message.h, NetworkManager.h, HeartbeatManager.h
│   ├── coordinator/            # TwoPCCoordinator.h
│   ├── deadlock/               # DeadlockDetector.h
│   ├── transaction/            # Transaction.h, TransactionLog.h, TransactionManager.h
│   └── node/                   # Node.h
├── src/                        # Implementations & executables
│   ├── common/                 # Logger.cpp
│   ├── lock_manager/           # LockManager.cpp
│   ├── network/                # Message.cpp, NetworkManager.cpp, HeartbeatManager.cpp
│   ├── coordinator/            # TwoPCCoordinator.cpp
│   ├── deadlock/               # DeadlockDetector.cpp
│   ├── transaction/            # Transaction.cpp, TransactionLog.cpp, TransactionManager.cpp
│   ├── node/                   # Node.cpp
│   ├── node_main.cpp           # Single-node standalone CLI process
│   └── simulation_main.cpp     # Multi-node in-process simulation runner
├── tests/
│   └── test_all.cpp            # 17 Unit & integration tests
└── logs/                       # Runtime WAL logs (*_wal.log)
```
