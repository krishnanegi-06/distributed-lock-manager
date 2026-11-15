#include "dlm/transaction/TransactionLog.h"
#include "dlm/common/Hash.h"
#include "dlm/common/Logger.h"
#include <nlohmann/json.hpp>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace dlm {

namespace {

std::string hex32(uint32_t v) {
    std::ostringstream oss;
    oss << std::hex << std::nouppercase << std::setfill('0') << std::setw(8) << v;
    return oss.str();
}

bool parseHex32(const std::string& s, uint32_t& out) {
    if (s.size() != 8) return false;
    for (char c : s) {
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))) {
            return false;
        }
    }
    try {
        out = static_cast<uint32_t>(std::stoul(s, nullptr, 16));
        return true;
    } catch (...) {
        return false;
    }
}

} // namespace

std::string LogEntry::serialize() const {
    nlohmann::json j;
    j["lsn"] = lsn;
    j["type"] = static_cast<int>(type);
    j["txnId"] = txnId;
    j["nodeId"] = nodeId;
    j["res"] = resourceId;
    j["data"] = data;
    j["prev"] = prevData;
    j["prevExisted"] = prevExisted;
    j["participants"] = participants;
    j["ts"] = timestamp;

    std::string jsonStr = j.dump();
    uint32_t csum = crc32(jsonStr);
    return hex32(csum) + "|" + jsonStr;
}

bool LogEntry::deserialize(const std::string& line, LogEntry& out) {
    if (line.empty()) return false;

    // Primary CRC32-protected JSON format: <8-char hex crc32>|<json>
    if (line.size() > 9 && line[8] == '|') {
        uint32_t expected = 0;
        if (parseHex32(line.substr(0, 8), expected)) {
            std::string jsonStr = line.substr(9);
            uint32_t actual = crc32(jsonStr);
            if (actual != expected) {
                return false;
            }
            try {
                auto j = nlohmann::json::parse(jsonStr);
                out.lsn = j.value("lsn", uint64_t{0});
                out.type = static_cast<Type>(j.value("type", 0));
                out.txnId = j.value("txnId", uint64_t{0});
                out.nodeId = j.value("nodeId", NodeId{0});
                out.resourceId = j.value("res", std::string{});
                out.data = j.value("data", std::string{});
                out.prevData = j.value("prev", std::string{});
                out.prevExisted = j.value("prevExisted", false);
                out.participants = j.value("participants", std::vector<NodeId>{});
                out.timestamp = j.value("ts", uint64_t{0});
                out.checksum = expected;
                return true;
            } catch (...) {
                return false;
            }
        }
    }

    // Legacy fallback: type|txnId|res|data|ts
    std::istringstream iss(line);
    std::string t0, t1, t2, t3, t4;
    if (!std::getline(iss, t0, '|') ||
        !std::getline(iss, t1, '|') ||
        !std::getline(iss, t2, '|') ||
        !std::getline(iss, t3, '|') ||
        !std::getline(iss, t4)) {
        return false;
    }
    try {
        out.type = static_cast<Type>(std::stoi(t0));
        out.txnId = std::stoull(t1);
        out.resourceId = t2;
        out.data = t3;
        out.timestamp = std::stoull(t4);
        return true;
    } catch (...) {
        return false;
    }
}

LogEntry LogEntry::deserialize(const std::string& line) {
    LogEntry e;
    deserialize(line, e);
    return e;
}

TransactionLog::TransactionLog(const std::string& filePath, NodeId nodeId)
    : filePath_(filePath), nodeId_(nodeId) {
    std::filesystem::path p(filePath_);
    if (p.has_parent_path()) {
        std::error_code ec;
        std::filesystem::create_directories(p.parent_path(), ec);
    }

    // Determine highest existing LSN before opening for append
    uint64_t maxLsn = 0;
    auto existing = readLogSafe(nullptr);
    for (const auto& e : existing) {
        if (e.lsn > maxLsn) maxLsn = e.lsn;
    }
    nextLsn_.store(maxLsn + 1);

    file_.openAppend(filePath_);
}

TransactionLog::~TransactionLog() {
    flush();
    file_.close();
}

uint64_t TransactionLog::writeEntry(LogEntry entry, bool durableSync) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (failWrites_.load() || !file_.isOpen()) {
        return 0;
    }
    entry.lsn = nextLsn_.fetch_add(1);
    if (entry.nodeId == 0) entry.nodeId = nodeId_;
    if (entry.timestamp == 0) entry.timestamp = nowEpochMs();

    std::string line = entry.serialize();
    if (!file_.writeLine(line)) {
        return 0;
    }
    if (durableSync) {
        if (!file_.sync()) {
            return 0;
        }
    }
    return entry.lsn;
}

