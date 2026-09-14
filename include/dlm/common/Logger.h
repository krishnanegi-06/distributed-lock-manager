#pragma once
#include <string>
#include <mutex>
#include <fstream>
#include <atomic>

namespace dlm {

enum class LogLevel : uint8_t { DEBUG, INFO, WARN, ERR };

class Logger {
public:
    static void debug(const std::string& message);
    static void info(const std::string& message);
    static void warn(const std::string& message);
    static void error(const std::string& message);

    /// Set the minimum log level (default: INFO).
    static void setLevel(LogLevel level);

    /// Optionally also write to a file.
    static void setLogFile(const std::string& path);

private:
    static void log(LogLevel level, const std::string& message);

    static std::mutex  mutex_;
    static LogLevel    minLevel_;
    static std::ofstream fileStream_;
};

} // namespace dlm