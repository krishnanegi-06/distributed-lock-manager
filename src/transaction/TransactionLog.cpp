#include "dlm/transaction/TransactionLog.h"
#include <sstream>
#include <chrono>

namespace dlm {

std::string LogEntry::serialize() const {
    std::ostringstream oss;
    oss << static_cast<int>(type) << "|" << txnId << "|" << resourceId << "|" << data << "|" << timestamp;
    return oss.str();
}

LogEntry LogEntry::deserialize(const std::string& line) {
    LogEntry entry;
    std::istringstream iss(line);
    std::string token;

    if (std::getline(iss, token, '|')) {
        entry.type = static_cast<LogEntry::Type>(std::stoi(token));
    }
    if (std::getline(iss, token, '|')) {
        entry.txnId = std::stoull(token);
    }
    if (std::getline(iss, token, '|')) {
        entry.resourceId = token;
    }
    if (std::getline(iss, token, '|')) {
        entry.data = token;
    }
    if (std::getline(iss, token, '|')) {
        entry.timestamp = std::stoull(token);
    }

    return entry;
}

TransactionLog::TransactionLog(const std::string& filePath) : filePath_(filePath) {
    logFile_.open(filePath_, std::ios::app);
}

TransactionLog::~TransactionLog() {
    flush();
    if (logFile_.is_open()) {
        logFile_.close();
    }
}

void TransactionLog::logBegin(TxnId txnId) {
    LogEntry entry;
    entry.type = LogEntry::Type::TXN_BEGIN;
    entry.txnId = txnId;
    entry.timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    writeEntry(entry);
}

void TransactionLog::logPrepare(TxnId txnId) {
    LogEntry entry;
    entry.type = LogEntry::Type::TXN_PREPARE;
    entry.txnId = txnId;
    entry.timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    writeEntry(entry);
}

void TransactionLog::logCommit(TxnId txnId) {
    LogEntry entry;
    entry.type = LogEntry::Type::TXN_COMMIT;
    entry.txnId = txnId;
    entry.timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    writeEntry(entry);
}

void TransactionLog::logAbort(TxnId txnId) {
    LogEntry entry;
    entry.type = LogEntry::Type::TXN_ABORT;
    entry.txnId = txnId;
    entry.timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    writeEntry(entry);
}

void TransactionLog::logOperation(TxnId txnId, const ResourceId& res, const std::string& data) {
    LogEntry entry;
    entry.type = LogEntry::Type::OPERATION;
    entry.txnId = txnId;
    entry.resourceId = res;
    entry.data = data;
    entry.timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    writeEntry(entry);
}

std::vector<LogEntry> TransactionLog::readLog() const {
    std::vector<LogEntry> entries;
    std::ifstream file(filePath_);
    std::string line;
    while (std::getline(file, line)) {
        if (!line.empty()) {
            entries.push_back(LogEntry::deserialize(line));
        }
    }
    return entries;
}

void TransactionLog::flush() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (logFile_.is_open()) {
        logFile_.flush();
    }
}

void TransactionLog::writeEntry(const LogEntry& entry) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (logFile_.is_open()) {
        logFile_ << entry.serialize() << "\n";
        logFile_.flush();
    }
}

} // namespace dlm
