#include "platform/windows/auto_start.hpp"

#include "client.hpp"
#include "core/logger.hpp"
#include "core/notifications.hpp"
#include "core/hotkey.hpp"
#include "platform/windows/overlay_hooks.hpp"

#include <filesystem>
#include <stdexcept>
#include <string>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <atomic>

namespace conduit::windows {
namespace {
HANDLE stopEvent = nullptr;
HANDLE worker = nullptr;
std::mutex popupMutex;
std::condition_variable popupWake;
bool popupPending = false;
bool popupActive = false;
std::atomic<bool> popupStopping{false};

void queueReloadPopup() {
    std::lock_guard lock(popupMutex);
    if (popupPending || popupActive || popupStopping) { return; }
    popupPending = true;
    popupWake.notify_one();
}

void runPopupWorker() {
    std::unique_lock lock(popupMutex);
    for (;;) {
        popupWake.wait(lock, [] { return popupPending || popupStopping; });
        if (popupStopping) { return; }
        popupPending = false;
        popupActive = true;
        lock.unlock();
        MessageBoxW(nullptr, L"Ctrl+R detected! JSON UI reload requested.", L"Conduit Client",
                    MB_OK | MB_ICONINFORMATION | MB_TOPMOST | MB_SETFOREGROUND);
        lock.lock();
        popupActive = false;
    }
}

std::string utf8(const std::filesystem::path& path) {
    const auto bytes = path.u8string();
    return {bytes.begin(), bytes.end()};
}

DWORD WINAPI runAuto(LPVOID argument) {
    (void)argument;
    HMODULE ownedModule = nullptr;
    // Hold a reference while this thread and its modal notification use DLL code.
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
        reinterpret_cast<LPCWSTR>(&runAuto), &ownedModule);
    try {
        wchar_t appData[32768]{};
        const DWORD length = GetEnvironmentVariableW(L"LOCALAPPDATA", appData, 32768);
        if (length == 0 || length >= 32768) {
            throw std::runtime_error("LOCALAPPDATA is unavailable");
        }
        const auto root = std::filesystem::path(appData) / L"ConduitClient";
        const auto ui = root / L"ui";
        const auto logPath = root / L"logs" / L"conduit.log";
        std::filesystem::create_directories(ui);
        Logger logger(logPath);
        logger.write("info", "DLL attached to Minecraft.Windows.exe; automatic initialization beginning");
        const auto watch = utf8(ui);
        const auto log = utf8(logPath);
        ConduitConfig config{sizeof(config), CONDUIT_API_VERSION, watch.c_str(), log.c_str(),
                             CONDUIT_HOTKEY_DEFAULT, 30, 150};
        NotificationCenter notifications;
        {
            std::lock_guard lock(popupMutex);
            popupStopping.store(false);
        }
        std::thread popupThread(runPopupWorker);
        notifications.push("Conduit Client", "Successfully injected!",
                           std::chrono::steady_clock::now());
        Client client(config, [&notifications] {
            notifications.push("Conduit Client", "Ctrl+R detected, but JSON UI reloading is not implemented yet.",
                               std::chrono::steady_clock::now());
            queueReloadPopup();
        });
        const auto status = client.start();
        if (status != CONDUIT_OK) {
            logger.write("error", "Automatic initialization failed; status=" +
                                  std::to_string(static_cast<int>(status)));
        } else {
            const bool overlayInstalled = installOverlayHooks(notifications, logger);
            logger.write("info", "Ctrl+R polling active: GetAsyncKeyState(VK_CONTROL) + GetAsyncKeyState('R'), 30 ms interval");
            logger.write("info", "Ctrl+K polling active");
            RisingEdge unloadEdge;
            RisingEdge debugEdge((GetAsyncKeyState(VK_F3) & 0x8000) != 0);
            bool debugVisible = false;
            bool unloadRequested = false;
            auto nextDiagnostic = std::chrono::steady_clock::now() + std::chrono::seconds(15);
            for (;;) {
                const DWORD wait = WaitForSingleObject(stopEvent, 30);
                if (wait == WAIT_OBJECT_0) { break; }
                if (wait != WAIT_TIMEOUT) {
                    logger.write("error", "Automatic worker stop event failed; Windows error=" +
                                          std::to_string(GetLastError()));
                    break;
                }
                const bool ctrl = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
                DWORD inputPid = 0;
                GetWindowThreadProcessId(GetForegroundWindow(), &inputPid);
                const bool focused = inputPid == GetCurrentProcessId();
                if (debugEdge.update(focused && (GetAsyncKeyState(VK_F3) & 0x8000))) {
                    debugVisible = !debugVisible;
                    logger.write("info", debugVisible ? "F3 diagnostics shown" : "F3 diagnostics hidden");
                }
                if (debugVisible) {
                    ConduitStats stats{};
                    client.getStats(stats);
                    const auto jsonUi = client.getJsonUiInfo();
                    notifications.setDebugPanels({
                        {"Conduit JSON UI", "Target 1.26.60 unverified", {}},
                        {"Reload backend", "Unsupported - no verified hooks", {}},
                        {"Disk JSON UI", std::to_string(jsonUi.files) + " files - " +
                            std::to_string(jsonUi.definitions) + " definitions", {}},
                        {"Namespace", jsonUi.namespaces.empty() ? "None in watched files" :
                            jsonUi.namespaces.front(), {}},
                        {"Validation", jsonUi.error.empty() ? "Disk syntax valid" : "Failed - see conduit.log", {}},
                        {"Reload attempts", std::to_string(stats.reload_attempts) + " - " +
                            std::to_string(stats.reload_successes) + " succeeded", {}},
                        {"Watcher", std::to_string(stats.file_changes) + " changes - " +
                            std::to_string(stats.monitor_errors) + " errors", {}}
                    });
                } else {
                    notifications.setDebugPanels({});
                }
                const bool k = (GetAsyncKeyState('K') & 0x8000) != 0;
                if (unloadEdge.update(ctrl && k)) {
                    logger.write("info", "Ctrl+K detected; stopping reload requests");
                    notifications.push("Conduit Client", "Unloading client...",
                                       std::chrono::steady_clock::now());
                    unloadRequested = true;
                    break;
                }
                if (std::chrono::steady_clock::now() < nextDiagnostic) { continue; }
                nextDiagnostic = std::chrono::steady_clock::now() + std::chrono::seconds(15);
                DWORD foregroundPid = 0;
                GetWindowThreadProcessId(GetForegroundWindow(), &foregroundPid);
                logger.write("info", "Ctrl+R diagnostic: foreground PID=" +
                    std::to_string(foregroundPid) + ", current PID=" +
                    std::to_string(GetCurrentProcessId()) + ", key-down=" +
                    std::to_string((GetAsyncKeyState('R') & 0x8000) != 0));
                if (overlayInstalled) { logOverlayDiagnostics(logger); }
            }
            client.stop();
            if (unloadRequested && overlayInstalled && overlayHasRendered()) {
                WaitForSingleObject(stopEvent, 3500);
            }
            if (overlayInstalled) { removeOverlayHooks(); }
            logger.write("info", "Automatic client shutdown complete");
        }
        {
            std::lock_guard lock(popupMutex);
            popupStopping.store(true);
            popupPending = false;
        }
        popupWake.notify_all();
        if (popupThread.joinable()) { popupThread.join(); }
    } catch (const std::exception& error) {
        OutputDebugStringA("Conduit Client automatic initialization failed: ");
        OutputDebugStringA(error.what());
    } catch (...) {
        OutputDebugStringA("Conduit Client automatic initialization failed unexpectedly");
    }
    {
        std::lock_guard lock(popupMutex);
        popupStopping.store(true);
        popupPending = false;
    }
    popupWake.notify_all();
    if (ownedModule) { FreeLibraryAndExitThread(ownedModule, 0); }
    return 0;
}

bool isMinecraftProcess() noexcept {
    wchar_t name[32768]{};
    const DWORD length = GetModuleFileNameW(nullptr, name, 32768);
    if (length == 0 || length >= 32768) { return false; }
    const wchar_t* basename = name;
    for (const wchar_t* cursor = name; *cursor; ++cursor) {
        if (*cursor == L'\\' || *cursor == L'/') { basename = cursor + 1; }
    }
    return _wcsicmp(basename, L"Minecraft.Windows.exe") == 0;
}
}

void scheduleAutoStart(HINSTANCE module) noexcept {
    if (!isMinecraftProcess()) { return; }
    stopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!stopEvent) {
        OutputDebugStringA("Conduit Client: CreateEvent failed");
        return;
    }
    worker = CreateThread(nullptr, 0, runAuto, module, 0, nullptr);
    if (!worker) {
        OutputDebugStringA("Conduit Client: CreateThread failed");
        CloseHandle(stopEvent);
        stopEvent = nullptr;
    }
}

void signalProcessExit() noexcept {
    if (stopEvent) { SetEvent(stopEvent); }
}

void stopAutoStart() noexcept {
    if (!worker) { return; }
    SetEvent(stopEvent);
    WaitForSingleObject(worker, INFINITE);
    CloseHandle(worker);
    worker = nullptr;
    CloseHandle(stopEvent);
    stopEvent = nullptr;
}
}
