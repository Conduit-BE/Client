#include "client.hpp"
#include "platform/windows/hotkey.hpp"

#include <exception>
#include <string>
#include <string_view>
#include <utility>

namespace conduit {
namespace {
std::filesystem::path pathFromUtf8(std::string_view bytes) {
    return std::filesystem::path(std::u8string(bytes.begin(), bytes.end()));
}
}
Client::Client(const ConduitConfig& config, std::function<void()> onHotkey,
               std::unique_ptr<ReloadBackend> backend, bool skipDevelopmentMetadata, bool monitorChanges, std::function<bool()> validateReload)
    : watchDirectory_(std::filesystem::absolute(pathFromUtf8(config.watch_directory_utf8))),
      virtualKey_(config.hotkey_virtual_key),
      pollInterval_(config.poll_interval_ms == 0 ? 50 : config.poll_interval_ms),
      debounce_(config.debounce_ms == 0 ? 150 : config.debounce_ms),
      skipDevelopmentMetadata_(skipDevelopmentMetadata),
      monitorChanges_(monitorChanges),
      logger_(std::filesystem::absolute(pathFromUtf8(config.log_file_utf8))),
      onHotkey_(std::move(onHotkey)),
      validateReload_(std::move(validateReload)),
      reloadManager_(backend ? std::move(backend) : std::make_unique<MinecraftReloadBackend>()) {}

Client::~Client() {
    stop();
}

ConduitStatus Client::start() {
    const std::lock_guard lock(lifecycleMutex_);
    if (running_) {
        return CONDUIT_ALREADY_RUNNING;
    }
    try {
        if (monitorChanges_) {
            monitor_ = std::make_unique<FileMonitor>(watchDirectory_, debounce_, skipDevelopmentMetadata_);
        }
        if (monitorChanges_ && !jsonUi_.refresh(watchDirectory_, skipDevelopmentMetadata_)) {
            logger_.write("error", jsonUi_.info().error);
        }
        hotkey_ = windows::makeHotkey(virtualKey_);
        requested_.store(false);
        workerFailed_.store(false);
        worker_ = std::jthread([this](std::stop_token stop) { run(stop); });
        running_ = true;
        logger_.write("info", "Client started; reload requests are validated before backend dispatch");
        return CONDUIT_OK;
    } catch (const std::exception& error) {
        monitor_.reset();
        hotkey_.reset();
        logger_.write("error", std::string("Client start failed: ") + error.what());
        return CONDUIT_INTERNAL_ERROR;
    } catch (...) {
        monitor_.reset();
        hotkey_.reset();
        logger_.write("error", "Client start failed; check the watch directory and resources");
        return CONDUIT_INTERNAL_ERROR;
    }
}

ConduitStatus Client::requestReload() {
    const std::lock_guard lock(lifecycleMutex_);
    if (!running_ || workerFailed_.load()) {
        return CONDUIT_NOT_RUNNING;
    }
    requested_.store(true);
    wake_.notify_all();
    return CONDUIT_OK;
}

void Client::stop() {
    const std::lock_guard lock(lifecycleMutex_);
    if (!running_) {
        return;
    }
    worker_.request_stop();
    wake_.notify_all();
    worker_.join();
    running_ = false;
    requested_.store(false);
    hotkey_.reset();
    monitor_.reset();
    logger_.write("info", "Client stopped; worker joined");
}

void Client::getStats(ConduitStats& stats) const {
    const std::lock_guard lock(lifecycleMutex_);
    stats.running = running_ && !workerFailed_.load() ? 1u : 0u;
    stats.reload_attempts = attempts_.load();
    stats.reload_successes = successes_.load();
    stats.reload_unsupported = unsupported_.load();
    stats.file_changes = fileChanges_.load();
    stats.monitor_errors = monitorErrors_.load();
    stats.logging_failed = logger_.failed() ? 1u : 0u;
    stats.worker_failed = workerFailed_.load() ? 1u : 0u;
}

void Client::run(std::stop_token stop) noexcept {
    bool monitorErrorReported = false;
    auto nextDiagnostic = std::chrono::steady_clock::now() + std::chrono::seconds(15);
    try {
        while (!stop.stop_requested()) {
            ReloadRequest request;
            request.explicitRequest = requested_.exchange(false);
            request.hotkey = hotkey_->pollPressed();
            if (request.hotkey) {
                logger_.write("info", "Ctrl+R press detected by GetAsyncKeyState");
                if (onHotkey_) {
                    try { onHotkey_(); }
                    catch (...) { logger_.write("error", "Ctrl+R notification failed"); }
                }
            }
            if (onHotkey_ && std::chrono::steady_clock::now() >= nextDiagnostic) {
                logger_.write("info", "Ctrl+R polling active; if no press appears, check Wine desktop focus and key mapping");
                nextDiagnostic = std::chrono::steady_clock::now() + std::chrono::seconds(15);
            }
            try {
                if (monitor_) request.changes = monitor_->poll(FileMonitor::Clock::now());
                monitorErrorReported = false;
            } catch (...) {
                ++monitorErrors_;
                if (!monitorErrorReported) {
                    logger_.write("error", "JSON UI scan failed; retaining the previous snapshot");
                    monitorErrorReported = true;
                }
            }
            if (stop.stop_requested()) {
                break;
            }
            if (request.explicitRequest || request.hotkey || !request.changes.empty()) {
                fileChanges_.fetch_add(request.changes.size());
                ++attempts_;
                try {
                    const bool valid = validateReload_ ? validateReload_() : jsonUi_.refresh(watchDirectory_, skipDevelopmentMetadata_);
                    if (!valid) { logger_.write("error", jsonUi_.info().error); }
                    else { logger_.write("info", "JSON validation completed; backend dispatch permitted"); }
                    switch (valid ? reloadManager_.reload(request) : ReloadResult::Failed) {
                    case ReloadResult::Succeeded:
                        ++successes_;
                        logger_.write("info", "Backend operation completed; visual UI refresh is not verified by this counter");
                        break;
                    case ReloadResult::Unsupported:
                        ++unsupported_;
                        logger_.write("info", "Reload requested; Minecraft backend is not implemented");
                        break;
                    case ReloadResult::Failed:
                        logger_.write("error", "Reload backend reported failure");
                        break;
                    }
                } catch (...) {
                    logger_.write("error", "Reload backend raised an exception");
                }
            }
            std::unique_lock waitLock(waitMutex_);
            wake_.wait_for(waitLock, stop, pollInterval_, [this] { return requested_.load(); });
        }
    } catch (...) {
        // Keep exceptions from escaping the worker or crossing the DLL boundary.
        workerFailed_.store(true);
        logger_.write("error", "Client worker stopped unexpectedly");
    }
}
}
