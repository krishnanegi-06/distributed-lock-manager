#pragma once
#include <cstdint>
#include <string>

namespace dlm {

/// Platform-specific durable append/write and fsync helpers.
class DurableFile {
public:
    DurableFile() = default;
    ~DurableFile();

    DurableFile(const DurableFile&) = delete;
    DurableFile& operator=(const DurableFile&) = delete;

    bool openAppend(const std::string& path);
    bool openTruncate(const std::string& path);
    bool writeAll(const void* data, size_t len);
    bool writeLine(const std::string& line);
    bool sync();
    void close();
    bool isOpen() const;
    uint64_t size() const;

private:
#ifdef _WIN32
    void* handle_ = nullptr; // HANDLE
#else
    int fd_ = -1;
#endif
    std::string path_;
};

/// Write entire buffer to `targetPath` via temp file + durable sync + atomic replace.
bool atomicWriteFile(const std::string& targetPath, const std::string& content);

/// Durable sync of a directory entry (POSIX fsync on directory fd; no-op on Windows).
bool syncDirectory(const std::string& dirPath);

/// Current UTC epoch time in milliseconds.
uint64_t nowEpochMs();

} // namespace dlm
