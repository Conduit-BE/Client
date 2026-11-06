#include "core/screen_metrics.hpp"
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <atomic>

void require(bool value) { if (!value) throw std::runtime_error("Screen snapshot assertion failed"); }
int main() {
    using namespace std::chrono;
    conduit::ScreenMetrics metrics;
    const auto now = conduit::ScreenMetrics::Clock::now();
    require(std::string(metrics.snapshot(now).data()) == "Unavailable");
    std::string source = "hud_screen";
    require(metrics.publish(source, now)); source.assign("changed");
    require(std::string(metrics.snapshot(now).data()) == "hud_screen");
    require(std::string(metrics.snapshot(now + milliseconds(999)).data()) == "hud_screen");
    require(std::string(metrics.snapshot(now + seconds(1)).data()) == "Unavailable");
    require(std::string(metrics.snapshot(now - milliseconds(1)).data()) == "Unavailable");
    require(metrics.publish("pause_screen", now));
    require(std::string(metrics.snapshot(now).data()) == "pause_screen");
    metrics.publish("", now); require(std::string(metrics.snapshot(now).data()) == "Unavailable");
    metrics.publish("bad\nname", now); require(std::string(metrics.snapshot(now).data()) == "Unavailable");
    metrics.publish(std::string(193, 'a'), now); require(std::string(metrics.snapshot(now).data()) == "Unavailable");
    require(metrics.publish(std::string(192, 'a'), now)); require(metrics.snapshot(now).back() == 0);
    require(metrics.invalidate()); require(std::string(metrics.snapshot(now).data()) == "Unavailable");
    require(metrics.publish("hud_screen", now, {conduit::ControlCountStatus::Complete, 1033}, 123));
    auto combined = metrics.diagnostics(now);
    require(std::string(combined.name.data()) == "hud_screen" && combined.elements.count == 1033 &&
        combined.elements.status == conduit::ControlCountStatus::Complete && combined.collectionMicros == 123);
    require(metrics.publish("pause_screen", now));
    require(metrics.diagnostics(now).elements.status == conduit::ControlCountStatus::Unavailable);
    require(metrics.publish("pause_screen", now, {conduit::ControlCountStatus::Incomplete, 4}));
    require(metrics.diagnostics(now).elements.status == conduit::ControlCountStatus::Incomplete);
    require(metrics.diagnostics(now+seconds(1)).elements.status == conduit::ControlCountStatus::Unavailable);
    metrics.invalidate();
    require(metrics.diagnostics(now).elements.status == conduit::ControlCountStatus::Unavailable);
    require(metrics.publish("empty_test_screen", now, {conduit::ControlCountStatus::Complete, 0}));
    require(metrics.diagnostics(now).elements.status == conduit::ControlCountStatus::Complete && metrics.diagnostics(now).elements.count == 0);
    metrics.invalidate();
    std::atomic<bool> valid{true};
    std::jthread publisher([&] { for (int i=0; i<10000; ++i) metrics.publish(i%2 ? "hud_screen" : "pause_screen", now); });
    std::jthread reader([&] {
        for (int i=0; i<10000; ++i) {
            const std::string name = metrics.snapshot(now).data();
            if (name != "hud_screen" && name != "pause_screen" && name != "Unavailable") valid = false;
        }
    });
    publisher.join(); reader.join(); require(valid);
    std::cout << "Copied screen snapshot, expiry, invalidation and concurrent access passed\n";
}
