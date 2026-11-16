# Chapter 10: Simulation Walkthrough

The simulation runner (`src/simulation_main.cpp`) is an end-to-end multi-threaded demonstration of the Distributed Lock Manager operating under realistic distributed database workloads.

---

## 1. Simulation Architecture

Instead of requiring three separate physical computers, `simulation_main.cpp` instantiates three fully functional `dlm::Node` instances within a single process:
- **Node 1**: Listening on `127.0.0.1:9001` (Peers: Node 2, Node 3)
- **Node 2**: Listening on `127.0.0.1:9002` (Peers: Node 1, Node 3)
- **Node 3**: Listening on `127.0.0.1:9003` (Peers: Node 1, Node 2)

Each node runs its own independent network server thread, heartbeat broadcaster, deadlock detector, and Write-Ahead Log.

---

## 2. Walkthrough of the 5 Demonstration Scenarios

### Scenario 1: Local Shared / Exclusive Locking
- **Objective**: Verify that multiple shared locks coexist, while an exclusive lock is blocked and times out cleanly.
- **Actions**:
  1. Transaction $T_1$ requests `SHARED` lock on `"row_100"` $\to$ **GRANTED**.
  2. Transaction $T_2$ requests `SHARED` lock on `"row_100"` $\to$ **GRANTED** (concurrent read).
  3. Transaction $T_3$ requests `EXCLUSIVE` lock on `"row_100"` with 5000ms timeout $\to$ Blocks on `condition_variable`.
  4. After 5000ms, $T_3$ times out $\to$ **DENIED (timeout)**.
- **Log Output**:
  ```text
  [INFO][22:29:25.999][21408] T1 SHARED lock: GRANTED
  [INFO][22:29:25.999][21408] T2 SHARED lock: GRANTED
  [INFO][22:29:25.999][21408] T3 acquiring EXCLUSIVE lock on row_100 (will timeout)...
  [INFO][22:29:31.008][21408] T3 EXCLUSIVE lock: DENIED (timeout)
  ```

---

### Scenario 2: Local Deadlock Detection & Victim Abort
- **Objective**: Create a circular dependency between two transactions, detect the cycle via DFS, and automatically abort the youngest transaction.
- **Actions**:
  1. $T_A$ acquires `EXCLUSIVE` lock on `"row_A"`.
  2. $T_B$ acquires `EXCLUSIVE` lock on `"row_B"`.
  3. Thread 1 launches: $T_A$ attempts to lock `"row_B"` (held by $T_B$, blocks).
  4. Thread 2 launches: $T_B$ attempts to lock `"row_A"` (held by $T_A$, blocks).
  5. Wait-For Graph now contains: $T_A \to T_B$ and $T_B \to T_A$ (Cycle!).
  6. Deadlock detector fires, detects cycle, selects $T_B$ (higher ID = youngest), and calls `abortTransaction(TB)`.
  7. $T_B$ is rolled back; its lock on `"row_B"` is released; $T_A$ unblocks and completes.
- **Log Output**:
  ```text
  [INFO][22:29:31.010][21408] TA=4 holds row_A, TB=5 holds row_B
  [INFO][22:29:31.010][21408] Now creating circular wait to trigger deadlock detection...
  [WARN][22:29:32.529][3648] Deadlock victim: txn 5
  [INFO][22:29:35.014][21408] TA state: ACTIVE
  [INFO][22:29:35.014][21408] TB state: ABORTED
  ```

---

### Scenario 3: Transaction Commit & Lock Eviction
- **Objective**: Verify that transaction commit properly flushes the WAL log and releases all held locks.
- **Actions**:
  1. Transaction $T_1$ begins with `SERIALIZABLE` isolation.
  2. Acquires exclusive locks on `"row_200"` and `"row_201"`.
  3. Simulates business logic work for 500ms.
  4. Commits transaction.
  5. Queries `LockManager::checkLock()` to verify locks are freed.
- **Log Output**:
  ```text
  [INFO][22:29:35.016][21408] T1 holds EXCLUSIVE on row_200 and row_201
  [INFO][22:29:35.530][21408] T1 commit: SUCCESS
  [INFO][22:29:35.531][21408] After commit, T1 still holds row_200? NO
  ```

---

### Scenario 4: Multi-Node Cluster Locking
- **Objective**: Demonstrate multi-node concurrency across distinct nodes in the cluster.
- **Actions**:
  1. Node 1 starts $T_1$ and locks `"table:accounts:row_1"`.
  2. Node 2 starts $T_2$ and locks `"table:accounts:row_2"`.
  3. Node 3 starts $T_3$ and locks `"table:accounts:row_3"`.
  4. Locks are held simultaneously across all three nodes.
  5. All three transactions commit cleanly.
- **Log Output**:
  ```text
  [INFO][22:29:35.532][21408] Node1 T6 → row_1, Node2 T2 → row_2, Node3 T1 → row_3
  [INFO][22:29:35.532][21408] --- Locks held across 3 nodes ---
  [INFO][22:29:36.601][21408] All transactions committed across 3 nodes.
  ```

---

### Scenario 5: Wait-For Graph (OS ↔ DBMS Bridge)
- **Objective**: Inspect the real-time Wait-For Graph extracted directly from operating system wait queues.
- **Actions**:
  1. $T_X$ acquires exclusive lock on `"row_WFG"`.
  2. $T_Y$ in a worker thread attempts to lock `"row_WFG"` and enters the wait queue.
  3. Main thread calls `getWaitForEdges()` to inspect the OS wait queue.
  4. Emits directed edge: `T_Y → T_X on row_WFG`.
  5. $T_X$ commits, releasing the lock, allowing $T_Y$ to acquire and commit.
- **Log Output**:
  ```text
  [INFO][22:29:36.602][21408] TX=2 holds EXCLUSIVE on row_WFG
  [INFO][22:29:36.630][15604] TY=3 requesting EXCLUSIVE on row_WFG (will block)...
  [INFO][22:29:37.112][21408] Current wait-for edges (local WFG):
  [INFO][22:29:37.112][21408]   T3 → T2 on row_WFG
  ```

---

### Cluster Teardown & Heartbeat Failure Detection
When the simulation finishes, nodes stop in sequence:
1. Node 1 stops its network listener and heartbeat thread.
2. Nodes 2 and 3 notice the absence of heartbeats from Node 1.
3. Node 2 logs: `[ERROR] Node 1 detected as FAILED — cleaning up locks.`
4. Nodes 2 and 3 safely evict any orphaned locks belonging to Node 1.
