#pragma once
#include "core/reload_manager.hpp"
#include <memory>
#include <filesystem>
#include <string>
namespace conduit::experimental {
std::unique_ptr<ReloadBackend> makeLegacyReloadBackend(const std::filesystem::path& diagnosticLog);
bool setResourceFallback(unsigned observedFlag);
bool armFromObservedThread(unsigned processId, unsigned threadId);
bool retireLegacyReloadHooks() noexcept;
void disableLegacyReload() noexcept;
std::string legacyReloadState();
}
