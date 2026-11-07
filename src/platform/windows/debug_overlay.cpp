#include "platform/windows/debug_overlay.hpp"
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
struct PanelState { char text[8192]{}; HFONT font{}; int lineHeight = 18; };
LRESULT CALLBACK panelProc(HWND window, UINT message, WPARAM w, LPARAM l) {
    if (message == WM_NCHITTEST) return HTTRANSPARENT;
    if (message == WM_MOUSEACTIVATE) return MA_NOACTIVATE;
    if (message == WM_PAINT) {
        PAINTSTRUCT paint{}; HDC dc = BeginPaint(window, &paint);
        auto* panel = reinterpret_cast<PanelState*>(GetWindowLongPtrW(window, GWLP_USERDATA));
        RECT rect{}; GetClientRect(window, &rect);
        SetDCBrushColor(dc, RGB(20,24,30)); FillRect(dc, &rect, static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
        if (panel) {
            auto old = SelectObject(dc, panel->font);
            SetBkMode(dc, TRANSPARENT); SetTextColor(dc, RGB(240,245,250));
            rect.left += 12; rect.right -= 12; rect.top += 10; rect.bottom -= 10;
            const char* line = panel->text;
            while (*line && rect.top+panel->lineHeight <= rect.bottom) {
                const char* end = std::strchr(line, '\n');
                const auto length = end ? static_cast<int>(end-line) : static_cast<int>(std::strlen(line));
                RECT row{rect.left, rect.top, rect.right, rect.top+panel->lineHeight};
                DrawTextA(dc, line, length, &row, DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
                rect.top += panel->lineHeight;
                if (!end) break;
                line = end+1;
            }
            SelectObject(dc, old);
        }
        EndPaint(window, &paint); return 0;
    }
    return DefWindowProcW(window, message, w, l);
}
struct PanelWindow {
    HWND window{}; HFONT font{}; HINSTANCE instance{};
    ~PanelWindow() {
        if (window) { SetWindowLongPtrW(window, GWLP_USERDATA, 0); DestroyWindow(window); }
        if (font) DeleteObject(font);
        if (instance) UnregisterClassW(L"ConduitDebugOverlay", instance);
    }
};
}
void runDebugOverlay(const wchar_t* disablePath, DebugReloadStats& stats) {
    PanelWindow panel;
    panel.instance = GetModuleHandleW(nullptr);
    WNDCLASSW klass{}; klass.lpfnWndProc = panelProc; klass.hInstance = panel.instance;
    klass.lpszClassName = L"ConduitDebugOverlay";
    if (!RegisterClassW(&klass)) return;
    HWND game{};
    EnumWindows(findWindow, reinterpret_cast<LPARAM>(&game));
    if (!game) return;
    gameWindow.store(game);
    panel.window = CreateWindowExW(WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW,
        klass.lpszClassName, L"", WS_POPUP, 0,0,420,170, game, nullptr, panel.instance, nullptr);
    if (!panel.window) return;
    panel.font = CreateFontW(-16,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,DEFAULT_QUALITY,FIXED_PITCH,L"Consolas");
    PanelState data{}; data.font = panel.font;
    SetWindowLongPtrW(panel.window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(&data));
    SetLayeredWindowAttributes(panel.window, 0, 230, LWA_ALPHA);
    PresentHooks hooks;
    hooks.install(panel.window); // Failure leaves FPS explicitly unavailable.
    RisingEdge edge;
    edge.update((GetAsyncKeyState(VK_F3) & 0x8000) != 0);
    SetWindowTextW(panel.window, L"ConduitClient Debug");
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
            RECT bounds{}; GetClientRect(game, &bounds);
            POINT corners[2]{{bounds.left,bounds.top}, {bounds.right,bounds.bottom}};
            ClientToScreen(game, &corners[0]); ClientToScreen(game, &corners[1]);
            bounds = {corners[0].x,corners[0].y,corners[1].x,corners[1].y};
            MONITORINFO monitor{}; monitor.cbSize = sizeof(monitor);
            if (GetMonitorInfoW(MonitorFromWindow(game, MONITOR_DEFAULTTONEAREST), &monitor)) {
                RECT intersection{};
                if (!IntersectRect(&intersection, &bounds, &monitor.rcWork)) { ShowWindow(panel.window, SW_HIDE); refresh = now; continue; }
                bounds = intersection;
            }
            const int width = std::min(560, std::max(1, static_cast<int>(bounds.right-bounds.left)-24));
            const int availableHeight = std::max(1, static_cast<int>(bounds.bottom-bounds.top)-24);
            HDC dc = GetDC(panel.window);
            if (!dc) { ShowWindow(panel.window, SW_HIDE); refresh = now; continue; }
            const auto oldFont = SelectObject(dc, panel.font);
            TEXTMETRICA fontMetrics{}; GetTextMetricsA(dc, &fontMetrics);
            data.lineHeight = std::max(1, static_cast<int>(fontMetrics.tmHeight));
            char fps[40] = "Unavailable";
            {
                const std::lock_guard lock(timingMutex);
                if (const auto rate = presentationRate.fps(now))
                    std::snprintf(fps, sizeof(fps), "%.1f", *rate);
            }
            char latency[48] = "Unavailable";
            const auto micros = stats.latencyMicros.load();
            if (micros >= 0) std::snprintf(latency, sizeof(latency), "%.1f ms", micros/1000.0);
            const auto metrics = stats.screenMetrics ? stats.screenMetrics->diagnostics(now) : ScreenMetrics::Diagnostics{};
            const auto& screen = metrics.name;
            char elements[64] = "Unavailable";
            if (metrics.elements.status == ControlCountStatus::Complete) {
                std::snprintf(elements, sizeof(elements), "%u", metrics.elements.count);
            } else if (metrics.elements.status == ControlCountStatus::Incomplete) {
                std::snprintf(elements, sizeof(elements), ">= %u (incomplete)", metrics.elements.count);
            }
            char factories[32] = "Unavailable", generated[64] = "Unavailable";
            if (metrics.factories.available) {
                std::snprintf(factories, sizeof(factories), "%u", metrics.factories.instances);
                std::snprintf(generated, sizeof(generated), "%u (%u roots)",
                    metrics.factories.generatedControls, metrics.factories.liveRoots);
            }
            std::size_t used = 0; int lines = 0;
            const auto append = [&](const char* format, auto... values) {
                if (used >= sizeof(data.text)-1) return;
                const int count = std::snprintf(data.text+used, sizeof(data.text)-used, format, values...);
                if (count > 0) used += std::min(static_cast<std::size_t>(count), sizeof(data.text)-used-1);
                ++lines;
            };
            append("ConduitClient | Debug\n");
            append("Screen: %s\n", screen[0] ? screen.data() : "Unavailable");
            append("Elements: %s\n", elements);
            append("Factory instances: %s\n", factories);
            if (metrics.factories.available) {
                const auto detailCount = std::min<std::size_t>(metrics.factories.detailCount, metrics.factories.details.size());
                const int rowBudget = std::max(0, (availableHeight-20)/data.lineHeight-10);
                const bool needsNotice = detailCount > static_cast<std::size_t>(rowBudget) ||
                    metrics.factories.instances > detailCount || metrics.factories.detailsTruncated;
                const auto shown = std::min(detailCount, static_cast<std::size_t>(std::max(0, rowBudget-(needsNotice ? 1 : 0))));
                for (std::size_t index=0; index<shown; ++index) {
                    const auto& row = metrics.factories.details[index];
                    const bool named = row.nameAvailable && row.name[0];
                    char label[160]{};
                    if (named) std::snprintf(label, sizeof(label), "%s%s", row.name.data(), row.nameTruncated ? "..." : "");
                    else std::snprintf(label, sizeof(label), "%s #%016llx",
                        row.ownerAvailable && row.owner[0] ? row.owner.data() : "factory",
                        static_cast<unsigned long long>(row.instanceId));
                    char suffix[80]{};
                    std::snprintf(suffix, sizeof(suffix), ": %u roots, %u controls", row.liveRoots, row.controls);
                    SIZE suffixSize{}; GetTextExtentPoint32A(dc, suffix, static_cast<int>(std::strlen(suffix)), &suffixSize);
                    const int labelWidth = std::max(0, width-24-16-static_cast<int>(suffixSize.cx));
                    int fit = 0; SIZE extent{};
                    GetTextExtentExPointA(dc, label, static_cast<int>(std::strlen(label)), labelWidth, &fit, nullptr, &extent);
                    if (!named && fit < static_cast<int>(std::strlen(label))) {
                        std::snprintf(label, sizeof(label), "#%016llx", static_cast<unsigned long long>(row.instanceId));
                        GetTextExtentExPointA(dc, label, static_cast<int>(std::strlen(label)), labelWidth, &fit, nullptr, &extent);
                    }
                    if (fit < static_cast<int>(std::strlen(label))) {
                        fit = std::max(0, fit-3); label[fit] = 0;
                        if (labelWidth >= 3*fontMetrics.tmAveCharWidth) std::strcat(label, "...");
                    }
                    append("  %s%s\n", label, suffix);
                }
                if (shown < detailCount || metrics.factories.instances > shown || metrics.factories.detailsTruncated)
                    append("  ... %u more factory instances\n", metrics.factories.instances > shown ?
                        metrics.factories.instances-static_cast<unsigned>(shown) : static_cast<unsigned>(detailCount-shown));
            }
            append("Generated controls: %s\n", generated);
            append("FPS (DXGI presents): %s\n", fps);
            append("\n");
            append("Successful reloads: %llu\n", static_cast<unsigned long long>(stats.successes.load()));
            append("Reload latency*: %s\n", latency);
            append("*Backend dispatch + completion");
            SelectObject(dc, oldFont); ReleaseDC(panel.window, dc);
            SetWindowTextA(panel.window, data.text); // Accessible diagnostic text; no input focus.
            const int height = std::min(availableHeight, lines*data.lineHeight+20);
            const int left = std::max(static_cast<int>(bounds.left), static_cast<int>(bounds.right)-12-width);
            const int top = std::max(static_cast<int>(bounds.top), std::min(static_cast<int>(bounds.top)+12, static_cast<int>(bounds.bottom)-height));
            SetWindowPos(panel.window, HWND_TOP, left,top,width,height,SWP_NOACTIVATE | SWP_SHOWWINDOW);
            InvalidateRect(panel.window, nullptr, FALSE);
            refresh = now;
        }
        if (!visible || !focused) ShowWindow(panel.window, SW_HIDE);
        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&message); DispatchMessageW(&message);
        }
        Sleep(15);
    }
    gameWindow.store(nullptr);
}
}
