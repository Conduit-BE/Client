#include "standalone/settings.hpp"
#include <nlohmann/json.hpp>
#ifdef _WIN32
#include <windows.h>
#endif
#include <fstream>
#include <cmath>
#include <set>
#include <stdexcept>
namespace conduit::standalone {
namespace {
DiagnosticsSettings parseDiagnostics(const nlohmann::json& settings) {
    DiagnosticsSettings result;
    if (!settings.contains("diagnostics")) return result;
    const auto& diagnostics = settings.at("diagnostics");
    if (!diagnostics.is_object()) throw std::runtime_error("diagnostics must be an object");
    if (diagnostics.contains("scale")) {
        const auto& scale = diagnostics.at("scale");
        if (!scale.is_number()) throw std::runtime_error("diagnostics.scale must be a number between 0.25 and 4");
        const double value = scale.get<double>();
        if (!std::isfinite(value) || value < 0.25 || value > 4) throw std::runtime_error("diagnostics.scale must be between 0.25 and 4");
        result.scale = static_cast<float>(value);
    }
    if (diagnostics.contains("factories")) {
        const auto& factories = diagnostics.at("factories");
        if (!factories.is_object()) throw std::runtime_error("diagnostics.factories must be an object");
        const auto readBool = [&](const char* key, bool& target) {
            if (!factories.contains(key)) return;
            if (!factories.at(key).is_boolean()) throw std::runtime_error(std::string("diagnostics.factories.")+key+" must be boolean");
            target = factories.at(key).get<bool>();
        };
        readBool("enabled", result.factoriesEnabled);
        readBool("empty", result.emptyFactories);
    }
    return result;
}
}
DiagnosticsSettings loadDiagnosticsSettings(const std::filesystem::path& file) {
    if (std::filesystem::file_size(file) > 16384) throw std::runtime_error("config.json exceeds 16 KiB");
    std::ifstream input(file);
    if (!input) throw std::runtime_error("Cannot read config.json");
    const auto settings = nlohmann::json::parse(input);
    if (!settings.is_object()) throw std::runtime_error("config.json must contain an object");
    return parseDiagnostics(settings);
}
void migrateSettings(const std::filesystem::path& file) {
    auto settings = nlohmann::json::object();
    if (std::filesystem::exists(file)) {
        if (std::filesystem::file_size(file) > 16384) throw std::runtime_error("config.json exceeds 16 KiB");
        std::ifstream input(file);
        if (!input) throw std::runtime_error("Cannot read config.json");
        settings = nlohmann::json::parse(input);
        if (!settings.is_object()) throw std::runtime_error("config.json must contain an object");
    }
    const auto diagnostics = parseDiagnostics(settings); // Validate before replacing the original file.
    auto& section = settings["diagnostics"];
    if (section.is_null()) section = nlohmann::json::object();
    section["scale"] = diagnostics.scale;
    auto& factories = section["factories"];
    if (factories.is_null()) factories = nlohmann::json::object();
    factories["enabled"] = diagnostics.factoriesEnabled;
    factories["empty"] = diagnostics.emptyFactories;
    settings.erase("enabled"); settings.erase("resourcePackPath"); settings.erase("reloadMode");
    std::filesystem::create_directories(file.parent_path());
    const auto temporary = std::filesystem::path(file.native() + std::filesystem::path(".tmp").native());
    {
        std::ofstream output(temporary, std::ios::trunc);
        output << settings.dump(2) << '\n'; output.close();
        if (!output) throw std::runtime_error("Cannot write migrated config.json");
    }
    // Windows rename cannot replace an existing file. The caller performs replacement
    // with MoveFileExW; portable hosts use atomic rename.
#ifdef _WIN32
    // Declared below through windows.h.
    if (!MoveFileExW(temporary.c_str(), file.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        throw std::runtime_error("Cannot replace migrated config.json");
#else
    std::filesystem::rename(temporary, file);
#endif
}
std::filesystem::path logDirectory(const std::filesystem::path& root) { return root / "logs"; }
std::vector<std::filesystem::path> discoverPackRoots(const std::filesystem::path& local,
                                                   const std::filesystem::path& roaming) {
    std::set<std::filesystem::path> profiles;
    const auto addProfile = [&](const std::filesystem::path& path) {
        if (std::filesystem::is_directory(path)) profiles.insert(path);
    };
    addProfile(local / "games/com.mojang");
    const auto packages = local / "Packages";
    if (std::filesystem::is_directory(packages)) for (const auto& package : std::filesystem::directory_iterator(packages)) {
        const auto name = package.path().filename().string();
        if (name.starts_with("Microsoft.Minecraft")) addProfile(package.path() / "LocalState/games/com.mojang");
    }
    if (!roaming.empty()) {
        const auto users = roaming / "Minecraft Bedrock/Users";
        if (std::filesystem::is_directory(users)) for (const auto& user : std::filesystem::directory_iterator(users))
            if (user.is_directory() && !user.is_symlink()) addProfile(user.path() / "games/com.mojang");
    }
    std::set<std::filesystem::path> packs;
    for (const auto& profile : profiles) for (const auto* directory : {"development_resource_packs", "resource_packs"}) {
        const auto root = profile / directory;
        if (!std::filesystem::is_directory(root)) continue;
        for (const auto& pack : std::filesystem::directory_iterator(root)) {
            if (pack.is_symlink() || !pack.is_directory()) continue;
            if (std::filesystem::is_directory(pack.path() / "ui") && std::filesystem::is_regular_file(pack.path() / "manifest.json"))
                packs.insert(std::filesystem::weakly_canonical(pack.path()));
            if (packs.size() > 256) throw std::runtime_error("Pack discovery exceeds 256 UI packs");
        }
    }
    return {packs.begin(), packs.end()};
}
}