uint64_t TransactionLog::logBegin(TxnId txnId) {
    LogEntry e;
    e.type = LogEntry::Type::TXN_BEGIN;
    e.txnId = txnId;
    return writeEntry(std::move(e), false);
}

uint64_t TransactionLog::logPrepare(TxnId txnId) {
    LogEntry e;
    e.type = LogEntry::Type::TXN_PREPARE;
    e.txnId = txnId;
    return writeEntry(std::move(e), true);
}

uint64_t TransactionLog::logCommit(TxnId txnId) {
    LogEntry e;
    e.type = LogEntry::Type::TXN_COMMIT;
    e.txnId = txnId;
    return writeEntry(std::move(e), true);
}

uint64_t TransactionLog::logAbort(TxnId txnId) {
    LogEntry e;
    e.type = LogEntry::Type::TXN_ABORT;
    e.txnId = txnId;
    return writeEntry(std::move(e), true);
}

uint64_t TransactionLog::logOperation(TxnId txnId, const ResourceId& res, const std::string& data) {
    LogEntry e;
    e.type = LogEntry::Type::OPERATION;
    e.txnId = txnId;
    e.resourceId = res;
    e.data = data;
    return writeEntry(std::move(e), false);
}

uint64_t TransactionLog::logPut(TxnId txnId, const std::string& key,
                                const std::string& prevValue, bool prevExisted,
                                const std::string& newValue) {
    LogEntry e;
    e.type = LogEntry::Type::OP_PUT;
    e.txnId = txnId;
    e.resourceId = key;
    e.prevData = prevValue;
    e.prevExisted = prevExisted;
    e.data = newValue;
    return writeEntry(std::move(e), false);
}

uint64_t TransactionLog::logDelete(TxnId txnId, const std::string& key,
                                   const std::string& prevValue, bool prevExisted) {
    LogEntry e;
    e.type = LogEntry::Type::OP_DELETE;
    e.txnId = txnId;
    e.resourceId = key;
    e.prevData = prevValue;
    e.prevExisted = prevExisted;
    return writeEntry(std::move(e), false);
}

uint64_t TransactionLog::logDecisionCommit(TxnId txnId, const std::vector<NodeId>& participants) {
    LogEntry e;
    e.type = LogEntry::Type::TXN_DECISION_COMMIT;
    e.txnId = txnId;
    e.participants = participants;
    return writeEntry(std::move(e), true);
}

uint64_t TransactionLog::logDecisionAbort(TxnId txnId, const std::vector<NodeId>& participants) {
    LogEntry e;
    e.type = LogEntry::Type::TXN_DECISION_ABORT;
    e.txnId = txnId;
    e.participants = participants;
    return writeEntry(std::move(e), true);
}

uint64_t TransactionLog::logCheckpoint(uint64_t checkpointLsn) {
    LogEntry e;
    e.type = LogEntry::Type::CHECKPOINT;
    e.data = std::to_string(checkpointLsn);
    return writeEntry(std::move(e), true);
}

std::vector<LogEntry> TransactionLog::readLogSafe(bool* truncatedTailDetected) const {
    if (truncatedTailDetected) *truncatedTailDetected = false;
    std::vector<LogEntry> entries;
    std::ifstream file(filePath_, std::ios::binary);
    if (!file.is_open()) return entries;

    std::string line;
    while (std::getline(file, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;
        LogEntry entry;
        if (!LogEntry::deserialize(line, entry)) {
            if (truncatedTailDetected) *truncatedTailDetected = true;
            Logger::warn("TransactionLog: detected corrupted/partial WAL record; stopping log scan safely.");
            break;
        }
        entries.push_back(std::move(entry));
    }
    return entries;
}

bool TransactionLog::compact(uint64_t checkpointLsn, const std::vector<LogEntry>& activeEntries) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (failWrites_.load()) return false;

    std::string content;
    for (const auto& e : activeEntries) {
        content += e.serialize();
        content.push_back('\n');
    }
    LogEntry cp;
    cp.lsn = nextLsn_.fetch_add(1);
    cp.type = LogEntry::Type::CHECKPOINT;
    cp.nodeId = nodeId_;
    cp.data = std::to_string(checkpointLsn);
    cp.timestamp = nowEpochMs();
    content += cp.serialize();
    content.push_back('\n');

    file_.close();
    bool ok = atomicWriteFile(filePath_, content);
    file_.openAppend(filePath_);
    return ok;
}

void TransactionLog::flush() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (file_.isOpen()) {
        file_.sync();
    }
}

} // namespace dlm
