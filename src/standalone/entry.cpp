#include "client.hpp"
#include "platform/windows/debug_overlay.hpp"
#include "platform/windows/key_chord.hpp"
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
#include <string_view>
#include <system_error>

namespace {
conduit::standalone::Initialization initialization;
std::atomic<bool> vanillaTitle{false};
bool hasVanillaTitle() noexcept { return vanillaTitle.load(std::memory_order_acquire); }
std::string utf8(const std::filesystem::path& path) {
    const auto text = path.u8string(); return {text.begin(), text.end()};
}
constexpr std::wstring_view residentPrefix = L"ConduitClient.runtime.";
std::filesystem::path dataRoot() {
    wchar_t appData[32768]{};
    const auto length = GetEnvironmentVariableW(L"LOCALAPPDATA", appData, 32768);
    if (!length || length >= 32768) throw std::runtime_error("LOCALAPPDATA unavailable");
    return std::filesystem::path(appData) / L"ConduitClient";
}
struct InstanceLease {
    HANDLE handle{};
    ~InstanceLease() { release(); }
    void release() noexcept { if (handle) CloseHandle(handle); handle = nullptr; }
    bool acquire() {
        const auto name = L"Local\\ConduitClient.Standalone." + std::to_wstring(GetCurrentProcessId());
        SetLastError(ERROR_SUCCESS);
        handle = CreateEventW(nullptr, TRUE, FALSE, name.c_str());
        if (!handle) throw std::system_error(GetLastError(), std::system_category(), "Cannot open Conduit instance lease");
        return GetLastError() != ERROR_ALREADY_EXISTS;
    }
    // A failed retirement may leave a native target hooked. Keep the lease for
    // the rest of this process so a fresh instance cannot wrap those hooks.
    void retain() noexcept { handle = nullptr; }
};
void initialize() noexcept {
    if (!initialization.begin()) return;
    InstanceLease lease;
    try {
        if (!lease.acquire()) {
            OutputDebugStringA("ConduitClient: an instance is already running or ejecting");
            initialization.fail();
            return;
        }
    } catch (const std::exception& error) {
        OutputDebugStringA(error.what());
        initialization.fail();
        return;
    }
    // This worker starts after loader-lock release. Never unload callback code.
    HMODULE pinned{};
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                           reinterpret_cast<LPCWSTR>(&initialize), &pinned)) {
        OutputDebugStringA("ConduitClient: cannot pin initialization worker");
        lease.release();
        initialization.fail(); return;
    }
    conduit::windows::DebugReloadStats debugStats;
    std::unique_ptr<conduit::Logger> log;
    std::unique_ptr<conduit::Client> client;
    try {
        const auto root = dataRoot();
        const auto appData = root.parent_path();
        log = std::make_unique<conduit::Logger>(conduit::standalone::logDirectory(root) / L"standalone.log");
        log->write("info", "Standalone initialization starting; no Frida controller required");
        const auto configFile = root / L"config.json";
        const auto disableFile = root / L"DISABLE";
        if (std::filesystem::exists(disableFile)) {
            log->write("info", "DISABLE present; no hooks installed");
            lease.release();
            initialization.disable();
            return;
        }
        conduit::standalone::migrateSettings(configFile);
        const auto settings = conduit::standalone::loadSettings(configFile);
        debugStats.settings = settings.diagnostics;
        debugStats.keybinds = settings.keybinds;
        const auto logs = conduit::standalone::logDirectory(root);
        wchar_t roamingData[32768]{};
        const auto roamingLength = GetEnvironmentVariableW(L"APPDATA", roamingData, 32768);
        const auto roaming = roamingLength && roamingLength < 32768 ? std::filesystem::path(roamingData) : std::filesystem::path{};
        auto backend = conduit::experimental::makeLegacyReloadBackend(logs / L"native-stages.log", true);
        conduit::windows::setTitleLogoGuards(&conduit::experimental::isActiveTitleView,
            &conduit::experimental::isActiveWorldView, &hasVanillaTitle);
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
                             CONDUIT_HOTKEY_DISABLED, 30, 150};
        // Minecraft owns the mounted definitions and their parsing. Do not gate
        // targeted reload on unrelated or inactive resource-pack files on disk.
        client = std::make_unique<conduit::Client>(config, std::function<void()>{},
            std::move(backend), true, false, [] { return true; });
        if (client->start() != CONDUIT_OK) throw std::runtime_error("Client initialization failed");
        initialization.running();
        log->write("info", "Client running; waiting for verified frame affinity. See native-stages.log for automatic_backend_armed");
        log->write("info", "Native mounted-definition reload; disk preflight and continuous watching disabled. Data root: " + watch);
        log->write("info", "Configured hotkeys active; native screen and control sampling active when verified");
        conduit::windows::KeyChordPoller ejectKey;
        conduit::windows::KeyChordPoller reloadKey;
        ejectKey.poll(debugStats.keybinds.eject);
        reloadKey.poll(debugStats.keybinds.reload);
        conduit::standalone::DeactivationRequest deactivation;
        unsigned preparedLogoGeneration = 0;
        auto nextTitleCheck = std::chrono::steady_clock::now();
        const auto shouldStop = [&] {
            // The overlay and the fallback loop both poll this callback. Preserve
            // the request after the overlay consumes the key press and returns.
            if (deactivation.requested()) return true;
            DWORD foregroundPid{};
            GetWindowThreadProcessId(GetForegroundWindow(), &foregroundPid);
            const bool focused = foregroundPid == GetCurrentProcessId();
            if ((ejectKey.poll(debugStats.keybinds.eject) && focused) || std::filesystem::exists(disableFile)) {
                deactivation.request();
                log->write("info", "Eject requested; retiring client hooks");
                return true;
            }
            const auto now = std::chrono::steady_clock::now();
            if (now >= nextTitleCheck) {
                vanillaTitle.store(conduit::standalone::vanillaGlobalPacks(appData,roaming),std::memory_order_release);
                nextTitleCheck = now+std::chrono::milliseconds(250);
            }
            const auto requestedLogoGeneration = conduit::windows::requestedTitleLogoGeneration();
            if (requestedLogoGeneration != preparedLogoGeneration) {
                preparedLogoGeneration = requestedLogoGeneration;
                if (requestedLogoGeneration >= conduit::windows::titleLogoImportLimit) {
                    log->write("warning", "Title logo import limit reached; using the original logo");
                } else {
                    try {
                        const auto file = conduit::standalone::prepareTitleLogoFile(root, requestedLogoGeneration);
                        if (conduit::windows::supplyTitleLogoFile(requestedLogoGeneration, file))
                            log->write("info", "Fresh title logo cache key prepared for generation " + std::to_string(requestedLogoGeneration));
                        else
                            log->write("warning", "Title logo cache path rejected; using the original logo");
                    } catch (const std::exception& error) {
                        log->write("warning", error.what());
                    }
                }
            }
            if (reloadKey.poll(debugStats.keybinds.reload) && focused) {
                try {
                    const auto updated = conduit::standalone::loadSettings(configFile);
                    debugStats.settings = updated.diagnostics;
                    debugStats.keybinds = updated.keybinds;
                    // Seed changed shortcuts before the next poll so a held key can't fire.
                    ejectKey.poll(debugStats.keybinds.eject);
                    reloadKey.poll(debugStats.keybinds.reload);
                    log->write("info", "Client settings refreshed from config.json");
                } catch (const std::exception& error) {
                    log->write("warning", std::string("Keeping active client settings: ") + error.what());
                }
                if (client->requestReload() != CONDUIT_OK)
                    log->write("warning", "Could not queue JSON UI reload");
            }
            return false;
        };
        try { conduit::windows::runDebugOverlay(disableFile.c_str(), debugStats, shouldStop); }
        catch (const std::exception& error) { log->write("error", std::string("Overlay stopped: ") + error.what()); }
        while (!shouldStop()) Sleep(15);
        vanillaTitle.store(false);
        conduit::experimental::disableLegacyReload();
        client.reset(); // Wake pending reload and join its worker before hook retirement.
        const bool retired = conduit::experimental::retireLegacyReloadHooks();
        if (retired) lease.release();
        else lease.retain();
        initialization.disable();
        log->write(retired ? "info" : "error", retired ? "Disabled; hooks retired; ready for reinjection" : "Disabled; hook retirement failed; restart Minecraft before reinjecting");
    } catch (const std::exception& error) {
        conduit::experimental::disableLegacyReload();
        client.reset();
        if (conduit::experimental::retireLegacyReloadHooks()) lease.release();
        else lease.retain();
        if (log) log->write("error", error.what());
        initialization.fail();
        OutputDebugStringA(error.what());
    } catch (...) {
        conduit::experimental::disableLegacyReload();
        client.reset();
        if (conduit::experimental::retireLegacyReloadHooks()) lease.release();
        else lease.retain();
        if (log) log->write("error", "Unexpected initialization failure; native backend disabled");
        initialization.fail();
    }
}

