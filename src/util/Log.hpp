#pragma once
#include <chrono>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <vector>

namespace pf {

enum class LogLevel : std::uint8_t { Info, Warn, Error, Sim, Game };

struct LogLine {
    std::chrono::system_clock::time_point time;
    LogLevel level;
    std::string text;
};

// Thread-safe bounded ring buffer; the console panel reads copies.
class Log {
public:
    static Log& instance();

    void add(LogLevel level, std::string text);
    std::vector<LogLine> snapshot() const;
    std::uint64_t totalAdded() const;
    void clear();
    void setCapacity(std::size_t n);

private:
    mutable std::mutex mu_;
    std::deque<LogLine> lines_;
    std::size_t capacity_ = 2000;
    std::uint64_t total_ = 0;
};

inline void logInfo(std::string s) { Log::instance().add(LogLevel::Info, std::move(s)); }
inline void logWarn(std::string s) { Log::instance().add(LogLevel::Warn, std::move(s)); }
inline void logError(std::string s) { Log::instance().add(LogLevel::Error, std::move(s)); }

}  // namespace pf
