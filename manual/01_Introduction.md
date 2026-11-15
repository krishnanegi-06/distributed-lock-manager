# Chapter 1: Introduction to Distributed Lock Managers

## 1.1 What is a Distributed Lock Manager (DLM)?

In computer systems, when two threads within the same process want to modify the same variable, we use an **OS mutex** (`std::mutex`, POSIX `pthread_mutex_t`, or Windows `CRITICAL_SECTION`). The operating system's kernel acts as an impartial referee, granting access to one thread and putting other threads to sleep until the resource is freed.

Now consider modern distributed architectures:
* What if the programs trying to access the resource are running on **completely different physical servers** across a data center?
* What if there is **no shared RAM**, no common CPU cache, and no single OS kernel to arbitrate access?

This is where a **Distributed Lock Manager (DLM)** is required.

> **Definition**: A **Distributed Lock Manager (DLM)** is a software service that coordinates mutually exclusive and shared access to shared digital resources (such as database rows, tables, files, or distributed data structures) across multiple independent computer nodes connected via a network.

```
Single-Node Locking (Shared Memory):
Thread 1 ──┐
           ├─► [ OS Kernel / std::mutex ] ──► [ Shared Variable in RAM ]
Thread 2 ──┘

Distributed Locking (Shared Nothing Network):
Server Node 1 (Txn 1) ──┐
                        ├─► [ Network / TCP Sockets ] ──► [ Lock Manager on Node 1/2/3 ]
Server Node 2 (Txn 2) ──┘                                      │
                                                               ▼
                                                      [ Distributed Resource ]
                                                      (e.g., "table:orders:42")
```

A DLM provides an abstraction layer where transactions can issue commands like:
* *"Grant me a Shared (Read) Lock on row 100"*
* *"Grant me an Exclusive (Write) Lock on row 100"*
* *"Release all locks held by Transaction 42"*

The DLM ensures that even under heavy concurrency, network delays, and node failures:
1. **Safety**: Two conflicting transactions are never simultaneously granted conflicting locks on the same resource.
2. **Liveness**: If a resource is freed, waiting transactions will eventually be awakened or safely timed out.
3. **Deadlock Resolution**: If transactions form a circular dependency waiting for each other, the cycle is detected and broken.

---

## 1.2 Real-World Distributed Databases

Distributed locking is not just an academic exercise; it is the fundamental foundation of modern **NewSQL** and cloud-native databases that power global finance, e-commerce, and cloud infrastructure:

### 1. Google Cloud Spanner
Google Spanner is a globally distributed, synchronously replicated database offering external consistency (linearizability).
* **How it uses a DLM**: Every Paxos leader in Spanner maintains a local lock table to implement strict **Two-Phase Locking (2PL)** for read-write transactions.
* **Why it matters**: To support serializable transactions across datacenters worldwide, Spanner uses hardware GPS/atomic clocks (TrueTime) alongside distributed read/write locks.

### 2. CockroachDB
CockroachDB is an open-source, cloud-native SQL database architected to survive datacenter failures without human intervention.
* **How it uses a DLM**: CockroachDB implements **intent locks** stored directly alongside multi-version data in its underlying key-value engine (Pebble/RocksDB). If transaction $T_2$ encounters an intent left behind by $T_1$, $T_2$ must either wait in a lock table queue or push $T_1$'s timestamp.

### 3. PingCAP TiDB
TiDB is an open-source distributed Hybrid Transactional/Analytical Processing (HTAP) database compatible with MySQL.
* **How it uses a DLM**: TiDB uses a distributed transaction model based on Google's Percolator paper. In its pessimistic transaction mode, TiDB uses a dedicated distributed lock manager protocol over gRPC with a centralized deadlock detector to coordinate locks on row keys across the storage nodes (TiKV).

---

## 1.3 Why Is This Project Important?

Building a DLM from scratch in modern C++17 provides deep insights into systems engineering that cannot be gained from textbooks alone:

1. **Overcoming Network Uncertainty**: Unlike memory accesses which take nanoseconds and always succeed (barring hardware crashes), network messages over TCP can take variable milliseconds, arrive out of order, or be dropped if a server dies.
2. **Handling Asynchronous Concurrency**: Code cannot simply "wait forever". Threads must handle timeouts gracefully, wake up cleanly, and release intermediate resources when an operation is cancelled.
3. **Mastering Dual-Level State**: Every node in our DLM manages state both locally (via in-memory hash tables and standard library mutexes) and globally (via network RPCs, heartbeats, and 2-Phase Commit).

---

## 1.4 Operating Systems (OS) Concepts Involved

The DLM relies heavily on fundamental concepts taught in core Operating Systems curricula:

### 1. Mutual Exclusion (`std::mutex` & `std::unique_lock`)
Every node contains in-memory data structures (such as `lockTable_`, `waiters_`, and `txnLocks_`). Multiple network worker threads might try to modify these tables at the exact same microsecond. We use `std::mutex` to ensure strict mutual exclusion, preventing race conditions and heap corruption.

### 2. Condition Variables (`std::condition_variable`)
When transaction $T_2$ wants an exclusive lock on `row_1`, but $T_1$ already holds it, $T_2$ cannot busy-wait in an infinite `while` loop (which wastes 100% of the CPU). Instead, the thread calls `cv_.wait_for()`, putting the OS thread to sleep. When $T_1$ finally releases its lock, it calls `cv_.notify_all()`, instructing the OS scheduler to wake up sleeping threads.

