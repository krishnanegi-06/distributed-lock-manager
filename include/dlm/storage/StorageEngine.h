#pragma once
#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <string>
#include <vector>
#include "dlm/common/Types.h"

namespace dlm {

struct StoredRecord {
    std::string key;
    std::string value;
    bool        isJson      = false;
    uint64_t    version     = 1;
    uint64_t    updatedAtMs = 0;
};

struct StorageStats {
    uint64_t keyCount       = 0;
    uint64_t checkpointLsn  = 0;
    uint64_t lastSyncEpochMs = 0;
    bool     recoveredCleanly = true;
};

/// Persistent Key-Value Storage Engine interface and file-backed implementation.
/// Maintains an ordered in-memory KV map backed by checksummed binary snapshots
/// under `data/node<id>/database/` and `data/node<id>/checkpoints/`.
class StorageEngine {
public:
    StorageEngine() = default;
    explicit StorageEngine(const std::string& dataDir, NodeId nodeId = 1);
    ~StorageEngine() = default;

    /// Initialize directories (`database/`, `wal/`, `checkpoints/`) and load durable state.
    bool initialize(const std::string& dataDir, NodeId nodeId);

    /// Basic KV operations
    std::optional<StoredRecord> get(const std::string& key) const;
    bool put(const std::string& key, const std::string& value, bool persistImmediately = true);
    bool erase(const std::string& key, bool persistImmediately = true);
    bool exists(const std::string& key) const;

    /// Ordered prefix scan with pagination.
    std::vector<StoredRecord> scan(const std::string& prefix = "",
                                   size_t limit = 1000,
                                   size_t offset = 0) const;

    /// Iterate over all committed records in sorted key order.
    void iterate(const std::function<bool(const StoredRecord&)>& visitor) const;

    /// Apply a batch of committed puts/deletes atomically in memory (and optionally persist).
    /// Each entry in `deletes` is true for erase, false for put.
    bool applyCommittedBatch(
        const std::vector<std::pair<std::string, std::optional<std::string>>>& changes,
        bool persistImmediately = true);

    /// Recover committed state from database snapshot & checkpoint files.
    /// Returns false if files are corrupted (preserving any valid backup).
    bool recover(uint64_t* outCheckpointLsn = nullptr);

    /// Write a checksummed, fsynced snapshot to `database/kv_store.dat` and `checkpoints/checkpoint.dat`.
    bool checkpoint(uint64_t checkpointLsn);

    /// Return paths for node directories
    std::string getDatabaseDir() const { return dbDir_; }
    std::string getWalDir() const { return walDir_; }
    std::string getCheckpointsDir() const { return checkpointsDir_; }

    StorageStats getStats() const;

    /// Utility: detect if a string is valid JSON object/array/value.
    static bool isValidJsonString(const std::string& s);

private:
    bool loadFromFile(const std::string& path,
                      std::map<std::string, StoredRecord>& outRecords,
                      uint64_t& outLsn) const;
    bool writeSnapshotFile(const std::string& path,
                           const std::map<std::string, StoredRecord>& records,
                           uint64_t lsn) const;

    std::string baseDir_;
    std::string dbDir_;
    std::string walDir_;
    std::string checkpointsDir_;
    NodeId      nodeId_ = 1;

    mutable std::shared_mutex           mutex_;
    std::map<std::string, StoredRecord> store_;
    uint64_t                            checkpointLsn_   = 0;
    uint64_t                            lastSyncEpochMs_ = 0;
    bool                                recoveredCleanly_ = true;
};

} // namespace dlm
