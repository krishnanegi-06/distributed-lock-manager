#include "dlm/api/AuthManager.h"
#include "dlm/common/Hash.h"
#include "dlm/common/Logger.h"
#include "dlm/common/Platform.h"
#include <nlohmann/json.hpp>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace dlm {

namespace {
constexpr uint32_t kDefaultPbkdf2Iters = 100000;
constexpr uint64_t kSessionTtlMs       = 12ULL * 60ULL * 60ULL * 1000ULL; // 12 hours

std::string readEnvStr(const char* name) {
#ifdef _WIN32
    char* buf = nullptr;
    size_t sz = 0;
    if (_dupenv_s(&buf, &sz, name) == 0 && buf != nullptr) {
        std::string v(buf);
        free(buf);
        return v;
    }
    return {};
#else
    const char* v = std::getenv(name);
    return v ? std::string(v) : std::string{};
#endif
}
} // namespace

AuthManager::AuthManager(std::string authStorePath)
    : authStorePath_(std::move(authStorePath)) {}

void AuthManager::initialize() {
    std::lock_guard<std::mutex> lock(mutex_);

    std::string envSecret = readEnvStr("DLM_AUTH_SECRET");
    signingSecret_ = envSecret.empty() ? randomHex(32) : envSecret;

    loadFromDisk();

    // Bootstrap from environment if configured and not already persisted
    std::string envUser = readEnvStr("DLM_ADMIN_USER");
    std::string envPass = readEnvStr("DLM_ADMIN_PASSWORD");
    std::string envHash = readEnvStr("DLM_ADMIN_PASSWORD_HASH");

    if (!envUser.empty() && users_.find(envUser) == users_.end()) {
        if (!envHash.empty()) {
            users_[envUser] = UserRecord{envUser, envHash, "admin"};
            saveToDisk();
        } else if (!envPass.empty() && envPass.size() >= 8) {
            users_[envUser] = UserRecord{envUser, hashPassword(envPass), "admin"};
            saveToDisk();
        }
    }
}

bool AuthManager::isSetupComplete() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return !users_.empty();
}

std::string AuthManager::hashPassword(const std::string& password) {
    std::string salt = randomHex(16);
    std::string derived = pbkdf2Sha256Hex(password, salt, kDefaultPbkdf2Iters);
    return "pbkdf2_sha256$" + std::to_string(kDefaultPbkdf2Iters) + "$" + salt + "$" + derived;
}

bool AuthManager::verifyPassword(const std::string& password, const std::string& storedHash) {
    std::istringstream iss(storedHash);
    std::string alg, iterStr, salt, expectedHex;
    if (!std::getline(iss, alg, '$') ||
        !std::getline(iss, iterStr, '$') ||
        !std::getline(iss, salt, '$') ||
        !std::getline(iss, expectedHex)) {
        return false;
    }
    if (alg != "pbkdf2_sha256") return false;
    uint32_t iters = 0;
    try {
        iters = static_cast<uint32_t>(std::stoul(iterStr));
    } catch (...) {
        return false;
    }
    if (iters < 10000 || iters > 2000000) return false;
    std::string actualHex = pbkdf2Sha256Hex(password, salt, iters);
    return constantTimeEqual(actualHex, expectedHex);
}

bool AuthManager::setupInitialAdmin(const std::string& username,
                                    const std::string& password,
                                    std::string& outError) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!users_.empty()) {
        outError = "Admin account is already configured.";
        return false;
    }
    if (username.size() < 3 || username.size() > 64) {
        outError = "Username must be between 3 and 64 characters.";
        return false;
    }
    if (password.size() < 8 || password.size() > 128) {
        outError = "Password must be between 8 and 128 characters.";
        return false;
    }
    users_[username] = UserRecord{username, hashPassword(password), "admin"};
    if (!saveToDisk()) {
        outError = "Failed to persist admin configuration.";
        return false;
    }
    return true;
}

