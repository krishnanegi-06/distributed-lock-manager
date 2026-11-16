#include "dlm/storage/StorageEngine.h"
#include "dlm/common/Hash.h"
#include "dlm/common/Logger.h"
#include "dlm/common/Platform.h"
#include <nlohmann/json.hpp>
#include <cstring>
#include <filesystem>
#include <fstream>

namespace dlm {

namespace {

constexpr char kSnapshotMagic[8] = {'D', 'L', 'M', 'K', 'V', '0', '1', '\0'};
constexpr uint32_t kSnapshotVersion = 1;

void appendU8(std::string& b, uint8_t v) {
    b.push_back(static_cast<char>(v));
}

void appendU32(std::string& b, uint32_t v) {
    for (int i = 0; i < 4; ++i) b.push_back(static_cast<char>((v >> (i * 8)) & 0xFFU));
}

void appendU64(std::string& b, uint64_t v) {
    for (int i = 0; i < 8; ++i) b.push_back(static_cast<char>((v >> (i * 8)) & 0xFFULL));
}

void appendStr(std::string& b, const std::string& s) {
    appendU32(b, static_cast<uint32_t>(s.size()));
    b.append(s);
}

bool readU8(const std::string& b, size_t& pos, uint8_t& v) {
    if (pos + 1 > b.size()) return false;
    v = static_cast<uint8_t>(b[pos++]);
    return true;
}

bool readU32(const std::string& b, size_t& pos, uint32_t& v) {
    if (pos + 4 > b.size()) return false;
    v = 0;
    for (int i = 0; i < 4; ++i) {
        v |= static_cast<uint32_t>(static_cast<uint8_t>(b[pos + i])) << (i * 8);
    }
    pos += 4;
    return true;
}

bool readU64(const std::string& b, size_t& pos, uint64_t& v) {
    if (pos + 8 > b.size()) return false;
    v = 0;
    for (int i = 0; i < 8; ++i) {
        v |= static_cast<uint64_t>(static_cast<uint8_t>(b[pos + i])) << (i * 8);
    }
    pos += 8;
    return true;
}

bool readStr(const std::string& b, size_t& pos, std::string& s) {
    uint32_t len = 0;
    if (!readU32(b, pos, len)) return false;
    if (pos + len > b.size()) return false;
    s.assign(b, pos, len);
    pos += len;
    return true;
}

} // namespace

StorageEngine::StorageEngine(const std::string& dataDir, NodeId nodeId) {
    initialize(dataDir, nodeId);
}

bool StorageEngine::initialize(const std::string& dataDir, NodeId nodeId) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    nodeId_ = nodeId;
    baseDir_ = dataDir + "/node" + std::to_string(nodeId);
    dbDir_ = baseDir_ + "/database";
    walDir_ = baseDir_ + "/wal";
    checkpointsDir_ = baseDir_ + "/checkpoints";

    std::error_code ec;
    std::filesystem::create_directories(dbDir_, ec);
    if (ec) return false;
    std::filesystem::create_directories(walDir_, ec);
    if (ec) return false;
    std::filesystem::create_directories(checkpointsDir_, ec);
    if (ec) return false;

    lock.unlock();
    return recover();
}

bool StorageEngine::isValidJsonString(const std::string& s) {
    if (s.empty()) return false;
    // Trim leading whitespace
    size_t i = 0;
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\r' || s[i] == '\n')) ++i;
    if (i >= s.size()) return false;
    char c = s[i];
    if (c != '{' && c != '[' && c != '"' && c != 't' && c != 'f' && c != 'n' &&
        !(c >= '0' && c <= '9') && c != '-') {
        return false;
    }
    return nlohmann::json::accept(s);
}

std::optional<StoredRecord> StorageEngine::get(const std::string& key) const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    auto it = store_.find(key);
    if (it == store_.end()) return std::nullopt;
    return it->second;
}

