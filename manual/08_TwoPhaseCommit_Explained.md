# Chapter 8: Two-Phase Commit (2PC) Protocol

The **Two-Phase Commit (2PC)** protocol (`include/dlm/coordinator/TwoPCCoordinator.h` and `src/coordinator/TwoPCCoordinator.cpp`) guarantees atomic commitment across multiple distributed nodes. It ensures that either **all** participants commit their portion of a distributed transaction or **all** roll back.

---

## 1. The Distributed Atomicity Problem

In a single-node database, atomic commitment is straightforward: write a `COMMIT` record to the local WAL.
In a distributed database:
- Transaction $T_1$ modifies rows on **Node 1**, **Node 2**, and **Node 3**.
- What if Node 1 and Node 2 commit, but Node 3 crashes or violates a constraint?
- If Node 3 aborts while Node 1 and Node 2 commit, the database enters an inconsistent, corrupt state!

Two-Phase Commit solves this by splitting commit into **Voting (Phase 1)** and **Decision (Phase 2)**.

---

## 2. Protocol Walkthrough

```
COORDINATOR (Node 1)                  PARTICIPANTS (Node 2 & Node 3)
      │                                             │
      │ ───────────── Phase 1: PREPARE ───────────► │
      │                                             │ (Check locks, WAL log)
      │ ◄────────── VOTE_YES / VOTE_NO ──────────── │
      │                                             │
[Evaluate Votes]                                    │
  All YES? ──► COMMIT                               │
  Any NO?  ──► ABORT                                │
      │                                             │
      │ ───────── Phase 2: COMMIT / ABORT ────────► │
      │                                             │ (Apply changes & release locks)
      ▼                                             ▼
```

### Phase 1: Prepare (Voting Phase)
1. **Coordinator Broadcast**: Coordinator sends `PREPARE` message to every participant node.
2. **Participant Evaluation**:
   - Each participant verifies that its local operations succeeded and all required locks are held.
   - It writes `PREPARE` to its local WAL log.
   - If ready to commit, it replies with `VOTE_YES`.
   - If unable to commit (e.g. lock conflict or aborted by deadlock), it replies with `VOTE_NO`.

### Phase 2: Commit / Decision Phase
1. **Coordinator Decision**:
   - **Case A (Unanimous YES)**: If *every* participant replied with `VOTE_YES` before timeout, the coordinator logs `COMMIT` and broadcasts `GLOBAL_COMMIT`.
   - **Case B (Any NO or Timeout)**: If *even one* node voted `VOTE_NO` or fails to respond within the timeout window, the coordinator logs `ABORT` and broadcasts `GLOBAL_ABORT`.
2. **Participant Execution**:
   - Upon receiving `GLOBAL_COMMIT`, participants transition to `COMMITTED` and release locks.
   - Upon receiving `GLOBAL_ABORT`, participants transition to `ABORTED`, roll back changes, and release locks.

---

## 3. Implementation in `TwoPCCoordinator.cpp`

### `coordinateCommit()`
```cpp
bool TwoPCCoordinator::coordinateCommit(TxnId txnId, const std::vector<NodeId>& participants) {
    if (participants.empty()) return true;

    // Step 1: Register transaction state
    {
        std::lock_guard<std::mutex> lock(mtx_);
        CoordTxn& ctx = activeTxns_[txnId];
        ctx.participants = participants;
        ctx.expectedVotes = participants.size();
        ctx.state = CoordState::PREPARING;
    }

    // Step 2: Broadcast PREPARE to all participants
    Message prepMsg;
    prepMsg.type = MessageType::PREPARE;
    prepMsg.senderId = localNodeId_;
    prepMsg.txnId = txnId;

    for (NodeId p : participants) {
        networkManager_.sendMessage(p, prepMsg);
    }

    // Step 3: Wait for votes with timeout
    std::unique_lock<std::mutex> lock(activeTxns_[txnId].mtx);
    bool done = activeTxns_[txnId].cv.wait_for(lock, timeout_, [&]() {
        return activeTxns_[txnId].yesVotes + activeTxns_[txnId].noVotes >= activeTxns_[txnId].expectedVotes;
    });

    // Step 4: Evaluate votes
    bool allYes = done && (activeTxns_[txnId].yesVotes == activeTxns_[txnId].expectedVotes);

    // Step 5: Broadcast Decision
    Message decMsg;
    decMsg.type = allYes ? MessageType::GLOBAL_COMMIT : MessageType::GLOBAL_ABORT;
    decMsg.senderId = localNodeId_;
    decMsg.txnId = txnId;

    for (NodeId p : participants) {
        networkManager_.sendMessage(p, decMsg);
    }

    return allYes;
}
```

---

## 4. Participant Message Dispatching

When a participant node receives a 2PC message:
```cpp
void TwoPCCoordinator::handleMessage(const Message& msg) {
    switch (msg.type) {
        case MessageType::PREPARE: {
            bool ok = prepareHandler_ ? prepareHandler_(msg.txnId) : false;
            Message reply;
            reply.type = ok ? MessageType::VOTE_YES : MessageType::VOTE_NO;
            reply.senderId = localNodeId_;
            reply.txnId = msg.txnId;
            networkManager_.sendMessage(msg.senderId, reply);
            break;
        }
        case MessageType::GLOBAL_COMMIT: {
            if (commitHandler_) commitHandler_(msg.txnId);
            break;
        }
        case MessageType::GLOBAL_ABORT: {
            if (abortHandler_) abortHandler_(msg.txnId);
            break;
        }
        // ...
    }
}
```

---

## 5. Failure Modes & Edge Cases

1. **Participant Crash During Phase 1**:
   - The coordinator's `wait_for` timeout expires before receiving the missing vote.
   - The coordinator safely treats the missing vote as a `VOTE_NO` and sends `GLOBAL_ABORT`.
2. **Participant Crash After Voting YES**:
   - When the participant reboots, it reads its WAL log and sees a `PREPARE` entry without a subsequent `COMMIT` or `ABORT`.
   - It queries the coordinator to discover the global decision and resolves the transaction.
