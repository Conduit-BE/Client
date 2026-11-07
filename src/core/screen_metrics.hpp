#pragma once
#include "core/control_count.hpp"
#include "core/factory_census.hpp"
#include <array>
#include <chrono>
#include <cstring>
#include <mutex>
#include <string_view>

namespace conduit {
// Copied text only: never contains a Minecraft object, string pointer or UI control.
class ScreenMetrics {
public:
    using Clock = std::chrono::steady_clock;
    static constexpr std::size_t maximumNameBytes = 192;
    using Name = std::array<char, maximumNameBytes + 1>;
    struct Diagnostics {
        Name name{};
        ControlCount elements{};
        std::uint32_t collectionMicros = 0;
        FactoryStatistics factories{};
        ControlCount bindings{};
    };
    bool publish(std::string_view name, Clock::time_point now, ControlCount elements = {},
                 std::uint32_t collectionMicros = 0, FactoryStatistics factories = {}, ControlCount bindings = {}) noexcept {
        if (name.empty() || name.size() > maximumNameBytes) return invalidate();
        for (const unsigned char ch : name) {
            if (ch < 0x20 || ch == 0x7f) return invalidate();
        }
        const std::unique_lock lock(mutex_, std::try_to_lock);
        if (!lock.owns_lock()) return false;
        name_.fill(0);
        std::memcpy(name_.data(), name.data(), name.size());
        elements_ = elements;
        collectionMicros_ = collectionMicros;
        factories_ = factories;
        bindings_ = bindings;
        sampled_ = now;
        available_ = true;
        return true;
    }
    bool invalidate() noexcept {
        const std::unique_lock lock(mutex_, std::try_to_lock);
        if (!lock.owns_lock()) return false;
        available_ = false;
        return true;
    }
    Name snapshot(Clock::time_point now) const noexcept { return diagnostics(now).name; }
    Diagnostics diagnostics(Clock::time_point now) const noexcept {
        const std::lock_guard lock(mutex_);
        if (!available_ || now < sampled_ || now-sampled_ >= std::chrono::seconds(1)) {
            Diagnostics unavailable{};
            constexpr char text[] = "Unavailable";
            std::memcpy(unavailable.name.data(), text, sizeof(text));
            return unavailable;
        }
        return {name_, elements_, collectionMicros_, factories_, bindings_};
    }

private:
    mutable std::mutex mutex_;
    Name name_{};
    ControlCount elements_{};
    std::uint32_t collectionMicros_ = 0;
    FactoryStatistics factories_{};
    ControlCount bindings_{};
    Clock::time_point sampled_{};
    bool available_ = false;
};
}
