#include "util/Log.hpp"

namespace pf {

Log& Log::instance() {
    static Log log;
    return log;
}

void Log::add(LogLevel level, std::string text) {
    std::lock_guard lock(mu_);
    lines_.push_back({std::chrono::system_clock::now(), level, std::move(text)});
    while (lines_.size() > capacity_) lines_.pop_front();
    ++total_;
}

std::vector<LogLine> Log::snapshot() const {
    std::lock_guard lock(mu_);
    return {lines_.begin(), lines_.end()};
}

std::uint64_t Log::totalAdded() const {
    std::lock_guard lock(mu_);
    return total_;
}

void Log::clear() {
    std::lock_guard lock(mu_);
    lines_.clear();
}

void Log::setCapacity(std::size_t n) {
    std::lock_guard lock(mu_);
    capacity_ = n < 1 ? 1 : n;
    while (lines_.size() > capacity_) lines_.pop_front();
}

}  // namespace pf
