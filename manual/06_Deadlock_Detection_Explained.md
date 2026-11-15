# Chapter 6: Deadlock Detection & Resolution

Deadlocks are among the most classic concurrency hazards in operating systems and distributed databases. This chapter explains how the DLM represents dependency graphs, executes cycle detection, and aborts victim transactions.

---

## 1. Deadlock Fundamentals & Coffman Conditions

A **deadlock** is a state where a set of concurrent transactions are permanently blocked because each is holding a lock that another needs and waiting for a lock that another holds.

### The Four Coffman Conditions:
1. **Mutual Exclusion**: Resources are held in non-shareable exclusive mode.
2. **Hold and Wait**: Transactions hold allocated locks while waiting for additional locks.
3. **No Preemption**: Locks cannot be forcibly confiscated until released by the owner.
4. **Circular Wait**: A closed chain of transactions exists where each waits for the next:
   $$T_1 \to T_2 \to T_3 \to \dots \to T_n \to T_1$$

In distributed databases with Strict 2PL, the first three conditions are unavoidable invariants required for ACID consistency. Therefore, the system must detect and break the **Circular Wait** condition.

---

## 2. The Wait-For Graph (WFG)

A **Wait-For Graph** is a directed graph $G = (V, E)$:
- **Vertices ($V$)**: Active transactions ($T_1, T_2, T_3, \dots$).
- **Edges ($E$)**: A directed edge $T_A \to T_B$ means transaction $T_A$ is blocked waiting for a lock currently held by $T_B$.

```
       [Transaction 1] ──holds R1, wants R2──> [Transaction 2]
              ▲                                        │
              │                                holds R2, wants R1
              └────────────────────────────────────────┘
```
**Theorem**: A deadlock exists if and only if the Wait-For Graph contains a directed cycle.

---

## 3. Depth-First Search (DFS) Cycle Detection Algorithm

Cycle detection is implemented using **Tarjan/Kosaraju 3-coloring DFS**:

### The Three Colors:
- **WHITE (0)**: Node has not yet been visited.
- **GRAY (1)**: Node is currently being processed and is on the active DFS recursion stack.
- **BLACK (2)**: Node and all its descendants have been completely explored.

### Algorithm Logic:
```cpp
bool dfs(TxnId current, 
         std::unordered_map<TxnId, int>& color,
         std::vector<TxnId>& path,
         std::vector<TxnId>& cycle) {
    color[current] = 1; // Mark GRAY (in current path)
    path.push_back(current);

    for (TxnId neighbor : adj[current]) {
        if (color[neighbor] == 1) {
            // Found a back edge to a node on the active stack -> CYCLE DETECTED!
            auto it = std::find(path.begin(), path.end(), neighbor);
            cycle.assign(it, path.end());
            return true;
        } else if (color[neighbor] == 0) {
            if (dfs(neighbor, color, path, cycle)) return true;
        }
    }

    path.pop_back();
    color[current] = 2; // Mark BLACK (finished)
    return false;
}
```

---

## 4. Victim Selection Policy: Youngest Transaction

Once a cycle is identified, exactly one transaction in the cycle must be aborted to break the circular dependency.

### Policy: Youngest Transaction (Highest `TxnId`)
```cpp
TxnId DeadlockDetector::selectVictim(const std::vector<TxnId>& cycle) {
    TxnId victim = cycle[0];
    for (TxnId id : cycle) {
        if (id > victim) { // Assuming monotonically increasing TxnIds
            victim = id;
        }
    }
    return victim;
}
```

### Why pick the youngest transaction?
1. **Minimizes Lost Work**: Older transactions have typically acquired more locks and performed more computation. Aborting the youngest transaction preserves the work of older, long-running transactions.
2. **Prevents Starvation**: As transactions age, their transaction IDs remain fixed, guaranteeing they will eventually become older than any competing transaction and will never be repeatedly aborted.

---

## 5. Local vs. Global Deadlock Detection

In a distributed cluster, deadlocks can span multiple physical nodes:

### Example of a Distributed Deadlock:
- **Node A**: $T_1$ holds resource `A:10` and requests `A:20` (held by $T_2$).
  - Local edge on Node A: $T_1 \to T_2$. (No cycle locally!)
- **Node B**: $T_2$ holds resource `B:30` and requests `B:40` (held by $T_1$).
  - Local edge on Node B: $T_2 \to T_1$. (No cycle locally!)

Neither Node A nor Node B can detect this deadlock locally!

### Global Deadlock Resolution:
1. The designated Deadlock Coordinator broadcasts `WFG_REQUEST` to all nodes.
2. Each node gathers its local edges via `LockManager::getWaitForEdges()` and returns them in a `WFG_RESPONSE`.
3. The coordinator merges all local edge lists into a single global adjacency map:
   $$E_{\text{global}} = E_{\text{Node A}} \cup E_{\text{Node B}}$$
4. The coordinator runs DFS cycle detection over $E_{\text{global}}$, discovers $T_1 \to T_2 \to T_1$, selects the victim, and transmits an `ABORT` command to the victim's originating node.

---

## 6. Architecture Comparison: Branch Variations

In this repository, you will find two implementations of deadlock detection:

| Feature | `niharika-deadlock-detector` Branch | `krishna` Branch (Integrated DLM) |
| :--- | :--- | :--- |
| **Header** | `include/dlm/deadlock/deadlock_detector.h` | `include/dlm/deadlock/DeadlockDetector.h` |
| **Graph Model** | Dynamic, incremental state (`addDependency`, `removeDependency`) | Snapshot-based (`detectCycles(edges)`) |
| **Invocation** | Manual hook upon lock grant/release | Automatic periodic timer thread querying `LockManager` |
| **Victim Policy** | First node in cycle | Youngest transaction (`max_element`) |
| **Integration** | Standalone graph utility | Fully integrated with Node, Network, and 2PC |

Both approaches use the standard 3-color DFS algorithm. The integrated version in `krishna` combines local snapshots with network-collected global edges to seamlessly detect both single-node and multi-node deadlocks.
