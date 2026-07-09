#include "logging/LogSession.h"

#include <algorithm>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <vector>

namespace reconstruct_one_frame {

namespace {

LogSession* g_activeSession = nullptr;

const char* levelName(LogLevel level)
{
    switch (level) {
    case LogLevel::Trace:
        return "trace";
    case LogLevel::Debug:
        return "debug";
    case LogLevel::Info:
        return "info";
    case LogLevel::Warn:
        return "warn";
    case LogLevel::Error:
        return "error";
    }
    return "info";
}

std::string timestampForFile()
{
    const auto now = std::chrono::system_clock::now();
    const std::time_t time = std::chrono::system_clock::to_time_t(now);
    std::tm localTime {};
#if defined(_WIN32)
    localtime_s(&localTime, &time);
#else
    localtime_r(&time, &localTime);
#endif
    std::ostringstream out;
    out << std::put_time(&localTime, "%Y-%m-%d_%H-%M-%S");
    return out.str();
}

std::string timestampForLine()
{
    const auto now = std::chrono::system_clock::now();
    const std::time_t time = std::chrono::system_clock::to_time_t(now);
    std::tm localTime {};
#if defined(_WIN32)
    localtime_s(&localTime, &time);
#else
    localtime_r(&time, &localTime);
#endif
    std::ostringstream out;
    out << std::put_time(&localTime, "%Y-%m-%d %H:%M:%S");
    return out.str();
}

void pruneLogs(const std::filesystem::path& directory, std::size_t maxLogFiles)
{
    std::vector<std::filesystem::directory_entry> logs;
    if (!std::filesystem::exists(directory)) {
        return;
    }

    for (const auto& entry : std::filesystem::directory_iterator(directory)) {
        if (!entry.is_regular_file()) {
            continue;
        }
        const auto filename = entry.path().filename().string();
        if (filename.rfind("log_", 0) == 0 && entry.path().extension() == ".log") {
            logs.push_back(entry);
        }
    }

    if (logs.size() <= maxLogFiles) {
        return;
    }

    std::sort(logs.begin(), logs.end(), [](const auto& lhs, const auto& rhs) {
        return lhs.last_write_time() < rhs.last_write_time();
    });

    const std::size_t deleteCount = logs.size() - maxLogFiles;
    for (std::size_t i = 0; i < deleteCount; ++i) {
        std::error_code ec;
        std::filesystem::remove(logs[i].path(), ec);
    }
}

} // namespace

LogSession::~LogSession()
{
    stop();
}

bool LogSession::start(const std::string& tag,
                       const std::filesystem::path& directory,
                       LogLevel level,
                       std::size_t maxLogFiles)
{
    std::lock_guard<std::mutex> lock(mutex_);
    std::filesystem::create_directories(directory);
    path_ = directory / ("log_" + tag + "_" + timestampForFile() + ".log");
    stream_.open(path_, std::ios::out | std::ios::app);
    if (!stream_) {
        return false;
    }
    level_ = level;
    pruneLogs(directory, maxLogFiles);
    g_activeSession = this;
    write(level_, "Log initialized at: " + path_.string());
    return true;
}

void LogSession::stop()
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (g_activeSession == this) {
        g_activeSession = nullptr;
    }
    if (stream_.is_open()) {
        stream_.flush();
        stream_.close();
    }
}

void LogSession::write(LogLevel level, const std::string& message)
{
    if (static_cast<int>(level) < static_cast<int>(level_)) {
        return;
    }

    if (stream_) {
        stream_ << "[" << timestampForLine() << "] [" << levelName(level) << "] " << message << '\n';
        stream_.flush();
    }
}

void setActiveLogSession(LogSession* session)
{
    g_activeSession = session;
}

void logInfo(const std::string& message)
{
    if (g_activeSession != nullptr) {
        g_activeSession->write(LogLevel::Info, message);
    }
}

void logWarn(const std::string& message)
{
    if (g_activeSession != nullptr) {
        g_activeSession->write(LogLevel::Warn, message);
    }
}

void logError(const std::string& message)
{
    if (g_activeSession != nullptr) {
        g_activeSession->write(LogLevel::Error, message);
    }
}

} // namespace reconstruct_one_frame