bool AuthManager::login(const std::string& username,
                        const std::string& password,
                        std::string& outToken,
                        std::string& outError) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (users_.empty()) {
        outError = "No admin account configured. Complete initial setup first.";
        return false;
    }
    auto it = users_.find(username);
    if (it == users_.end() || !verifyPassword(password, it->second.passwordHash)) {
        outError = "Invalid username or password.";
        return false;
    }
    outToken = issueToken(it->second.username, it->second.role, kSessionTtlMs);
    return true;
}

void AuthManager::logout(const std::string& token) {
    if (token.empty()) return;
    AuthSession sess;
    if (validateToken(token, sess)) {
        std::lock_guard<std::mutex> lock(mutex_);
        revokedTokens_[token] = sess.expiresAtMs;
        // Prune expired revocations
        uint64_t now = nowEpochMs();
        for (auto it = revokedTokens_.begin(); it != revokedTokens_.end(); ) {
            if (it->second < now) it = revokedTokens_.erase(it);
            else ++it;
        }
    }
}

std::string AuthManager::issueToken(const std::string& username,
                                    const std::string& role,
                                    uint64_t ttlMs) const {
    uint64_t exp = nowEpochMs() + ttlMs;
    nlohmann::json payload = {
        {"u", username},
        {"r", role},
        {"exp", exp},
        {"jti", randomHex(8)}
    };
    std::string b64 = base64Encode(payload.dump());
    std::string sig = hmacSha256Hex(signingSecret_, b64);
    return b64 + "." + sig;
}

bool AuthManager::validateToken(const std::string& token, AuthSession& outSession) const {
    auto dot = token.find('.');
    if (dot == std::string::npos || dot == 0 || dot + 1 >= token.size()) {
        return false;
    }
    std::string b64 = token.substr(0, dot);
    std::string sig = token.substr(dot + 1);

    std::lock_guard<std::mutex> lock(mutex_);
    if (revokedTokens_.find(token) != revokedTokens_.end()) {
        return false;
    }

    std::string expectedSig = hmacSha256Hex(signingSecret_, b64);
    if (!constantTimeEqual(sig, expectedSig)) {
        return false;
    }

    std::string decoded;
    if (!base64Decode(b64, decoded)) {
        return false;
    }

    try {
        auto j = nlohmann::json::parse(decoded);
        uint64_t exp = j.value("exp", uint64_t{0});
        if (exp == 0 || nowEpochMs() > exp) {
            return false;
        }
        outSession.username = j.value("u", std::string{});
        outSession.role = j.value("r", std::string{});
        outSession.expiresAtMs = exp;
        return !outSession.username.empty();
    } catch (...) {
        return false;
    }
}

bool AuthManager::saveToDisk() const {
    if (authStorePath_.empty()) return true;
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& [name, u] : users_) {
        arr.push_back({
            {"username", u.username},
            {"passwordHash", u.passwordHash},
            {"role", u.role}
        });
    }
    nlohmann::json root = {{"users", arr}};
    return atomicWriteFile(authStorePath_, root.dump(2));
}

bool AuthManager::loadFromDisk() {
    if (authStorePath_.empty() || !std::filesystem::exists(authStorePath_)) return false;
    std::ifstream f(authStorePath_, std::ios::binary);
    if (!f.is_open()) return false;
    try {
        nlohmann::json root;
        f >> root;
        if (!root.contains("users") || !root["users"].is_array()) return false;
        users_.clear();
        for (const auto& item : root["users"]) {
            UserRecord u;
            u.username = item.value("username", std::string{});
            u.passwordHash = item.value("passwordHash", std::string{});
            u.role = item.value("role", std::string{"admin"});
            if (!u.username.empty() && !u.passwordHash.empty()) {
                users_[u.username] = std::move(u);
            }
        }
        return true;
    } catch (...) {
        return false;
    }
}

} // namespace dlm
