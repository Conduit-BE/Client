#pragma once
#include <array>
#include <cstdint>

namespace conduit {
struct HoveredControl {
    using Name = std::array<char, 160>;
    Name name{};
    std::array<Name, 6> parents{}; // Immediate parent first.
    unsigned parentCount{};
    bool moreParents{};
    const char* type = "control"; // Static label for a verified runtime component.
    float x{}, y{}, width{}, height{};
    std::int64_t layer{};
    bool visible{};
};

using InspectHoveredControl = bool (*)(void* view, void* scene, float x, float y,
                                      HoveredControl& result) noexcept;
}
