#include "standalone/settings.hpp"
#include "core/logger.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <chrono>
namespace fs = std::filesystem;
void require(bool value) { if (!value) throw std::runtime_error("Settings assertion failed"); }
std::string read(const fs::path& file) { std::ifstream in(file); return {std::istreambuf_iterator<char>(in), {}}; }
int main() {
    const auto root = fs::temp_directory_path() / ("conduit-settings-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    try {
        fs::create_directories(root);
        const auto config = root / "config.json";
        std::ofstream(config) << R"({"enabled":false,"resourcePackPath":17,"reloadMode":null,"future":42})";
        conduit::standalone::migrateSettings(config);
        const auto migrated = read(config);
        require(migrated.find("\n  \"enabled\"") == std::string::npos && migrated.find("resourcePackPath") == std::string::npos && migrated.find("reloadMode") == std::string::npos);
        require(migrated.find("future") != std::string::npos);
        conduit::standalone::migrateSettings(config); require(read(config) == migrated);
        std::ofstream(config) << "{invalid";
        bool failed = false;
        try { conduit::standalone::migrateSettings(config); } catch (...) { failed = true; }
        require(failed && read(config) == "{invalid");
        fs::remove(config); conduit::standalone::migrateSettings(config); require(conduit::standalone::loadDiagnosticsSettings(config).uiScale == 1.0f);
        const auto defaults = conduit::standalone::loadDiagnosticsSettings(config);
        require(defaults.factoriesEnabled && defaults.emptyFactories && defaults.uiAlpha == 0.5f);
        std::ofstream(config) << R"({"diagnostics":{"scale":0.7,"factories":{"enabled":true,"empty":false},"future":9}})";
        conduit::standalone::migrateSettings(config);
        const auto custom = conduit::standalone::loadDiagnosticsSettings(config);
        require(custom.uiScale == 0.7f && !custom.emptyFactories);
        require(read(config).find("\"ui_scale\"") == std::string::npos && read(config).find("\"ui\"") != std::string::npos);
        require(!custom.showFactory(0,0) && custom.showFactory(1,0) && custom.showFactory(0,1));
        auto hidden = custom; hidden.factoriesEnabled = false; require(!hidden.showFactory(1,1));
        require(read(config).find("future") != std::string::npos);
        std::ofstream(config) << R"({"ui_scale":1.5,"diagnostics":{"scale":0.7}})";
        conduit::standalone::migrateSettings(config);
        require(conduit::standalone::loadDiagnosticsSettings(config).uiScale == 1.5f);
        std::ofstream(config) << R"({"ui":{"scale":2,"alpha":0,"future":13},"ui_scale":1.5,"diagnostics":{"scale":0.7}})";
        conduit::standalone::migrateSettings(config);
        const auto ui = conduit::standalone::loadSettings(config).diagnostics;
        require(ui.uiScale == 2.0f && ui.uiAlpha == 0.0f);
        require(read(config).find("\"future\": 13") != std::string::npos);
        for (const auto* invalid : {R"({"ui":null})", R"({"ui":{"alpha":-0.1}})", R"({"ui":{"alpha":1.1}})", R"({"ui":{"alpha":true}})", R"({"ui":{"scale":5}})", R"({"ui_scale":0})", R"({"ui_scale":true})", R"({"diagnostics":{"scale":0}})", R"({"diagnostics":{"factories":{"empty":1}}})", R"({"diagnostics":null})"}) {
            std::ofstream(config) << invalid; failed = false;
            try { conduit::standalone::migrateSettings(config); } catch (...) { failed = true; }
            require(failed && read(config) == invalid);
        }
        std::ofstream(config) << R"({"keybinds":{"reload":" shift + Control + f12 ","tooltip":"Alt+5","future":true},"launcher":{"version":"v0.6.1-beta","future":42}})";
        conduit::standalone::migrateSettings(config);
        const auto settings = conduit::standalone::loadSettings(config);
        require(settings.keybinds.diagnostics == conduit::KeyChord{0x72});
        require(settings.keybinds.eject == conduit::KeyChord{'K', true});
        require(settings.keybinds.reload == conduit::KeyChord{0x7b, true, true});
        require(settings.keybinds.tooltip == conduit::KeyChord{'5', false, false, true});
        require(migrated.find("\"launcher\"") == std::string::npos);
        require(read(config).find("\"version\": \"v0.6.1-beta\"") != std::string::npos);
        std::ofstream(config) << R"({"launcher":{"version":42,"future":17}})";
        require(conduit::standalone::loadSettings(config).keybinds.reload == conduit::KeyChord{'R', true});
        conduit::standalone::migrateSettings(config);
        require(read(config).find("\"version\": 42") != std::string::npos);
        require(read(config).find("\"future\": 17") != std::string::npos);
        for (const auto* invalid : {R"({"keybinds":{"reload":"F3"}})", R"({"keybinds":{"reload":"Ctrl++R"}})",
                                    R"({"keybinds":{"reload":"Ctrl+Ctrl+R"}})", R"({"keybinds":{"reload":"F25"}})"}) {
            std::ofstream(config) << invalid;
            failed = false;
            try { conduit::standalone::loadSettings(config); } catch (...) { failed = true; }
            require(failed && read(config) == invalid);
        }
        std::ofstream(root / "standalone.log") << "old log";
        { conduit::Logger logger(conduit::standalone::logDirectory(root) / "standalone.log"); logger.write("info", "new log"); }
        require(read(root / "standalone.log") == "old log"); require(read(root / "logs/standalone.log").find("new log") != std::string::npos);
        std::ofstream(root / "blocked") << "file";
        failed = false; try { conduit::Logger logger(root / "blocked/logs/test.log"); } catch (...) { failed = true; } require(failed);
        const auto local = root / "local", roaming = root / "roaming";
        const auto a = local / "Packages/Microsoft.MinecraftUWP_8wekyb3d8bbwe/LocalState/games/com.mojang/development_resource_packs/a";
        const auto b = roaming / "Minecraft Bedrock/Users/123/games/com.mojang/resource_packs/b";
        for (const auto& pack : {a,b}) { fs::create_directories(pack / "ui"); std::ofstream(pack / "manifest.json") << "{}"; }
        fs::create_directories(a.parent_path() / "incomplete/ui");
        const auto discovered = conduit::standalone::discoverPackRoots(local, roaming);
        require(discovered.size() == 2);
        require(conduit::standalone::discoverPackRoots(root / "missing", {}).empty());
        fs::remove_all(root);
        std::cout << "Settings migration, discovery, log preservation and filesystem failure checks passed\n";
    } catch (const std::exception& error) { std::cerr << error.what(); fs::remove_all(root); return 1; }
}
