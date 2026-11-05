#pragma once

#include <atomic>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string_view>

namespace conduit {
class Logger {
public:
    explicit Logger(const std::filesystem::path& file);
    void write(std::string_view level, std::string_view message) noexcept;
    [[nodiscard]] bool failed() const noexcept { return failed_.load(); }
private:
    std::mutex mutex_;
    std::ofstream stream_;
    std::atomic<bool> failed_{false};
};
}
