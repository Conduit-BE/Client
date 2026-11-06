#include "platform/windows/debug_overlay.hpp"
#include <windows.h>
#include <d3d11.h>
#include <wrl/client.h>
#include <MinHook.h>
#include <string>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <thread>

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
            screenMetrics.publish("test_screen", conduit::ScreenMetrics::Clock::now(), {conduit::ControlCountStatus::Complete, 42});
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
    screenMetrics.publish("test_screen", conduit::ScreenMetrics::Clock::now(), {conduit::ControlCountStatus::Complete, 42});
    ok = key(true) && ok; pump(1000);
    if (!overlay || !IsWindowVisible(overlay) || GetForegroundWindow() != window) std::cerr << "Open/focus failed\n";
    ok = overlay && IsWindowVisible(overlay) && GetForegroundWindow() == window && ok;
    char text[512]{}; GetWindowTextA(overlay, text, sizeof(text));
    std::cout << "Panel text: " << text << "\nPresent result: " << std::hex << chain->Present(0,0) << std::dec << "\n";
    ok = std::string(text).find("Elements: 42") != std::string::npos && std::string(text).find("Screen: test_screen") != std::string::npos && std::string(text).find("FPS (DXGI presents): Unavailable") == std::string::npos &&
        std::string(text).find("FPS (DXGI presents): ") != std::string::npos && ok;
    pump(500); ok = IsWindowVisible(overlay) && ok; // Holding F3 must not toggle again.
    key(false); pump(100); key(true); pump(350);
    ok = !IsWindowVisible(overlay) && ok;
    key(false);
    std::ofstream(stopFile).put('\n');
    while (IsWindow(overlay)) pump(20);
    worker.join();
    MH_Uninitialize();
    std::filesystem::remove(stopFile);
    DestroyWindow(window); UnregisterClassW(klass.lpszClassName, klass.hInstance);
    std::cout << (ok ? "F3 visibility, held key, focus, DXGI FPS and backend statistics passed\n" : "Overlay smoke failed\n");
    return ok ? 0 : 4;
}
