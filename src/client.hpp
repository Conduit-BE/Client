#pragma once

#include "conduit/api.h"
#include "core/file_monitor.hpp"
#include "core/hotkey.hpp"
#include "core/logger.hpp"
#include "core/reload_manager.hpp"
#include "core/json_ui.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>

namespace conduit {
class Client {
public:
    explicit Client(const ConduitConfig& config, std::function<void()> onHotkey = {},
                    std::unique_ptr<ReloadBackend> backend = {}, bool skipDevelopmentMetadata = false,
                    bool monitorChanges = true, std::function<bool()> validateReload = {});
    ~Client();
    Client(const Client&) = delete;
    Client& operator=(const Client&) = delete;
    ConduitStatus start();
    ConduitStatus requestReload();
    void stop();
    void getStats(ConduitStats& stats) const;
    JsonUiInfo getJsonUiInfo() const { return jsonUi_.info(); }
private:
    void run(std::stop_token stop) noexcept;
    const std::filesystem::path watchDirectory_;
    const std::uint32_t virtualKey_;
    const std::chrono::milliseconds pollInterval_;
    const std::chrono::milliseconds debounce_;
    const bool skipDevelopmentMetadata_;
    const bool monitorChanges_;
    Logger logger_;
    std::function<void()> onHotkey_;
    std::function<bool()> validateReload_;
    ReloadManager reloadManager_;
    JsonUiRepository jsonUi_;
    std::unique_ptr<FileMonitor> monitor_;
    std::unique_ptr<Hotkey> hotkey_;
    mutable std::mutex lifecycleMutex_;
    std::mutex waitMutex_;
    std::condition_variable_any wake_;
    std::atomic<bool> requested_{false};
    std::atomic<bool> workerFailed_{false};
    bool running_ = false; // Protected by lifecycleMutex_.
    std::atomic<std::uint64_t> attempts_{0};
    std::atomic<std::uint64_t> successes_{0};
    std::atomic<std::uint64_t> unsupported_{0};
    std::atomic<std::uint64_t> fileChanges_{0};
    std::atomic<std::uint64_t> monitorErrors_{0};
    std::jthread worker_; // Destroyed before the resources it uses.
};
}
