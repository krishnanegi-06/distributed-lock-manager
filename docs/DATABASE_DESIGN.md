# Database Design & Storage Engine

## 1. Key-Value Storage Abstraction

`StorageEngine` (`include/dlm/storage/StorageEngine.h`, `src/storage/StorageEngine.cpp`) implements a thread-safe ordered key-value store backed by checksummed snapshots and Write-Ahead Logging (WAL).

Supported operations:
- `get(key)` -> `std::optional<StoredRecord>`
- `put(key, value)` -> inserts or updates string / JSON value (detects valid JSON via `nlohmann::json::accept`)
- `erase(key)` -> deletes committed key
- `exists(key)` -> presence check
- `scan(prefix, limit, offset)` -> ordered prefix range scan
- `iterate(visitor)` -> ordered full-store iteration
- `recover()` -> loads latest valid snapshot from `database/` or `checkpoints/`

## 2. Deterministic Hash-Based Sharding

Keys are sharded across cluster nodes using **FNV-1a 64-bit** (`ShardRouter`, `include/dlm/storage/ShardRouter.h`):

$$\text{owner\_index} = \text{FNV1a\_64}(\text{key}) \bmod N$$

- `offset_basis = 14695981039346656037ULL`
- `prime = 1099511628211ULL`
- Node IDs are sorted ascending before modulo indexing so every node computes identical ownership.
- **No Replication:** Each key maps to a single owner node. If the owner is unavailable, operations return `NODE_UNAVAILABLE`.

## 3. On-Disk Storage Layout

Each node manages its own isolated storage directory:

```text
data/
  node1/
    database/
      kv_store.dat
    wal/
      wal.log
      2pc_decisions.log
    checkpoints/
      checkpoint.dat
  node2/ ...
  node3/ ...
```

### Snapshot File Format (`kv_store.dat` / `checkpoint.dat`)
- Magic: `DLMKV01\0` (8 bytes)
- Version: `uint32_t` (`1`)
- Entry Count: `uint64_t`
- Checkpoint LSN: `uint64_t`
- Payload CRC32: `uint32_t`
- Payload entries: `[keyLen | key | valLen | value | isJson | version | updatedAtMs]`
- Written via `atomicWriteFile()` (temp file + `FlushFileBuffers` / `fsync` + atomic replace).
