#include "minecraft/compatibility.hpp"
#include "minecraft/versions/bedrock_1_26_60_5.hpp"
#include <atomic>
#include <stdexcept>
namespace conduit::minecraft {
namespace { std::atomic<const CompatibilityProfile*> selected{}; }
const CompatibilityProfile* selectCompatibility(const ExecutableIdentity& identity) noexcept {
    // Select by exact AMD64 image identity, independent of file version metadata.
    if (identity.machine != 0x8664) return nullptr;
    if (identity.sha256 == versions::bedrock126605.sha256 ||
        identity.sha256 == versions::bedrock126605LeviLauncherSha256)
        return &versions::bedrock126605;
    return nullptr;
}
const CompatibilityProfile* runningCompatibility() noexcept { return selected.load(std::memory_order_acquire); }
const CompatibilityProfile& requireRunningCompatibility() {
    if (const auto* profile = runningCompatibility()) return *profile;
    const auto identity = detectRunningExecutable();
    const auto* profile = selectCompatibility(identity);
    if (!profile) throw std::runtime_error("Unsupported Minecraft executable: requires verified Bedrock GDK x64 1.26.60.5 image");
    selected.store(profile, std::memory_order_release);
    return *profile;
}
}
