#include "dlm/common/Platform.h"
#include <chrono>
#include <filesystem>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace dlm {

DurableFile::~DurableFile() {
    close();
}

bool DurableFile::openAppend(const std::string& path) {
    close();
    path_ = path;
#ifdef _WIN32
    HANDLE h = CreateFileA(
        path.c_str(),
        FILE_APPEND_DATA | GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr,
        OPEN_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        return false;
    }
    handle_ = static_cast<void*>(h);
    return true;
#else
    int fd = ::open(path.c_str(), O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (fd < 0) return false;
    fd_ = fd;
    return true;
#endif
}

bool DurableFile::openTruncate(const std::string& path) {
    close();
    path_ = path;
#ifdef _WIN32
    HANDLE h = CreateFileA(
        path.c_str(),
        GENERIC_WRITE | GENERIC_READ,
        FILE_SHARE_READ,
        nullptr,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        return false;
    }
    handle_ = static_cast<void*>(h);
    return true;
#else
    int fd = ::open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) return false;
    fd_ = fd;
    return true;
#endif
}

bool DurableFile::writeAll(const void* data, size_t len) {
    if (!isOpen()) return false;
    if (len == 0) return true;
    const auto* ptr = static_cast<const uint8_t*>(data);
    size_t remaining = len;
#ifdef _WIN32
    HANDLE h = static_cast<HANDLE>(handle_);
    while (remaining > 0) {
        DWORD toWrite = static_cast<DWORD>(remaining > 0x7FFFFFFFUL ? 0x7FFFFFFFUL : remaining);
        DWORD written = 0;
        if (!WriteFile(h, ptr, toWrite, &written, nullptr) || written == 0) {
            return false;
        }
        ptr += written;
        remaining -= written;
    }
    return true;
#else
    while (remaining > 0) {
        ssize_t n = ::write(fd_, ptr, remaining);
        if (n < 0) {
            if (errno == EINTR) continue;
            return false;
        }
        ptr += n;
        remaining -= static_cast<size_t>(n);
    }
    return true;
#endif
}

bool DurableFile::writeLine(const std::string& line) {
    std::string buf = line;
    buf.push_back('\n');
    return writeAll(buf.data(), buf.size());
}

bool DurableFile::sync() {
    if (!isOpen()) return false;
#ifdef _WIN32
    return FlushFileBuffers(static_cast<HANDLE>(handle_)) != 0;
#else
    while (::fsync(fd_) < 0) {
        if (errno != EINTR) return false;
    }
    return true;
#endif
}

void DurableFile::close() {
#ifdef _WIN32
    if (handle_ != nullptr) {
        CloseHandle(static_cast<HANDLE>(handle_));
        handle_ = nullptr;
    }
#else
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
#endif
}

bool DurableFile::isOpen() const {
#ifdef _WIN32
    return handle_ != nullptr;
#else
    return fd_ >= 0;
#endif
}

uint64_t DurableFile::size() const {
    if (!isOpen()) return 0;
#ifdef _WIN32
    LARGE_INTEGER sz{};
    if (!GetFileSizeEx(static_cast<HANDLE>(handle_), &sz)) return 0;
    return static_cast<uint64_t>(sz.QuadPart);
#else
    struct stat st{};
    if (::fstat(fd_, &st) != 0) return 0;
    return static_cast<uint64_t>(st.st_size);
#endif
}

bool atomicWriteFile(const std::string& targetPath, const std::string& content) {
    std::filesystem::path target(targetPath);
    if (target.has_parent_path()) {
        std::error_code ec;
        std::filesystem::create_directories(target.parent_path(), ec);
        if (ec) return false;
    }

    std::string tmpPath = targetPath + ".tmp";
    {
        DurableFile f;
        if (!f.openTruncate(tmpPath)) return false;
        if (!f.writeAll(content.data(), content.size())) {
            f.close();
            std::error_code ec;
            std::filesystem::remove(tmpPath, ec);
            return false;
        }
        if (!f.sync()) {
            f.close();
            std::error_code ec;
            std::filesystem::remove(tmpPath, ec);
            return false;
        }
        f.close();
    }

#ifdef _WIN32
    if (!MoveFileExA(tmpPath.c_str(), targetPath.c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        std::error_code ec;
        std::filesystem::remove(tmpPath, ec);
        return false;
    }
#else
    std::error_code ec;
    std::filesystem::rename(tmpPath, targetPath, ec);
    if (ec) {
        std::filesystem::remove(tmpPath, ec);
        return false;
    }
    if (target.has_parent_path()) {
        syncDirectory(target.parent_path().string());
    }
#endif
    return true;
}

bool syncDirectory(const std::string& dirPath) {
#ifdef _WIN32
    (void)dirPath;
    return true;
#else
    int dfd = ::open(dirPath.c_str(), O_RDONLY);
    if (dfd < 0) return false;
    bool ok = (::fsync(dfd) == 0);
    ::close(dfd);
    return ok;
#endif
}

uint64_t nowEpochMs() {
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch())
            .count());
}

} // namespace dlm
