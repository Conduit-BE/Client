#include "standalone/title_logo_asset.hpp"
#include "standalone/logo_resource.hpp"
#include <windows.h>
#include <nlohmann/json.hpp>
#include <fstream>
#include <cstring>
#include <stdexcept>
#include <vector>
namespace conduit::standalone {
std::filesystem::path prepareTitleLogoFile(const std::filesystem::path& root) {
    const auto image = embeddedLogo();
    if (image.png.empty() || image.width != 2004 || image.height != 686)
        throw std::runtime_error("Embedded title PNG unavailable; original logo retained");
    const auto directory = root / "cache";
    std::filesystem::create_directories(directory);
    const auto file = directory / "logo-3d2b75a9f89f0a0c.png";
    bool matches = false;
    if (std::filesystem::is_regular_file(file) && std::filesystem::file_size(file) == image.png.size()) {
        std::ifstream input(file,std::ios::binary);
        const std::vector<char> bytes{std::istreambuf_iterator<char>(input),{}};
        matches = !input.bad() && bytes.size() == image.png.size() &&
            std::memcmp(bytes.data(),image.png.data(),bytes.size()) == 0;
    }
    if (!matches) {
        auto temporary = file; temporary += L".tmp";
        std::ofstream output(temporary,std::ios::binary|std::ios::trunc);
        output.write(reinterpret_cast<const char*>(image.png.data()),static_cast<std::streamsize>(image.png.size()));
        output.close();
        if (!output || !MoveFileExW(temporary.c_str(),file.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))
            throw std::runtime_error("Cannot prepare cached title PNG; original logo retained");
    }
    return file;
}
bool vanillaGlobalPacks(const std::filesystem::path& local, const std::filesystem::path& roaming) noexcept {
    try {
        std::vector<std::filesystem::path> profiles;
        const auto add = [&](const std::filesystem::path& path) {
            if (std::filesystem::is_directory(path)) profiles.push_back(path);
        };
        add(local / "games/com.mojang");
        const auto packages = local / "Packages";
        if (std::filesystem::is_directory(packages)) for (const auto& entry : std::filesystem::directory_iterator(packages))
            if (entry.path().filename().string().starts_with("Microsoft.Minecraft"))
                add(entry.path()/"LocalState/games/com.mojang");
        if (!roaming.empty()) {
            const auto users = roaming/"Minecraft Bedrock/Users";
            if (std::filesystem::is_directory(users)) for (const auto& entry : std::filesystem::directory_iterator(users))
                if (entry.is_directory() && !entry.is_symlink()) add(entry.path()/"games/com.mojang");
        }
        bool verified = false;
        for (const auto& profile : profiles) {
            const auto file = profile/"global_resource_packs.json";
            if (!std::filesystem::exists(file)) {
                // A recognized initialized profile without a global stack uses
                // the vanilla default. Unknown layouts remain rejected.
                if (std::filesystem::is_regular_file(profile/"minecraftpe/options.txt")) verified = true;
                continue;
            }
            if (!std::filesystem::is_regular_file(file)) return false;
            if (std::filesystem::file_size(file) > 65536) return false;
            std::ifstream input(file);
            const auto packs = nlohmann::json::parse(input);
            if (!packs.is_array() || !packs.empty()) return false;
            verified = true;
        }
        return verified;
    } catch (...) { return false; }
}
}
