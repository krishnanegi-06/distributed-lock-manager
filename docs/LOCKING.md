# Locking & Strict Two-Phase Locking (SS2PL)

## 1. Lock Modes & Compatibility Matrix

| Held \ Requested | `SHARED` | `EXCLUSIVE` |
| :--- | :---: | :---: |
| **None** | Grant | Grant |
| **SHARED** | Grant | Block (unless sole owner upgrade) |
| **EXCLUSIVE** | Block | Block |

## 2. Re-Entrancy, Upgrades, Downgrades, and Queues

Implemented in `LockManager` (`src/lock_manager/LockManager.cpp`):

- **Re-Entrancy:** A transaction holding `EXCLUSIVE` satisfies subsequent `SHARED` or `EXCLUSIVE` requests immediately. A transaction holding `SHARED` satisfies subsequent `SHARED` requests immediately without queuing behind waiting writers.
- **Lock Upgrade (`SHARED -> EXCLUSIVE`):**
  - If the requesting transaction is the sole `SHARED` owner, it upgrades immediately to `EXCLUSIVE`.
  - If other `SHARED` owners hold the resource, the upgrade waiter is queued ahead of non-upgrade waiters (`isUpgrade = true`) to prevent circular lock inversion with newly arriving waiters.
  - If two `SHARED` owners simultaneously attempt upgrades, both appear in `getWaitForEdges()` and `DeadlockDetector` breaks the upgrade deadlock.
- **Lock Downgrade:** `downgradeLock(txnId, resourceId)` safely downgrades a sole `EXCLUSIVE` lock to `SHARED` and notifies waiting readers.
- **Cancellation & Timeouts:** `acquireLock` supports bounded timeouts (`5000ms` default) and immediate wake-up via `cancelWait(txnId)` or `releaseAllLocks(txnId)`.

## 3. Remote Locking

When `Node::lockResource`, `Node::get`, `Node::put`, or `Node::remove` targets a key whose `ShardRouter` owner is a remote node:
1. Checks `HeartbeatManager` peer health (`FAILED` / `SHUTDOWN` fails fast with `NODE_UNAVAILABLE`).
2. Sends a correlated TCP RPC (`LOCK_REQUEST`, `KV_GET_REQ`, `KV_PUT_REQ`, or `KV_DELETE_REQ`) to the owner.
3. Registers the owner node in `Transaction::participantNodes_` so commit executes 2PC across all touched nodes.
4. Remote locks remain held on the owning node until `GLOBAL_COMMIT` or `GLOBAL_ABORT`.
