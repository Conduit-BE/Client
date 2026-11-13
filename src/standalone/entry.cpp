#include "client.hpp"
#include "platform/windows/debug_overlay.hpp"
#include "core/logger.hpp"
#include "experimental/legacy_reload.hpp"
#include "standalone/initialization.hpp"
#include "standalone/settings.hpp"
#include "minecraft/compatibility.hpp"
#include "standalone/title_logo_asset.hpp"
#include "platform/windows/native_title_logo.hpp"
#include <windows.h>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>

namespace {
conduit::standalone::Initialization initialization;
std::atomic<bool> vanillaTitle{false};
bool hasVanillaTitle() noexcept { return vanillaTitle.load(std::memory_order_acquire); }
std::string utf8(const std::filesystem::path& path) {
    const auto text = path.u8string(); return {text.begin(), text.end()};
}
DWORD WINAPI initialize(LPVOID) noexcept {
    if (!initialization.begin()) return 0;
    // This worker starts after loader-lock release. Never unload callback code.
    HMODULE pinned{};
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                           reinterpret_cast<LPCWSTR>(&initialize), &pinned)) {
        OutputDebugStringA("ConduitClient: cannot pin initialization worker");
        initialization.fail(); return 0;
    }
    conduit::windows::DebugReloadStats debugStats;
    std::unique_ptr<conduit::Logger> log;
    std::unique_ptr<conduit::Client> client;
    try {
        wchar_t appData[32768]{};
        const auto length = GetEnvironmentVariableW(L"LOCALAPPDATA", appData, 32768);
        if (!length || length >= 32768) throw std::runtime_error("LOCALAPPDATA unavailable");
        const auto root = std::filesystem::path(appData) / L"ConduitClient";
        log = std::make_unique<conduit::Logger>(conduit::standalone::logDirectory(root) / L"standalone.log");
        log->write("info", "Standalone initialization starting; no Frida controller required");
        const auto configFile = root / L"config.json";
        const auto disableFile = root / L"DISABLE";
        if (std::filesystem::exists(disableFile)) {
            log->write("info", "DISABLE present; no hooks installed"); initialization.disable(); return 0;
        }
        conduit::standalone::migrateSettings(configFile);
        debugStats.settings = conduit::standalone::loadDiagnosticsSettings(configFile);
        const auto logs = conduit::standalone::logDirectory(root);
        wchar_t roamingData[32768]{};
        const auto roamingLength = GetEnvironmentVariableW(L"APPDATA", roamingData, 32768);
        const auto roaming = roamingLength && roamingLength < 32768 ? std::filesystem::path(roamingData) : std::filesystem::path{};
        auto backend = conduit::experimental::makeLegacyReloadBackend(logs / L"native-stages.log", true);
        conduit::windows::setTitleLogoGuards(&conduit::experimental::isActiveTitleView,&hasVanillaTitle);
        try {
            debugStats.titleLogoFile = conduit::standalone::prepareTitleLogoFile(root);
            vanillaTitle.store(conduit::standalone::vanillaGlobalPacks(appData,roaming));
            log->write("info", "Title logo cache prepared; replacement requires verified active vanilla title context");
        } catch (const std::exception& error) { log->write("warning",error.what()); }
        debugStats.clientVersion = conduit::minecraft::runningCompatibility()->build;
        debugStats.screenMetrics = &conduit::experimental::nativeScreenMetrics();
        debugStats.inspectHoveredControl = &conduit::experimental::inspectHoveredControl;
        debugStats.diagnostics = log.get();
        backend = conduit::windows::trackDebugReloads(std::move(backend), debugStats);
        const auto clientLog = utf8(logs / L"client.log");
        const auto watch = utf8(root); // Native engine owns mounted packs; no manual pack setting.
        ConduitConfig config{sizeof(ConduitConfig), CONDUIT_API_VERSION, watch.c_str(), clientLog.c_str(),
                             CONDUIT_HOTKEY_DEFAULT, 30, 150};
        // Minecraft owns the mounted definitions and their parsing. Do not gate
        // targeted reload on unrelated or inactive resource-pack files on disk.
        client = std::make_unique<conduit::Client>(config, std::function<void()>{},
            std::move(backend), true, false, [] { return true; });
        if (client->start() != CONDUIT_OK) throw std::runtime_error("Client initialization failed");
        initialization.running();
        log->write("info", "Client running; waiting for verified frame affinity. See native-stages.log for automatic_backend_armed");
        log->write("info", "Native mounted-definition reload; disk preflight and continuous watching disabled. Data root: " + watch);
        log->write("info", "F3 overlay enabled; native screen and control sampling active when verified");
        conduit::standalone::DeactivationRequest deactivation((GetAsyncKeyState(VK_CONTROL) & 0x8000) && (GetAsyncKeyState('K') & 0x8000));
        auto nextTitleCheck = std::chrono::steady_clock::now();
        const auto shouldStop = [&] {
            const auto now = std::chrono::steady_clock::now();
            if (now >= nextTitleCheck) {
                vanillaTitle.store(conduit::standalone::vanillaGlobalPacks(appData,roaming),std::memory_order_release);
                nextTitleCheck = now+std::chrono::milliseconds(250);
            }
            const bool chordDown = (GetAsyncKeyState(VK_CONTROL) & 0x8000) && (GetAsyncKeyState('K') & 0x8000);
            DWORD foregroundPid{};
            GetWindowThreadProcessId(GetForegroundWindow(), &foregroundPid);
            return deactivation.update(chordDown, foregroundPid == GetCurrentProcessId()) || std::filesystem::exists(disableFile);
        };
        log->write("info", "Ctrl+K deactivates client; DLL remains pinned for callback safety");
        try { conduit::windows::runDebugOverlay(disableFile.c_str(), debugStats, shouldStop); }
        catch (const std::exception& error) { log->write("error", std::string("Overlay stopped: ") + error.what()); }
        while (!shouldStop()) Sleep(15);
        vanillaTitle.store(false);
        conduit::experimental::disableLegacyReload();
        client.reset(); // Wake pending reload and join its worker before hook retirement.
        const bool retired = conduit::experimental::retireLegacyReloadHooks();
        initialization.disable();
        log->write(retired ? "info" : "error", retired ? "Disabled; worker joined and hooks disabled; DLL stays pinned" : "Disabled; hook retirement failed; DLL stays pinned");
    } catch (const std::exception& error) {
        conduit::experimental::disableLegacyReload();
        client.reset();
        conduit::experimental::retireLegacyReloadHooks();
        if (log) log->write("error", error.what());
        initialization.fail();
        OutputDebugStringA(error.what());
    } catch (...) {
        conduit::experimental::disableLegacyReload();
        client.reset();
        conduit::experimental::retireLegacyReloadHooks();
        if (log) log->write("error", "Unexpected initialization failure; native backend disabled");
        initialization.fail();
    }
    return 0;
}
}
extern "C" __declspec(dllexport) unsigned ConduitStandalonePhase() {
    return static_cast<unsigned>(initialization.phase());
}
BOOL WINAPI DllMain(HINSTANCE, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        // No waits, hooks, filesystem access or game inspection under loader lock.
        const auto worker = CreateThread(nullptr, 0, initialize, nullptr, 0, nullptr);
        if (worker) CloseHandle(worker);
        else OutputDebugStringA("ConduitClient: cannot schedule initialization");
    }
    return TRUE;
}
