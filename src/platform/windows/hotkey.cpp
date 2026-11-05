#include "platform/windows/hotkey.hpp"

#include <windows.h>
#include <limits>

namespace conduit::windows {
namespace {
class WindowsHotkey final : public Hotkey {
public:
    explicit WindowsHotkey(std::uint32_t virtualKey)
        : virtualKey_(virtualKey == 0 ? static_cast<std::uint32_t>('R') : virtualKey), edge_(isDown()) {}
    bool pollPressed() noexcept override { return edge_.update(isDown()); }
private:
    bool isDown() const noexcept {
        return virtualKey_ != std::numeric_limits<std::uint32_t>::max()
            && (GetAsyncKeyState(static_cast<int>(virtualKey_)) & 0x8000) != 0
            && (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
    }
    std::uint32_t virtualKey_;
    RisingEdge edge_;
};
}

std::unique_ptr<Hotkey> makeHotkey(std::uint32_t virtualKey) {
    return std::make_unique<WindowsHotkey>(virtualKey);
}
}
