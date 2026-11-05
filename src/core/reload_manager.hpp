#pragma once

#include "core/file_monitor.hpp"

#include <memory>
#include <vector>

namespace conduit {
enum class ReloadResult { Succeeded, Unsupported, Failed };
struct ReloadRequest {
    bool explicitRequest = false;
    bool hotkey = false;
    std::vector<FileChange> changes;
};

class ReloadBackend {
public:
    virtual ~ReloadBackend() = default;
    // Called serially on the coordinator worker. A future game implementation
    // must marshal mutations to a verified game/UI thread and report completion.
    virtual ReloadResult reload(const ReloadRequest& request) = 0;
};

class MinecraftReloadBackend final : public ReloadBackend {
public:
    ReloadResult reload(const ReloadRequest& request) override;
};

class ReloadManager {
public:
    explicit ReloadManager(std::unique_ptr<ReloadBackend> backend);
    ReloadResult reload(const ReloadRequest& request);
private:
    std::unique_ptr<ReloadBackend> backend_;
};
}
