#include "standalone/initialization.hpp"
#include <atomic>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <vector>
using namespace conduit::standalone;
void require(bool value) { if (!value) throw std::runtime_error("Standalone policy assertion failed"); }
int main() {
    try {
        Initialization lifecycle;
        std::atomic<unsigned> winners{0};
        std::vector<std::jthread> workers;
        for (unsigned i = 0; i < 16; ++i) workers.emplace_back([&] { if (lifecycle.begin()) ++winners; });
        workers.clear();
        require(winners == 1 && lifecycle.phase() == Phase::Starting);
        lifecycle.running(); require(lifecycle.phase() == Phase::Running && !lifecycle.begin());
        lifecycle.disable(); lifecycle.running(); lifecycle.fail();
        require(lifecycle.phase() == Phase::Disabled && !lifecycle.begin());
        Initialization failure;
        require(failure.begin()); failure.fail(); failure.running();
        require(failure.phase() == Phase::Failed && !failure.begin());
        Initialization runningFailure;
        require(runningFailure.begin()); runningFailure.running(); runningFailure.fail();
        require(runningFailure.phase() == Phase::Failed);
        FrameAffinity affinity;
        require(!affinity.observe(0x1000, 7, false));
        require(!affinity.observe(0, 7, true));
        require(!affinity.observe(0x1000, 0, true));
        require(!affinity.observe(0x1000, 7, true));
        require(!affinity.observe(0x1000, 7, true));
        require(affinity.observe(0x1000, 7, true));
        require(!affinity.observe(0x1000, 8, true) && affinity.conflicted());
        require(!affinity.observe(0x1000, 7, true));
        FrameAffinity changedOwner;
        require(!changedOwner.observe(0x1000, 7, true));
        require(!changedOwner.observe(0x2000, 7, true) && changedOwner.conflicted());
        std::cout << "Initialization concurrency, terminal failure/disable and verified frame affinity passed\n";
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
