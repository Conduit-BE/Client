#pragma once

#include <cstddef>
#include <filesystem>
#include <mutex>
#include <string>
#include <vector>

namespace conduit {
struct JsonUiInfo {
    std::size_t files = 0;
    std::size_t definitions = 0;
    std::vector<std::string> namespaces;
    std::string error;
};

// Owns metadata for the last completely validated disk tree, not live game UI.
// The coordinator writes it; diagnostics readers receive a synchronized copy.
class JsonUiRepository {
public:
    bool refresh(const std::filesystem::path& root, bool skipDevelopmentMetadata = false);
    JsonUiInfo info() const;
private:
    mutable std::mutex mutex_;
    JsonUiInfo info_;
};
}