bool StorageEngine::put(const std::string& key, const std::string& value, bool persistImmediately) {
    if (key.empty()) return false;
    std::unique_lock<std::shared_mutex> lock(mutex_);
    auto& rec = store_[key];
    rec.key = key;
    rec.value = value;
    rec.isJson = isValidJsonString(value);
    rec.version = (rec.version == 0 ? 1 : rec.version + 1);
    rec.updatedAtMs = nowEpochMs();

    if (persistImmediately && !dbDir_.empty()) {
        if (!writeSnapshotFile(dbDir_ + "/kv_store.dat", store_, checkpointLsn_)) {
            return false;
        }
        lastSyncEpochMs_ = nowEpochMs();
    }
    return true;
}

bool StorageEngine::erase(const std::string& key, bool persistImmediately) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    auto it = store_.find(key);
    if (it == store_.end()) return false;
    store_.erase(it);
    if (persistImmediately && !dbDir_.empty()) {
        if (!writeSnapshotFile(dbDir_ + "/kv_store.dat", store_, checkpointLsn_)) {
            return false;
        }
        lastSyncEpochMs_ = nowEpochMs();
    }
    return true;
}

bool StorageEngine::exists(const std::string& key) const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    return store_.find(key) != store_.end();
}

std::vector<StoredRecord> StorageEngine::scan(const std::string& prefix,
                                              size_t limit,
                                              size_t offset) const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    std::vector<StoredRecord> results;
    auto it = prefix.empty() ? store_.begin() : store_.lower_bound(prefix);
    size_t skipped = 0;
    for (; it != store_.end(); ++it) {
        if (!prefix.empty() && it->first.compare(0, prefix.size(), prefix) != 0) {
            break;
        }
        if (skipped < offset) {
            ++skipped;
            continue;
        }
        results.push_back(it->second);
        if (results.size() >= limit) break;
    }
    return results;
}

void StorageEngine::iterate(const std::function<bool(const StoredRecord&)>& visitor) const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    for (const auto& [k, rec] : store_) {
        if (!visitor(rec)) break;
    }
}

bool StorageEngine::applyCommittedBatch(
        const std::vector<std::pair<std::string, std::optional<std::string>>>& changes,
        bool persistImmediately) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    uint64_t now = nowEpochMs();
    for (const auto& [k, optVal] : changes) {
        if (k.empty()) continue;
        if (optVal.has_value()) {
            auto& rec = store_[k];
            rec.key = k;
            rec.value = *optVal;
            rec.isJson = isValidJsonString(*optVal);
            rec.version = (rec.version == 0 ? 1 : rec.version + 1);
            rec.updatedAtMs = now;
        } else {
            store_.erase(k);
        }
    }
    if (persistImmediately && !dbDir_.empty()) {
        if (!writeSnapshotFile(dbDir_ + "/kv_store.dat", store_, checkpointLsn_)) {
            return false;
        }
        lastSyncEpochMs_ = now;
    }
    return true;
}

bool StorageEngine::writeSnapshotFile(const std::string& path,
                                      const std::map<std::string, StoredRecord>& records,
                                      uint64_t lsn) const {
    std::string payload;
    payload.reserve(records.size() * 64 + 16);
    for (const auto& [k, rec] : records) {
        appendStr(payload, rec.key);
        appendStr(payload, rec.value);
        appendU8(payload, rec.isJson ? 1 : 0);
        appendU64(payload, rec.version);
        appendU64(payload, rec.updatedAtMs);
    }

    uint32_t csum = crc32(payload);
    std::string full;
    full.reserve(8 + 4 + 8 + 8 + 4 + payload.size());
    full.append(kSnapshotMagic, 8);
    appendU32(full, kSnapshotVersion);
    appendU64(full, static_cast<uint64_t>(records.size()));
    appendU64(full, lsn);
    appendU32(full, csum);
    full.append(payload);

    return atomicWriteFile(path, full);
}

