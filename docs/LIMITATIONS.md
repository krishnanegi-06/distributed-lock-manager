# Known Limitations & Scope Boundaries

1. **No Replication / High Availability:** Each key is owned by a single node (`FNV1a_64(key) % N`). If that node crashes or is stopped, keys on that shard are unavailable (`NODE_UNAVAILABLE`) until the node restarts and recovers from its persistent WAL/checkpoint.
2. **Blocking Two-Phase Commit (2PC):** Standard 2PC is a blocking atomic commit protocol, not non-blocking consensus (Raft/Paxos). If the coordinator crashes after a participant persists `TXN_PREPARE` (`PREPARED` state), the participant retains locks and remains in-doubt until the coordinator recovers and responds to `TXN_STATUS_REQ`.
3. **Static Cluster Membership:** Cluster node topology (`nodeId`, `host`, `port`) is configured at startup via `config/nodes.json` or `DLM_NODES`; dynamic shard rebalancing / consistent hashing ring resharding is not implemented.
4. **Minimal SQL-like Command Language:** `CommandParser` implements a transactional key-value DSL (`BEGIN`, `PUT`, `GET`, `DELETE`, `EXISTS`, `SCAN`, `COMMIT`, `ABORT`, `SHOW`), not ANSI SQL joins, schemas, or secondary indexes.
5. **In-Memory Index with Snapshot + WAL Persistence:** `StorageEngine` keeps an ordered `std::map` in memory backed by atomic CRC32-checksummed snapshots and append-only WAL; datasets must fit in node RAM.
6. **Docker Runtime Not Installed on Current Build Host:** Dockerfiles and `docker-compose.yml` are provided and statically validated, while live execution in this Windows environment was performed natively (`dlm_node.exe`, `dlm_api.exe`, `vite`).
7. **Sanitizers (ASan/UBSan):** Configured in `.github/workflows/ci.yml` for Linux `ubuntu-24.04` GCC/Clang; MSVC Debug builds ran with standard MSVC runtime checks on Windows.
