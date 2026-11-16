# Application API & WebSocket Reference

Base URL: `http://127.0.0.1:8080` (or via Vite proxy / Nginx `/api`).  
Protected endpoints require header: `Authorization: Bearer <token>`.  
Every response includes `X-Request-Id`.

## 1. Authentication & Security

| Method | Path | Auth | Description |
| :--- | :--- | :--- | :--- |
| `GET` | `/api/health` | Public | Server liveness & `setupComplete` flag |
| `GET` | `/api/auth/status` | Public | Returns `{ setupComplete }` |
| `POST` | `/api/auth/setup` | Public (first-run only) | Create initial admin `{ username, password }` |
| `POST` | `/api/auth/login` | Public | Authenticate and issue HMAC-SHA256 bearer token |
| `POST` | `/api/auth/logout` | Bearer | Revoke active bearer token |
| `GET` | `/api/auth/me` | Bearer | Current authenticated user metadata |

## 2. Cluster & Nodes

| Method | Path | Description |
| :--- | :--- | :--- |
| `GET` | `/api/cluster` (or `/api/snapshot`) | Full aggregated cluster snapshot (nodes, keys, locks, WFG, txns, 2PC, WAL, recovery, metrics) |
| `GET` | `/api/nodes` | Node list & health status |
| `GET` | `/api/nodes/{id}` | Single node details |
| `POST` | `/api/nodes/{id}/crash` | Crash node (kills child process in `--supervise` mode or disconnects in container mode) |
| `POST` | `/api/nodes/{id}/restart` | Restart node & trigger WAL recovery |

## 3. Transactions

| Method | Path | Body / Description |
| :--- | :--- | :--- |
| `POST` | `/api/transactions` | `{ "nodeId": 1, "isolation": "SERIALIZABLE" }` |
| `GET` | `/api/transactions` | List all cluster transactions |
| `GET` | `/api/transactions/{id}` | Inspect single transaction |
| `POST` | `/api/transactions/{id}/read` | `{ "key": "..." }` |
| `POST` | `/api/transactions/{id}/write` | `{ "key": "...", "value": "..." }` |
| `POST` | `/api/transactions/{id}/delete` | `{ "key": "..." }` |
| `POST` | `/api/transactions/{id}/commit` | Commit (uses 2PC automatically if distributed) |
| `POST` | `/api/transactions/{id}/abort` | `{ "reason": "..." }` |

## 4. Database, SQL Console, Locks, WAL, Recovery & Metrics

| Method | Path | Description |
| :--- | :--- | :--- |
| `GET` | `/api/database/keys` | Query params: `?prefix=...&nodeId=...` |
| `GET` | `/api/database/keys/{key}` | Transactional read of committed key |
| `GET` | `/api/database/keys/{key}/owner` | Deterministic FNV-1a owner node |
| `POST` | `/api/database/checkpoint` | `{ "nodeId": 1 }` — snapshot + WAL compaction |
| `POST` | `/api/sql` | `{ "command": "...", "nodeId": 1, "activeTxnId": 0 }` |
| `GET` | `/api/locks` | Active & waiting locks |
| `GET` | `/api/locks/waiting` | Waiting locks + wait-for edges |
| `GET` | `/api/deadlocks` | Detected deadlock cycles + current WFG |
| `GET` | `/api/two-pc` | 2PC coordinator & participant state |
| `GET` | `/api/logs` | Backend system logs |
| `GET` | `/api/wal` | Persisted WAL records across nodes |
| `GET` | `/api/recovery` | Startup crash recovery reports |
| `GET` | `/api/metrics` | Throughput, avg latency, HTTP & txn counters |

## 5. WebSocket Stream (`/ws`)

- Connect to `ws://<host>/ws?token=<bearer_token>`.
- Emits JSON events:
  `{ "eventId": 1, "timestamp": 1794..., "type": "transaction.committed", "nodeId": 1, "txnId": 281474976710657, "payload": {...} }`
- Supported event types: `node.status_changed`, `transaction.created`, `transaction.committed`, `transaction.aborted`, `deadlock.detected`, `wal.record_written`, `recovery.completed`.
- On reconnect, the frontend immediately calls `GET /api/cluster` to restore full state.
