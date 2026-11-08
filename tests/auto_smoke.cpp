#include <windows.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>

namespace fs = std::filesystem;
using namespace std::chrono_literals;

namespace {
void require(bool value, const char* message) {
    if (!value) { throw std::runtime_error(message); }
}

template<class Predicate>
bool waitUntil(Predicate predicate) {
    const auto deadline = std::chrono::steady_clock::now() + 8s;
    while (std::chrono::steady_clock::now() < deadline) {
        if (predicate()) { return true; }
        std::this_thread::sleep_for(50ms);
    }
    return false;
}

std::string readFile(const fs::path& path) {
    std::ifstream input(path);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}
}

int main(int argc, char** argv) {
    if (argc != 2) { return 2; }
    HMODULE module = nullptr;
    HWND dialog = nullptr;
    try {
        wchar_t local[32768]{};
        require(GetEnvironmentVariableW(L"LOCALAPPDATA", local, 32768) > 0,
                "LOCALAPPDATA unavailable");
        const auto log = fs::path(local) / L"ConduitClient" / L"logs" / L"conduit.log";
        std::error_code ignored;
        fs::remove(log, ignored);
        const std::string bytes(argv[1]);
        const fs::path dll(std::u8string(bytes.begin(), bytes.end()));
        module = LoadLibraryW(dll.c_str());
        require(module != nullptr, "DLL load failed");
        require(waitUntil([&] { return readFile(log).find("Ctrl+R polling active") != std::string::npos; }),
                "Automatic initialization or hotkey monitoring did not start");

        const auto instance = GetModuleHandleW(nullptr);
        WNDCLASSW windowClass{};
        windowClass.lpfnWndProc = DefWindowProcW;
        windowClass.hInstance = instance;
        windowClass.lpszClassName = L"ConduitAutoSmokeWindow";
        require(RegisterClassW(&windowClass) != 0, "Window registration failed");
        const HWND window = CreateWindowW(windowClass.lpszClassName, L"Conduit Ctrl+R Test",
            WS_OVERLAPPEDWINDOW, 100, 100, 320, 200, nullptr, nullptr, instance, nullptr);
        require(window != nullptr, "Test window creation failed");
        ShowWindow(window, SW_SHOW);
        SetForegroundWindow(window);
        std::this_thread::sleep_for(200ms);
        INPUT debugKey{};
        debugKey.type = INPUT_KEYBOARD;
        debugKey.ki.wVk = VK_F3;
        require(SendInput(1, &debugKey, sizeof(INPUT)) == 1, "F3 key-down failed");
        require(waitUntil([&] { return readFile(log).find("F3 diagnostics shown") != std::string::npos; }),
                "F3 did not show diagnostics");
        debugKey.ki.dwFlags = KEYEVENTF_KEYUP;
        SendInput(1, &debugKey, sizeof(INPUT));
        std::this_thread::sleep_for(100ms);
        debugKey.ki.dwFlags = 0;
        SendInput(1, &debugKey, sizeof(INPUT));
        require(waitUntil([&] { return readFile(log).find("F3 diagnostics hidden") != std::string::npos; }),
                "F3 did not hide diagnostics");
        debugKey.ki.dwFlags = KEYEVENTF_KEYUP;
        SendInput(1, &debugKey, sizeof(INPUT));
        INPUT chord[2]{};
        chord[0].type = INPUT_KEYBOARD;
        chord[0].ki.wVk = VK_CONTROL;
        chord[1].type = INPUT_KEYBOARD;
        chord[1].ki.wVk = 'R';
        require(SendInput(2, chord, sizeof(INPUT)) == 2, "Ctrl+R key-down injection failed");
        dialog = nullptr;
        const bool visible = waitUntil([&] {
            dialog = FindWindowW(nullptr, L"Conduit Client");
            return dialog != nullptr;
        });
        chord[0].ki.dwFlags = KEYEVENTF_KEYUP;
        chord[1].ki.dwFlags = KEYEVENTF_KEYUP;
        SendInput(2, chord, sizeof(INPUT));
        require(visible, "Ctrl+R message box did not appear");
        require(readFile(log).find("Ctrl+R press detected") != std::string::npos,
                "Ctrl+R press was not logged");
        PostMessageW(dialog, WM_CLOSE, 0, 0);
        require(waitUntil([&] { return !IsWindow(dialog); }), "Message box did not close");
        chord[0] = {};
        chord[1] = {};
        chord[0].type = INPUT_KEYBOARD;
        chord[0].ki.wVk = VK_CONTROL;
        chord[1].type = INPUT_KEYBOARD;
        chord[1].ki.wVk = 'K';
        require(SendInput(2, chord, sizeof(INPUT)) == 2, "Ctrl+K injection failed");
        require(waitUntil([&] {
            return readFile(log).find("Ctrl+K detected") != std::string::npos;
        }), "Ctrl+K was not detected");
        chord[0].ki.dwFlags = KEYEVENTF_KEYUP;
        chord[1].ki.dwFlags = KEYEVENTF_KEYUP;
        SendInput(2, chord, sizeof(INPUT));
        const auto autoStop = reinterpret_cast<void (*)()>(GetProcAddress(module, "ConduitAutoStop"));
        require(autoStop != nullptr, "ConduitAutoStop export missing");
        autoStop();
        require(readFile(log).find("Automatic client shutdown complete") != std::string::npos,
                "Shutdown was not logged");
        INPUT down{};
        down.type = INPUT_KEYBOARD;
        down.ki.wVk = 'R';
        require(SendInput(1, &down, sizeof(down)) == 1, "Post-shutdown R key-down failed");
        std::this_thread::sleep_for(200ms);
        require(FindWindowW(nullptr, L"Conduit Client") == nullptr,
                "Ctrl+R notification appeared after Ctrl+K shutdown");
        require(FreeLibrary(module) != 0, "DLL unload failed");
        DestroyWindow(window);
        std::cout << "Automatic smoke passed: init, Ctrl+R popup, Ctrl+K logical shutdown\n";
        return 0;
    } catch (const std::exception& error) {
        if (dialog && IsWindow(dialog)) { PostMessageW(dialog, WM_CLOSE, 0, 0); }
        if (module) {
            if (const auto autoStop = reinterpret_cast<void (*)()>(GetProcAddress(module, "ConduitAutoStop"))) {
                autoStop();
            }
            FreeLibrary(module);
        }
        std::cerr << error.what() << '\n';
        return 1;
    }
}
