#pragma once

#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <string>

namespace reconstruct_one_frame {

enum class LogLevel {
    Trace = 0,
    Debug,
    Info,
    Warn,
    Error
};

class LogSession {
public:
    LogSession() = default;
    ~LogSession();

    LogSession(const LogSession&) = delete;
    LogSession& operator=(const LogSession&) = delete;

    bool start(const std::string& tag = "reconstructSample",
               const std::filesystem::path& directory = "logs",
               LogLevel level = LogLevel::Info,
               std::size_t maxLogFiles = 10);
    void stop();
    void write(LogLevel level, const std::string& message);

    [[nodiscard]] const std::filesystem::path& path() const noexcept { return path_; }

private:
    std::filesystem::path path_;
    std::ofstream stream_;
    LogLevel level_ = LogLevel::Info;
    std::mutex mutex_;
};

void setActiveLogSession(LogSession* session);
void logInfo(const std::string& message);
void logWarn(const std::string& message);
void logError(const std::string& message);

} // namespace reconstruct_one_frame
