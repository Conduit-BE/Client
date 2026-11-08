#pragma once
#include "core/hotkey.hpp"
#include <atomic>
#include <cstdint>
namespace conduit::standalone {
enum class Phase { Idle, Starting, Running, Failed, Disabled };
class Initialization {
public:
    bool begin() noexcept { auto expected = Phase::Idle; return phase_.compare_exchange_strong(expected, Phase::Starting); }
    void running() noexcept { auto expected = Phase::Starting; phase_.compare_exchange_strong(expected, Phase::Running); }
    void fail() noexcept {
        auto expected = Phase::Starting;
        if (!phase_.compare_exchange_strong(expected, Phase::Failed) && expected == Phase::Running)
            phase_.compare_exchange_strong(expected, Phase::Failed);
    }
    void disable() noexcept { phase_.store(Phase::Disabled); }
    Phase phase() const noexcept { return phase_.load(); }
private:
    std::atomic<Phase> phase_{Phase::Idle};
};
// Poll the physical chord even when unfocused so changing focus cannot synthesize a press.
class DeactivationRequest {
public:
    explicit DeactivationRequest(bool initiallyDown = false) : edge_(initiallyDown) {}
    bool update(bool chordDown, bool focused) noexcept {
        if (edge_.update(chordDown) && focused) requested_ = true;
        return requested_;
    }
private:
    RisingEdge edge_;
    bool requested_ = false;
};
// Used only under the backend mutex. No memory dereferences or native calls.
class FrameAffinity {
public:
    bool observe(std::uintptr_t owner, unsigned thread, bool verifiedCall) noexcept {
        if (!verifiedCall || !owner || !thread || conflicted_) return false;
        if (samples_ && (owner_ != owner || thread_ != thread)) { conflicted_ = true; return false; }
        owner_ = owner; thread_ = thread;
        if (samples_ < 3) ++samples_;
        return samples_ == 3;
    }
    bool conflicted() const noexcept { return conflicted_; }
private:
    std::uintptr_t owner_ = 0;
    unsigned thread_ = 0, samples_ = 0;
    bool conflicted_ = false;
};
}
