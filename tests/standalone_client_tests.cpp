#include "client.hpp"
#include <windows.h>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <vector>
using namespace std::chrono_literals;
namespace {
struct ControlledBackend final : conduit::ReloadBackend {
    std::mutex mutex;
    std::condition_variable wake;
    unsigned calls = 0, active = 0, maxActive = 0;
    bool released = false;
    conduit::ReloadResult reload(const conduit::ReloadRequest&) override {
        std::unique_lock lock(mutex);
        ++calls; ++active; maxActive = std::max(maxActive, active); wake.notify_all();
        wake.wait(lock, [&] { return released; });
        --active; return conduit::ReloadResult::Succeeded;
    }
};
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
}
int main() {
    const auto root = std::filesystem::temp_directory_path() / ("conduit-client-serialization-" + std::to_string(GetCurrentProcessId()));
    try {
        std::filesystem::create_directories(root / "ui");
        std::ofstream(root / "ui" / "screen.json") << "{\"namespace\":\"test\"}";
        {
            const auto watch = (root / "ui").string(), log = (root / "client.log").string();
            ConduitConfig config{sizeof(ConduitConfig), CONDUIT_API_VERSION, watch.c_str(), log.c_str(), 0xffffffff, 10, 0};
            auto backend = std::make_unique<ControlledBackend>(); auto* control = backend.get();
            conduit::Client client(config, {}, std::move(backend), true, false);
            require(client.start() == CONDUIT_OK, "start failed");
            require(client.start() == CONDUIT_ALREADY_RUNNING, "duplicate worker allowed");
            require(client.getJsonUiInfo().files == 0, "Startup unexpectedly scanned JSON");
            std::ofstream(root / "ui" / "screen.json") << "invalid JSON";
            std::this_thread::sleep_for(100ms);
            ConduitStats idle{};
            client.getStats(idle);
            require(idle.reload_attempts == 0 && idle.file_changes == 0 && idle.monitor_errors == 0,
                    "File change triggered background work");
            require(client.requestReload() == CONDUIT_OK, "invalid request failed");
            const auto deadline = std::chrono::steady_clock::now() + 2s;
            while (client.getJsonUiInfo().error.empty() && std::chrono::steady_clock::now() < deadline)
                std::this_thread::sleep_for(10ms);
            require(!client.getJsonUiInfo().error.empty(), "On-demand JSON validation missing");
            {
                std::lock_guard lock(control->mutex);
                require(control->calls == 0, "Invalid JSON reached backend");
            }
            std::ofstream(root / "ui" / "screen.json") << "{\"namespace\":\"test\"}";
            require(client.requestReload() == CONDUIT_OK, "request failed");
            {
                std::unique_lock lock(control->mutex);
                require(control->wake.wait_for(lock, 2s, [&] { return control->calls == 1; }), "first dispatch missing");
            }
            std::vector<std::jthread> requesters;
            std::atomic<unsigned> accepted{0};
            for (unsigned i = 0; i < 16; ++i) requesters.emplace_back([&] {
                for (unsigned n = 0; n < 20; ++n) if (client.requestReload() == CONDUIT_OK) ++accepted;
            });
            requesters.clear();
            require(accepted == 320, "concurrent requests failed");
            {
                std::lock_guard lock(control->mutex);
                require(control->calls == 1 && control->maxActive == 1, "overlapping dispatch");
                control->released = true;
            }
            control->wake.notify_all();
            {
                std::unique_lock lock(control->mutex);
                require(control->wake.wait_for(lock, 2s, [&] { return control->calls == 2; }), "coalesced request missing");
            }
            client.stop(); client.stop();
            require(client.requestReload() == CONDUIT_NOT_RUNNING, "request accepted after stop");
            require(control->active == 0 && control->maxActive == 1 && control->calls == 2,
                    "worker did not join or requests were not coalesced");
        }
        std::filesystem::remove_all(root);
        std::cout << "Client serializes concurrent requests, coalesces overlap, and joins on repeated stop\n";
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
