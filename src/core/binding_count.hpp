#pragma once
#include "core/control_count.hpp"
#include <array>
#include <limits>

namespace conduit {
struct BindingVector { std::uintptr_t begin = 0, end = 0, capacity = 0; };
static_assert(sizeof(BindingVector) == 24);
// Exact native record/storage evidence: docs/reverse-engineering/1.26.60-ui-bindings.md.
// Descriptors only: never dereferences records or evaluates a binding.
inline ControlCount countBindingRecords(const std::array<BindingVector, 3>& vectors) noexcept {
    constexpr std::uintptr_t recordBytes = 264, maximumRecordsPerVector = 65536;
    std::uint64_t total = 0;
    for (std::size_t i = 0; i < vectors.size(); ++i) {
        const auto& value = vectors[i];
        if (value.end < value.begin || value.capacity < value.end ||
            (!value.begin && (value.end || value.capacity)) ||
            (value.end-value.begin)%recordBytes || (value.capacity-value.begin)%recordBytes) return {};
        const auto count = (value.end-value.begin)/recordBytes;
        if (count > maximumRecordsPerVector) return {ControlCountStatus::Incomplete, 0};
        for (std::size_t j = 0; j < i; ++j) {
            const auto& other = vectors[j];
            if (value.begin < value.end && other.begin < other.end &&
                value.begin < other.end && other.begin < value.end) return {};
        }
        total += count;
    }
    return {ControlCountStatus::Complete, static_cast<std::uint32_t>(total)};
}
// The caller supplies the existing deduplicated active-screen control list.
// Missing components are verified zeroes, failed reads invalidate the metric.
template<class ReadCount, class StopRequested>
ControlCount countScreenBindings(std::size_t controls, ReadCount readCount, StopRequested stopRequested) noexcept {
    std::uint64_t total = 0;
    for (std::size_t index = 0; index < controls; ++index) {
        if (stopRequested()) return {ControlCountStatus::Incomplete, static_cast<std::uint32_t>(total)};
        const auto value = readCount(index);
        if (value.status == ControlCountStatus::Unavailable) return {};
        if (total+value.count > std::numeric_limits<std::uint32_t>::max())
            return {ControlCountStatus::Incomplete, static_cast<std::uint32_t>(total)};
        total += value.count;
        if (value.status == ControlCountStatus::Incomplete)
            return {ControlCountStatus::Incomplete, static_cast<std::uint32_t>(total)};
    }
    return {ControlCountStatus::Complete, static_cast<std::uint32_t>(total)};
}
}
