#include "core/reload_manager.hpp"

#include <stdexcept>
#include <utility>

namespace conduit {
ReloadResult MinecraftReloadBackend::reload(const ReloadRequest&) {
    // No verified Minecraft interface exists yet. No game state is touched.
    return ReloadResult::Unsupported;
}

ReloadManager::ReloadManager(std::unique_ptr<ReloadBackend> backend) : backend_(std::move(backend)) {
    if (!backend_) {
        throw std::invalid_argument("Reload backend is required");
    }
}

ReloadResult ReloadManager::reload(const ReloadRequest& request) {
    return backend_->reload(request);
}
}
