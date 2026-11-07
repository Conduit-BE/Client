#include "platform/windows/debug_overlay.hpp"
#include "platform/windows/native_debug_renderer.hpp"
#include "core/hotkey.hpp"
#include "core/presentation_rate.hpp"
#include "core/logger.hpp"
#include <windows.h>
#include <dxgi1_2.h>
#include <d3d11.h>
#include <wrl/client.h>
#include <MinHook.h>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <mutex>
#include <iomanip>
#include <sstream>
#include <algorithm>
#include <cstring>

namespace conduit::windows {
namespace {
using Clock = std::chrono::steady_clock;
class TrackedBackend final : public ReloadBackend {
public:
    TrackedBackend(std::unique_ptr<ReloadBackend> backend, DebugReloadStats& stats)
        : backend_(std::move(backend)), stats_(stats) {}
    ReloadResult reload(const ReloadRequest& request) override {
        const auto start = Clock::now();
        const auto result = backend_->reload(request);
        if (result == ReloadResult::Succeeded) {
            stats_.latencyMicros.store(std::chrono::duration_cast<std::chrono::microseconds>(Clock::now()-start).count());
            stats_.successes.fetch_add(1);
        }
        return result;
    }
private:
    std::unique_ptr<ReloadBackend> backend_;
    DebugReloadStats& stats_;
};
using Present = HRESULT (STDMETHODCALLTYPE*)(IDXGISwapChain*, UINT, UINT);
using Present1 = HRESULT (STDMETHODCALLTYPE*)(IDXGISwapChain1*, UINT, UINT, const DXGI_PRESENT_PARAMETERS*);
Present originalPresent{};
Present1 originalPresent1{};
std::atomic<bool> measuring{false};
std::atomic<HWND> gameWindow{nullptr};
std::mutex timingMutex;
PresentationRate presentationRate;
// Used only as an identity token, never dereferenced outside a live Present.
IDXGISwapChain* selectedChain{};
thread_local bool presenting = false;
void resetTiming() {
    presentationRate.reset(); selectedChain = nullptr;
}
void observe(IDXGISwapChain* chain, HRESULT result, UINT flags) noexcept {
    if (!measuring.load() || (flags & DXGI_PRESENT_TEST)) return;
    DXGI_SWAP_CHAIN_DESC desc{};
    if (FAILED(chain->GetDesc(&desc)) || desc.OutputWindow != gameWindow.load()) return;
    const std::lock_guard lock(timingMutex);
    if (result != S_OK || IsIconic(desc.OutputWindow) || GetForegroundWindow() != desc.OutputWindow) {
        resetTiming(); return;
    }
    const auto now = Clock::now();
    if (selectedChain != chain) { resetTiming(); selectedChain = chain; }
    presentationRate.observe(now);
}
HRESULT STDMETHODCALLTYPE onPresent(IDXGISwapChain* chain, UINT sync, UINT flags) {
    const bool nested = presenting; presenting = true;
    const auto result = originalPresent(chain, sync, flags);
    presenting = nested;
    if (!nested) observe(chain, result, flags);
    return result;
}
HRESULT STDMETHODCALLTYPE onPresent1(IDXGISwapChain1* chain, UINT sync, UINT flags, const DXGI_PRESENT_PARAMETERS* p) {
    const bool nested = presenting; presenting = true;
    const auto result = originalPresent1(chain, sync, flags, p);
    presenting = nested;
    if (!nested) observe(chain, result, flags);
    return result;
}
// Query DXGI implementations through a temporary D3D11 chain. No game resources or queues are used.
struct PresentHooks {
    void* present{}; void* present1{};
    bool install(HWND probe) {
        using Microsoft::WRL::ComPtr;
        ComPtr<ID3D11Device> device;
        ComPtr<ID3D11DeviceContext> context;
        ComPtr<IDXGISwapChain> chain;
        DXGI_SWAP_CHAIN_DESC desc{};
        desc.BufferDesc.Width = desc.BufferDesc.Height = 8;
        desc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.SampleDesc.Count = 1; desc.BufferCount = 2;
        desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        desc.OutputWindow = probe; desc.Windowed = TRUE;
        desc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
        if (FAILED(D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
            nullptr, 0, D3D11_SDK_VERSION, &desc, &chain, &device, nullptr, &context))) return false;
        ComPtr<IDXGISwapChain1> chain1;
        if (FAILED(chain.As(&chain1))) return false;
        auto** table = *reinterpret_cast<void***>(chain.Get());
        auto** table1 = *reinterpret_cast<void***>(chain1.Get());
        // MinHook is already owned/initialized by the verified native backend.
        if (MH_CreateHook(table[8], reinterpret_cast<void*>(&onPresent),
                          reinterpret_cast<void**>(&originalPresent)) != MH_OK) return false;
        present = table[8];
        if (MH_CreateHook(table1[22], reinterpret_cast<void*>(&onPresent1),
                          reinterpret_cast<void**>(&originalPresent1)) != MH_OK) return false;
        present1 = table1[22];
        if (MH_EnableHook(present) != MH_OK || MH_EnableHook(present1) != MH_OK) return false;
        measuring.store(true); return true;
    }
    ~PresentHooks() {
        measuring.store(false);
        // DLL is pinned. Trampolines remain allocated so an in-flight original call is safe.
        if (present) MH_DisableHook(present);
        if (present1) MH_DisableHook(present1);
    }
};
BOOL CALLBACK findWindow(HWND window, LPARAM out) {
    DWORD pid{}; GetWindowThreadProcessId(window, &pid);
    if (pid == GetCurrentProcessId() && IsWindowVisible(window) && !GetWindow(window, GW_OWNER)) {
        *reinterpret_cast<HWND*>(out) = window; return FALSE;
    }
    return TRUE;
}
}
std::unique_ptr<ReloadBackend> trackDebugReloads(std::unique_ptr<ReloadBackend> backend, DebugReloadStats& stats) {
    return std::make_unique<TrackedBackend>(std::move(backend), stats);
}
}