// Each resident copy stays mapped until process exit. Remove staging folders
// belonging to exited processes when a later injection creates a new copy.
void pruneRuntimeCopies(const std::filesystem::path& root) {
    std::error_code error;
    for (std::filesystem::directory_iterator it(root, error), end; !error && it != end; it.increment(error)) {
        const auto name = it->path().filename().wstring();
        if (name.empty() || name.find_first_not_of(L"0123456789") != std::wstring::npos ||
            name.size() > 10 || it->is_symlink(error) || !it->is_directory(error)) continue;
        const auto pid = std::stoull(name);
        if (!pid || pid > MAXDWORD || pid == GetCurrentProcessId()) continue;
        const auto process = OpenProcess(SYNCHRONIZE, FALSE, static_cast<DWORD>(pid));
        const bool exited = process ? WaitForSingleObject(process, 0) == WAIT_OBJECT_0 :
            GetLastError() == ERROR_INVALID_PARAMETER;
        if (process) CloseHandle(process);
        if (exited) {
            std::error_code ignored;
            std::filesystem::remove_all(it->path(), ignored);
        }
    }
}
void loadResidentCopy(const std::filesystem::path& source) {
    const auto runtimeRoot = dataRoot() / L"runtime";
    const auto directory = runtimeRoot / std::to_wstring(GetCurrentProcessId());
    std::filesystem::create_directories(directory);
    pruneRuntimeCopies(runtimeRoot);
    auto candidate = GetTickCount64();
    for (;;) {
        const auto path = directory / (std::wstring(residentPrefix) + std::to_wstring(candidate++) + L".dll");
        if (!CopyFileW(source.c_str(), path.c_str(), TRUE)) {
            const auto error = GetLastError();
            if (error == ERROR_FILE_EXISTS) continue;
            throw std::system_error(error, std::system_category(), "Cannot stage Conduit runtime");
        }
        // The resident's DllMain schedules its own worker. Its initial loader
        // reference is kept so it cannot disappear before that worker pins it.
        if (LoadLibraryW(path.c_str())) return;
        const auto error = GetLastError();
        DeleteFileW(path.c_str());
        throw std::system_error(error, std::system_category(), "Cannot load Conduit runtime");
    }
}
DWORD WINAPI dispatchInjection(LPVOID argument) noexcept {
    const auto module = static_cast<HMODULE>(argument);
    try {
        wchar_t modulePath[32768]{};
        const auto length = GetModuleFileNameW(module, modulePath, 32768);
        if (!length || length >= 32768) throw std::runtime_error("Cannot locate injected Conduit DLL");
        const std::filesystem::path source(modulePath);
        if (source.filename().wstring().starts_with(residentPrefix)) {
            initialize();
            return 0;
        }
        loadResidentCopy(source);
    } catch (const std::exception& error) {
        OutputDebugStringA(error.what());
    } catch (...) {
        OutputDebugStringA("ConduitClient: unexpected injection loader failure");
    }
    // Only the loader reference is released here. No hooks were installed
    // from this image, so Windows can call DllMain on the same path next time.
    FreeLibraryAndExitThread(module, 0);
}
}
extern "C" __declspec(dllexport) unsigned ConduitStandalonePhase() {
    return static_cast<unsigned>(initialization.phase());
}
BOOL WINAPI DllMain(HINSTANCE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        // No waits, hooks, filesystem access or game inspection under loader lock.
        const auto worker = CreateThread(nullptr, 0, dispatchInjection, module, 0, nullptr);
        if (worker) CloseHandle(worker);
        else OutputDebugStringA("ConduitClient: cannot schedule initialization");
    }
    return TRUE;
}
