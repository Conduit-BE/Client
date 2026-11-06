#include "client.hpp"
#include "platform/windows/debug_overlay.hpp"
#include "core/logger.hpp"
#include "experimental/legacy_reload.hpp"
#include "standalone/initialization.hpp"
#include <windows.h>
#include <nlohmann/json.hpp>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>

namespace {
conduit::standalone::Initialization initialization;
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
        log = std::make_unique<conduit::Logger>(root / L"standalone.log");
        log->write("info", "Standalone initialization starting; no Frida controller required");
        const auto configFile = root / L"config.json";
        const auto disableFile = root / L"DISABLE";
        if (std::filesystem::exists(disableFile)) {
            log->write("info", "DISABLE present; no hooks installed"); initialization.disable(); return 0;
        }
        if (!std::filesystem::exists(configFile)) {
            std::ofstream config(configFile);
            config << "{\n  \"enabled\": true,\n  \"resourcePackPath\": \"\"\n}\n";
            if (!config) throw std::runtime_error("Cannot create config.json");
            throw std::runtime_error("Set resourcePackPath in config.json to the active pack root, then restart Minecraft and inject again");
        }
        if (std::filesystem::file_size(configFile) > 16384) throw std::runtime_error("config.json exceeds 16 KiB");
        std::ifstream configStream(configFile);
        const auto settings = nlohmann::json::parse(configStream);
        configStream.close();
        if (!settings.value("enabled", true)) {
            log->write("info", "Disabled by config; no hooks installed"); initialization.disable(); return 0;
        }
        const auto watch = settings.at("resourcePackPath").get<std::string>();
        const auto pack = std::filesystem::path(std::u8string(watch.begin(), watch.end()));
        if (watch.empty() || !pack.is_absolute() || !std::filesystem::is_directory(pack / L"ui") ||
            !std::filesystem::is_regular_file(pack / L"manifest.json"))
            throw std::runtime_error("resourcePackPath must be an absolute existing pack root containing ui and manifest.json");
        const auto modeName = settings.value("reloadMode", std::string("FullResourceReload"));
        const auto mode = conduit::experimental::parseLegacyReloadMode(modeName);
        log->write("info", "Selected reload mode: " + modeName);
        auto backend = conduit::experimental::makeLegacyReloadBackend(root / L"native-stages.log", true, mode);
        debugStats.screenMetrics = &conduit::experimental::nativeScreenMetrics();
        backend = conduit::windows::trackDebugReloads(std::move(backend), debugStats);
        const auto clientLog = utf8(root / L"client.log");
        ConduitConfig config{sizeof(ConduitConfig), CONDUIT_API_VERSION, watch.c_str(), clientLog.c_str(),
                             CONDUIT_HOTKEY_DEFAULT, 30, 150};
        client = std::make_unique<conduit::Client>(config, std::function<void()>{}, std::move(backend), true, false);
        if (client->start() != CONDUIT_OK) throw std::runtime_error("Client initialization failed");
        initialization.running();
        log->write("info", "Client running; waiting for verified frame affinity. See native-stages.log for automatic_backend_armed");
        log->write("info", "Pack root (validation on request only; continuous watching disabled): " + watch);
        log->write("info", "F3 overlay enabled; native screen and control sampling active when verified");
        try { conduit::windows::runDebugOverlay(disableFile.c_str(), debugStats); }
        catch (const std::exception& error) { log->write("error", std::string("Overlay stopped: ") + error.what()); }
        while (!std::filesystem::exists(disableFile)) Sleep(250);
        initialization.disable();
        conduit::experimental::disableLegacyReload();
        client.reset(); // Wake pending reload and join its worker before hook retirement.
        const bool retired = conduit::experimental::retireLegacyReloadHooks();
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
