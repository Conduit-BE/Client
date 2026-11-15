#pragma once
#include <array>
#include <cstdint>

namespace conduit {
struct HoveredControl {
    using Type = std::array<char, 64>;
    using Name = std::array<char, 160>;
    Name name{};
    std::array<Name, 6> parents{}; // Immediate parent first.
    unsigned parentCount{};
    bool moreParents{};
    Type type{"unknown"};
    float x{}, y{}, width{}, height{};
    std::int64_t layer{};
    bool visible{};
};

using InspectHoveredControl = bool (*)(void* view, void* scene, float x, float y,
                                      HoveredControl& result) noexcept;
}
