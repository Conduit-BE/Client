#include "core/control_count.hpp"
#include <map>
#include <vector>
#include <stdexcept>
#include <iostream>

void require(bool good) { if (!good) throw std::runtime_error("Control counting assertion failed"); }
int main() {
    using namespace conduit;
    std::map<std::uintptr_t,std::vector<std::uintptr_t>> graph;
    auto reader = [&](auto node, auto&& emit) {
        const auto it=graph.find(node);
        if (it==graph.end()) return ChildReadStatus::Unavailable;
        for (auto child:it->second) if (!emit(child)) return ChildReadStatus::Limited;
        return ChildReadStatus::Complete;
    };
    auto never=[] { return false; };
    ControlCounter<16,64> counter;
    auto result=counter.count(0,reader,never);
    require(result.status==ControlCountStatus::Complete && result.count==0);
    graph={{1,{}}};result=counter.count(1,reader,never);require(result.count==1 && result.status==ControlCountStatus::Complete);
    graph={{1,{2,3}},{2,{4}},{3,{5,6}},{4,{}},{5,{}},{6,{}}};
    result=counter.count(1,reader,never);require(result.status==ControlCountStatus::Complete && result.count==6);
    graph[3].push_back(4); // Shared descendant counted once.
    result=counter.count(1,reader,never);require(result.status==ControlCountStatus::Complete && result.count==6);
    graph[4]={1}; // Cycle to root; no depth-dependent recursion.
    result=counter.count(1,reader,never);require(result.status==ControlCountStatus::Complete && result.count==6);
    graph[5]={7};graph[7]={};result=counter.count(1,reader,never);require(result.count==7);
    graph[5].clear();graph.erase(7);result=counter.count(1,reader,never);require(result.count==6);
    ControlCounter<4,64> small;
    result=small.count(1,reader,never);require(result.status==ControlCountStatus::Incomplete && result.count==4);
    ControlCounter<16,2> fewEdges;
    result=fewEdges.count(1,reader,never);require(result.status==ControlCountStatus::Incomplete);
    result=counter.count(1,reader,[] { return true; });require(result.status==ControlCountStatus::Incomplete);
    graph[2].push_back(99);result=counter.count(1,reader,never);require(result.status==ControlCountStatus::Unavailable);
    graph[2]={0};result=counter.count(1,reader,never);require(result.status==ControlCountStatus::Unavailable);
    graph.clear();for (unsigned i=1;i<=16;++i) graph[i]=i==16 ? std::vector<std::uintptr_t>{1} : std::vector<std::uintptr_t>{i+1};
    result=counter.count(1,reader,never);require(result.status==ControlCountStatus::Complete && result.count==16);
    std::cout << "Empty/single/nested/branched/shared/cyclic/dynamic/limited/invalid control graphs passed\n";
}
