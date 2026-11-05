#include "core/notifications.hpp"

#include <algorithm>
#include <utility>

namespace conduit {
void NotificationCenter::push(std::string title, std::string description,
                              std::chrono::steady_clock::time_point now,
                              std::chrono::milliseconds duration) {
    const std::lock_guard lock(mutex_);
    if (capacity_ == 0) { return; }
    while (pending_.size() >= capacity_) { pending_.pop_front(); }
    pending_.push_back({std::move(title), std::move(description), now + duration});
}

std::vector<Notification> NotificationCenter::snapshot(std::chrono::steady_clock::time_point now) {
    const std::lock_guard lock(mutex_);
    std::erase_if(pending_, [now](const Notification& item) { return item.expiresAt <= now; });
    auto result = debugPanels_;
    result.insert(result.end(), pending_.begin(), pending_.end());
    return result;
}

void NotificationCenter::setDebugPanels(std::vector<Notification> panels) {
    const std::lock_guard lock(mutex_);
    debugPanels_ = std::move(panels);
}
}
