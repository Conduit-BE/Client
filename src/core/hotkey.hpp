#pragma once

namespace conduit {
class Hotkey {
public:
    virtual ~Hotkey() = default;
    virtual bool pollPressed() noexcept = 0;
};

class RisingEdge {
public:
    explicit RisingEdge(bool initiallyDown = false) : wasDown_(initiallyDown) {}
    bool update(bool down) noexcept {
        const bool pressed = down && !wasDown_;
        wasDown_ = down;
        return pressed;
    }
private:
    bool wasDown_;
};
}
