# Write-Ahead Logging (WAL) & Crash Recovery

## 1. WAL Record Format

`TransactionLog` (`src/transaction/TransactionLog.cpp`) writes append-only lines in CRC32-checksummed JSONL format:

```text
<8-hex-crc32>|{"lsn":12,"type":5,"txnId":281474976710657,"nodeId":1,"res":"account_101","data":"{\"balance\":5000}","prev":"","prevExisted":false,"participants":[],"ts":1794288716784}
```

Record types:
- `TXN_BEGIN`, `TXN_PREPARE`, `TXN_COMMIT`, `TXN_ABORT`
- `OP_PUT`, `OP_DELETE`, `OPERATION`
- `TXN_DECISION_COMMIT`, `TXN_DECISION_ABORT`
- `CHECKPOINT`

Durable synchronization uses `DurableFile::sync()` (`FlushFileBuffers` on Windows, `fsync` on POSIX) before acknowledging `PREPARE`, `COMMIT`, `ABORT`, or 2PC decisions.

## 2. Recovery Algorithm (`TransactionManager::recoverFromWal()`)

1. **Snapshot Load:** `StorageEngine::recover()` loads the latest checksum-verified snapshot (`kv_store.dat` or `checkpoint.dat`) and returns `checkpointLsn`.
2. **Safe WAL Scan:** `TransactionLog::readLogSafe()` validates CRC32 on every line. Any partially written or corrupted tail record stops the scan cleanly and sets `truncatedCorruptTail = true`.
3. **Sequence Restoration:** Advances `nextSeq_` above the highest seen local sequence number so restarted nodes never reuse `TxnId`s.
4. **REDO Pass (forward LSN order):** For every transaction with `TXN_COMMIT`, applies `OP_PUT`/`OP_DELETE` idempotently to `StorageEngine`.
5. **UNDO & In-Doubt Pass (reverse LSN order):**
   - For transactions with `TXN_PREPARE` but no `TXN_COMMIT`/`TXN_ABORT`: restores `TxnState::PREPARED`, re-acquires `EXCLUSIVE` locks in `LockManager`, re-stages writes, and awaits coordinator resolution (`TXN_STATUS_REQ`).
   - For uncommitted `ACTIVE` transactions: undoes any logged changes and appends `TXN_ABORT`.
6. **Checkpointing & Compaction:** `TransactionManager::createCheckpoint()` writes checksummed `checkpoint.dat` + `kv_store.dat` and atomically compacts `wal.log`, keeping only records for still-active or `PREPARED` in-doubt transactions.
