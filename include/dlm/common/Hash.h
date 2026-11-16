#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace dlm {

/// Deterministic, platform-independent 64-bit FNV-1a hash used for key sharding.
uint64_t fnv1a64(const std::string& data);

/// Standard IEEE 802.3 CRC-32 checksum for WAL and checkpoint records.
uint32_t crc32(const void* data, size_t len);
inline uint32_t crc32(const std::string& s) {
    return crc32(s.data(), s.size());
}

/// SHA-1 digest (used for RFC-6455 WebSocket handshake accept key).
std::array<uint8_t, 20> sha1(const std::string& data);

/// SHA-256 digest & hex helper.
std::array<uint8_t, 32> sha256(const std::string& data);
std::string toHex(const uint8_t* data, size_t len);
std::string sha256Hex(const std::string& data);

/// HMAC-SHA256 & constant-time equality check.
std::array<uint8_t, 32> hmacSha256(const std::string& key, const std::string& message);
std::string hmacSha256Hex(const std::string& key, const std::string& message);
bool constantTimeEqual(const std::string& a, const std::string& b);

/// PBKDF2-HMAC-SHA256 password hashing.
std::string pbkdf2Sha256Hex(const std::string& password,
                            const std::string& salt,
                            uint32_t iterations = 100000);

/// Base64 encoding / decoding.
std::string base64Encode(const uint8_t* data, size_t len);
std::string base64Encode(const std::string& data);
bool base64Decode(const std::string& b64, std::string& out);

/// Cryptographically strong random bytes / hex string.
std::string randomHex(size_t numBytes = 16);

} // namespace dlm
