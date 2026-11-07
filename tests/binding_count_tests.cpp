#include "core/binding_count.hpp"
#include <stdexcept>
#include <iostream>
#include <limits>
using namespace conduit;
void require(bool condition) { if (!condition) throw std::runtime_error("Binding count assertion failed"); }
int main() {
    std::array<BindingVector, 3> storage{};
    auto result = countBindingRecords(storage);
    require(result.status == ControlCountStatus::Complete && result.count == 0);
    storage[0] = {1000, 1264, 1528};
    storage[1] = {2000, 2528, 2792};
    storage[2] = {4000, 4792, 4792};
    require(countBindingRecords(storage).count == 6);
    storage[2].end -= 264; require(countBindingRecords(storage).count == 5);
    storage[2].end += 264; require(countBindingRecords(storage).count == 6);
    auto invalid = storage; invalid[0].end = 999;
    require(countBindingRecords(invalid).status == ControlCountStatus::Unavailable);
    invalid = storage; invalid[0].end++;
    require(countBindingRecords(invalid).status == ControlCountStatus::Unavailable);
    invalid = storage; invalid[0].capacity = 1000;
    require(countBindingRecords(invalid).status == ControlCountStatus::Unavailable);
    invalid = storage; invalid[0].begin = 0;
    require(countBindingRecords(invalid).status == ControlCountStatus::Unavailable);
    invalid = storage; invalid[1] = invalid[0];
    require(countBindingRecords(invalid).status == ControlCountStatus::Unavailable);
    invalid = {}; invalid[0] = {1000, 1000+264*65537, 1000+264*65537};
    require(countBindingRecords(invalid).status == ControlCountStatus::Incomplete);
    const auto neverStop = [] { return false; };
    std::array<ControlCount, 3> counts{{{ControlCountStatus::Complete, 0}, {ControlCountStatus::Complete, 4}, {ControlCountStatus::Complete, 2}}};
    const auto read = [&](std::size_t i) { return counts[i]; };
    result = countScreenBindings(counts.size(), read, neverStop);
    require(result.status == ControlCountStatus::Complete && result.count == 6);
    require(countScreenBindings(0, read, neverStop).count == 0);
    counts[1] = {}; require(countScreenBindings(3, read, neverStop).status == ControlCountStatus::Unavailable);
    counts[1] = {ControlCountStatus::Incomplete, 3};
    result = countScreenBindings(3, read, neverStop);
    require(result.status == ControlCountStatus::Incomplete && result.count == 3);
    counts[1] = {ControlCountStatus::Complete, 4};
    unsigned reads = 0;
    result = countScreenBindings(3, read, [&] { return ++reads > 2; });
    require(result.status == ControlCountStatus::Incomplete && result.count == 4);
    counts[1].count = std::numeric_limits<std::uint32_t>::max();
    result = countScreenBindings(3, read, neverStop);
    require(result.status == ControlCountStatus::Incomplete && result.count == counts[1].count);
    std::cout << "Owned binding partitions, changing counts, invalid/overlapping vectors, limits and unavailable samples passed\n";
}
