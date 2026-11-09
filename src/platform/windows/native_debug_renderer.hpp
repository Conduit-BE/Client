#pragma once
#include "core/debug_panel.hpp"
#include <filesystem>
namespace conduit::windows {
// Requires the standalone backend's exact-image verification and MinHook initialization.
// Owns no game objects, graphics devices, queues, windows, or font assets.
class NativeDebugRenderer {
public:
    bool install(DebugPanelMailbox& panel, const std::filesystem::path& logoFile = {}) noexcept;
    ~NativeDebugRenderer();
private:
    bool installed_ = false;
};
}
