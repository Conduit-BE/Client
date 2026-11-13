#pragma once
#include "core/debug_panel.hpp"
#include "core/hover_inspection.hpp"
#include <filesystem>
#include <windows.h>
namespace conduit::windows {
// Requires the standalone backend's native profile and MinHook initialization.
// Owns no game objects, graphics devices, queues, windows, or font assets.
class NativeDebugRenderer {
public:
    bool install(DebugPanelMailbox& panel, const std::filesystem::path& logoFile = {},
                 HWND window = nullptr, InspectHoveredControl inspect = nullptr, float uiScale = 1.0f) noexcept;
    void setHoverInspection(bool active) noexcept;
    ~NativeDebugRenderer();
private:
    bool installed_ = false;
};
}
