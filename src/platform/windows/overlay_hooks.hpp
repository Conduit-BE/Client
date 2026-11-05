#pragma once

namespace conduit { class NotificationCenter; class Logger; }

namespace conduit::windows {
bool installOverlayHooks(NotificationCenter& notifications, Logger& logger) noexcept;
void removeOverlayHooks() noexcept;
bool overlayHasRendered() noexcept;
void logOverlayDiagnostics(Logger& logger) noexcept;
}
