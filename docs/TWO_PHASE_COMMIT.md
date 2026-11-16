# Two-Phase Commit (2PC) Protocol

## 1. Protocol Flow

```mermaid
sequenceDiagram
    participant C as Coordinator Node
    participant WAL_C as Coordinator WAL
    participant P as Participant Node(s)
    participant WAL_P as Participant WAL

    C->>P: PREPARE (txnId)
    P->>WAL_P: Write OP_PUT/OP_DELETE + TXN_PREPARE + fsync
    alt Ready & Locks Valid
        P-->>C: VOTE_YES
    else Missing / Aborted / Error
        P-->>C: VOTE_NO
    end

    alt All Participants Voted YES
        C->>WAL_C: Write TXN_DECISION_COMMIT + fsync
        C->>P: GLOBAL_COMMIT
        P->>WAL_P: Write TXN_COMMIT + fsync
        P->>P: Apply writeBuffer to StorageEngine + Release Locks
        P-->>C: COMMIT_ACK
    else Any VOTE_NO or Timeout
        C->>WAL_C: Write TXN_DECISION_ABORT + fsync
        C->>P: GLOBAL_ABORT
        P->>WAL_P: Write TXN_ABORT + fsync
        P->>P: Discard writeBuffer + Release Locks
        P-->>C: ABORT_ACK
    end
```

## 2. Durability Guarantees & Blocking Property

- **Durable Decision Before Broadcast:** `TwoPCCoordinator::coordinateCommit()` persists `TXN_DECISION_COMMIT` / `TXN_DECISION_ABORT` to both `TransactionLog` and `2pc_decisions.log` with OS-level `FlushFileBuffers` / `fsync` **before** sending Phase-2 messages.
- **Idempotency:** Duplicate `PREPARE`, `GLOBAL_COMMIT`, and `GLOBAL_ABORT` messages are handled safely without double-applying mutations.
- **Blocking Limitation:** Standard 2PC is **blocking**. If the coordinator crashes after a participant persists `TXN_PREPARE` (`PREPARED` state) and votes `VOTE_YES`, the participant retains its locks and does not unilaterally commit or abort until the coordinator recovers and answers `TXN_STATUS_REQ`.
