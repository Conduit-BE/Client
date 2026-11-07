#include "core/presentation_rate.hpp"
#include "core/hotkey.hpp"
#include <cmath>
#include <iostream>
#include <stdexcept>

void require(bool condition) { if (!condition) throw std::runtime_error("Presentation rate assertion failed"); }
int main() {
    using namespace std::chrono;
    conduit::PresentationRate rate;
    auto now = conduit::PresentationRate::Clock::time_point(seconds(1));
    require(!rate.fps(now));
    for (int i=0; i<301; ++i) { rate.observe(now); now += milliseconds(10); }
    require(std::abs(*rate.fps(now)-100.0) < 0.001);
    // Ring replacement must remove old samples rather than accumulate indefinitely.
    for (int i=0; i<121; ++i) { rate.observe(now); now += milliseconds(20); }
    require(std::abs(*rate.fps(now)-50.0) < 0.001);
    require(!rate.fps(now+seconds(1)));
    now += seconds(1); rate.observe(now); require(!rate.fps(now));
    rate.reset(); require(!rate.fps(now));
    conduit::RisingEdge edge(true);
    require(!edge.update(true)); require(!edge.update(false));
    require(edge.update(true)); require(!edge.update(true));
    require(!edge.update(false)); require(edge.update(true));
    std::cout << "Presentation rate and F3 edge tests passed\n";
}
