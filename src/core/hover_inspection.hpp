#pragma once
#include <array>
#include <cstdint>

namespace conduit {
struct HoveredControl {
    std::array<char, 160> name{};
    const char* type = "control"; // Static label for a verified runtime component.
    float x{}, y{}, width{}, height{};
    std::int64_t layer{};
    bool visible{};
};

using InspectHoveredControl = bool (*)(void* view, void* scene, float x, float y,
                                      HoveredControl& result) noexcept;
}
