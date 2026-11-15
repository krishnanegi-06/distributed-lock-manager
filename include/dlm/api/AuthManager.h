#pragma once
#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>

namespace dlm {

struct AuthSession {
    std::string username;
    std::string role;
    uint64_t    expiresAtMs = 0;
};

/// Admin authentication, PBKDF2-HMAC-SHA256 password hashing, and HMAC-SHA256 session tokens.
class AuthManager {
public:
    explicit AuthManager(std::string authStorePath = "data/auth.json");

    /// Load configured credentials from disk or environment variables
    /// (`DLM_ADMIN_USER`, `DLM_ADMIN_PASSWORD`, `DLM_ADMIN_PASSWORD_HASH`, `DLM_AUTH_SECRET`).
    void initialize();

    /// True if at least one admin account has been configured.
    bool isSetupComplete() const;

    /// First-run admin setup when no admin user exists yet.
    bool setupInitialAdmin(const std::string& username,
                           const std::string& password,
                           std::string& outError);

    /// Authenticate username/password and issue a signed bearer token.
    bool login(const std::string& username,
               const std::string& password,
               std::string& outToken,
               std::string& outError);

    /// Revoke a token (logout).
    void logout(const std::string& token);

    /// Validate a bearer token; populates `outSession` if valid and unexpired.
    bool validateToken(const std::string& token, AuthSession& outSession) const;

    /// Hash & verify helpers using PBKDF2-HMAC-SHA256 with random per-user salt.
    static std::string hashPassword(const std::string& password);
    static bool verifyPassword(const std::string& password, const std::string& storedHash);

private:
    bool saveToDisk() const;
    bool loadFromDisk();
    std::string issueToken(const std::string& username, const std::string& role, uint64_t ttlMs) const;

    struct UserRecord {
        std::string username;
        std::string passwordHash; // pbkdf2_sha256$<iterations>$<salt>$<hex>
        std::string role = "admin";
    };

    std::string authStorePath_;
    std::string signingSecret_;
    mutable std::mutex mutex_;
    std::unordered_map<std::string, UserRecord> users_;
    std::unordered_map<std::string, uint64_t>   revokedTokens_; // token -> expiry
};

} // namespace dlm
