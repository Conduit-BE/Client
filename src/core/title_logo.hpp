#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>
#include <string_view>
namespace conduit {
struct LogoRectangle { float x, y, width, height; };
inline std::optional<LogoRectangle> containTitleLogo(LogoRectangle bounds, float width, float height) noexcept {
    if (!std::isfinite(bounds.x) || !std::isfinite(bounds.y) || !std::isfinite(bounds.width) ||
        !std::isfinite(bounds.height) || !std::isfinite(width) || !std::isfinite(height) ||
        bounds.width <= 0 || bounds.height <= 0 || width <= 0 || height <= 0 ||
        bounds.width > 32768 || bounds.height > 32768) return std::nullopt;
    const float scale = std::min(bounds.width / width, bounds.height / height);
    const float resultWidth = width * scale, resultHeight = height * scale;
    return LogoRectangle{bounds.x + (bounds.width-resultWidth)*0.5f,
        bounds.y + (bounds.height-resultHeight)*0.5f, resultWidth, resultHeight};
}
inline bool isVanillaTitleImage(bool activeTitle, bool vanillaProvenance, std::string_view control,
    std::uint32_t filesystem, std::string_view source, unsigned width, unsigned height) noexcept {
    return activeTitle && vanillaProvenance && control == "title_image" && filesystem == 0 &&
        source == "textures/ui/title" && width == 1937 && height == 333;
}
// Verified constructor formula; see 1.26.60-image-submission.md.
inline std::uint64_t resourceLocationHash(std::uint8_t filesystem, std::uint64_t pathHash) noexcept {
    return ((std::uint64_t{filesystem} ^ 0xcbf29ce484222325ULL) * 0x100000001b3ULL) ^ pathHash;
}
}
