#include "experimental/legacy_reload.hpp"
#include "client.hpp"
#include <windows.h>
#include <algorithm>
#include <atomic>
#include <utility>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>

namespace {
std::mutex lifecycle;
std::unique_ptr<conduit::Client> client;
std::string lastError;
std::atomic<bool> starting{false};
std::atomic<bool> cancelStart{false};
}
extern "C" __declspec(dllexport) int ConduitPrototypeStart(const char* watchDirectory, const char* logFile) {
    std::lock_guard lock(lifecycle);
    if (cancelStart.load() || client || !watchDirectory || !*watchDirectory || !logFile || !*logFile) return 0;
    lastError.clear();
    try {
        ConduitConfig config{sizeof(ConduitConfig), CONDUIT_API_VERSION, watchDirectory, logFile,
                             CONDUIT_HOTKEY_DEFAULT, 30, 150};
        const auto logPath = std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(logFile)));
        auto diagnosticLogPath = logPath;
        diagnosticLogPath += L".stages.log";
        auto backend = conduit::experimental::makeLegacyReloadBackend(diagnosticLogPath);
        auto candidate = std::make_unique<conduit::Client>(config, std::function<void()>{}, std::move(backend), true);
        if (candidate->start() != CONDUIT_OK) throw std::runtime_error("Client initialization failed");
        client = std::move(candidate);
        return 1;
    } catch (const std::exception& error) {
        lastError = error.what();
        conduit::experimental::disableLegacyReload();
        return 0;
    }
}
extern "C" __declspec(dllexport) int ConduitPrototypeStartAsync(const char* watchDirectory, const char* logFile) {
    if (!watchDirectory || !*watchDirectory || !logFile || !*logFile) return 0;
    bool expected = false;
    if (!starting.compare_exchange_strong(expected, true)) return 0;
    cancelStart.store(false);
    try {
        auto arguments = std::make_unique<std::pair<std::string, std::string>>(watchDirectory, logFile);
        HMODULE pinned{};
        if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                               reinterpret_cast<LPCWSTR>(&ConduitPrototypeStartAsync), &pinned)) {
            starting.store(false); return 0;
        }
        HANDLE thread = CreateThread(nullptr, 0, [](LPVOID raw) -> DWORD {
            std::unique_ptr<std::pair<std::string, std::string>> owned(static_cast<std::pair<std::string, std::string>*>(raw));
            ConduitPrototypeStart(owned->first.c_str(), owned->second.c_str());
            starting.store(false);
            return 0;
        }, arguments.get(), 0, nullptr);
        if (!thread) { starting.store(false); return 0; }
        arguments.release();
        CloseHandle(thread);
        return 1;
    } catch (...) { starting.store(false); return 0; }
}
extern "C" __declspec(dllexport) int ConduitPrototypeArmFromObservation(unsigned processId, unsigned threadId) {
    return conduit::experimental::armFromObservedThread(processId, threadId) ? 1 : 0;
}
extern "C" __declspec(dllexport) int ConduitPrototypeSetResourceFallback(unsigned observedFlag) {
    return conduit::experimental::setResourceFallback(observedFlag) ? 1 : 0;
}
extern "C" __declspec(dllexport) void ConduitPrototypeDisable() {
    cancelStart.store(true);
    std::lock_guard lock(lifecycle);
    conduit::experimental::disableLegacyReload();
    client.reset();
    conduit::experimental::retireLegacyReloadHooks();
}
extern "C" __declspec(dllexport) unsigned ConduitPrototypeSnapshot(char* output, unsigned capacity) {
    std::unique_lock lock(lifecycle, std::try_to_lock);
    std::string text;
    if (!lock.owns_lock() || starting.load()) {
        text = "{\"starting\":1}";
    } else {
    text = conduit::experimental::legacyReloadState();
    if (!lastError.empty()) text = "ERROR: " + lastError;
    if (client) {
        ConduitStats stats{};
        client->getStats(stats);
        text += " attempts=" + std::to_string(stats.reload_attempts) + " successes=" + std::to_string(stats.reload_successes);
    }
    }
    if (output && capacity) {
        const auto length = std::min<std::size_t>(text.size(), capacity - 1);
        std::memcpy(output, text.data(), length);
        output[length] = 0;
    }
    return static_cast<unsigned>(text.size() + 1);
}
BOOL WINAPI DllMain(HINSTANCE, DWORD, LPVOID) { return TRUE; }
