#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

namespace conduit::standalone {

struct EmbeddedLogo {
    // Borrowed from the DLL's read-only resource section; valid only while loaded.
    std::span<const std::byte> png;
    std::uint32_t width{};
    std::uint32_t height{};
};

// Checks the resource size and PNG/IHDR header, not compressed image validity.
// Missing or malformed resources return an empty result without throwing.
[[nodiscard]] EmbeddedLogo embeddedLogo() noexcept;

} // namespace conduit::standalone
