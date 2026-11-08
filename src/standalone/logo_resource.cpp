#include "logo_resource.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace conduit::standalone {
namespace {
constexpr WORD logoResourceId = 104;
constexpr std::size_t maximumPngBytes = 4 * 1024 * 1024;

std::uint32_t readBigEndian(const unsigned char* bytes) noexcept {
    return (std::uint32_t{bytes[0]} << 24) | (std::uint32_t{bytes[1]} << 16) |
           (std::uint32_t{bytes[2]} << 8) | std::uint32_t{bytes[3]};
}
} // namespace

EmbeddedLogo embeddedLogo() noexcept {
    HMODULE module{};
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                               GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCWSTR>(&embeddedLogo), &module)) return {};
    const auto resource = FindResourceW(module, MAKEINTRESOURCEW(logoResourceId), MAKEINTRESOURCEW(10));
    if (!resource) return {};
    const auto size = SizeofResource(module, resource);
    if (size < 33 || size > maximumPngBytes) return {};
    const auto loaded = LoadResource(module, resource);
    const auto* bytes = static_cast<const unsigned char*>(LockResource(loaded));
    if (!bytes) return {};
    constexpr unsigned char signature[]{137, 80, 78, 71, 13, 10, 26, 10};
    for (std::size_t i = 0; i < sizeof(signature); ++i)
        if (bytes[i] != signature[i]) return {};
    if (readBigEndian(bytes + 8) != 13 || bytes[12] != 'I' || bytes[13] != 'H' ||
        bytes[14] != 'D' || bytes[15] != 'R') return {};
    const auto width = readBigEndian(bytes + 16);
    const auto height = readBigEndian(bytes + 20);
    if (!width || !height || width > 16384 || height > 16384) return {};
    return {{reinterpret_cast<const std::byte*>(bytes), size}, width, height};
}

} // namespace conduit::standalone
