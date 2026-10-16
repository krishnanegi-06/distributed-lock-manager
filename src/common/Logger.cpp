#include "dlm/common/Logger.h"
#include <iostream>
#include <chrono>
#include <iomanip>
#include <thread>
#include <sstream>

namespace dlm {

std::mutex Logger::mutex_;
LogLevel Logger::minLevel_ = LogLevel::INFO;
std::ofstream Logger::fileStream_;

void Logger::setLevel(LogLevel level) {
    std::lock_guard<std::mutex> lock(mutex_);
    minLevel_ = level;
}

void Logger::setLogFile(const std::string& path) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (fileStream_.is_open()) {
        fileStream_.close();
    }
    fileStream_.open(path, std::ios::out | std::ios::app);
}

void Logger::debug(const std::string& message) { log(LogLevel::DEBUG, message); }
void Logger::info(const std::string& message) { log(LogLevel::INFO, message); }
void Logger::warn(const std::string& message) { log(LogLevel::WARN, message); }
void Logger::error(const std::string& message) { log(LogLevel::ERR, message); }

void Logger::log(LogLevel level, const std::string& message) {
    if (level < minLevel_) return;

    auto now = std::chrono::system_clock::now();
    auto in_time_t = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                  now.time_since_epoch()) % 1000;

#ifdef _WIN32
    std::tm bt{};
    localtime_s(&bt, &in_time_t);
#else
    std::tm bt{};
    localtime_r(&in_time_t, &bt);
#endif

    std::stringstream time_ss;
    time_ss << std::put_time(&bt, "%H:%M:%S")
            << '.' << std::setfill('0') << std::setw(3) << ms.count();

    const char* levelStr = "INFO";
    switch(level) {
        case LogLevel::DEBUG: levelStr = "DEBUG"; break;
        case LogLevel::INFO:  levelStr = "INFO"; break;
        case LogLevel::WARN:  levelStr = "WARN"; break;
        case LogLevel::ERR: levelStr = "ERROR"; break;
    }

    std::stringstream out;
    out << "[" << levelStr << "][" << time_ss.str() << "][" 
        << std::this_thread::get_id() << "] " << message << "\n";

    std::string outStr = out.str();

    std::lock_guard<std::mutex> lock(mutex_);
    std::cout << outStr;
    if (fileStream_.is_open()) {
        fileStream_ << outStr;
        fileStream_.flush();
    }
}

} // namespace dlm