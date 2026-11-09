#pragma once
#include <filesystem>
namespace conduit::standalone {
// Materializes only the DLL-owned PNG; no game installation or pack is changed.
std::filesystem::path prepareTitleLogoFile(const std::filesystem::path& dataRoot);
// Conservative on-disk customization gate. Missing/invalid metadata rejects.
bool vanillaGlobalPacks(const std::filesystem::path& local, const std::filesystem::path& roaming) noexcept;
}
