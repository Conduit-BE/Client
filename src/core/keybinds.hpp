#pragma once

#include <cstdint>

namespace conduit {
struct KeyChord {
    // Windows virtual-key code for the non-modifier key.
    std::uint8_t key;
    bool ctrl = false;
    bool shift = false;
    bool alt = false;

    bool operator==(const KeyChord&) const = default;
};

struct Keybinds {
    KeyChord diagnostics{0x72}; // F3
    KeyChord tooltip{0x74};     // F5
    KeyChord eject{'K', true};
    KeyChord reload{'R', true};
};
}
