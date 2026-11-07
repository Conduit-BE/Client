#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

namespace conduit {
struct FactoryBreakdown {
    std::array<char, 97> name{}, owner{};
    bool nameAvailable = false, ownerAvailable = false, nameTruncated = false, ownerTruncated = false;
    std::uint64_t instanceId = 0; // Structural position in this screen, not an address or definition ID.
    std::uint32_t instances = 1, liveRoots = 0, controls = 0, descendants = 0;
    bool operator==(const FactoryBreakdown&) const = default;
};
struct FactoryStatistics {
    bool available = false;
    std::uint32_t instances = 0, liveRoots = 0, generatedControls = 0;
    static constexpr std::size_t maximumDetails = 32;
    std::array<FactoryBreakdown, maximumDetails> details{};
    std::uint32_t detailCount = 0;
    bool detailsTruncated = false;
    bool operator==(const FactoryStatistics&) const = default;
};

// A borrowed, callback-local graph. Roots come from verified native factory
// provenance, never from control names. No allocation or native object ownership.
template<std::size_t NodeCapacity = 8192, std::size_t EdgeCapacity = 32768>
class FactoryCensus {
    static_assert(NodeCapacity > 0 && EdgeCapacity > 0);
    struct Node { std::uintptr_t object = 0; std::size_t begin = 0, count = 0; std::uint64_t path = 0; };
public:
    void clear() noexcept {
        nodes_.fill({}); edges_.fill(0); lookup_.fill(0);
        roots_.fill(false); currentRoots_.fill(false); generated_.fill(false); queue_.fill(0);
        size_ = edgeCount_ = instances_ = 0; valid_ = true;
    }
    bool recordControl(std::uintptr_t object) noexcept {
        if (!object || size_ == NodeCapacity || find(object) != NodeCapacity) return valid_ = false;
        auto slot = hash(object);
        while (lookup_[slot]) slot = (slot+1)%lookup_.size();
        nodes_[size_] = {object, edgeCount_, 0}; lookup_[slot] = ++size_;
        return true;
    }
    bool recordChild(std::uintptr_t child) noexcept {
        if (!size_ || !child || edgeCount_ == EdgeCapacity) return valid_ = false;
        edges_[edgeCount_++] = child; ++nodes_[size_-1].count;
        return true;
    }
    std::size_t size() const noexcept { return size_; }
    std::uintptr_t control(std::size_t index) const noexcept { return index < size_ ? nodes_[index].object : 0; }
    bool contains(std::uintptr_t object) const noexcept { return find(object) != NodeCapacity; }
    void addFactory() noexcept { ++instances_; currentRoots_.fill(false); }
    void addLiveRoot(std::uintptr_t object) noexcept {
        const auto index = find(object);
        if (index != NodeCapacity) roots_[index] = currentRoots_[index] = true; // Exclude other screens and duplicates.
    }
    template<class StopRequested>
    bool prepareInstanceIds(StopRequested&& stopped) noexcept {
        if (!valid_) return false;
        if (!size_) return true;
        nodes_[0].path = 14695981039346656037ull;
        // Nodes arrive in BFS order from the existing counter. First discovery
        // defines a structural position; cycles/shared references do not change it.
        for (std::size_t i=0; i<size_; ++i) {
            if (stopped() || !nodes_[i].path) return false;
            for (std::size_t edge=nodes_[i].begin; edge<nodes_[i].begin+nodes_[i].count; ++edge) {
                const auto child = find(edges_[edge]);
                if (child == NodeCapacity) return false;
                if (!nodes_[child].path) {
                    const auto ordinal = edge-nodes_[i].begin+1;
                    auto hash = nodes_[i].path;
                    for (unsigned byte=0; byte<sizeof(ordinal); ++byte)
                        hash = (hash ^ ((ordinal>>(byte*8))&255))*1099511628211ull;
                    nodes_[child].path = hash ? hash : 1;
                }
            }
        }
        return !stopped();
    }
    std::uint64_t instanceId(std::uintptr_t object) const noexcept {
        const auto index = find(object);
        return index == NodeCapacity ? 0 : nodes_[index].path;
    }
    template<class StopRequested>
    FactoryStatistics finishFactory(StopRequested&& stopped) noexcept { return countRoots(currentRoots_, stopped); }
    template<class StopRequested>
    FactoryStatistics finish(StopRequested&& stopped) noexcept { return countRoots(roots_, stopped); }
private:
    template<class StopRequested>
    FactoryStatistics countRoots(const std::array<bool, NodeCapacity>& selected, StopRequested&& stopped) noexcept {
        if (!valid_) return {};
        generated_.fill(false);
        std::size_t tail = 0, rootCount = 0;
        for (std::size_t i=0; i<size_; ++i) if (selected[i]) {
            generated_[i] = true; queue_[tail++] = i; ++rootCount;
        }
        for (std::size_t head=0; head<tail; ++head) {
            if (stopped()) return {};
            const auto& node = nodes_[queue_[head]];
            for (std::size_t edge=node.begin; edge<node.begin+node.count; ++edge) {
                if (stopped()) return {};
                const auto child = find(edges_[edge]);
                if (child == NodeCapacity) return {};
                if (!generated_[child]) { generated_[child] = true; queue_[tail++] = child; }
            }
        }
        if (stopped()) return {};
        return {true, static_cast<std::uint32_t>(instances_), static_cast<std::uint32_t>(rootCount),
            static_cast<std::uint32_t>(tail)};
    }
    std::size_t hash(std::uintptr_t object) const noexcept { return (object^(object>>4))%lookup_.size(); }
    std::size_t find(std::uintptr_t object) const noexcept {
        if (!object) return NodeCapacity;
        auto slot = hash(object);
        while (lookup_[slot]) {
            const auto index = lookup_[slot]-1;
            if (nodes_[index].object == object) return index;
            slot = (slot+1)%lookup_.size();
        }
        return NodeCapacity;
    }
    std::array<Node, NodeCapacity> nodes_{};
    std::array<std::uintptr_t, EdgeCapacity> edges_{};
    std::array<std::size_t, NodeCapacity*2> lookup_{};
    std::array<bool, NodeCapacity> roots_{}, currentRoots_{}, generated_{};
    std::array<std::size_t, NodeCapacity> queue_{};
    std::size_t size_ = 0, edgeCount_ = 0, instances_ = 0;
    bool valid_ = true;
};
}
