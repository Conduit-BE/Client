#pragma once
#include <array>
#include <chrono>
#include <cstddef>
#include <optional>

namespace conduit {
// Fixed storage, successful presentation intervals only. Caller serializes access.
class PresentationRate {
public:
    using Clock = std::chrono::steady_clock;
    void reset() noexcept { samples_ = cursor_ = 0; sum_ = 0; previous_ = {}; }
    void observe(Clock::time_point now) noexcept {
        if (previous_ != Clock::time_point{}) {
            const double delta = std::chrono::duration<double>(now - previous_).count();
            if (delta > 0 && delta < 0.5) {
                if (samples_ == intervals_.size()) sum_ -= intervals_[cursor_]; else ++samples_;
                intervals_[cursor_] = delta; sum_ += delta;
                cursor_ = (cursor_ + 1) % intervals_.size();
            } else reset();
        }
        previous_ = now;
    }
    std::optional<double> fps(Clock::time_point now) const noexcept {
        if (samples_ < 8 || sum_ <= 0 || now < previous_ ||
            now - previous_ >= std::chrono::milliseconds(500)) return std::nullopt;
        return static_cast<double>(samples_) / sum_;
    }
private:
    std::array<double, 120> intervals_{};
    std::size_t samples_{}, cursor_{};
    double sum_{};
    Clock::time_point previous_{};
};
}
