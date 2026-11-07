#include "platform/windows/debug_overlay.hpp"
#include "core/logger.hpp"
#include <windows.h>
#include <d3d11.h>
#include <wrl/client.h>
#include <MinHook.h>
#include <string>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <thread>
#include <algorithm>
#include <iterator>

namespace {
struct Backend final : conduit::ReloadBackend {
    conduit::ReloadResult result = conduit::ReloadResult::Succeeded;
    conduit::ReloadResult reload(const conduit::ReloadRequest&) override { return result; }
};
bool key(bool down) {
    INPUT input{}; input.type = INPUT_KEYBOARD; input.ki.wVk = VK_F3;
    input.ki.dwFlags = down ? 0 : KEYEVENTF_KEYUP;
    return SendInput(1, &input, sizeof(input)) == 1;
}
}
int main() {
    conduit::windows::DebugReloadStats stats;
    conduit::ScreenMetrics screenMetrics;
    stats.screenMetrics = &screenMetrics;
    auto backend = std::make_unique<Backend>(); auto* raw = backend.get();
    auto tracked = conduit::windows::trackDebugReloads(std::move(backend), stats);
    if (tracked->reload({}) != conduit::ReloadResult::Succeeded || stats.successes != 1 || stats.latencyMicros < 0) return 1;
    const auto latency = stats.latencyMicros.load();
    raw->result = conduit::ReloadResult::Failed; tracked->reload({});
    raw->result = conduit::ReloadResult::Unsupported; tracked->reload({});
    if (stats.successes != 1 || stats.latencyMicros != latency) return 2;
    const auto stopFile = std::filesystem::temp_directory_path() /
        (L"conduit-overlay-smoke-" + std::to_wstring(GetCurrentProcessId()));
    std::filesystem::remove(stopFile);
    auto logFile = stopFile; logFile += L".log";
    auto logger = std::make_unique<conduit::Logger>(logFile);
    stats.diagnostics = logger.get();
    conduit::FactoryStatistics factories{true, 3, 7, 28};
    factories.detailCount = 3;
    for (unsigned i=0; i<3; ++i) {
        factories.details[i].instanceId = i+1;
        factories.details[i].nameAvailable = true;
    }
    constexpr char ownerName[] = "popup";
    for (unsigned i=1; i<3; ++i) {
        factories.details[i].ownerAvailable = true;
        std::copy(std::begin(ownerName), std::end(ownerName), factories.details[i].owner.begin());
    }
    constexpr char testFactory[] = "test_factory";
    std::copy(std::begin(testFactory), std::end(testFactory), factories.details[0].name.begin());
    factories.details[0].liveRoots=7; factories.details[0].controls=28; factories.details[0].descendants=21;
    WNDCLASSW klass{}; klass.lpfnWndProc = DefWindowProcW;
    klass.hInstance = GetModuleHandleW(nullptr); klass.lpszClassName = L"ConduitOverlaySmoke";
    RegisterClassW(&klass);
    auto window = CreateWindowW(klass.lpszClassName, L"Conduit overlay smoke", WS_OVERLAPPEDWINDOW | WS_VISIBLE,
        40,40,900,600,nullptr,nullptr,klass.hInstance,nullptr);
    if (!window) return 3;
    SetForegroundWindow(window);
    Microsoft::WRL::ComPtr<ID3D11Device> device;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> context;
    Microsoft::WRL::ComPtr<IDXGISwapChain> chain;
    DXGI_SWAP_CHAIN_DESC desc{};
    desc.BufferDesc.Width = 900; desc.BufferDesc.Height = 600;
    desc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1; desc.BufferCount = 2;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.OutputWindow = window; desc.Windowed = TRUE;
    if (FAILED(D3D11CreateDeviceAndSwapChain(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,0,
        nullptr,0,D3D11_SDK_VERSION,&desc,&chain,&device,nullptr,&context))) return 5;
    if (MH_Initialize() != MH_OK) return 6;
    std::jthread worker([&] { conduit::windows::runDebugOverlay(stopFile.c_str(), stats); });
    auto pump = [&](unsigned ms) {
        const auto end = GetTickCount64() + ms;
        do {
            MSG message{};
            while (PeekMessageW(&message,nullptr,0,0,PM_REMOVE)) { TranslateMessage(&message); DispatchMessageW(&message); }
            screenMetrics.publish("test_screen", conduit::ScreenMetrics::Clock::now(), {conduit::ControlCountStatus::Complete, 42}, 0, factories);
            chain->Present(0, 0);
            Sleep(10);
        } while (GetTickCount64() < end);
    };
    HWND overlay{};
    const auto deadline = GetTickCount64() + 8000;
    do {
        pump(50);
        overlay = FindWindowW(L"ConduitDebugOverlay", L"ConduitClient Debug");
    } while (!overlay && GetTickCount64() < deadline);
    bool ok = overlay && !IsWindowVisible(overlay);
    if (!ok) std::cerr << "Initial visibility failed\n";
    screenMetrics.publish("test_screen", conduit::ScreenMetrics::Clock::now(), {conduit::ControlCountStatus::Complete, 42}, 0, factories);
    SetForegroundWindow(window);
    ok = key(true) && ok; pump(1000);
    if (!overlay || !IsWindowVisible(overlay) || GetForegroundWindow() != window) std::cerr << "Open/focus failed\n";
    ok = overlay && IsWindowVisible(overlay) && GetForegroundWindow() == window && ok;
    char text[8192]{}; GetWindowTextA(overlay, text, sizeof(text));
    std::cout << "Panel text: " << text << "\nPresent result: " << std::hex << chain->Present(0,0) << std::dec << "\n";
    ok = std::string(text).find("Factory instances: 3") != std::string::npos && std::string(text).find("Generated controls: 28 (7 roots)") != std::string::npos && std::string(text).find("Elements: 42") != std::string::npos && std::string(text).find("Screen: test_screen") != std::string::npos && std::string(text).find("FPS (DXGI presents): Unavailable") == std::string::npos &&
        std::string(text).find("FPS (DXGI presents): ") != std::string::npos && ok;
    ok = std::string(text).find("test_factory: 7 roots, 28 controls") != std::string::npos &&
        std::string(text).find("popup #0000000000000002: 0 roots, 0 controls") != std::string::npos &&
        std::string(text).find("popup #0000000000000003: 0 roots, 0 controls") != std::string::npos && ok;
    RECT normalPanel{}; GetWindowRect(overlay, &normalPanel);
    if (normalPanel.bottom-normalPanel.top <= 210) std::cerr << "Factory panel growth failed: " << normalPanel.bottom-normalPanel.top << '\n';
    ok = normalPanel.bottom-normalPanel.top > 210 && ok;
    pump(500); ok = IsWindowVisible(overlay) && ok; // Holding F3 must not toggle again.
    const auto originalFactories = factories;
    factories.details[0].liveRoots = 2; factories.details[0].controls = 9;
    pump(350); GetWindowTextA(overlay, text, sizeof(text));
    if (std::string(text).find("test_factory: 2 roots, 9 controls") == std::string::npos) std::cerr << "Factory row update failed\n" << text << '\n';
    ok = std::string(text).find("test_factory: 2 roots, 9 controls") != std::string::npos && ok;
    factories.instances = 32; factories.detailCount = 32;
    for (auto& row : factories.details) {
        row.nameAvailable = true; row.name.fill('x'); row.name.back() = 0;
    }
    SetWindowPos(window, nullptr, 40,40,360,360,SWP_NOACTIVATE | SWP_NOZORDER);
    SetForegroundWindow(window);
    pump(500); GetWindowTextA(overlay, text, sizeof(text));
    RECT smallPanel{}, client{}; GetWindowRect(overlay, &smallPanel); GetClientRect(window, &client);
    POINT first{client.left,client.top}, last{client.right,client.bottom};
    ClientToScreen(window, &first); ClientToScreen(window, &last);
    if (!(smallPanel.left >= first.x && smallPanel.right <= last.x && smallPanel.top >= first.y && smallPanel.bottom <= last.y))
        std::cerr << "Factory bounds failed: " << smallPanel.left << ',' << smallPanel.top << ',' << smallPanel.right << ',' << smallPanel.bottom
            << " client=" << first.x << ',' << first.y << ',' << last.x << ',' << last.y << '\n';
    // A tiling WM may ignore SetWindowPos. Demand omission only when the actual
    // viewport is small; the isolated-display run exercises that case directly.
    const bool constrained = last.y-first.y < 600;
    if (constrained && std::string(text).find("more factory instances") == std::string::npos) std::cerr << "Factory omission notice failed\n" << text << '\n';
    ok = smallPanel.left >= first.x && smallPanel.right <= last.x &&
        smallPanel.top >= first.y && smallPanel.bottom <= last.y &&
        (!constrained || std::string(text).find("more factory instances") != std::string::npos) &&
        std::string(text).find("...: 2 roots, 9 controls") != std::string::npos && ok;
    factories = {}; pump(350); GetWindowTextA(overlay, text, sizeof(text));
    if (std::string(text).find("Factory instances: Unavailable") == std::string::npos || std::string(text).find("roots, ") != std::string::npos)
        std::cerr << "Factory invalidation failed\n" << text << '\n';
    ok = std::string(text).find("Factory instances: Unavailable") != std::string::npos &&
        std::string(text).find("roots, ") == std::string::npos && ok;
    factories = originalFactories;
    SetWindowPos(window, nullptr, 40,40,900,600,SWP_NOACTIVATE | SWP_NOZORDER);
    SetForegroundWindow(window);
    pump(350); GetWindowTextA(overlay, text, sizeof(text));
    if (std::string(text).find("test_factory: 7 roots, 28 controls") == std::string::npos) std::cerr << "Factory restore failed\n" << text << '\n';
    ok = std::string(text).find("test_factory: 7 roots, 28 controls") != std::string::npos && ok;
    key(false); pump(100); key(true); pump(350);
    ok = !IsWindowVisible(overlay) && ok;
    key(false);
    std::ofstream(stopFile).put('\n');
    while (IsWindow(overlay)) pump(20);
    worker.join();
    logger.reset();
    std::ifstream log(logFile);
    const std::string logged{std::istreambuf_iterator<char>(log), std::istreambuf_iterator<char>()};
    ok = logged.find("factory_breakdown snapshot=") != std::string::npos &&
        logged.find("name=\"test_factory\"") != std::string::npos &&
        logged.find("name=\"\"") != std::string::npos &&
        logged.find("descendants_excluding_roots=21") != std::string::npos && ok;
    log.close(); std::filesystem::remove(logFile);
    MH_Uninitialize();
    std::filesystem::remove(stopFile);
    DestroyWindow(window); UnregisterClassW(klass.lpszClassName, klass.hInstance);
    std::cout << (ok ? "F3 visibility, held key, focus, DXGI FPS and backend statistics passed\n" : "Overlay smoke failed\n");
    return ok ? 0 : 4;
}
