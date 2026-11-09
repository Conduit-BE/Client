#pragma once
#include <array>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <mutex>

namespace conduit {
// Copied text only. Neither this mailbox nor its readers own native UI objects.
struct DebugPanelText {
    std::array<char, 8192> text{};
    std::uint64_t revision = 0;
    bool visible = false;
    float scale = 1.0f;
};
class DebugPanelMailbox {
public:
    void publish(const char* text, bool visible, float scale = 1.0f) {
        const std::lock_guard lock(mutex_);
        if (state_.visible == visible && state_.scale == scale && std::strcmp(state_.text.data(), text) == 0) return;
        const auto length = std::min(std::strlen(text), state_.text.size()-1);
        std::memcpy(state_.text.data(), text, length);
        state_.text[length] = 0;
        state_.visible = visible;
        state_.scale = scale;
        ++state_.revision;
    }
    // Rendering never waits for the worker, and releases the lock before native calls.
    bool tryRead(DebugPanelText& destination) const {
        const std::unique_lock lock(mutex_, std::try_to_lock);
        if (!lock.owns_lock()) return false;
        if (destination.revision != state_.revision) destination = state_;
        return true;
    }
private:
    mutable std::mutex mutex_;
    DebugPanelText state_;
};
struct DebugPanelLayout {
    static constexpr unsigned maxRows = 60;
    std::array<std::array<char, 256>, maxRows> rows{};
    unsigned count = 0;
    float left = 8, top = 8, width = 0, height = 0;
    float padding = 4, fontScale = 1.0f, lineHeight = 12;
};
// Width is provided by Minecraft's current font, not a guessed character advance.
// Called only when text or logical viewport changes. No Conduit heap allocations.
template<class Measure>
DebugPanelLayout layoutDebugPanel(const char* text, float viewportWidth, float viewportHeight, Measure measure, float scale = 1.0f) {
    DebugPanelLayout result;
    if (!std::isfinite(scale) || scale < 0.25f || scale > 4.0f) return result;
    result.fontScale = scale; result.lineHeight = 12*scale; result.padding = 4*scale;
    if (!std::isfinite(viewportWidth) || !std::isfinite(viewportHeight) || viewportWidth < 40 || viewportHeight < 38) return result;
    const float widthBudget = viewportWidth-2*result.left-2*result.padding;
    if (widthBudget <= 0 || viewportHeight-2*result.top-2*result.padding < result.lineHeight) return result;
    const auto rowBudget = std::min(DebugPanelLayout::maxRows,
        static_cast<unsigned>((viewportHeight-2*result.top-2*result.padding)/result.lineHeight));
    unsigned totalRows = 0, factoryRows = 0;
    for (auto* cursor = text; *cursor;) {
        ++totalRows;
        if (std::strncmp(cursor, "    ", 4) == 0) ++factoryRows;
        const auto* newline = std::strchr(cursor, '\n');
        if (!newline) break;
        cursor = newline+1;
    }
    const bool omitFactories = totalRows > rowBudget && factoryRows > 0;
    const unsigned keepFactories = rowBudget > totalRows-factoryRows+1 ? ((rowBudget-(totalRows-factoryRows)-1)/3)*3 : 0;
    unsigned seenFactories = 0;
    while (*text && result.count < rowBudget) {
        bool factoryNotice = false;
        const char* source = text;
        if (omitFactories && std::strncmp(text, "    ", 4) == 0 && seenFactories++ >= keepFactories) {
            do {
                const auto* newline = std::strchr(text, '\n');
                text = newline ? newline+1 : text+std::strlen(text);
            } while (std::strncmp(text, "    ", 4) == 0);
            source = "    ... more factories; see standalone.log";
            factoryNotice = true;
        }
        auto& row = result.rows[result.count];
        const auto* end = std::strchr(source, '\n');
        const auto length = end ? static_cast<std::size_t>(end-source) : std::strlen(source);
        auto size = std::min(length, row.size()-1);
        // Never cut inside a UTF-8 continuation sequence.
        while (size && (static_cast<unsigned char>(source[size]) & 0xc0) == 0x80) --size;
        std::memcpy(row.data(), source, size); row[size] = 0;
        const bool omittedRows = result.count+1 == rowBudget && ((end && end[1]) || (factoryNotice && *text));
        if (omittedRows) { std::strcpy(row.data(), "... more diagnostics; see standalone.log"); size = std::strlen(row.data()); }
        if (size < length || measure(row.data(), size) > widthBudget) {
            // Binary search bounds expensive native font measurements to <= 8 per clipped row.
            std::size_t low = 0, high = std::min(size, row.size()-4);
            const auto dots = measure("...", 3);
            while (low < high) {
                const auto middle = (low+high+1)/2;
                auto boundary = middle;
                while (boundary && (static_cast<unsigned char>(row[boundary]) & 0xc0) == 0x80) --boundary;
                if (measure(row.data(), boundary)+dots <= widthBudget) low = middle;
                else high = middle-1;
            }
            size = low;
            while (size && (static_cast<unsigned char>(row[size]) & 0xc0) == 0x80) --size;
            std::memcpy(row.data()+size, "...", 4); size += 3;
        }
        result.width = std::max(result.width, static_cast<float>(measure(row.data(), size))+2*result.padding);
        ++result.count;
        if (factoryNotice) continue;
        if (!end || omittedRows) break;
        text = end+1;
    }
    result.width = std::min(result.width, viewportWidth-2*result.left);
    if (result.count) result.height = result.count*result.lineHeight+2*result.padding;
    return result;
}
}