namespace conduit::windows {
namespace {
}
void runDebugOverlay(const wchar_t* disablePath, DebugReloadStats& stats) {
    HWND game{};
    EnumWindows(findWindow, reinterpret_cast<LPARAM>(&game));
    if (!game) return;
    gameWindow.store(game);
    char text[8192]{};
    PresentHooks hooks;
    hooks.install(game); // Probe uses the existing HWND; it creates no desktop window.
    NativeDebugRenderer renderer;
    const bool nativeReady = renderer.install(stats.panel);
    if (stats.diagnostics) stats.diagnostics->write(nativeReady ? "info" : "warning", nativeReady ?
        "F3 rendering: Minecraft native UI font and filled rectangle; logical viewport top-left" :
        "F3 native renderer unavailable; no OS overlay fallback installed");
    RisingEdge edge;
    edge.update((GetAsyncKeyState(VK_F3) & 0x8000) != 0);
    bool visible = false;
    auto refresh = Clock::time_point{};
    auto logRefresh = Clock::time_point{};
    ScreenMetrics::Name loggedScreen{};
    FactoryStatistics loggedFactories{};
    std::uint64_t loggedReloads = 0, snapshotSequence = 0;
    while (!std::filesystem::exists(disablePath) && IsWindow(game)) {
        const bool focused = GetForegroundWindow() == game && !IsIconic(game);
        const bool pressed = edge.update((GetAsyncKeyState(VK_F3) & 0x8000) != 0);
        if (pressed && focused) visible = !visible;
        const auto now = Clock::now();
        // Format/write copied diagnostics on this worker, never on the UI or Present thread.
        // Coalesce changes to at most one block / 2 seconds, only while F3 is visible.
        if (visible && focused && stats.diagnostics && stats.screenMetrics &&
            now-logRefresh >= std::chrono::seconds(2)) {
            logRefresh = now;
            const auto snapshot = stats.screenMetrics->diagnostics(now);
            const auto reloads = stats.successes.load();
            if (snapshot.name != loggedScreen || snapshot.factories != loggedFactories || reloads != loggedReloads) {
                loggedScreen = snapshot.name; loggedFactories = snapshot.factories; loggedReloads = reloads;
                ++snapshotSequence;
                std::ostringstream summary;
                summary << "factory_breakdown snapshot=" << snapshotSequence << " screen=" << std::quoted(snapshot.name.data())
                    << " available=" << snapshot.factories.available << " instances=" << snapshot.factories.instances
                    << " roots=" << snapshot.factories.liveRoots << " controls_including_roots=" << snapshot.factories.generatedControls
                    << " rows=" << snapshot.factories.detailCount << " truncated=" << snapshot.factories.detailsTruncated
                    << " successful_reloads=" << reloads;
                stats.diagnostics->write("debug", summary.str());
                if (snapshot.factories.available) for (std::size_t i=0; i<snapshot.factories.detailCount; ++i) {
                    const auto& row = snapshot.factories.details[i];
                    std::ostringstream text;
                    text << "factory_instance snapshot=" << snapshotSequence << " screen=" << std::quoted(snapshot.name.data())
                        << " id=position-" << std::hex << row.instanceId << std::dec
                        << " name=" << std::quoted(row.nameAvailable ? row.name.data() : "Unavailable")
                        << " owner=" << std::quoted(row.ownerAvailable ? row.owner.data() : "Unavailable")
                        << " instances=" << row.instances << " roots=" << row.liveRoots
                        << " controls_including_roots=" << row.controls << " descendants_excluding_roots=" << row.descendants
                        << " name_available=" << row.nameAvailable << " owner_available=" << row.ownerAvailable
                        << " name_truncated=" << row.nameTruncated << " owner_truncated=" << row.ownerTruncated;
                    stats.diagnostics->write("debug", text.str());
                }
            }
        }
        if (visible && focused && now - refresh >= std::chrono::milliseconds(250)) {
            char fps[40] = "Unavailable";
            {
                const std::lock_guard lock(timingMutex);
                if (const auto rate = presentationRate.fps(now))
                    std::snprintf(fps, sizeof(fps), "%.1f", *rate);
            }
            const auto metrics = stats.screenMetrics ? stats.screenMetrics->diagnostics(now) : ScreenMetrics::Diagnostics{};
            const auto& screen = metrics.name;
            char elements[64] = "Unavailable";
            if (metrics.elements.status == ControlCountStatus::Complete) {
                std::snprintf(elements, sizeof(elements), "%u", metrics.elements.count);
            } else if (metrics.elements.status == ControlCountStatus::Incomplete) {
                std::snprintf(elements, sizeof(elements), ">= %u (incomplete)", metrics.elements.count);
            }
            char bindings[64] = "Unavailable";
            if (metrics.bindings.status == ControlCountStatus::Complete)
                std::snprintf(bindings, sizeof(bindings), "%u", metrics.bindings.count);
            else if (metrics.bindings.status == ControlCountStatus::Incomplete)
                std::snprintf(bindings, sizeof(bindings), ">= %u (incomplete)", metrics.bindings.count);
            std::size_t used = 0;
            const auto append = [&](const char* format, auto... values) {
                if (used >= sizeof(text)-1) return;
                const int count = std::snprintf(text+used, sizeof(text)-used, format, values...);
                if (count > 0) used += std::min(static_cast<std::size_t>(count), sizeof(text)-used-1);
            };
            append("\xc2\xa7" "b\xc2\xa7" "lConduit Client\xc2\xa7" "r\n\n");
            append("current screen:\n");
            append("  type: \"%s\"\n", screen[0] ? screen.data() : "Unavailable");
            append("  elements: %s\n", elements);
            append("  bindings: %s\n", bindings);
            append("  factories:%s\n", metrics.factories.available ? "" : " Unavailable");
            if (metrics.factories.available) {
                const auto shown = std::min<std::size_t>(metrics.factories.detailCount, metrics.factories.details.size());
                for (std::size_t index=0; index<shown; ++index) {
                    const auto& row = metrics.factories.details[index];
                    const bool named = row.nameAvailable && row.name[0];
                    char label[160]{};
                    if (named) std::snprintf(label, sizeof(label), "%s%s", row.name.data(), row.nameTruncated ? "..." : "");
                    else std::snprintf(label, sizeof(label), "%s #%016llx",
                        row.ownerAvailable && row.owner[0] ? row.owner.data() : "factory",
                        static_cast<unsigned long long>(row.instanceId));
                    append("    %s:\n", label);
                    append("      children: %u\n", row.liveRoots);
                    append("      elements: %u\n", row.controls);
                }
                if (metrics.factories.instances > shown || metrics.factories.detailsTruncated)
                    append("    ... more factory instances; see standalone.log\n");
            }
            append("\ngeneral:\n");
            append("  frames_per_second: %s\n", fps);
            append("  conduit_version: %s\n", CONDUIT_DISPLAY_VERSION);
            // Standalone initialization verifies this exact client image before enabling F3.
            append("  client_version: 1.26.60.5");
            stats.panel.publish(text, true);
            refresh = now;
        }
        if (!visible || !focused) stats.panel.publish(text, false);
        Sleep(15);
    }
    stats.panel.publish(text, false);
    gameWindow.store(nullptr);
}
}
