#include "core/logger.hpp"

#include <chrono>
#include <stdexcept>

namespace conduit {
Logger::Logger(const std::filesystem::path& file) {
    if (file.has_parent_path()) {
        std::filesystem::create_directories(file.parent_path());
    }
    stream_.open(file, std::ios::app);
    if (!stream_) {
        throw std::runtime_error("Cannot open log file");
    }
}

void Logger::write(std::string_view level, std::string_view message) noexcept {
    try {
        const auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        const std::lock_guard lock(mutex_);
        stream_ << '[' << timestamp << "] [" << level << "] ";
        for (const char ch : message) {
            stream_ << ((ch == '\r' || ch == '\n') ? ' ' : ch);
        }
        stream_ << '\n';
        stream_.flush();
        if (!stream_) {
            failed_.store(true);
        }
    } catch (...) {
        failed_.store(true);
    }
}
}
