# Inter-Node Network Protocol

## 1. Framing & Serialization

`NetworkManager` (`src/network/NetworkManager.cpp`) exchanges length-prefixed binary frames over TCP:

```text
[ 4-byte uint32 BE payload length ] [ serialized Message payload ]
```

- **Maximum Message Size:** `16 MiB` (`Message::kMaxSerializedBytes`). Frames with `len == 0` or `len > 16 MiB` are rejected and the socket is closed.
- **Binary Codec (`Message.cpp`):** Magic header `DLM1` (`0x444C4D31`), explicit little-endian integers, length-prefixed strings/vectors (`key`, `value`, `error`, `waitEdges`, `participants`, `stagedWrites`, `kvPairs`), avoiding delimiter injection bugs.

## 2. Request-Response Correlation & Timeouts

- `NetworkManager::sendRequest(targetNode, req, resp, timeout)` assigns a monotonic `requestId`, registers a conditional-variable waiter before sending, and correlates the inbound response by `requestId`.
- Synchronous control RPCs (`CTRL_RPC_REQ` / `CTRL_RPC_RESP`) reply directly over the accepted client socket via `registerRpcHandler()`.
- Non-blocking `connectWithTimeout()` and `SO_RCVTIMEO` / `SO_SNDTIMEO` ensure unreachable or crashed nodes fail fast instead of blocking indefinitely.

## 3. Message Types

- **Locking:** `LOCK_REQUEST`, `LOCK_RESPONSE`, `LOCK_RELEASE`, `LOCK_RELEASE_ALL`
- **KV Operations:** `KV_GET_REQ/RESP`, `KV_PUT_REQ/RESP`, `KV_DELETE_REQ/RESP`, `KV_SCAN_REQ/RESP`
- **2PC:** `PREPARE`, `VOTE_YES`, `VOTE_NO`, `GLOBAL_COMMIT`, `GLOBAL_ABORT`, `COMMIT_ACK`, `ABORT_ACK`, `TXN_STATUS_REQ/RESP`
- **Deadlock:** `WFG_REQUEST`, `WFG_RESPONSE`, `ABORT_TXN`
- **Membership:** `HEARTBEAT`, `HEARTBEAT_ACK`, `NODE_LEAVE`
- **Control RPC:** `CTRL_RPC_REQ`, `CTRL_RPC_RESP`
