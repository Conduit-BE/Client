#include "core/binding_count.hpp"
#include "core/screen_sampling.hpp"
// Build-specific opt-in prototype. Evidence and ABI: docs/reverse-engineering/1.26.60-legacy-reload-path.md
#include "experimental/legacy_reload.hpp"
#include "core/logger.hpp"
#include "standalone/initialization.hpp"
#ifdef _MSC_VER
#include <intrin.h>
#endif
#include <windows.h>
#include "minecraft/compatibility.hpp"
#include <MinHook.h>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <filesystem>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <vector>
#include <limits>
#include <bit>

namespace conduit::experimental {
namespace {
const minecraft::CompatibilityProfile* profile{};
thread_local void* borrowedFrameOwner{};
// Verified natural caller: legacy-prototype-20261106/events.jsonl; versioned reload note.
// Complete native button.reload_ui_definitions path; see versioned UI-only evidence.
using UiReloadCallback = void (*)(void*, void*, void*);
// Observed natural primary-client stack name accessor; exact ABI and callers:
// docs/reverse-engineering/1.26.60-native-screen.md
constexpr std::uint64_t maximumNativeStringCapacity = 1024 * 1024;
using ScreenNameMethod = void* (*)(void*, void*);
using OwnerMethod = void (*)(void*);
using CompletionMethod = void (*)(void*, void*);
struct State {
    std::mutex mutex;
    std::condition_variable wake;
    bool enabled = false, installed = false, pending = false, busy = false, faulted = false;
    bool automatic = false, automaticFrame = false;
    standalone::FrameAffinity affinity;
    UiReloadCompletion uiCompletion;
    std::unique_ptr<Logger> diagnostics;
    std::uintptr_t owner = 0, base = 0;
    DWORD gameThread = 0, observedThread = 0;
    std::uint64_t submitted = 0, completed = 0, natural = 0, generation = 0, finished = 0;
    ScreenMetrics screenMetrics;
    std::atomic<DWORD> screenThread{0};
    ScreenNameMethod screenName = nullptr;
    OwnerMethod loader = nullptr, frame = nullptr;
    CompletionMethod completion = nullptr;
};
// Resident process-lifetime hooks never reference a Client or its logger.
State state;
thread_local bool injectedLoader = false;
void diagnostic(std::string message) noexcept {
    if (state.diagnostics) state.diagnostics->write("experimental", message + " thread=" + std::to_string(GetCurrentThreadId()));
}

bool readable(const void* p, std::size_t size) noexcept {
    MEMORY_BASIC_INFORMATION info{};
    if (!p || VirtualQuery(p, &info, sizeof(info)) != sizeof(info) || info.State != MEM_COMMIT ||
        (info.Protect & (PAGE_GUARD | PAGE_NOACCESS))) return false;
    const auto start = reinterpret_cast<std::uintptr_t>(p);
    const auto end = reinterpret_cast<std::uintptr_t>(info.BaseAddress) + info.RegionSize;
    return start <= end && size <= end - start;
}
std::uintptr_t pointerAt(std::uintptr_t p) noexcept {
    if (!readable(reinterpret_cast<void*>(p), sizeof(std::uintptr_t))) return 0;
    std::uintptr_t result{};
    std::memcpy(&result, reinterpret_cast<void*>(p), sizeof(result));
    return result;
}
bool validOwner(void* owner) noexcept {
    const auto p = reinterpret_cast<std::uintptr_t>(owner);
    if (pointerAt(p) != state.base + profile->reload.ownerTableRva) return false;
    if (pointerAt(state.base + profile->reload.ownerTableRva + profile->layout.ownerLoaderSlot) != state.base + profile->reload.loaderRva) return false;
    const auto service = pointerAt(p + profile->layout.ownerService), context = pointerAt(p + profile->layout.ownerContext);
    return service && context && pointerAt(service) == state.base + profile->reload.serviceTableRva &&
           pointerAt(pointerAt(service) + 8) == state.base + profile->reload.serviceMethodRva &&
           pointerAt(context) == state.base + profile->reload.contextTableRva;
}
bool liveGuard(std::uintptr_t address) noexcept {
    if (!readable(reinterpret_cast<void*>(address), 1)) return false;
    unsigned char value{};
    std::memcpy(&value, reinterpret_cast<void*>(address), 1);
    return value != 0;
}
std::uintptr_t validatedUiContext(void* currentOwner) noexcept {
    const auto owner = reinterpret_cast<std::uintptr_t>(currentOwner);
    // Native completion traverses this exact map and passes node+0x38 as RCX.
    // Restrict this first experiment to one client, rather than guessing a split-screen context.
    if (pointerAt(owner + profile->layout.ownerClientCount) != 1) return 0;
    const auto head = pointerAt(owner + profile->layout.ownerClientMap), node = pointerAt(head);
    if (!head || !node || node == head) return 0;
    const auto client = pointerAt(node + profile->layout.clientMapValue), table = pointerAt(client);
    if (table != state.base + profile->reload.clientTableRva || pointerAt(table + profile->layout.clientOwnerSlot) != state.base + profile->reload.clientOwnerGetterRva ||
        pointerAt(table + profile->layout.clientCacheSlot) != state.base + profile->reload.clientCacheGetterRva || pointerAt(client + profile->layout.clientOwner) != owner ||
        !liveGuard(pointerAt(client + 8)) || !pointerAt(client + profile->layout.clientGuardContext)) return 0;
    const auto service = pointerAt(owner + profile->layout.ownerService), serviceTable = pointerAt(service);
    if (pointerAt(state.base + profile->reload.ownerTableRva + profile->layout.ownerWaitSlot) != state.base + profile->reload.ownerWaitRva ||
        pointerAt(serviceTable + profile->layout.serviceWaitSlot) != state.base + profile->reload.uiWaitRva ||
        !liveGuard(pointerAt(service + profile->layout.serviceGuard)) || !pointerAt(service + profile->layout.serviceGuardContext)) return 0;
    const auto cache = pointerAt(client + profile->layout.clientCache), tree = pointerAt(cache + profile->layout.cacheTree);
    if (!readable(reinterpret_cast<void*>(cache), profile->layout.minimumCacheBytes) || !readable(reinterpret_cast<void*>(tree), profile->layout.minimumTreeBytes)) return 0;
    unsigned char sentinel{};
    std::memcpy(&sentinel, reinterpret_cast<void*>(tree + profile->layout.treeSentinel), 1);
    if (sentinel != 1 || !pointerAt(tree) || !pointerAt(tree + 8) || !pointerAt(tree + profile->layout.treeRight)) return 0;
    return client;
}
bool copyMemory(const void* source, void* target, std::size_t size) noexcept {
    SIZE_T copied{};
    return source && ReadProcessMemory(GetCurrentProcess(), source, target, size, &copied) && copied == size;
}
// Exact-build ownership/traversal evidence: docs/reverse-engineering/1.26.60-ui-controls.md.
constexpr std::size_t maximumControlEdges = 32768;
constexpr auto controlSampleInterval = std::chrono::seconds(1);
constexpr auto controlSampleBudget = std::chrono::milliseconds(5);
ControlCounter<8192, maximumControlEdges> controlCounter; // Only the verified UI thread uses scratch.
// Evidence/semantics: docs/reverse-engineering/1.26.60-ui-factories.md.
FactoryCensus<8192, maximumControlEdges> factoryCensus;
struct NativeVector { std::uintptr_t begin, end, capacity; };
struct SharedControl { std::uintptr_t object, owner; };
static_assert(sizeof(SharedControl) == 16);
bool readAt(std::uintptr_t object, std::size_t offset, void* output, std::size_t size) noexcept {
    return object && object <= std::numeric_limits<std::uintptr_t>::max()-offset &&
        copyMemory(reinterpret_cast<const void*>(object+offset), output, size);
}
std::uintptr_t readPointerAt(std::uintptr_t object, std::size_t offset = 0) noexcept {
    std::uintptr_t result{};
    return readAt(object, offset, &result, sizeof(result)) ? result : 0;
}
struct CollectedControls { ControlCount elements; std::uint32_t micros = 0; FactoryStatistics factories{}; std::uint32_t totalMicros = 0; ControlCount bindings{}; };
template<std::size_t Size>
bool copyControlName(std::uintptr_t object, std::array<char, Size>& text, bool& truncated) noexcept {
    struct NativeString { std::array<char, 16> storage; std::uint64_t size, capacity; } value{};
    if (!readAt(object, profile->layout.controlName, &value, sizeof(value)) || value.size > value.capacity ||
        value.size > ScreenMetrics::maximumNameBytes || value.capacity > maximumNativeStringCapacity ||
        (value.capacity < 16 && value.size > 15)) return false;
    const auto bytes = std::min(static_cast<std::size_t>(value.size), Size-1);
    if (value.capacity < 16) std::memcpy(text.data(), value.storage.data(), bytes);
    else {
        std::uintptr_t source{};
        std::memcpy(&source, value.storage.data(), sizeof(source));
        if (bytes && !readAt(source, 0, text.data(), bytes)) { text.fill(0); return false; }
    }
    for (std::size_t i=0; i<bytes; ++i) if (static_cast<unsigned char>(text[i]) < 0x20 || text[i] == 0x7f) {
        text.fill(0); return false;
    }
    truncated = value.size > bytes;
    return true;
}
FactoryStatistics collectFactories() noexcept {
    const auto deadline = ScreenMetrics::Clock::now()+controlSampleBudget;
    const auto stopped = [&] { return ScreenMetrics::Clock::now() >= deadline; };
    std::uint64_t factoryBit{};
    if (!readAt(state.base, profile->reload.factoryComponentMaskRva, &factoryBit, sizeof(factoryBit)) ||
        !std::has_single_bit(factoryBit)) return {};
    if (!factoryCensus.prepareInstanceIds(stopped)) return {};
    FactoryStatistics breakdown{};
    std::size_t inspectedRoots = 0;
    for (std::size_t index=0; index<factoryCensus.size(); ++index) {
        if (stopped()) return {};
        const auto control = factoryCensus.control(index);
        struct Components { std::uint64_t mask; NativeVector entries; } components{};
        static_assert(sizeof(Components) == 32);
        if (!readAt(control, profile->layout.controlComponents, &components, sizeof(components))) return {};
        if (!(components.mask & factoryBit)) continue;
        const auto& entries = components.entries;
        if (entries.end < entries.begin || entries.capacity < entries.end ||
            (entries.end-entries.begin)%sizeof(std::uintptr_t)) return {};
        const auto count = (entries.end-entries.begin)/sizeof(std::uintptr_t);
        const auto rank = static_cast<std::size_t>(std::popcount(components.mask & (factoryBit-1)));
        if (count > 128 || rank >= count) return {};
        const auto component = readPointerAt(entries.begin, rank*sizeof(std::uintptr_t));
        if (readPointerAt(component) != state.base+profile->reload.factoryComponentTableRva) return {};
        if (readPointerAt(component, 8) != control) return {};
        factoryCensus.addFactory();
        NativeVector roots{};
        if (!readAt(component, profile->layout.factoryRoots, &roots, sizeof(roots)) || roots.end < roots.begin ||
            roots.capacity < roots.end || (roots.end-roots.begin)%sizeof(SharedControl)) return {};
        const auto rootCount = (roots.end-roots.begin)/sizeof(SharedControl);
        if (rootCount > 8192 || inspectedRoots+rootCount > maximumControlEdges) return {};
        inspectedRoots += rootCount;
        std::array<SharedControl, 32> chunk{};
        for (std::size_t offset=0; offset<rootCount;) {
            if (stopped()) return {};
            const auto size = std::min(chunk.size(), static_cast<std::size_t>(rootCount-offset));
            if (!readAt(roots.begin, offset*sizeof(SharedControl), chunk.data(), size*sizeof(SharedControl))) return {};
            for (std::size_t i=0; i<size; ++i) {
                // The component owns the weak reference's control block. Never
                // dereference its object; require membership in the owned active tree.
                if (!chunk[i].owner || !factoryCensus.contains(chunk[i].object)) continue;
                std::int32_t strong{};
                if (!readAt(chunk[i].owner, 8, &strong, sizeof(strong)) || strong < 0) return {};
                if (strong) factoryCensus.addLiveRoot(chunk[i].object);
            }
            offset += size;
        }
        NativeVector rootsAfter{};
        if (!readAt(component, profile->layout.factoryRoots, &rootsAfter, sizeof(rootsAfter)) ||
            rootsAfter.begin != roots.begin || rootsAfter.end != roots.end || rootsAfter.capacity != roots.capacity) return {};
        if (breakdown.detailCount < breakdown.details.size()) {
            const auto counts = factoryCensus.finishFactory(stopped);
            if (!counts.available) return {};
            auto& row = breakdown.details[breakdown.detailCount++];
            row.instanceId = factoryCensus.instanceId(control);
            row.nameAvailable = copyControlName(component, row.name, row.nameTruncated);
            row.ownerAvailable = copyControlName(control, row.owner, row.ownerTruncated);
            row.liveRoots = counts.liveRoots;
            row.controls = counts.generatedControls;
            row.descendants = row.controls-row.liveRoots;
        } else breakdown.detailsTruncated = true;
        Components after{};
        if (!readAt(control, profile->layout.controlComponents, &after, sizeof(after)) || after.mask != components.mask ||
            after.entries.begin != entries.begin || after.entries.end != entries.end ||
            after.entries.capacity != entries.capacity) return {};
    }
    const auto total = factoryCensus.finish(stopped);
    if (!total.available) return {};
    breakdown.available = true;
    breakdown.instances = total.instances;
    breakdown.liveRoots = total.liveRoots;
    breakdown.generatedControls = total.generatedControls;
    return breakdown;
}
// Reuses the complete, unique control list; no second hierarchy traversal and no new hook.
// DataBindingComponent layout: docs/reverse-engineering/1.26.60-ui-bindings.md.
ControlCount collectBindings() noexcept {
    const auto deadline = ScreenMetrics::Clock::now()+controlSampleBudget;
    const auto stopped = [&] { return ScreenMetrics::Clock::now() >= deadline; };
    std::uint64_t bindingBit{};
    if (!readAt(state.base, profile->reload.bindingComponentMaskRva, &bindingBit, sizeof(bindingBit)) ||
        !std::has_single_bit(bindingBit)) return {};
    return countScreenBindings(factoryCensus.size(), [&](std::size_t index) -> ControlCount {
        const auto control = factoryCensus.control(index);
        struct Components { std::uint64_t mask; NativeVector entries; } components{};
        if (!readAt(control, profile->layout.controlComponents, &components, sizeof(components))) return {};
        if (!(components.mask & bindingBit)) return {ControlCountStatus::Complete, 0};
        const auto& entries = components.entries;
        if (!entries.begin || entries.end < entries.begin || entries.capacity < entries.end ||
            (entries.end-entries.begin)%sizeof(std::uintptr_t)) return {};
        const auto count = (entries.end-entries.begin)/sizeof(std::uintptr_t);
        const auto rank = static_cast<std::size_t>(std::popcount(components.mask & (bindingBit-1)));
        if (count > 128 || rank >= count) return {};
        const auto component = readPointerAt(entries.begin, rank*sizeof(std::uintptr_t));
        std::array<std::uintptr_t, 2> identity{};
        if (!readAt(component, 0, identity.data(), sizeof(identity)) ||
            identity[0] != state.base+profile->reload.bindingComponentTableRva || identity[1] != control) return {};
        std::array<BindingVector, 3> vectors{}, afterVectors{};
        if (!readAt(component, profile->layout.bindingVectors, vectors.data(), sizeof(vectors))) return {};
        const auto result = countBindingRecords(vectors);
        if (!readAt(component, profile->layout.bindingVectors, afterVectors.data(), sizeof(afterVectors)) ||
            std::memcmp(vectors.data(), afterVectors.data(), sizeof(vectors))) return {};
        Components after{};
        if (!readAt(control, profile->layout.controlComponents, &after, sizeof(after)) || after.mask != components.mask ||
            after.entries.begin != entries.begin || after.entries.end != entries.end ||
            after.entries.capacity != entries.capacity) return {};
        return result;
    }, stopped);
}
CollectedControls collectActiveControls(void* stack) noexcept {
    static std::atomic_flag collecting = ATOMIC_FLAG_INIT;
    if (collecting.test_and_set(std::memory_order_acquire)) return {};
    struct Release { std::atomic_flag& flag; ~Release() { flag.clear(std::memory_order_release); } } release{collecting};
    struct ClearCensus { ~ClearCensus() { factoryCensus.clear(); } } clearCensus;
    const auto start = ScreenMetrics::Clock::now();
    const auto deadline = start + controlSampleBudget;
    const auto stackAddress = reinterpret_cast<std::uintptr_t>(stack);
    NativeVector scenes{};
    if (!readAt(stackAddress, profile->layout.stackScenes, &scenes, sizeof(scenes)) || !scenes.begin ||
        scenes.end <= scenes.begin || scenes.capacity < scenes.end ||
        (scenes.end-scenes.begin)%32 || scenes.end-scenes.begin > 128*32) return {};
    const auto scene = readPointerAt(scenes.end-24);
    // Reject HBUI and every unverified scene implementation, rather than reading a DOM as controls.
    if (readPointerAt(scene) != state.base+profile->reload.legacySceneTableRva) return {};
    const auto view = readPointerAt(scene, profile->layout.sceneView), tree = readPointerAt(view, profile->layout.viewTree);
    if (readPointerAt(tree) != state.base+profile->reload.visualTreeTableRva) return {};
    SharedControl root{};
    if (!readAt(tree, 8, &root, sizeof(root)) || !root.object || !root.owner) return {};
    const auto stopped = [&] { return ScreenMetrics::Clock::now() >= deadline; };
    const auto result = controlCounter.count(root.object, [&](std::uintptr_t node, auto&& emit) noexcept {
        factoryCensus.recordControl(node);
        NativeVector children{};
        if (!readAt(node, profile->layout.controlChildren, &children, sizeof(children)) || children.end < children.begin ||
            children.capacity < children.end || (children.end-children.begin)%sizeof(SharedControl))
            return ChildReadStatus::Unavailable;
        const auto total = (children.end-children.begin)/sizeof(SharedControl);
        if (total > maximumControlEdges) return ChildReadStatus::Limited;
        std::array<SharedControl, 32> chunk{};
        for (std::size_t offset = 0; offset < total;) {
            if (stopped()) return ChildReadStatus::Limited;
            const auto size = std::min(chunk.size(), static_cast<std::size_t>(total-offset));
            if (!readAt(children.begin, offset*sizeof(SharedControl), chunk.data(), size*sizeof(SharedControl)))
                return ChildReadStatus::Unavailable;
            for (std::size_t index=0; index<size; ++index) {
                if (!chunk[index].object || !chunk[index].owner) return ChildReadStatus::Unavailable;
                factoryCensus.recordChild(chunk[index].object);
                if (!emit(chunk[index].object)) return ChildReadStatus::Limited;
            }
            offset += size;
        }
        // A concurrent vector edit invalidates the observation instead of publishing a mixed total.
        NativeVector after{};
        if (!readAt(node, profile->layout.controlChildren, &after, sizeof(after)) ||
            after.begin != children.begin || after.end != children.end || after.capacity != children.capacity)
            return ChildReadStatus::Unavailable;
        return ChildReadStatus::Complete;
    }, stopped);
    const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(ScreenMetrics::Clock::now()-start).count();
    // Separate bounded phase: failure cannot replace a valid element total.
    const auto factories = result.status == ControlCountStatus::Complete ? collectFactories() : FactoryStatistics{};
    const auto bindings = result.status == ControlCountStatus::Complete ? collectBindings() : ControlCount{};
    NativeVector after{};
    SharedControl afterRoot{};
    if (!readAt(stackAddress, profile->layout.stackScenes, &after, sizeof(after)) || after.begin != scenes.begin || after.end != scenes.end ||
        readPointerAt(after.end-24) != scene || readPointerAt(view, profile->layout.viewTree) != tree ||
        !readAt(tree, 8, &afterRoot, sizeof(afterRoot)) || afterRoot.object != root.object || afterRoot.owner != root.owner)
        return {};
    const auto totalElapsed = std::chrono::duration_cast<std::chrono::microseconds>(ScreenMetrics::Clock::now()-start).count();
    return {result, static_cast<std::uint32_t>(std::max<std::int64_t>(0, elapsed)), factories,
        static_cast<std::uint32_t>(std::max<std::int64_t>(0, totalElapsed)), bindings};
}
bool beginScreenSample(ScreenMetrics::Clock::time_point now, bool sourceActive = true) noexcept {
    static thread_local ScreenSampleGate gate;
    return gate.begin(now, sourceActive);
}

void publishScreenSample(void* stack, const ScreenMetrics::Name& name,
                         std::string_view currentName, ScreenMetrics::Clock::time_point now) {
    // Retain copied names/counts only, never a scene/root/control pointer between callbacks.
    static thread_local ScreenMetrics::Name countedName{};
    static thread_local auto countedAt = ScreenMetrics::Clock::time_point{};
    static thread_local CollectedControls controls{};
    if (now-countedAt >= controlSampleInterval || currentName != std::string_view(countedName.data())) {
        controls = collectActiveControls(stack);
        countedName = name;
        countedAt = now;
        // Bounded first-screen cost evidence only; never emit per-frame/per-control logs.
        static thread_local std::array<ScreenMetrics::Name, 4> loggedNames{};
        static thread_local std::size_t logged = 0;
        bool seen = false;
        for (std::size_t index=0; index<logged; ++index)
            seen = seen || currentName == std::string_view(loggedNames[index].data());
        if (!seen && logged < loggedNames.size() && controls.elements.status != ControlCountStatus::Unavailable) {
            loggedNames[logged++] = name;
            diagnostic("native_controls_sample screen=" + std::string(currentName) + " count=" +
                std::to_string(controls.elements.count) + " complete=" +
                std::to_string(controls.elements.status == ControlCountStatus::Complete) + " collection_us=" +
                std::to_string(controls.micros) + " factory_available=" + std::to_string(controls.factories.available) +
                " factory_instances=" + std::to_string(controls.factories.instances) +
                " generated_roots=" + std::to_string(controls.factories.liveRoots) +
                " generated_controls=" + std::to_string(controls.factories.generatedControls) +
                " bindings=" + std::to_string(controls.bindings.count) +
                " bindings_complete=" + std::to_string(controls.bindings.status == ControlCountStatus::Complete) +
                " total_collection_us=" + std::to_string(controls.totalMicros));
        }
    }
    state.screenMetrics.publish(currentName, now, controls.elements, controls.micros, controls.factories, controls.bindings);
}
void* observeScreenName(void* stack, void* output) {
#ifdef _MSC_VER
    const auto caller = reinterpret_cast<std::uintptr_t>(_ReturnAddress());
#else
    const auto caller = reinterpret_cast<std::uintptr_t>(__builtin_return_address(0));
#endif
    // No diagnostic mutex is held across the original native call. Its output is
    // caller-owned and remains alive until this wrapper returns to that caller.
    void* result = state.screenName(stack, output);
    if (caller != state.base + profile->reload.primaryScreenNameCallerRva ||
        state.screenThread.load(std::memory_order_acquire) != GetCurrentThreadId()) return result;
    // An empty player stack means the native selector delegates to the frontend.
    // Do not consume its sampling slot or invalidate a freshly collected menu snapshot.
    NativeVector playerScenes{};
    const auto stackAddress = reinterpret_cast<std::uintptr_t>(stack);
    const bool empty = readPointerAt(stackAddress) == state.base+profile->reload.sceneStackTableRva &&
        readAt(stackAddress, profile->layout.stackScenes, &playerScenes, sizeof(playerScenes)) &&
        playerScenes.begin == playerScenes.end && playerScenes.capacity >= playerScenes.end;
    const auto now = ScreenMetrics::Clock::now();
    if (!beginScreenSample(now, !empty)) return result;
    // Private MSVC string layout verified in the exact native accessor. Never
    // construct/destroy a game string with this DLL's C++ runtime.
    struct NativeString { std::array<char, 16> storage; std::uint64_t size, capacity; };
    static_assert(sizeof(NativeString) == 32);
    NativeString value{};
    std::uintptr_t table{};
    if (result != output || !copyMemory(stack, &table, sizeof(table)) || table != state.base + profile->reload.sceneStackTableRva ||
        !copyMemory(output, &value, sizeof(value)) || value.size == 0 ||
        value.size > ScreenMetrics::maximumNameBytes || value.size > value.capacity ||
        value.capacity > maximumNativeStringCapacity || (value.capacity < 16 && value.size > 15)) {
        state.screenMetrics.invalidate();
        return result;
    }
    ScreenMetrics::Name name{};
    if (value.capacity < 16) {
        std::memcpy(name.data(), value.storage.data(), static_cast<std::size_t>(value.size));
    } else {
        const char* text{};
        std::memcpy(&text, value.storage.data(), sizeof(text));
        if (!copyMemory(text, name.data(), static_cast<std::size_t>(value.size))) {
            state.screenMetrics.invalidate(); return result;
        }
    }
    publishScreenSample(stack, name, std::string_view(name.data(), static_cast<std::size_t>(value.size)), now);
    return result;
}
// Freshly reacquire both stacks through the exact fields used by the verified
// native getters. No native call, retained screen pointer, or renderer traversal.
// docs/reverse-engineering/1.26.60-menu-screen.md
void sampleFrontend(void* currentOwner) {
    const auto now = ScreenMetrics::Clock::now();
    static thread_local auto checkedAt = ScreenMetrics::Clock::time_point{};
    if (now-checkedAt < std::chrono::milliseconds(250)) return;
    checkedAt = now;
    if (!validOwner(currentOwner)) return;
    const auto client = validatedUiContext(currentOwner);
    if (!client || readPointerAt(state.base+profile->reload.clientTableRva, profile->layout.playerStackSlot) != state.base+profile->reload.playerStackGetterRva ||
        readPointerAt(state.base+profile->reload.ownerTableRva, profile->layout.frontendStackSlot) != state.base+profile->reload.frontendStackGetterRva ||
        readPointerAt(state.base+profile->reload.sceneStackTableRva, profile->layout.sceneNameSlot) != state.base+profile->reload.sceneNameGetterRva) return;
    const auto player = readPointerAt(client, profile->layout.clientPlayerStack);
    NativeVector playerScenes{};
    if (readPointerAt(player) != state.base+profile->reload.sceneStackTableRva ||
        !readAt(player, profile->layout.stackScenes, &playerScenes, sizeof(playerScenes)) ||
        playerScenes.end < playerScenes.begin || playerScenes.capacity < playerScenes.end) return;
    // Native ClientInstance selector chooses the frontend only when the player stack is empty.
    if (playerScenes.begin != playerScenes.end || !beginScreenSample(now)) return;
    const auto owner = reinterpret_cast<std::uintptr_t>(currentOwner);
    auto* stack = reinterpret_cast<void*>(readPointerAt(owner, profile->layout.ownerFrontendStack));
    const auto address = reinterpret_cast<std::uintptr_t>(stack);
    NativeVector entries{};
    if (readPointerAt(address) != state.base+profile->reload.sceneStackTableRva ||
        !readAt(address, profile->layout.stackScenes, &entries, sizeof(entries)) || !entries.begin ||
        entries.end <= entries.begin || entries.capacity < entries.end ||
        (entries.end-entries.begin)%32 || entries.end-entries.begin > 128*32) {
        state.screenMetrics.invalidate(); return;
    }
    const auto scene = readPointerAt(entries.end-24);
    if (readPointerAt(scene) != state.base+profile->reload.legacySceneTableRva) {
        // HBUI is not a legacy control hierarchy. Never keep the previous menu/world's totals.
        state.screenMetrics.invalidate(); return;
    }
    const auto view = readPointerAt(scene, profile->layout.sceneView);
    const auto tree = readPointerAt(view, profile->layout.viewTree);
    if (readPointerAt(tree) != state.base+profile->reload.visualTreeTableRva) {
        state.screenMetrics.invalidate(); return;
    }
    ScreenMetrics::Name name{};
    bool truncated = false;
    if (!copyControlName(readPointerAt(tree, 8), name, truncated) || truncated || !name[0]) {
        state.screenMetrics.invalidate(); return;
    }
    publishScreenSample(stack, name, std::string_view(name.data()), now);
}


void observeLoader(void* owner) {
    {
        std::lock_guard lock(state.mutex);
        if (state.enabled && validOwner(owner) && (!state.automaticFrame ||
            (state.owner == reinterpret_cast<std::uintptr_t>(owner) && state.gameThread == GetCurrentThreadId()))) {
            state.owner = reinterpret_cast<std::uintptr_t>(owner);
            state.gameThread = GetCurrentThreadId();
            state.busy = true;
            diagnostic("native_loader_entered origin=" + std::string(injectedLoader ? "prototype" : "natural"));
            if (!injectedLoader) {
                ++state.natural;
                // A real reload supersedes queued experimental work.
                state.pending = false;
            }
        }
    }
    state.loader(owner);
}
void observeCompletion(void* callable, void* result) {
    const auto p = reinterpret_cast<std::uintptr_t>(callable);
    const auto captured = pointerAt(p + 8);
    const bool matchingTable = pointerAt(p) == state.base + profile->reload.callableTableRva;
    state.completion(callable, result);
    {
        std::lock_guard lock(state.mutex);
        if (state.enabled && matchingTable && captured == state.owner &&
            GetCurrentThreadId() == state.gameThread) {
            state.busy = state.uiCompletion.actionInFlight();
            state.uiCompletion.loaderCompleted();
            if (state.submitted > state.completed) {
                state.completed = state.submitted;
                if (!state.uiCompletion.actionInFlight()) state.finished = state.generation;
                diagnostic("native_UI_completion_returned visual_refresh=UNVERIFIED");
            }
        }
    }
    state.wake.notify_all();
}
void onFrameBoundary(void* currentOwner) {
#ifdef _MSC_VER
    const auto caller = reinterpret_cast<std::uintptr_t>(_ReturnAddress());
#else
    const auto caller = reinterpret_cast<std::uintptr_t>(__builtin_return_address(0));
#endif
    {
        auto* previousOwner = borrowedFrameOwner;
        borrowedFrameOwner = currentOwner;
        struct RestoreOwner { void* previous; ~RestoreOwner() { borrowedFrameOwner = previous; } } restore{previousOwner};
        state.frame(currentOwner);
    }
    if (profile->features.screenMetrics && caller == state.base+profile->reload.frameCallerRva &&
        state.screenThread.load(std::memory_order_acquire) == GetCurrentThreadId())
        sampleFrontend(currentOwner); // Outside the reload mutex; owner is borrowed from this natural frame.

    std::uintptr_t uiContext = 0;
    {
        std::lock_guard lock(state.mutex);
        if (state.enabled && state.automaticFrame && !state.owner && !state.faulted) {
            const bool ready = state.affinity.observe(reinterpret_cast<std::uintptr_t>(currentOwner),
                GetCurrentThreadId(), caller == state.base + profile->reload.frameCallerRva && validOwner(currentOwner));
            if (state.affinity.conflicted()) {
                state.faulted = true;
                diagnostic("automatic_affinity_conflict no_dispatch_until_restart");
            } else if (ready) {
                state.owner = reinterpret_cast<std::uintptr_t>(currentOwner);
                state.gameThread = GetCurrentThreadId();
                diagnostic("automatic_backend_armed mode=LegacyJsonUiOnly");
            }
        }
        if (state.enabled && !state.automaticFrame && !state.owner && state.observedThread == GetCurrentThreadId() && validOwner(currentOwner)) {
            state.owner = reinterpret_cast<std::uintptr_t>(currentOwner);
            state.gameThread = GetCurrentThreadId();
            diagnostic("owner_acquired_from_current_frame after_live_reload_thread_observation");
        }
        if (state.enabled && !state.faulted && state.automaticFrame &&
            caller == state.base + profile->reload.frameCallerRva && state.gameThread == GetCurrentThreadId() &&
            state.owner == reinterpret_cast<std::uintptr_t>(currentOwner)) {
            state.screenThread.store(pointerAt(reinterpret_cast<std::uintptr_t>(currentOwner) + profile->layout.ownerClientCount) == 1
                ? GetCurrentThreadId() : 0, std::memory_order_release);
        } else {
            state.screenThread.store(0, std::memory_order_release);
        }
        // Reacquire from this natural call; never dereference a cached heap pointer.
        if ((state.automaticFrame && caller != state.base + profile->reload.frameCallerRva) ||
            !state.enabled || state.faulted || !state.pending || state.busy ||
            state.gameThread != GetCurrentThreadId() ||
            state.owner != reinterpret_cast<std::uintptr_t>(currentOwner) || !validOwner(currentOwner)) return;
        {
            uiContext = validatedUiContext(currentOwner);
            if (!uiContext) {
                state.pending = false; state.faulted = true;
                diagnostic("targeted_context_guard_failed no_native_call");
                state.wake.notify_all(); return;
            }
            if (!state.uiCompletion.begin()) return;
        }
        state.pending = false;
        state.busy = true;
        ++state.submitted;
        diagnostic("native_reload_dispatched mode=legacy_json_ui_only");
    }
    // Single RCX method with ignored return, independently confirmed from the original code.
    injectedLoader = true;
    if (uiContext) {
        diagnostic("native_ui_button_callback_entered");
        // RCX/RDX are unused by this exact callback. It constructs/releases its own
        // guard/control wrapper from R8, then runs the complete native UI action.
        reinterpret_cast<UiReloadCallback>(state.base + profile->reload.uiReloadCallbackRva)(nullptr, nullptr,
            reinterpret_cast<void*>(uiContext));
        {
            std::lock_guard lock(state.mutex);
            state.uiCompletion.actionReturned();
            if (state.uiCompletion.ready()) { state.finished = state.generation; state.busy = false; }
            diagnostic("native_ui_button_callback_returned visual_refresh=UNVERIFIED resource_isolation=UNVERIFIED");
        }
        state.wake.notify_all();
    }
    injectedLoader = false;
}

void install(const std::filesystem::path& diagnosticLog, bool automaticFrame) {
    std::unique_lock lock(state.mutex);
    if (state.enabled || state.installed) throw std::runtime_error("Prototype is single-use per process; restart to re-enable");
    const auto& selected = minecraft::requireRunningCompatibility();
    profile = &selected;
    if (!selected.features.targetedReload) throw std::runtime_error("Targeted JSON UI reload unavailable for this build");
    state.diagnostics = std::make_unique<Logger>(diagnosticLog);
    state.base = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
    if (!minecraft::matchesMainExecutableCode(profile->reload.loaderRva, profile->signatures.loaderPrefix) ||
        !minecraft::matchesMainExecutableCode(profile->reload.frameRva, profile->signatures.framePrefix) ||
        !minecraft::matchesMainExecutableCode(profile->reload.completionRva, profile->signatures.completionPrefix))
        throw std::runtime_error("Code is already hooked or incompatible; detach diagnostics first");
    if (!minecraft::matchesMainExecutableCode(profile->reload.uiReloadCallbackRva, profile->signatures.uiCallbackPrefix))
        throw std::runtime_error("Native UI callback is already hooked or incompatible");
    const auto initialized = MH_Initialize();
    if (initialized != MH_OK) throw std::runtime_error("Cannot initialize private hook library");
    const std::array<std::uintptr_t, 3> rvas{profile->reload.loaderRva, profile->reload.frameRva, profile->reload.completionRva};
    const std::array<void*, 3> hooks{reinterpret_cast<void*>(&observeLoader), reinterpret_cast<void*>(&onFrameBoundary), reinterpret_cast<void*>(&observeCompletion)};
    const std::array<void**, 3> originals{reinterpret_cast<void**>(&state.loader), reinterpret_cast<void**>(&state.frame), reinterpret_cast<void**>(&state.completion)};
    for (std::size_t i = 0; i < rvas.size(); ++i) {
        auto* address = reinterpret_cast<void*>(state.base + rvas[i]);
        if (MH_CreateHook(address, hooks[i], originals[i]) != MH_OK) {
            for (auto rva : rvas) MH_RemoveHook(reinterpret_cast<void*>(state.base + rva));
            MH_Uninitialize();
            throw std::runtime_error("Native hook creation failed");
        }
    }
    // Pin code before making any callback reachable. Disable is logical; module remains resident.
    HMODULE pinned{};
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                          reinterpret_cast<LPCWSTR>(&onFrameBoundary), &pinned)) throw std::runtime_error("Cannot pin prototype");
    state.installed = true;
    state.enabled = false;
    lock.unlock();
    // Never hold our state mutex while MinHook suspends/resumes other threads.
    // Activate required reload hooks before installing optional diagnostics.
    const auto activation = MH_EnableHook(MH_ALL_HOOKS);
    lock.lock();
    if (activation != MH_OK) {
        state.enabled = false;
        lock.unlock();
        MH_DisableHook(MH_ALL_HOOKS);
        throw std::runtime_error("Native hook activation failed");
    }
    state.automaticFrame = automaticFrame;
    state.enabled = true;
    diagnostic("backend_enabled waiting_for_live_owner_affinity");
    lock.unlock();
    const bool screenCompatible = minecraft::matchesMainExecutableCode(profile->reload.screenNameRva, profile->signatures.screenPrefix);
    if (profile->features.screenMetrics && automaticFrame && screenCompatible &&
        MH_CreateHook(reinterpret_cast<void*>(state.base + profile->reload.screenNameRva), reinterpret_cast<void*>(&observeScreenName),
            reinterpret_cast<void**>(&state.screenName)) == MH_OK) {
        if (MH_EnableHook(reinterpret_cast<void*>(state.base + profile->reload.screenNameRva)) == MH_OK) {
            diagnostic("native_screen_observer_installed sample_ms=250");
        } else {
            diagnostic("native_screen_observer_enable_failed reload_hooks_preserved");
        }
    } else {
        diagnostic("native_screen_observer_unavailable reload_hooks_preserved");
    }
}
class LegacyReloadBackend final : public ReloadBackend {
public:
    explicit LegacyReloadBackend(const std::filesystem::path& diagnosticLog, bool automaticFrame) { install(diagnosticLog, automaticFrame); }
    ~LegacyReloadBackend() override { disableLegacyReload(); }
    ReloadResult reload(const ReloadRequest& request) override {
        std::unique_lock lock(state.mutex);
        if (!state.enabled || state.faulted || !state.owner || !state.gameThread) {
            if (request.hotkey || request.explicitRequest) diagnostic("native_reload_not_ready enabled=" +
                std::to_string(state.enabled) + " armed=" + std::to_string(state.owner != 0) +
                " faulted=" + std::to_string(state.faulted));
            return ReloadResult::Unsupported;
        }
        if (!request.hotkey && !request.explicitRequest && !state.automatic) return ReloadResult::Unsupported;
        if (state.busy || state.pending || state.uiCompletion.inFlight()) return ReloadResult::Failed;
        const auto generation = ++state.generation;
        state.pending = true;
        diagnostic("native_reload_queued mode=legacy_json_ui_only");
        if (!state.wake.wait_for(lock, std::chrono::seconds(8), [&] { return !state.enabled || state.faulted || state.finished >= generation; })) {
            state.pending = false;
            state.faulted = true;
            diagnostic("native_reload_timeout backend_disabled_until_restart"); // No retry after an unexpected or missing completion.
            return ReloadResult::Failed;
        }
        return state.enabled && !state.faulted && state.finished >= generation ? ReloadResult::Succeeded : ReloadResult::Failed;
    }
};
}
bool isActiveTitleView(void* view, void* scene) noexcept {
    // The owner is borrowed only while the original, naturally entered frame is
    // executing. Never dereference a cached heap identity from a later callback.
    if (!profile || !borrowedFrameOwner || !view || !scene) return false;
    std::unique_lock lock(state.mutex, std::try_to_lock);
    if (!lock.owns_lock() || !state.enabled || state.faulted ||
        state.gameThread != GetCurrentThreadId() || !validOwner(borrowedFrameOwner)) return false;
    const auto client = validatedUiContext(borrowedFrameOwner);
    const auto player = readPointerAt(client, profile->layout.clientPlayerStack);
    NativeVector playerScenes{};
    if (!client || readPointerAt(player) != state.base+profile->reload.sceneStackTableRva ||
        !readAt(player,profile->layout.stackScenes,&playerScenes,sizeof(playerScenes)) ||
        playerScenes.begin != playerScenes.end || playerScenes.capacity < playerScenes.end) return false;
    const auto owner = reinterpret_cast<std::uintptr_t>(borrowedFrameOwner);
    const auto stack = readPointerAt(owner,profile->layout.ownerFrontendStack);
    NativeVector entries{};
    if (readPointerAt(stack) != state.base+profile->reload.sceneStackTableRva ||
        !readAt(stack,profile->layout.stackScenes,&entries,sizeof(entries)) || !entries.begin ||
        entries.end <= entries.begin || entries.capacity < entries.end ||
        (entries.end-entries.begin)%32 || entries.end-entries.begin > 128*32) return false;
    const auto active = readPointerAt(entries.end-24);
    return active == reinterpret_cast<std::uintptr_t>(scene) &&
        readPointerAt(active) == state.base+profile->reload.legacySceneTableRva &&
        readPointerAt(active,profile->layout.sceneView) == reinterpret_cast<std::uintptr_t>(view);
}
ScreenMetrics& nativeScreenMetrics() noexcept { return state.screenMetrics; }
std::unique_ptr<ReloadBackend> makeLegacyReloadBackend(const std::filesystem::path& diagnosticLog, bool automaticFrame) { return std::make_unique<LegacyReloadBackend>(diagnosticLog, automaticFrame); }
bool armFromObservedThread(unsigned processId, unsigned threadId) {
    if (processId != GetCurrentProcessId() || !threadId) return false;
    HANDLE thread = OpenThread(THREAD_QUERY_LIMITED_INFORMATION, FALSE, threadId);
    if (!thread) return false;
    const bool belongs = GetProcessIdOfThread(thread) == GetCurrentProcessId();
    CloseHandle(thread);
    if (!belongs) return false;
    std::lock_guard lock(state.mutex);
    if (!state.enabled || state.busy || state.pending || (state.gameThread && state.gameThread != threadId)) return false;
    state.observedThread = threadId;
    diagnostic("live_reload_thread_observation_accepted tid=" + std::to_string(threadId));
    return true;
}
bool retireLegacyReloadHooks() noexcept {
    disableLegacyReload();
    return !state.installed || MH_DisableHook(MH_ALL_HOOKS) == MH_OK;
}
void disableLegacyReload() noexcept {
    std::lock_guard lock(state.mutex);
    state.enabled = false;
    state.screenThread.store(0, std::memory_order_release);
    state.screenMetrics.invalidate();
    state.pending = false;
    state.wake.notify_all();
}
std::string legacyReloadState() {
    std::unique_lock lock(state.mutex, std::try_to_lock);
    if (!lock.owns_lock()) return "{\"transition\":1}";
    std::ostringstream out;
    out << "{\"enabled\":" << state.enabled << ",\"armed\":" << (state.owner != 0)
        << ",\"thread\":" << state.gameThread << ",\"busy\":" << state.busy
        << ",\"pending\":" << state.pending << ",\"faulted\":" << state.faulted
        << ",\"uiOnly\":" << true
        << ",\"uiActionInFlight\":" << state.uiCompletion.actionInFlight()
        << ",\"natural\":" << state.natural << ",\"submitted\":" << state.submitted
        << ",\"completed\":" << state.completed << "}";
    return out.str();
}
}
