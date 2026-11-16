#pragma once

#include "core/hotkey.hpp"
#include "core/keybinds.hpp"
#include <optional>
#include <windows.h>

namespace conduit::windows {
class KeyChordPoller {
public:
    bool poll(KeyChord chord) noexcept {
        const auto down = [](int key) { return (GetAsyncKeyState(key) & 0x8000) != 0; };
        const bool pressed = down(chord.key) && down(VK_CONTROL) == chord.ctrl
            && down(VK_SHIFT) == chord.shift && down(VK_MENU) == chord.alt
            && !down(VK_LWIN) && !down(VK_RWIN);
        if (chord_ != chord) {
            chord_ = chord;
            edge_ = RisingEdge(pressed);
            return false;
        }
        return edge_.update(pressed);
    }

private:
    std::optional<KeyChord> chord_;
    RisingEdge edge_;
};
}
