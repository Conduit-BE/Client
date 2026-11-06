#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <map>
#include <vector>

namespace conduit {
enum class ChangeKind { Created, Modified, Removed };
struct FileChange {
    std::filesystem::path path;
    ChangeKind kind;
};

// Polls contents, including same-size edits with unchanged timestamps. A scan
// failure throws without publishing a partial snapshot. Does not parse JSON.
class FileMonitor {
public:
    using Clock = std::chrono::steady_clock;
    FileMonitor(std::filesystem::path root, std::chrono::milliseconds debounce, bool skipDevelopmentMetadata = false);
    std::vector<FileChange> poll(Clock::time_point now);
private:
    struct Fingerprint {
        std::uint64_t hash;
        std::uint64_t size;
        bool operator==(const Fingerprint&) const = default;
    };
    using Snapshot = std::map<std::filesystem::path, Fingerprint>;
    Snapshot scan() const;
    std::filesystem::path root_;
    std::chrono::milliseconds debounce_;
    bool skipDevelopmentMetadata_ = false;
    Snapshot observed_;
    Snapshot published_;
    Clock::time_point lastChange_{};
};
}
