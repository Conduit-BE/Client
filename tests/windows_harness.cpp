#include "conduit/api.h"

#include <windows.h>
#include <atomic>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace fs = std::filesystem;
using namespace std::chrono_literals;

namespace {
void require(bool condition, const char* message) {
    if (!condition) { throw std::runtime_error(message); }
}

template<class Function>
Function symbol(HMODULE module, const char* name) {
    const auto address = GetProcAddress(module, name);
    require(address != nullptr, name);
    static_assert(sizeof(Function) == sizeof(address));
    Function result;
    std::memcpy(&result, &address, sizeof(result));
    return result;
}

struct Api {
    decltype(&ConduitGetApiVersion) version;
    decltype(&ConduitCreate) create;
    decltype(&ConduitStart) start;
    decltype(&ConduitRequestReload) request;
    decltype(&ConduitGetStats) stats;
    decltype(&ConduitStop) stop;
    decltype(&ConduitDestroy) destroy;
    explicit Api(HMODULE module)
        : version(symbol<decltype(version)>(module, "ConduitGetApiVersion")),
          create(symbol<decltype(create)>(module, "ConduitCreate")),
          start(symbol<decltype(start)>(module, "ConduitStart")),
          request(symbol<decltype(request)>(module, "ConduitRequestReload")),
          stats(symbol<decltype(stats)>(module, "ConduitGetStats")),
          stop(symbol<decltype(stop)>(module, "ConduitStop")),
          destroy(symbol<decltype(destroy)>(module, "ConduitDestroy")) {}
};

ConduitStats getStats(const Api& api, ConduitClient* client) {
    ConduitStats stats{};
    stats.size = sizeof(stats);
    require(api.stats(client, &stats) == CONDUIT_OK, "GetStats failed");
    return stats;
}

template<class Predicate>
void waitUntil(Predicate predicate, const char* message) {
    const auto deadline = std::chrono::steady_clock::now() + 5s;
    while (std::chrono::steady_clock::now() < deadline) {
        if (predicate()) { return; }
        std::this_thread::sleep_for(10ms);
    }
    throw std::runtime_error(message);
}

std::string utf8(const fs::path& path) {
    const auto bytes = path.u8string();
    return std::string(bytes.begin(), bytes.end());
}

void exercise(const Api& api, const fs::path& root) {
    require(api.version() == CONDUIT_API_VERSION, "API version mismatch");
    require(api.start(nullptr) == CONDUIT_INVALID_ARGUMENT, "Null context accepted");
    require(api.create(nullptr, nullptr) == CONDUIT_INVALID_ARGUMENT, "Null config accepted");
    api.destroy(nullptr);
    fs::create_directories(root / "ui");
    const auto directory = utf8(root / "ui");
    const auto log = utf8(root / "client.log");
    ConduitConfig config{sizeof(ConduitConfig), CONDUIT_API_VERSION,
        directory.c_str(), log.c_str(), CONDUIT_HOTKEY_DISABLED, 10, 30};
    ConduitClient* client = nullptr;
    auto bad = config;
    bad.api_version = 999;
    require(api.create(&bad, &client) == CONDUIT_VERSION_MISMATCH && client == nullptr,
            "Incompatible API version accepted");
    bad = config;
    bad.hotkey_virtual_key = 300;
    require(api.create(&bad, &client) == CONDUIT_INVALID_ARGUMENT, "Invalid virtual key accepted");
    require(api.create(&config, &client) == CONDUIT_OK && client, "Create failed");
    // Cleanup must happen while the module is still loaded, also on test failure.
    struct ContextGuard {
        const Api& api;
        ConduitClient* client;
        ~ContextGuard() { api.destroy(client); }
    } guard{api, client};

    require(api.request(client) == CONDUIT_NOT_RUNNING, "Reload before start accepted");
    require(api.stop(client) == CONDUIT_OK, "Stop before start failed");
    require(getStats(api, client).running == 0, "Context unexpectedly running");
    ConduitStats badStats{};
    require(api.stats(client, &badStats) == CONDUIT_INVALID_ARGUMENT, "Invalid stats size accepted");
    require(api.start(client) == CONDUIT_OK, "Start failed");
    require(api.start(client) == CONDUIT_ALREADY_RUNNING, "Duplicate start accepted");
    require(api.request(client) == CONDUIT_OK, "Request failed");
    waitUntil([&] { return getStats(api, client).reload_unsupported >= 1; }, "Request was not processed");
    require(getStats(api, client).reload_successes == 0, "Placeholder claimed reload success");

    { std::ofstream file(root / "ui" / "screen.json"); file << "{\"test\":true}"; }
    waitUntil([&] { return getStats(api, client).file_changes >= 1; }, "File creation was not detected");
    { std::ofstream file(root / "ui" / "screen.json"); file << "{\"test\":false}"; }
    waitUntil([&] { return getStats(api, client).file_changes >= 2; }, "File modification was not detected");
    fs::remove(root / "ui" / "screen.json");
    waitUntil([&] { return getStats(api, client).file_changes >= 3; }, "File deletion was not detected");

    std::atomic<bool> callsFailed{false};
    std::vector<std::jthread> callers;
    for (int i = 0; i < 4; ++i) {
        callers.emplace_back([&] {
            for (int j = 0; j < 100; ++j) {
                ConduitStats stats{};
                stats.size = sizeof(stats);
                if (api.request(client) != CONDUIT_OK || api.stats(client, &stats) != CONDUIT_OK) {
                    callsFailed.store(true);
                }
            }
        });
    }
    callers.clear();
    require(!callsFailed.load(), "Concurrent calls failed");
    require(api.stop(client) == CONDUIT_OK && api.stop(client) == CONDUIT_OK, "Stop is not idempotent");
    const auto stopped = getStats(api, client);
    require(stopped.running == 0 && stopped.logging_failed == 0, "Stop or logging failed");
    require(api.request(client) == CONDUIT_NOT_RUNNING, "Reload after stop accepted");
    std::this_thread::sleep_for(50ms);
    require(getStats(api, client).reload_attempts == stopped.reload_attempts, "Work continued after stop");

    // Missing directory must fail safely and allow a later retry.
    fs::remove(root / "ui");
    require(api.start(client) == CONDUIT_INTERNAL_ERROR, "Missing directory accepted at start");
    fs::create_directory(root / "ui");
    require(api.start(client) == CONDUIT_OK, "Restart after start failure failed");
    require(api.request(client) == CONDUIT_OK, "Request after restart failed");
    waitUntil([&] { return getStats(api, client).reload_attempts > stopped.reload_attempts; }, "Restart did not process work");
    // ContextGuard tests Destroy while running, without a separate Stop.
}
}

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "Usage: ConduitHarness.exe ConduitClient.dll\n";
        return 2;
    }
    const auto root = fs::temp_directory_path() / ("conduit-harness-" +
        std::to_string(GetCurrentProcessId()) + "-" + std::to_string(GetTickCount64()));
    try {
        for (int cycle = 0; cycle < 3; ++cycle) {
            const std::string_view bytes(argv[1]);
            const auto path = fs::path(std::u8string(bytes.begin(), bytes.end()));
            const auto module = LoadLibraryW(path.c_str());
            require(module != nullptr, "LoadLibrary failed");
            try {
                exercise(Api(module), root / std::to_string(cycle));
            } catch (...) {
                FreeLibrary(module);
                throw;
            }
            require(FreeLibrary(module) != 0, "FreeLibrary failed");
        }
        // Windows refuses this if the DLL left its log handles open.
        require(fs::remove_all(root) > 0, "Temporary file cleanup failed");
        std::cout << "DLL harness passed: 3 load/start/reload/stop/restart/destroy/unload cycles\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << "\nFixtures retained at " << root << '\n';
        return 1;
    }
}
