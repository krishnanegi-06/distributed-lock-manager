# Implementation Roadmap

**Tracking Status:** Active (Phases 0–12 Implemented, Phase 13–14 Verification & Docs)

---

| Phase | Scope | Status |
| :--- | :--- | :--- |
| **Phase 0** | Repository audit, baseline build & tests (`INITIAL_AUDIT.md`, `ARCHITECTURE.md`, `IMPLEMENTATION_ROADMAP.md`) | **Completed** |
| **Phase 1** | Core C++ fixes: 64-bit global `TxnId`, `TxnState` lifecycle, `LockManager` upgrade/re-entry/cancellation/snapshots, `DeadlockDetector` interruptible stop & detailed cycles, cross-platform `NetworkManager` RPC & timeouts, binary `Message` codec | **Completed** |
| **Phase 2** | Persistent `StorageEngine`, FNV-1a `ShardRouter`, per-node `data/node<id>/` directories, durable OS flush (`FlushFileBuffers` / `fsync`) | **Completed** |
| **Phase 3** | `TransactionManager` + `StorageEngine` write-buffering, Strict 2PL, CRC32 WAL, checkpointing, log compaction, undo/redo recovery | **Completed** |
| **Phase 4** | Distributed sharding, remote lock RPC, remote `GET`/`PUT`/`DELETE`, participant registration | **Completed** |
| **Phase 5** | Durable 2PC decision logging, participant `PREPARED` persistence, in-doubt resolution (`TXN_STATUS_REQ`) | **Completed** |
| **Phase 6** | Hardened `HeartbeatManager` (`HEALTHY`/`SUSPECTED`/`FAILED`/`SHUTDOWN`), distributed victim `ABORT_TXN`, predicated global WFG collection | **Completed** |
| **Phase 7** | C++ HTTP REST + RFC-6455 WebSocket `ApiServer` (`dlm_api`), process supervisor, event stream | **Completed** |
| **Phase 8** | React + TypeScript + Vite + Tailwind CSS dashboard (`frontend/`, 14 pages, legacy simulation removed) | **Completed** |
| **Phase 9** | SQL-like `CommandParser` tokenizer/parser/executor routed through `TransactionManager` | **Completed** |
| **Phase 10** | PBKDF2-HMAC-SHA256 password hashing, HMAC-SHA256 bearer tokens, WS auth, CORS, rate limiting, input limits | **Completed** |
| **Phase 11** | Native Windows PowerShell scripts, `config/nodes.example.json`, `.env.example` | **Completed** |
| **Phase 12** | Dockerfiles, Nginx reverse proxy, `docker-compose.yml` with persistent volumes & healthchecks | **Completed** |
| **Phase 13** | GoogleTest suite (`dlm_gtest`), `dlm_benchmark`, live 3-node Windows smoke test, CI workflow | **In Progress** |
| **Phase 14** | Complete 16 documentation files & final delivery verification | **In Progress** |
