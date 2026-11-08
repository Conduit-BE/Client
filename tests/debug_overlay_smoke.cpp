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
    stats.clientVersion = "test_build";
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
            screenMetrics.publish("test_screen", conduit::ScreenMetrics::Clock::now(), {conduit::ControlCountStatus::Complete, 42}, 0, factories, {conduit::ControlCountStatus::Complete, 17});
            chain->Present(0, 0);
            Sleep(10);
        } while (GetTickCount64() < end);
    };
    conduit::DebugPanelText panel;
    const auto readText = [&] { stats.panel.tryRead(panel); return std::string(panel.text.data()); };
    pump(500);
    bool ok = !panel.visible && !FindWindowW(L"ConduitDebugOverlay", nullptr);
    if (!ok) std::cerr << "Initial visibility failed\n";
    screenMetrics.publish("test_screen", conduit::ScreenMetrics::Clock::now(), {conduit::ControlCountStatus::Complete, 42}, 0, factories, {conduit::ControlCountStatus::Complete, 17});
    SetForegroundWindow(window);
    ok = key(true) && ok; pump(1000); readText();
    if (!panel.visible || GetForegroundWindow() != window) std::cerr << "Open/focus failed\n";
    ok = panel.visible && GetForegroundWindow() == window && ok;
    std::string text = readText();
    std::cout << "Panel text: " << text << "\nPresent result: " << std::hex << chain->Present(0,0) << std::dec << "\n";
    ok = std::string(text).find("general:\n") != std::string::npos && std::string(text).find("conduit_version: " CONDUIT_DISPLAY_VERSION) != std::string::npos && std::string(text).find("client_version: test_build") != std::string::npos && std::string(text).find("reloads:") == std::string::npos && std::string(text).find("elements: 42\n  bindings: 17\n  factories:") != std::string::npos && std::string(text).find("type: \"test_screen\"") != std::string::npos && std::string(text).find("frames_per_second: Unavailable") == std::string::npos &&
        std::string(text).find("frames_per_second: ") != std::string::npos && ok;
    ok = std::string(text).find("test_factory:\n      children: 7\n      elements: 28") != std::string::npos &&
        std::string(text).find("popup #0000000000000002:\n      children: 0\n      elements: 0") != std::string::npos &&
        std::string(text).find("popup #0000000000000003:\n      children: 0\n      elements: 0") != std::string::npos && ok;
    pump(500); readText(); ok = panel.visible && ok; // Held F3 must not toggle.
    const auto originalFactories = factories;
    factories.details[0].liveRoots = 2; factories.details[0].controls = 9;
    pump(350); text = readText();
    if (std::string(text).find("test_factory:\n      children: 2\n      elements: 9") == std::string::npos) std::cerr << "Factory row update failed\n" << text << '\n';
    ok = std::string(text).find("test_factory:\n      children: 2\n      elements: 9") != std::string::npos && ok;
    factories.instances = 32; factories.detailCount = 32;
    for (auto& row : factories.details) {
        row.nameAvailable = true; row.name.fill('x'); row.name.back() = 0;
    }
    SetWindowPos(window, nullptr, 40,40,360,360,SWP_NOACTIVATE | SWP_NOZORDER);
    SetForegroundWindow(window);
    pump(500); text = readText();
    // Pixel/layout bounds are exercised separately in debug_panel tests with logical dimensions.
    ok = text.find("      elements: 9") != std::string::npos && ok;
    factories = {}; pump(350); text = readText();
    if (std::string(text).find("factories: Unavailable") == std::string::npos || std::string(text).find("children: ") != std::string::npos)
        std::cerr << "Factory invalidation failed\n" << text << '\n';
    ok = std::string(text).find("factories: Unavailable") != std::string::npos &&
        std::string(text).find("children: ") == std::string::npos && ok;
    factories = originalFactories;
    SetWindowPos(window, nullptr, 40,40,900,600,SWP_NOACTIVATE | SWP_NOZORDER);
    SetForegroundWindow(window);
    pump(350); text = readText();
    if (std::string(text).find("test_factory:\n      children: 7\n      elements: 28") == std::string::npos) std::cerr << "Factory restore failed\n" << text << '\n';
    ok = std::string(text).find("test_factory:\n      children: 7\n      elements: 28") != std::string::npos && ok;
    key(false); pump(100); key(true); pump(350);
    readText(); ok = !panel.visible && ok;
    key(false);
    std::ofstream(stopFile).put('\n');
    pump(100);
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
