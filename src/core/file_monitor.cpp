#include "core/file_monitor.hpp"

#include <array>
#include <cctype>
#include <fstream>
#include <stdexcept>
#include <utility>

namespace conduit {
FileMonitor::FileMonitor(std::filesystem::path root, std::chrono::milliseconds debounce, bool skipDevelopmentMetadata)
    : root_(std::filesystem::absolute(std::move(root))), debounce_(debounce), skipDevelopmentMetadata_(skipDevelopmentMetadata),
      observed_(scan()), published_(observed_) {}

FileMonitor::Snapshot FileMonitor::scan() const {
    if (!std::filesystem::is_directory(root_)) {
        throw std::runtime_error("JSON UI directory is missing or inaccessible");
    }
    Snapshot snapshot;
    for (auto iterator = std::filesystem::recursive_directory_iterator(root_);
         iterator != std::filesystem::recursive_directory_iterator(); ++iterator) {
        const auto& entry = *iterator;
        if (skipDevelopmentMetadata_ && entry.is_directory() &&
            (entry.path().filename() == ".git" || entry.path().filename() == ".vscode")) {
            iterator.disable_recursion_pending();
            continue;
        }
        if (entry.is_symlink() || !entry.is_regular_file()) {
            continue;
        }
        auto extension = entry.path().extension().string();
        for (char& ch : extension) {
            ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
        }
        if (extension != ".json") {
            continue;
        }
        std::ifstream stream(entry.path(), std::ios::binary);
        if (!stream) {
            throw std::runtime_error("Cannot read a JSON UI file");
        }
        Fingerprint fingerprint{14695981039346656037ull, 0};
        std::array<char, 8192> bytes{};
        while (stream.read(bytes.data(), static_cast<std::streamsize>(bytes.size())) || stream.gcount()) {
            for (std::streamsize i = 0; i < stream.gcount(); ++i) {
                fingerprint.hash ^= static_cast<unsigned char>(bytes[static_cast<std::size_t>(i)]);
                fingerprint.hash *= 1099511628211ull;
            }
            fingerprint.size += static_cast<std::uint64_t>(stream.gcount());
        }
        if (!stream.eof()) {
            throw std::runtime_error("Failed to finish reading a JSON UI file");
        }
        snapshot.emplace(entry.path(), fingerprint);
    }
    return snapshot;
}

std::vector<FileChange> FileMonitor::poll(Clock::time_point now) {
    auto next = scan();
    if (next != observed_) {
        observed_ = std::move(next);
        lastChange_ = now;
    }
    if (observed_ == published_ || now - lastChange_ < debounce_) {
        return {};
    }
    std::vector<FileChange> changes;
    for (const auto& [path, fingerprint] : observed_) {
        const auto previous = published_.find(path);
        if (previous == published_.end()) {
            changes.push_back({path, ChangeKind::Created});
        } else if (previous->second != fingerprint) {
            changes.push_back({path, ChangeKind::Modified});
        }
    }
    for (const auto& [path, fingerprint] : published_) {
        (void)fingerprint;
        if (!observed_.contains(path)) {
            changes.push_back({path, ChangeKind::Removed});
        }
    }
    published_ = observed_;
    return changes;
}
}
