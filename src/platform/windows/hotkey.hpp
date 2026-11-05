#pragma once

#include "core/hotkey.hpp"
#include <cstdint>
#include <memory>

namespace conduit::windows {
std::unique_ptr<Hotkey> makeHotkey(std::uint32_t virtualKey);
}