### 3. Inter-Process Communication (IPC) via Sockets
Nodes in the DLM run as independent processes (or threads mimicking isolated physical servers). They communicate over TCP/IP sockets using Windows Sockets 2 (Winsock), establishing client-server socket connections, framing variable-length binary payloads, and exchanging messages without shared address space.

### 4. Deadlock Theory & Wait-For Graphs (WFG)
Textbooks define Coffman's four conditions for deadlock:
1. Mutual exclusion
2. Hold and wait
3. No preemption
4. Circular wait

In this project, rather than trying to prevent deadlocks (which causes excessive lock contention and reduces throughput), we implement **deadlock detection**. We construct a directed graph where nodes represent transactions and edges represent dependencies ($T_A \to T_B$ means $T_A$ is waiting for $T_B$). We run **Depth-First Search (DFS)** to detect cycles and automatically abort a victim transaction.

---

## 1.5 Database Management Systems (DBMS) Concepts Involved

From a database perspective, the DLM implements the core machinery of distributed query engines and storage managers:

### 1. Transactions & ACID Guarantees
* **Atomicity**: The transaction either completely finishes or has zero effect. We achieve this through Two-Phase Commit (2PC) and rollback handlers.
* **Consistency**: Invariants (such as account balances not dropping below zero) are protected by ensuring no conflicting reads or writes corrupt state.
* **Isolation**: Concurrent transactions cannot observe partial, uncommitted state. We support both `READ_COMMITTED` and `SERIALIZABLE` isolation.
* **Durability**: Once committed, state changes survive power outages. We implement this through Write-Ahead Logging (WAL) written to disk.

### 2. Shared vs. Exclusive Lock Modes
Following the classical database compatibility matrix:
* **SHARED (S)**: Read-only locks. Multiple transactions can hold SHARED locks on the same resource concurrently.
* **EXCLUSIVE (X)**: Write locks. Only one transaction can hold an EXCLUSIVE lock on a resource, and no other transactions may hold either S or X locks simultaneously.

```
Lock Compatibility Matrix:
             ┌────────────────────────┐
             │       Requested        │
             ├────────────┬───────────┤
             │   SHARED   │ EXCLUSIVE │
┌───┬────────┼────────────┼───────────┤
│ H │ SHARED │   OK (✔)   │  NO (✖)   │
│ e ├────────┼────────────┼───────────┤
│ l │ EXCLUS │   NO (✖)   │  NO (✖)   │
│ d │        │            │           │
└───┴────────┴────────────┴───────────┘
```

### 3. Two-Phase Commit (2PC)
When a transaction touches resources on multiple nodes (e.g., deducting money on Node 1 and crediting an account on Node 2), neither node can commit independently. If Node 2 crashes while Node 1 commits, atomicity is violated. We implement the two-phase commit protocol:
* **Phase 1 (Prepare)**: Coordinator asks all participants: *"Can you commit?"* Participants lock resources, flush their logs, and vote `YES` or `NO`.
* **Phase 2 (Decision)**: If all vote `YES`, coordinator broadcasts `GLOBAL_COMMIT`. If even a single participant votes `NO` (or times out), coordinator broadcasts `GLOBAL_ABORT`.

### 4. Write-Ahead Logging (WAL)
The cardinal rule of database recovery is: **Never modify database state on disk without first appending an immutable record of the change to persistent storage.** In this project, `TransactionLog` maintains append-only logs (`node_1_wal.log`) tracking `TXN_BEGIN`, `TXN_PREPARE`, `OPERATION`, `TXN_COMMIT`, and `TXN_ABORT`.

---

## 1.6 How OS and DBMS Concepts Interact in This Project

The most exciting aspect of this project is the elegant intersection of Operating Systems concepts and Database Management concepts:

```
┌────────────────────────────────────────────────────────────────────────┐
│                        OS ↔ DBMS INTERSECTION                          │
├──────────────────────────────────┬─────────────────────────────────────┤
│      OS Concurrency Domain       │        DBMS Transaction Domain      │
├──────────────────────────────────┼─────────────────────────────────────┤
│ OS Thread (`std::thread`)        │ Database Transaction (`TxnId`)      │
│ In-Memory Mutex (`std::mutex`)   │ Logical Resource Lock (`LockEntry`) │
│ Condition Variable (`cv_.wait`)  │ Lock Wait Queue (`waiters_`)        │
│ Process Thread Schedulers        │ Two-Phase Commit Coordinator (2PC)  │
│ Deadlock in OS Processes         │ Distributed Deadlock in Txn WFG     │
│ POSIX/Winsock Network Sockets    │ Distributed RPC & Wire Protocol     │
│ File Descriptor / Page Cache     │ Write-Ahead Log (`TransactionLog`)  │
└──────────────────────────────────┴─────────────────────────────────────┘
```

1. **Local Concurrency Escalates to Distributed Concurrency**:
   Inside a single node, lock requests use OS-level synchronization (`std::mutex` and `std::condition_variable`). When a transaction requires a resource owned by another cluster node, the local system delegates the request through the socket network layer.

2. **Wait-For Graph Merging**:
   Locally, a node's `LockManager` tracks wait edges between transactions. In isolation, Node 1 sees $T_1 \to T_2$, and Node 2 sees $T_2 \to T_1$. Neither node has a local cycle! But when the global deadlock detector collects and merges these edges over TCP, the global cycle $T_1 \to T_2 \to T_1$ is revealed, prompting a coordinated victim abort.

3. **Crash Recovery & Heartbeats**:
   The `HeartbeatManager` continuously monitors node health via periodic socket pings. If an OS process crashes or machine disconnects, the failure callback cleans up orphaned locks, mimicking OS process termination handlers while preserving DBMS ACID properties.
