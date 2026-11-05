#pragma once

#include <windows.h>

namespace conduit::windows {
void scheduleAutoStart(HINSTANCE module) noexcept;
void signalProcessExit() noexcept;
void stopAutoStart() noexcept;
}
