#include "dlm/network/Message.h"
#include <cstring>

namespace dlm {

namespace {

constexpr uint32_t kWireMagic = 0x444C4D31U; // "DLM1"

void writeU8(std::string& buf, uint8_t v) {
    buf.push_back(static_cast<char>(v));
}

void writeU32(std::string& buf, uint32_t v) {
    for (int i = 0; i < 4; ++i) {
        buf.push_back(static_cast<char>((v >> (i * 8)) & 0xFFU));
    }
}

void writeU64(std::string& buf, uint64_t v) {
    for (int i = 0; i < 8; ++i) {
        buf.push_back(static_cast<char>((v >> (i * 8)) & 0xFFULL));
    }
}

void writeStr(std::string& buf, const std::string& s) {
    writeU32(buf, static_cast<uint32_t>(s.size()));
    buf.append(s);
}

class Reader {
public:
    explicit Reader(const std::string& data) : data_(data) {}

    bool readU8(uint8_t& out) {
        if (pos_ + 1 > data_.size()) return false;
        out = static_cast<uint8_t>(data_[pos_++]);
        return true;
    }

    bool readU32(uint32_t& out) {
        if (pos_ + 4 > data_.size()) return false;
        uint32_t v = 0;
        for (int i = 0; i < 4; ++i) {
            v |= static_cast<uint32_t>(static_cast<uint8_t>(data_[pos_ + i])) << (i * 8);
        }
        pos_ += 4;
        out = v;
        return true;
    }

    bool readU64(uint64_t& out) {
        if (pos_ + 8 > data_.size()) return false;
        uint64_t v = 0;
        for (int i = 0; i < 8; ++i) {
            v |= static_cast<uint64_t>(static_cast<uint8_t>(data_[pos_ + i])) << (i * 8);
        }
        pos_ += 8;
        out = v;
        return true;
    }

    bool readStr(std::string& out) {
        uint32_t len = 0;
        if (!readU32(len)) return false;
        if (len > Message::kMaxSerializedBytes || pos_ + len > data_.size()) return false;
        out.assign(data_, pos_, len);
        pos_ += len;
        return true;
    }

    size_t remaining() const { return data_.size() - pos_; }

private:
    const std::string& data_;
    size_t pos_ = 0;
};

} // namespace

std::string Message::serialize() const {
    std::string buf;
    buf.reserve(64 + resourceId.size() + key.size() + value.size() + error.size());

    writeU32(buf, kWireMagic);
    writeU8(buf, static_cast<uint8_t>(type));
    writeU32(buf, senderId);
    writeU64(buf, requestId);
    writeU64(buf, txnId);
    writeStr(buf, resourceId);
    writeU8(buf, static_cast<uint8_t>(lockMode));
    writeU8(buf, success ? 1 : 0);
    writeU8(buf, static_cast<uint8_t>(status));
    writeU8(buf, found ? 1 : 0);
    writeStr(buf, key);
    writeStr(buf, value);
    writeStr(buf, error);

    writeU32(buf, static_cast<uint32_t>(waitEdges.size()));
    for (const auto& e : waitEdges) {
        writeU64(buf, e.waitingTxn);
        writeU64(buf, e.holdingTxn);
        writeStr(buf, e.resourceId);
    }

    writeU32(buf, static_cast<uint32_t>(participants.size()));
    for (NodeId p : participants) {
        writeU32(buf, p);
    }

    writeU32(buf, static_cast<uint32_t>(stagedWrites.size()));
    for (const auto& sw : stagedWrites) {
        writeStr(buf, sw.key);
        writeU8(buf, sw.isDelete ? 1 : 0);
        writeStr(buf, sw.value);
    }

    writeU32(buf, static_cast<uint32_t>(kvPairs.size()));
    for (const auto& kv : kvPairs) {
        writeStr(buf, kv.first);
        writeStr(buf, kv.second);
    }

    return buf;
}

bool Message::deserialize(const std::string& data, Message& out) {
    if (data.size() > kMaxSerializedBytes) return false;
    Reader r(data);

    uint32_t magic = 0;
    if (!r.readU32(magic) || magic != kWireMagic) return false;

    uint8_t typeByte = 0, modeByte = 0, succByte = 0, statusByte = 0, foundByte = 0;
    if (!r.readU8(typeByte)) return false;
    if (!r.readU32(out.senderId)) return false;
    if (!r.readU64(out.requestId)) return false;
    if (!r.readU64(out.txnId)) return false;
    if (!r.readStr(out.resourceId)) return false;
    if (!r.readU8(modeByte)) return false;
    if (!r.readU8(succByte)) return false;
    if (!r.readU8(statusByte)) return false;
    if (!r.readU8(foundByte)) return false;
    if (!r.readStr(out.key)) return false;
    if (!r.readStr(out.value)) return false;
    if (!r.readStr(out.error)) return false;

    out.type = static_cast<MessageType>(typeByte);
    out.lockMode = static_cast<LockMode>(modeByte);
    out.success = (succByte != 0);
    out.status = static_cast<OpStatus>(statusByte);
    out.found = (foundByte != 0);

    uint32_t edgeCount = 0;
    if (!r.readU32(edgeCount) || edgeCount > 100000U) return false;
    out.waitEdges.clear();
    out.waitEdges.reserve(edgeCount);
    for (uint32_t i = 0; i < edgeCount; ++i) {
        WaitEdge e;
        if (!r.readU64(e.waitingTxn) || !r.readU64(e.holdingTxn) || !r.readStr(e.resourceId)) {
            return false;
        }
        out.waitEdges.push_back(std::move(e));
    }

    uint32_t partCount = 0;
    if (!r.readU32(partCount) || partCount > 10000U) return false;
    out.participants.clear();
    out.participants.reserve(partCount);
    for (uint32_t i = 0; i < partCount; ++i) {
        NodeId p = 0;
        if (!r.readU32(p)) return false;
        out.participants.push_back(p);
    }

    uint32_t swCount = 0;
    if (!r.readU32(swCount) || swCount > 100000U) return false;
    out.stagedWrites.clear();
    out.stagedWrites.reserve(swCount);
    for (uint32_t i = 0; i < swCount; ++i) {
        StagedWriteItem sw;
        uint8_t delByte = 0;
        if (!r.readStr(sw.key) || !r.readU8(delByte) || !r.readStr(sw.value)) {
            return false;
        }
        sw.isDelete = (delByte != 0);
        out.stagedWrites.push_back(std::move(sw));
    }

    uint32_t kvCount = 0;
    if (!r.readU32(kvCount) || kvCount > 100000U) return false;
    out.kvPairs.clear();
    out.kvPairs.reserve(kvCount);
    for (uint32_t i = 0; i < kvCount; ++i) {
        std::string k, v;
        if (!r.readStr(k) || !r.readStr(v)) return false;
        out.kvPairs.emplace_back(std::move(k), std::move(v));
    }

    return true;
}

Message Message::deserialize(const std::string& data) {
    Message msg;
    if (!deserialize(data, msg)) {
        return Message{};
    }
    return msg;
}

} // namespace dlm
