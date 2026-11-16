#pragma once
#include "core/diagnostics_settings.hpp"
#include "core/keybinds.hpp"
#include <filesystem>
#include <vector>
namespace conduit::standalone {
struct Settings {
    DiagnosticsSettings diagnostics;
    Keybinds keybinds;
};

Settings loadSettings(const std::filesystem::path& file);
// Obsolete keys are ignored regardless of their former types, then removed atomically.
DiagnosticsSettings loadDiagnosticsSettings(const std::filesystem::path& file);
void migrateSettings(const std::filesystem::path& file);
std::filesystem::path logDirectory(const std::filesystem::path& dataRoot);
// Known profile layouts only. Discovers candidates, never claims they are mounted.
std::vector<std::filesystem::path> discoverPackRoots(const std::filesystem::path& localAppData,
                                                   const std::filesystem::path& roamingAppData);
}
