#pragma once
#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>
#include "dlm/common/Platform.h"
#include "dlm/common/Types.h"

namespace dlm {

struct LogEntry {
    enum class Type : uint8_t {
        TXN_BEGIN = 0,
        TXN_PREPARE = 1,
        TXN_COMMIT = 2,
        TXN_ABORT = 3,
        OPERATION = 4,
        OP_PUT = 5,
        OP_DELETE = 6,
        TXN_DECISION_COMMIT = 7,
        TXN_DECISION_ABORT = 8,
        CHECKPOINT = 9
    };

    uint64_t    lsn         = 0;
    Type        type        = Type::TXN_BEGIN;
    TxnId       txnId       = 0;
    NodeId      nodeId      = 0;
    ResourceId  resourceId;
    std::string data;          // newValue for OP_PUT or generic operation payload
    std::string prevData;      // undo previous value
    bool        prevExisted = false;
    std::vector<NodeId> participants;
    uint64_t    timestamp   = 0;   // ms since epoch
    uint32_t    checksum    = 0;

    std::string serialize() const;
    static bool deserialize(const std::string& line, LogEntry& out);
    static LogEntry deserialize(const std::string& line);
};

inline const char* to_string(LogEntry::Type t) {
    switch (t) {
        case LogEntry::Type::TXN_BEGIN:           return "TXN_BEGIN";
        case LogEntry::Type::TXN_PREPARE:         return "TXN_PREPARE";
        case LogEntry::Type::TXN_COMMIT:          return "TXN_COMMIT";
        case LogEntry::Type::TXN_ABORT:           return "TXN_ABORT";
        case LogEntry::Type::OPERATION:           return "OPERATION";
        case LogEntry::Type::OP_PUT:              return "OP_PUT";
        case LogEntry::Type::OP_DELETE:           return "OP_DELETE";
        case LogEntry::Type::TXN_DECISION_COMMIT: return "TXN_DECISION_COMMIT";
        case LogEntry::Type::TXN_DECISION_ABORT:  return "TXN_DECISION_ABORT";
        case LogEntry::Type::CHECKPOINT:          return "CHECKPOINT";
    }
    return "UNKNOWN";
}

/// Append-only CRC32-checksummed Write-Ahead Log with OS-level durable sync.
class TransactionLog {
public:
    explicit TransactionLog(const std::string& filePath, NodeId nodeId = 0);
    ~TransactionLog();

    uint64_t logBegin(TxnId txnId);
    uint64_t logPrepare(TxnId txnId);
    uint64_t logCommit(TxnId txnId);
    uint64_t logAbort(TxnId txnId);
    uint64_t logOperation(TxnId txnId, const ResourceId& res, const std::string& data);

    uint64_t logPut(TxnId txnId, const std::string& key,
                    const std::string& prevValue, bool prevExisted,
                    const std::string& newValue);
    uint64_t logDelete(TxnId txnId, const std::string& key,
                       const std::string& prevValue, bool prevExisted);
    uint64_t logDecisionCommit(TxnId txnId, const std::vector<NodeId>& participants);
    uint64_t logDecisionAbort(TxnId txnId, const std::vector<NodeId>& participants);
    uint64_t logCheckpoint(uint64_t checkpointLsn);

    /// Read all valid records from the log. If `truncatedTailDetected` is non-null,
    /// sets it true when an incomplete or checksum-corrupted tail record is found.
    std::vector<LogEntry> readLogSafe(bool* truncatedTailDetected = nullptr) const;
    std::vector<LogEntry> readLog() const { return readLogSafe(nullptr); }

    /// Compact/truncate records at or below `checkpointLsn`, retaining `activeEntries`
    /// for in-flight or in-doubt transactions.
    bool compact(uint64_t checkpointLsn, const std::vector<LogEntry>& activeEntries);

    uint64_t getLastLsn() const { return nextLsn_.load() > 0 ? nextLsn_.load() - 1 : 0; }
    const std::string& getFilePath() const { return filePath_; }

    void flush();

    /// Simulate/check write health (for failure testing).
    void setFailWrites(bool fail) { failWrites_.store(fail); }

private:
    uint64_t writeEntry(LogEntry entry, bool durableSync);

    std::string           filePath_;
    NodeId                nodeId_ = 0;
    mutable std::mutex    mutex_;
    DurableFile           file_;
    std::atomic<uint64_t> nextLsn_{1};
    std::atomic<bool>     failWrites_{false};
};

} // namespace dlm