bool StorageEngine::loadFromFile(const std::string& path,
                                 std::map<std::string, StoredRecord>& outRecords,
                                 uint64_t& outLsn) const {
    std::ifstream f(path, std::ios::binary);
    if (!f.is_open()) return false;

    std::string data((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    if (data.size() < 32) return false;
    if (std::memcmp(data.data(), kSnapshotMagic, 8) != 0) return false;

    size_t pos = 8;
    uint32_t version = 0;
    uint64_t count = 0;
    uint64_t lsn = 0;
    uint32_t expectedCsum = 0;
    if (!readU32(data, pos, version) || version != kSnapshotVersion) return false;
    if (!readU64(data, pos, count)) return false;
    if (!readU64(data, pos, lsn)) return false;
    if (!readU32(data, pos, expectedCsum)) return false;

    std::string payload = data.substr(pos);
    if (crc32(payload) != expectedCsum) {
        Logger::error("StorageEngine: checksum mismatch in " + path);
        return false;
    }

    std::map<std::string, StoredRecord> parsed;
    size_t p = 0;
    for (uint64_t i = 0; i < count; ++i) {
        StoredRecord rec;
        uint8_t jsonByte = 0;
        if (!readStr(payload, p, rec.key) ||
            !readStr(payload, p, rec.value) ||
            !readU8(payload, p, jsonByte) ||
            !readU64(payload, p, rec.version) ||
            !readU64(payload, p, rec.updatedAtMs)) {
            return false;
        }
        rec.isJson = (jsonByte != 0);
        parsed[rec.key] = std::move(rec);
    }
    if (p != payload.size()) return false;

    outRecords = std::move(parsed);
    outLsn = lsn;
    return true;
}

bool StorageEngine::recover(uint64_t* outCheckpointLsn) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    store_.clear();
    checkpointLsn_ = 0;
    recoveredCleanly_ = true;

    const std::string dbFile = dbDir_ + "/kv_store.dat";
    const std::string cpFile = checkpointsDir_ + "/checkpoint.dat";

    std::map<std::string, StoredRecord> cpRecords, dbRecords;
    uint64_t cpLsn = 0, dbLsn = 0;
    bool hasCp = std::filesystem::exists(cpFile);
    bool hasDb = std::filesystem::exists(dbFile);
    bool cpOk = hasCp && loadFromFile(cpFile, cpRecords, cpLsn);
    bool dbOk = hasDb && loadFromFile(dbFile, dbRecords, dbLsn);

    if (hasCp && !cpOk) recoveredCleanly_ = false;
    if (hasDb && !dbOk) recoveredCleanly_ = false;

    if (dbOk && (!cpOk || dbLsn >= cpLsn)) {
        store_ = std::move(dbRecords);
        checkpointLsn_ = dbLsn;
    } else if (cpOk) {
        store_ = std::move(cpRecords);
        checkpointLsn_ = cpLsn;
    }

    if (outCheckpointLsn) *outCheckpointLsn = checkpointLsn_;
    return recoveredCleanly_;
}

bool StorageEngine::checkpoint(uint64_t checkpointLsn) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    if (dbDir_.empty() || checkpointsDir_.empty()) return false;
    checkpointLsn_ = checkpointLsn;
    bool ok1 = writeSnapshotFile(checkpointsDir_ + "/checkpoint.dat", store_, checkpointLsn_);
    bool ok2 = writeSnapshotFile(dbDir_ + "/kv_store.dat", store_, checkpointLsn_);
    if (ok1 && ok2) {
        lastSyncEpochMs_ = nowEpochMs();
    }
    return ok1 && ok2;
}

StorageStats StorageEngine::getStats() const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    StorageStats st;
    st.keyCount = store_.size();
    st.checkpointLsn = checkpointLsn_;
    st.lastSyncEpochMs = lastSyncEpochMs_;
    st.recoveredCleanly = recoveredCleanly_;
    return st;
}

} // namespace dlm
