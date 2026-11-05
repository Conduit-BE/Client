#pragma once

#include <chrono>
#include <cstddef>
#include <deque>
#include <mutex>
#include <string>
#include <vector>

namespace conduit {
struct Notification {
    std::string title;
    std::string description;
    std::chrono::steady_clock::time_point expiresAt;
};

class NotificationCenter {
public:
    explicit NotificationCenter(std::size_t capacity = 4) : capacity_(capacity) {}
    void push(std::string title, std::string description,
              std::chrono::steady_clock::time_point now,
              std::chrono::milliseconds duration = std::chrono::milliseconds(3500));
    std::vector<Notification> snapshot(std::chrono::steady_clock::time_point now);
    // Replaced atomically by the diagnostics worker; copied by the render thread.
    void setDebugPanels(std::vector<Notification> panels);
private:
    const std::size_t capacity_;
    std::mutex mutex_;
    std::deque<Notification> pending_;
    std::vector<Notification> debugPanels_;
};
}
