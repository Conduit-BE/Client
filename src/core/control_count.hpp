#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <algorithm>

namespace conduit {
enum class ControlCountStatus { Unavailable, Complete, Incomplete };
struct ControlCount {
    ControlCountStatus status = ControlCountStatus::Unavailable;
    std::uint32_t count = 0;
};
enum class ChildReadStatus { Complete, Unavailable, Limited };

// Scratch is reusable but never retains handles after count() returns. A native
// adapter supplies live children; this algorithm owns no game objects.
template<std::size_t NodeCapacity = 8192, std::size_t EdgeCapacity = 32768>
class ControlCounter {
    static_assert(NodeCapacity > 0 && EdgeCapacity > 0);
public:
    template<class ReadChildren, class StopRequested>
    ControlCount count(std::uintptr_t root, ReadChildren&& readChildren, StopRequested&& stopRequested) noexcept {
        struct ClearOnExit {
            ControlCounter& self;
            ~ClearOnExit() { self.queue_.fill(0); self.visited_.fill(0); }
        } clear{*this};
        visited_.fill(0);
        if (!root) return {ControlCountStatus::Complete, 0};
        std::size_t discovered = 0, edges = 0;
        bool limited = false, invalid = false;
        const auto add = [&](std::uintptr_t node) {
            if (!node) { invalid = true; return false; }
            std::size_t slot = (node ^ (node >> 4)) % visited_.size();
            while (visited_[slot]) {
                if (visited_[slot] == node) return true;
                slot = (slot + 1) % visited_.size();
            }
            if (discovered == NodeCapacity) { limited = true; return false; }
            visited_[slot] = node;
            queue_[discovered++] = node;
            return true;
        };
        add(root);
        for (std::size_t index = 0; index < discovered; ++index) {
            if (stopRequested()) return {ControlCountStatus::Incomplete, static_cast<std::uint32_t>(discovered)};
            const auto status = readChildren(queue_[index], [&](std::uintptr_t child) {
                if (edges == EdgeCapacity || stopRequested()) { limited = true; return false; }
                ++edges;
                return add(child);
            });
            if (invalid || status == ChildReadStatus::Unavailable) return {};
            if (limited || status == ChildReadStatus::Limited)
                return {ControlCountStatus::Incomplete, static_cast<std::uint32_t>(discovered)};
        }
        return {ControlCountStatus::Complete, static_cast<std::uint32_t>(discovered)};
    }
private:
    std::array<std::uintptr_t, NodeCapacity> queue_{};
    std::array<std::uintptr_t, NodeCapacity * 2> visited_{};
};
}
