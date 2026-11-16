# Deadlock Detection & Resolution

## 1. Wait-For Graph (WFG) Construction

`LockManager::getWaitForEdges()` produces directed edges `waitingTxn -> holdingTxn` across:
1. Waiting transactions blocked by current lock owners.
2. Queued waiters blocked behind incompatible earlier waiters in the FIFO queue.

## 2. Global Wait-For Graph Aggregation

`Node::collectGlobalEdges()` runs periodically on each node:
1. Queries `HeartbeatManager::getAliveNodes()` so unresponsive/crashed nodes do not stall collection.
2. Assigns a monotonic `wfgRoundId_` (`requestId`) and broadcasts `WFG_REQUEST`.
3. Waits up to `600ms` with an explicit completion predicate (`wfgRespondedPeers_.size() >= expected`) rather than unpredicated wakeups.
4. Merges local and remote edges using globally unique 64-bit `TxnId`s.

## 3. Cycle Detection & Victim Selection

`DeadlockDetector` (`src/deadlock/DeadlockDetector.cpp`) runs 3-color DFS (`WHITE`/`GRAY`/`BLACK`) over sorted nodes and canonicalizes detected cycles to avoid duplicate victim selection across multiple simultaneous cycles.

- **Victim Policy:** Youngest transaction in the cycle (highest `txnSequence(txnId)`, tie-broken by full `TxnId`).
- **Distributed Victim Abort:** If `txnOriginNode(victim)` is local, `Node` aborts locally (and broadcasts `GLOBAL_ABORT` if distributed). If remote, `Node` sends `ABORT_TXN` to the originating coordinator node and cleans up any local participant state.
