#pragma once
#include <chrono>
namespace conduit {
// Shared by player and frontend producers. An inactive source must never steal
// the other producer's refresh interval (e.g. an empty player stack in menus).
class ScreenSampleGate {
public:
    using Clock = std::chrono::steady_clock;
    bool begin(Clock::time_point now, bool sourceActive = true) noexcept {
        if (!sourceActive) return false;
        if (sampled_ && (now < last_ || now-last_ < std::chrono::milliseconds(250))) return false;
        last_ = now; sampled_ = true;
        return true;
    }
private:
    Clock::time_point last_{};
    bool sampled_ = false;
};
}
