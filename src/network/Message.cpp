#include "dlm/network/Message.h"
#include <sstream>
#include <vector>
#include <string>

namespace dlm {

static std::vector<std::string> split(const std::string& s, char delim) {
    std::vector<std::string> result;
    std::stringstream ss(s);
    std::string item;
    while (std::getline(ss, item, delim)) {
        result.push_back(item);
    }
    // If the string ends with a delimiter, the last empty string isn't always pushed by std::getline
    if (!s.empty() && s.back() == delim) {
        result.push_back("");
    }
    return result;
}

std::string Message::serialize() const {
    std::ostringstream oss;
    oss << static_cast<int>(type) << "|"
        << senderId << "|"
        << txnId << "|"
        << resourceId << "|"
        << static_cast<int>(lockMode) << "|"
        << (success ? 1 : 0) << "|"
        << waitEdges.size() << "|";
    
    oss << "[";
    for (size_t i = 0; i < waitEdges.size(); ++i) {
        oss << waitEdges[i].waitingTxn << "," << waitEdges[i].holdingTxn << "," << waitEdges[i].resourceId;
        if (i + 1 < waitEdges.size()) oss << ";";
    }
    oss << "]|";

    oss << participants.size() << "|";
    oss << "[";
    for (size_t i = 0; i < participants.size(); ++i) {
        oss << participants[i];
        if (i + 1 < participants.size()) oss << ",";
    }
    oss << "]";
    
    return oss.str();
}

Message Message::deserialize(const std::string& data) {
    Message msg;
    auto parts = split(data, '|');
    if (parts.size() < 10) return msg;

    msg.type = static_cast<MessageType>(std::stoi(parts[0]));
    msg.senderId = std::stoul(parts[1]);
    msg.txnId = std::stoul(parts[2]);
    msg.resourceId = parts[3];
    msg.lockMode = static_cast<LockMode>(std::stoi(parts[4]));
    msg.success = (parts[5] == "1");

    // size_t edgeCount = std::stoul(parts[6]);
    std::string edgesStr = parts[7];
    if (edgesStr.length() >= 2) {
        edgesStr = edgesStr.substr(1, edgesStr.length() - 2); // remove [ and ]
        if (!edgesStr.empty()) {
            auto edgesList = split(edgesStr, ';');
            for (const auto& e : edgesList) {
                auto eParts = split(e, ',');
                if (eParts.size() == 3) {
                    WaitEdge edge;
                    edge.waitingTxn = std::stoul(eParts[0]);
                    edge.holdingTxn = std::stoul(eParts[1]);
                    edge.resourceId = eParts[2];
                    msg.waitEdges.push_back(edge);
                }
            }
        }
    }

    // size_t participantCount = std::stoul(parts[8]);
    std::string partsStr = parts[9];
    if (partsStr.length() >= 2) {
        partsStr = partsStr.substr(1, partsStr.length() - 2); // remove [ and ]
        if (!partsStr.empty()) {
            auto pList = split(partsStr, ',');
            for (const auto& p : pList) {
                msg.participants.push_back(std::stoul(p));
            }
        }
    }

    return msg;
}

} // namespace dlm
