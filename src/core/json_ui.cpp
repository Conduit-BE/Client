#include "core/json_ui.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <fstream>
#include <set>
#include <stdexcept>
#include <utility>

namespace conduit {
bool JsonUiRepository::refresh(const std::filesystem::path& root, bool skipDevelopmentMetadata) {
    JsonUiInfo candidate;
    std::set<std::string> namespaces;
    std::filesystem::path current = root;
    try {
        constexpr std::size_t maxFileBytes = 8 * 1024 * 1024;
        constexpr std::size_t maxTreeBytes = 64 * 1024 * 1024;
        std::size_t treeBytes = 0;
        for (auto iterator = std::filesystem::recursive_directory_iterator(root);
             iterator != std::filesystem::recursive_directory_iterator(); ++iterator) {
            const auto& entry = *iterator;
            if (skipDevelopmentMetadata && entry.is_directory() &&
                (entry.path().filename() == ".git" || entry.path().filename() == ".vscode")) {
                iterator.disable_recursion_pending();
                continue;
            }
            current = entry.path();
            if (entry.is_symlink() || !entry.is_regular_file()) { continue; }
            auto extension = current.extension().string();
            std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c) {
                return c >= 'A' && c <= 'Z' ? static_cast<char>(c + ('a' - 'A')) : static_cast<char>(c);
            });
            if (extension != ".json") { continue; }
            std::ifstream input(current, std::ios::binary);
            if (!input) { throw std::runtime_error("Cannot open file"); }
            // Read with a hard bound even if an editor grows the file during reading.
            std::string bytes;
            char buffer[4096];
            while (input.read(buffer, sizeof(buffer)) || input.gcount() != 0) {
                const auto count = static_cast<std::size_t>(input.gcount());
                if (bytes.size() + count > maxFileBytes || treeBytes + count > maxTreeBytes) {
                    throw std::runtime_error("JSON UI input size limit exceeded");
                }
                bytes.append(buffer, count);
                treeBytes += count;
            }
            if (input.bad()) { throw std::runtime_error("Cannot read file"); }
            // Bedrock resource-pack JSON permits comments. This is syntax validation,
            // not a substitute for Minecraft's definition and binding validation.
            const auto json = nlohmann::json::parse(bytes, nullptr, true, true);
            if (!json.is_object()) { throw std::runtime_error("Expected a JSON UI object"); }
            if (json.contains("namespace")) {
                if (!json["namespace"].is_string()) { throw std::runtime_error("Namespace must be a string"); }
                namespaces.insert(json["namespace"].get<std::string>());
            }
            for (const auto& [name, value] : json.items()) {
                if (name != "namespace" && value.is_object()) { ++candidate.definitions; }
            }
            ++candidate.files;
        }
        candidate.namespaces.assign(namespaces.begin(), namespaces.end());
        const std::lock_guard lock(mutex_);
        info_ = std::move(candidate);
        return true;
    } catch (const std::exception& error) {
        const std::lock_guard lock(mutex_);
        info_.error = current.string() + ": " + error.what();
        return false;
    }
}

JsonUiInfo JsonUiRepository::info() const {
    const std::lock_guard lock(mutex_);
    return info_;
}
}
