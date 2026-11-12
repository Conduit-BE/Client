#include "minecraft/compatibility.hpp"
#include "minecraft/versions/bedrock_1_26_60_5.hpp"
#include "minecraft/versions/bedrock_1_26_62_1.hpp"
#include "minecraft/versions/bedrock_1_26_63_1.hpp"
#include <atomic>
#include <stdexcept>
#ifdef _WIN32
#include <windows.h>
#include <cstring>
#endif
namespace conduit::minecraft {
namespace { std::atomic<const CompatibilityProfile*> selected{}; }
const CompatibilityProfile* selectCompatibility(const ExecutableIdentity& identity) noexcept {
    if (identity.machine != 0x8664 || !identity.hasFileVersion) return nullptr;
    if (identity.fileVersion == versions::bedrock126605.fileVersion)
        return &versions::bedrock126605;
    if (identity.fileVersion == versions::bedrock126621.fileVersion)
        return &versions::bedrock126621;
    if (identity.fileVersion == versions::bedrock126631.fileVersion)
        return &versions::bedrock126631;
    return nullptr;
}
const CompatibilityProfile* runningCompatibility() noexcept { return selected.load(std::memory_order_acquire); }
const CompatibilityProfile& requireRunningCompatibility() {
    if (const auto* profile = runningCompatibility()) return *profile;
    const auto identity = detectRunningExecutable();
    const auto* profile = selectCompatibility(identity);
    if (!profile) throw std::runtime_error("Unsupported Minecraft executable: requires a supported Bedrock GDK x64 file version");
    selected.store(profile, std::memory_order_release);
    return *profile;
}
#ifdef _WIN32
bool matchesMainExecutableCode(std::uintptr_t rva, std::span<const unsigned char> expected) noexcept {
    const auto module = GetModuleHandleW(nullptr);
    if (!module || !rva || expected.empty()) return false;
    const auto address = reinterpret_cast<std::uintptr_t>(module) + rva;
    const auto* code = reinterpret_cast<const void*>(address);
    MEMORY_BASIC_INFORMATION region{};
    if (VirtualQuery(code, &region, sizeof(region)) != sizeof(region) || region.State != MEM_COMMIT)
        return false;
    switch (region.Protect) {
        case PAGE_EXECUTE_READ:
        case PAGE_EXECUTE_READWRITE:
        case PAGE_EXECUTE_WRITECOPY:
            break;
        default:
            return false;
    }
    const auto offset = address - reinterpret_cast<std::uintptr_t>(region.BaseAddress);
    return offset < region.RegionSize && expected.size() <= region.RegionSize - offset &&
        std::memcmp(code, expected.data(), expected.size()) == 0;
}
#endif
}
