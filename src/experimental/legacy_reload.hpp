#pragma once
#include "core/reload_manager.hpp"
#include "core/screen_metrics.hpp"
#include "experimental/legacy_reload_completion.hpp"
#include <memory>
#include <filesystem>
#include <string>
namespace conduit::experimental {
std::unique_ptr<ReloadBackend> makeLegacyReloadBackend(const std::filesystem::path& diagnosticLog, bool automaticFrame = false);
bool armFromObservedThread(unsigned processId, unsigned threadId);
bool retireLegacyReloadHooks() noexcept;
void disableLegacyReload() noexcept;
std::string legacyReloadState();
// Resident copied diagnostics; no game pointer escapes the native callback.
ScreenMetrics& nativeScreenMetrics() noexcept;
bool isActiveTitleView(void* view, void* scene) noexcept;
}
