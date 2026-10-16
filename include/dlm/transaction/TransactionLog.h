#pragma once
#include <string>
#include <fstream>
#include <mutex>
#include <vector>
#include <cstdint>
#include "dlm/common/Types.h"

namespace dlm {

struct LogEntry {
    enum class Type : uint8_t { TXN_BEGIN, TXN_PREPARE, TXN_COMMIT, TXN_ABORT, OPERATION };
    Type        type;
    TxnId       txnId;
    ResourceId  resourceId;
    std::string data;
    uint64_t    timestamp;   // ms since epoch

    std::string serialize() const;
    static LogEntry deserialize(const std::string& line);
};

/// Append-only write-ahead log for transaction durability.
class TransactionLog {
public:
    explicit TransactionLog(const std::string& filePath);
    ~TransactionLog();

    void logBegin(TxnId txnId);
    void logPrepare(TxnId txnId);
    void logCommit(TxnId txnId);
    void logAbort(TxnId txnId);
    void logOperation(TxnId txnId, const ResourceId& res,
                      const std::string& data);

    /// Read the entire log back (for crash recovery).
    std::vector<LogEntry> readLog() const;

    void flush();

private:
    void writeEntry(const LogEntry& entry);

    std::string   filePath_;
    std::ofstream logFile_;
    std::mutex    mutex_;
};

} // namespace dlm
