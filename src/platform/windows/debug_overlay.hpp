#pragma once
#include "core/reload_manager.hpp"
#include "core/screen_metrics.hpp"
#include "core/debug_panel.hpp"
#include <atomic>
#include <cstdint>
#include <memory>
#include <functional>
#include <string_view>

namespace conduit { class Logger; }
namespace conduit::windows {
// Written only after the real backend reports success; excludes file validation.
struct DebugReloadStats {
    std::string_view clientVersion = "Unavailable"; // Static selected profile metadata.
    DebugPanelMailbox panel; // Copied presentation state; native rendering reads without waiting.
    ScreenMetrics* screenMetrics = nullptr; // Immutable after start; resident backend owns it.
    Logger* diagnostics = nullptr; // Borrowed from the worker that runs this overlay.
    std::atomic<std::uint64_t> successes{0};
    std::atomic<std::int64_t> latencyMicros{-1};
};
std::unique_ptr<ReloadBackend> trackDebugReloads(std::unique_ptr<ReloadBackend> backend,
                                               DebugReloadStats& stats);
// Run on the standalone initialization worker, outside loader lock, after image verification.
// Returns when DISABLE appears. Never consumes input or accesses native UI objects.
void runDebugOverlay(const wchar_t* disablePath, DebugReloadStats& stats, const std::function<bool()>& stopRequested = {});
}
