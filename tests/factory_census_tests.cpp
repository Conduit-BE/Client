#include "core/factory_census.hpp"
#include <stdexcept>
#include <iostream>

void require(bool value) { if (!value) throw std::runtime_error("Factory census assertion failed"); }
int main() {
    conduit::FactoryCensus<8,16> census;
    const auto never = [] { return false; };
    auto result = census.finish(never);
    require(result.available && !result.instances && !result.liveRoots && !result.generatedControls);
    // Active root 1, generated branches 2/3 share 4, with a cycle back to 2.
    require(census.recordControl(1)); require(census.recordChild(2)); require(census.recordChild(3));
    require(census.recordControl(2)); require(census.recordChild(4));
    require(census.recordControl(3)); require(census.recordChild(4));
    require(census.recordControl(4)); require(census.recordChild(2));
    census.addFactory(); census.addFactory();
    census.addLiveRoot(2); census.addLiveRoot(2); census.addLiveRoot(3);
    census.addLiveRoot(99); census.addLiveRoot(0); // Other screen / expired / absent.
    result = census.finish(never);
    require(result.available && result.instances==2 && result.liveRoots==2 && result.generatedControls==3);
    require(census.finish(never).generatedControls==3);
    require(census.prepareInstanceIds(never));
    const auto firstPosition = census.instanceId(2);
    require(firstPosition && firstPosition != census.instanceId(3));
    require(census.instanceId(99)==0 && !census.prepareInstanceIds([] { return true; }));
    auto perFactory = census.finishFactory(never);
    require(perFactory.available && perFactory.liveRoots==2 && perFactory.generatedControls==3);
    census.addFactory(); // A second runtime instance can overlap an existing subtree.
    census.addLiveRoot(4);
    perFactory = census.finishFactory(never);
    require(perFactory.available && perFactory.liveRoots==1 && perFactory.generatedControls==2);
    result = census.finish(never);
    require(result.available && result.liveRoots==3 && result.generatedControls==3);
    census.addFactory(); // Empty/inactive factories remain instances with exact zero counts.
    perFactory = census.finishFactory(never);
    require(perFactory.available && perFactory.liveRoots==0 && perFactory.generatedControls==0);
    require(!census.finishFactory([] { return true; }).available);
    require(!census.finish([] { return true; }).available);
    census.clear(); require(census.size()==0 && !census.contains(2));
    // Next snapshot reflects removals and a new generated root.
    require(census.recordControl(1)); require(census.recordChild(5)); require(census.recordControl(5));
    census.addFactory(); census.addLiveRoot(5);
    require(census.prepareInstanceIds(never) && census.instanceId(5)==firstPosition);
    result=census.finish(never); require(result.available && result.liveRoots==1 && result.generatedControls==1);
    census.clear(); require(census.recordControl(1)); require(census.recordChild(99));
    census.addLiveRoot(1); require(!census.finish(never).available); // Incomplete active graph.
    require(!census.prepareInstanceIds(never));
    census.clear(); require(census.recordControl(1)); require(!census.recordControl(1));
    require(!census.finish(never).available);
    conduit::FactoryCensus<1,1> small;
    require(small.recordControl(1)); require(!small.recordControl(2)); require(!small.finish(never).available);
    small.clear(); require(small.recordControl(1)); require(small.recordChild(1));
    require(!small.recordChild(1)); require(!small.finish(never).available);
    small.clear(); require(!small.recordControl(0)); require(!small.finish(never).available);
    std::cout << "Factory provenance, roots/subtree union, cycles, dynamic snapshots and bounds passed\n";
}
