#include "standalone/settings.hpp"
#include <nlohmann/json.hpp>
#ifdef _WIN32
#include <windows.h>
#endif
#include <array>
#include <charconv>
#include <fstream>
#include <string>
#include <string_view>
#include <cmath>
#include <set>
#include <stdexcept>
namespace conduit::standalone {
namespace {
DiagnosticsSettings parseDiagnostics(const nlohmann::json& settings) {
    DiagnosticsSettings result;
    const auto diagnostics = settings.value("diagnostics", nlohmann::json::object());
    if (!diagnostics.is_object()) throw std::runtime_error("diagnostics must be an object");
    const auto ui = settings.value("ui", nlohmann::json::object());
    if (!ui.is_object()) throw std::runtime_error("ui must be an object");
    const auto readNumber = [](const nlohmann::json& field, double min, double max, const char* error) {
        if (!field.is_number()) throw std::runtime_error(error);
        const double value = field.get<double>();
        if (!std::isfinite(value) || value < min || value > max) throw std::runtime_error(error);
        return static_cast<float>(value);
    };
    const auto* scale = ui.contains("scale") ? &ui.at("scale") :
        settings.contains("ui_scale") ? &settings.at("ui_scale") :
        diagnostics.contains("scale") ? &diagnostics.at("scale") : nullptr;
    if (scale) result.uiScale = readNumber(*scale, 0.25, 4, "ui.scale must be a number between 0.25 and 4");
    if (ui.contains("alpha"))
        result.uiAlpha = readNumber(ui.at("alpha"), 0, 1, "ui.alpha must be a number between 0 and 1");
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
struct BindingSetting {
    const char* name;
    const char* defaultValue;
    KeyChord Keybinds::*member;
};
constexpr std::array bindings{
    BindingSetting{"diagnostics", "F3", &Keybinds::diagnostics},
    BindingSetting{"tooltip", "F5", &Keybinds::tooltip},
    BindingSetting{"eject", "Ctrl+K", &Keybinds::eject},
    BindingSetting{"reload", "Ctrl+R", &Keybinds::reload},
};

KeyChord parseKeyChord(std::string_view text, const char* name) {
    KeyChord chord{};
    const auto invalid = [&] { return std::runtime_error(std::string("Invalid keybinds.") + name); };
    for (;;) {
        const auto separator = text.find('+');
        const auto part = text.substr(0, separator);
        const auto first = part.find_first_not_of(" \t");
        if (first == std::string_view::npos) throw invalid();
        auto token = std::string(part.substr(first, part.find_last_not_of(" \t") - first + 1));
        for (auto& ch : token) if (ch >= 'a' && ch <= 'z') ch -= 'a' - 'A';
        if (separator == std::string_view::npos) {
            const char key = token.front();
            if (token.size() == 1 && ((key >= 'A' && key <= 'Z') || (key >= '0' && key <= '9'))) {
                chord.key = static_cast<std::uint8_t>(key);
                return chord;
            }
            if (key == 'F') {
                unsigned number{};
                const auto end = token.data() + token.size();
                const auto parsed = std::from_chars(token.data() + 1, end, number);
                if (parsed.ec == std::errc{} && parsed.ptr == end && number >= 1 && number <= 24) {
                    chord.key = static_cast<std::uint8_t>(0x70 + number - 1); // VK_F1 + index
                    return chord;
                }
            }
            throw invalid();
        }
        bool* modifier = nullptr;
        if (token == "CTRL" || token == "CONTROL") modifier = &chord.ctrl;
        else if (token == "SHIFT") modifier = &chord.shift;
        else if (token == "ALT") modifier = &chord.alt;
        if (!modifier || *modifier) throw invalid();
        *modifier = true;
        text.remove_prefix(separator + 1);
    }
}

Settings parseSettings(const nlohmann::json& config) {
    Settings result;
    result.diagnostics = parseDiagnostics(config);
    const auto keybinds = config.value("keybinds", nlohmann::json::object());
    if (!keybinds.is_object()) throw std::runtime_error("keybinds must be an object");
    for (std::size_t i = 0; i < bindings.size(); ++i) {
        const auto& binding = bindings[i];
        const auto value = keybinds.value(binding.name, std::string(binding.defaultValue));
        const auto chord = parseKeyChord(value, binding.name);
        for (std::size_t j = 0; j < i; ++j) {
            if (chord == result.keybinds.*bindings[j].member)
                throw std::runtime_error(std::string("Duplicate keybinds.") + binding.name + " and keybinds." + bindings[j].name);
        }
        result.keybinds.*binding.member = chord;
    }
    return result;
}

nlohmann::json readSettings(const std::filesystem::path& file) {
    if (std::filesystem::file_size(file) > 16384) throw std::runtime_error("config.json exceeds 16 KiB");
    std::ifstream input(file);
    if (!input) throw std::runtime_error("Cannot read config.json");
    auto settings = nlohmann::json::parse(input);
    if (!settings.is_object()) throw std::runtime_error("config.json must contain an object");
    return settings;
}
}

Settings loadSettings(const std::filesystem::path& file) {
    return parseSettings(readSettings(file));
}

DiagnosticsSettings loadDiagnosticsSettings(const std::filesystem::path& file) {
    return parseDiagnostics(readSettings(file));
}

void migrateSettings(const std::filesystem::path& file) {
    auto settings = std::filesystem::exists(file) ? readSettings(file) : nlohmann::json::object();
    const auto parsed = parseSettings(settings); // Validate before replacing the original file.
    const auto& diagnostics = parsed.diagnostics;
    auto& keybinds = settings["keybinds"];
    for (const auto& binding : bindings) {
        if (!keybinds.contains(binding.name)) keybinds[binding.name] = binding.defaultValue;
    }
    auto& section = settings["diagnostics"];
    if (section.is_null()) section = nlohmann::json::object();
    auto& ui = settings["ui"];
    ui["alpha"] = diagnostics.uiAlpha;
    ui["scale"] = diagnostics.uiScale;
    settings.erase("ui_scale");
    section.erase("scale");
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
