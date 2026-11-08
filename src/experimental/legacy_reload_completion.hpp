#pragma once
namespace conduit::experimental {
// Protected by the backend mutex; completion may run while the action pumps tasks.
class UiReloadCompletion {
public:
    bool begin() noexcept {
        if (inFlight()) return false;
        started_ = true; returned_ = false; loaded_ = false; return true;
    }
    void loaderCompleted() noexcept { if (started_) loaded_ = true; }
    void actionReturned() noexcept { if (started_) returned_ = true; }
    bool ready() const noexcept { return started_ && returned_ && loaded_; }
    bool actionInFlight() const noexcept { return started_ && !returned_; }
    bool inFlight() const noexcept { return started_ && !ready(); }
private:
    bool started_ = false, returned_ = false, loaded_ = false;
};
}
