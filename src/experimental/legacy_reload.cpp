// Build-specific opt-in prototype. Evidence and ABI: docs/reverse-engineering/1.26.60-legacy-reload-path.md
#include "experimental/legacy_reload.hpp"
#include "core/logger.hpp"
#include "standalone/initialization.hpp"
#ifdef _MSC_VER
#include <intrin.h>
#endif
#include <windows.h>
#include <bcrypt.h>
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
constexpr std::uintptr_t loaderRva = 0x83a190;
constexpr std::uintptr_t frameRva = 0x879030;
// Verified natural caller: legacy-prototype-20261106/events.jsonl; versioned reload note.
constexpr std::uintptr_t frameCallerRva = 0x83274;
constexpr std::uintptr_t completionRva = 0x8f60b0;
constexpr std::uintptr_t ownerTableRva = 0xf091b70;
constexpr std::uintptr_t callableTableRva = 0xf094230;
// Outer natural reload: preserves the three observed inputs and performs invalidation before task submission.
constexpr std::uintptr_t resourceReloadRva = 0x873a20;
// Complete native button.reload_ui_definitions path; see versioned UI-only evidence.
constexpr std::uintptr_t uiReloadCallbackRva = 0xf203e0;
constexpr std::uintptr_t clientTableRva = 0xf2b9330;
constexpr std::uintptr_t clientOwnerGetterRva = 0x1a58900;
constexpr std::uintptr_t clientCacheGetterRva = 0x56f040;
constexpr std::uintptr_t uiWaitRva = 0xde790;
using UiReloadCallback = void (*)(void*, void*, void*);
// Observed natural primary-client stack name accessor; exact ABI and callers:
// docs/reverse-engineering/1.26.60-native-screen.md
constexpr std::uintptr_t screenNameRva = 0x56d000;
constexpr std::uintptr_t sceneStackTableRva = 0xf0829b0;
constexpr std::uint64_t maximumNativeStringCapacity = 1024 * 1024;
constexpr std::uintptr_t primaryScreenNameCallerRva = 0x65b0119;
using ScreenNameMethod = void* (*)(void*, void*);
using OwnerMethod = void (*)(void*);
using CompletionMethod = void (*)(void*, void*);
using ResourceMethod = void (*)(void*, void*, std::uint32_t);
struct State {
    std::mutex mutex;
    std::condition_variable wake;
    bool enabled = false, installed = false, pending = false, busy = false, faulted = false;
    bool automatic = false, resourceFallback = false, automaticFrame = false;
    standalone::FrameAffinity affinity;
    unsigned resourceFlag = 0;
    LegacyReloadMode mode = LegacyReloadMode::FullResourceReload;
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
    if (pointerAt(p) != state.base + ownerTableRva) return false;
    if (pointerAt(state.base + ownerTableRva + 0x60) != state.base + loaderRva) return false;
    const auto service = pointerAt(p + 0x818), context = pointerAt(p + 0x228);
    return service && context && pointerAt(service) == state.base + 0xefca210 &&
           pointerAt(pointerAt(service) + 8) == state.base + 0xda940 &&
           pointerAt(context) == state.base + 0xf0f9500;
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
    if (pointerAt(owner + 0x9d8) != 1) return 0;
    const auto head = pointerAt(owner + 0x9d0), node = pointerAt(head);
    if (!head || !node || node == head) return 0;
    const auto client = pointerAt(node + 0x38), table = pointerAt(client);
    if (table != state.base + clientTableRva || pointerAt(table + 0x270) != state.base + clientOwnerGetterRva ||
        pointerAt(table + 0x790) != state.base + clientCacheGetterRva || pointerAt(client + 0x1c8) != owner ||
        !liveGuard(pointerAt(client + 8)) || !pointerAt(client + 0x10)) return 0;
    const auto service = pointerAt(owner + 0x818), serviceTable = pointerAt(service);
    if (pointerAt(state.base + ownerTableRva + 0x68) != state.base + 0x8ac660 ||
        pointerAt(serviceTable + 0x28) != state.base + uiWaitRva ||
        !liveGuard(pointerAt(service + 0x18)) || !pointerAt(service + 0x28)) return 0;
    const auto cache = pointerAt(client + 0x230), tree = pointerAt(cache + 0x10);
    if (!readable(reinterpret_cast<void*>(cache), 0x20) || !readable(reinterpret_cast<void*>(tree), 0x20)) return 0;
    unsigned char sentinel{};
    std::memcpy(&sentinel, reinterpret_cast<void*>(tree + 0x19), 1);
    if (sentinel != 1 || !pointerAt(tree) || !pointerAt(tree + 8) || !pointerAt(tree + 0x10)) return 0;
    return client;
}
bool copyMemory(const void* source, void* target, std::size_t size) noexcept {
    SIZE_T copied{};
    return source && ReadProcessMemory(GetCurrentProcess(), source, target, size, &copied) && copied == size;
}
// Exact-build ownership/traversal evidence: docs/reverse-engineering/1.26.60-ui-controls.md.
constexpr std::uintptr_t legacySceneTableRva = 0xf20ec60;
constexpr std::uintptr_t visualTreeTableRva = 0xf2094c0;
constexpr std::size_t maximumControlEdges = 32768;
constexpr auto controlSampleInterval = std::chrono::seconds(1);
constexpr auto controlSampleBudget = std::chrono::milliseconds(5);
ControlCounter<8192, maximumControlEdges> controlCounter; // Only the verified UI thread uses scratch.
// Evidence/semantics: docs/reverse-engineering/1.26.60-ui-factories.md.
constexpr std::uintptr_t factoryComponentTableRva = 0xf0fcaf0;
constexpr std::uintptr_t factoryComponentMaskRva = 0x128f5dc8;
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
struct CollectedControls { ControlCount elements; std::uint32_t micros = 0; FactoryStatistics factories{}; std::uint32_t totalMicros = 0; };
template<std::size_t Size>
bool copyControlName(std::uintptr_t object, std::array<char, Size>& text, bool& truncated) noexcept {
    struct NativeString { std::array<char, 16> storage; std::uint64_t size, capacity; } value{};
    if (!readAt(object, 0x20, &value, sizeof(value)) || value.size > value.capacity ||
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
    if (!readAt(state.base, factoryComponentMaskRva, &factoryBit, sizeof(factoryBit)) ||
        !std::has_single_bit(factoryBit)) return {};
    if (!factoryCensus.prepareInstanceIds(stopped)) return {};
    FactoryStatistics breakdown{};
    std::size_t inspectedRoots = 0;
    for (std::size_t index=0; index<factoryCensus.size(); ++index) {
        if (stopped()) return {};
        const auto control = factoryCensus.control(index);
        struct Components { std::uint64_t mask; NativeVector entries; } components{};
        static_assert(sizeof(Components) == 32);
        if (!readAt(control, 0xb0, &components, sizeof(components))) return {};
        if (!(components.mask & factoryBit)) continue;
        const auto& entries = components.entries;
        if (entries.end < entries.begin || entries.capacity < entries.end ||
            (entries.end-entries.begin)%sizeof(std::uintptr_t)) return {};
        const auto count = (entries.end-entries.begin)/sizeof(std::uintptr_t);
        const auto rank = static_cast<std::size_t>(std::popcount(components.mask & (factoryBit-1)));
        if (count > 128 || rank >= count) return {};
        const auto component = readPointerAt(entries.begin, rank*sizeof(std::uintptr_t));
        if (readPointerAt(component) != state.base+factoryComponentTableRva) return {};
        if (readPointerAt(component, 8) != control) return {};
        factoryCensus.addFactory();
        NativeVector roots{};
        if (!readAt(component, 0xa8, &roots, sizeof(roots)) || roots.end < roots.begin ||
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
        if (!readAt(component, 0xa8, &rootsAfter, sizeof(rootsAfter)) ||
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
        if (!readAt(control, 0xb0, &after, sizeof(after)) || after.mask != components.mask ||
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
CollectedControls collectActiveControls(void* stack) noexcept {
    static std::atomic_flag collecting = ATOMIC_FLAG_INIT;
    if (collecting.test_and_set(std::memory_order_acquire)) return {};
    struct Release { std::atomic_flag& flag; ~Release() { flag.clear(std::memory_order_release); } } release{collecting};
    struct ClearCensus { ~ClearCensus() { factoryCensus.clear(); } } clearCensus;
    const auto start = ScreenMetrics::Clock::now();
    const auto deadline = start + controlSampleBudget;
    const auto stackAddress = reinterpret_cast<std::uintptr_t>(stack);
    NativeVector scenes{};
    if (!readAt(stackAddress, 0x18, &scenes, sizeof(scenes)) || !scenes.begin ||
        scenes.end <= scenes.begin || scenes.capacity < scenes.end ||
        (scenes.end-scenes.begin)%32 || scenes.end-scenes.begin > 128*32) return {};
    const auto scene = readPointerAt(scenes.end-24);
    // Reject HBUI and every unverified scene implementation, rather than reading a DOM as controls.
    if (readPointerAt(scene) != state.base+legacySceneTableRva) return {};
    const auto view = readPointerAt(scene, 0x40), tree = readPointerAt(view, 0x50);
    if (readPointerAt(tree) != state.base+visualTreeTableRva) return {};
    SharedControl root{};
    if (!readAt(tree, 8, &root, sizeof(root)) || !root.object || !root.owner) return {};
    const auto stopped = [&] { return ScreenMetrics::Clock::now() >= deadline; };
    const auto result = controlCounter.count(root.object, [&](std::uintptr_t node, auto&& emit) noexcept {
        factoryCensus.recordControl(node);
        NativeVector children{};
        if (!readAt(node, 0x98, &children, sizeof(children)) || children.end < children.begin ||
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
        if (!readAt(node, 0x98, &after, sizeof(after)) ||
            after.begin != children.begin || after.end != children.end || after.capacity != children.capacity)
            return ChildReadStatus::Unavailable;
        return ChildReadStatus::Complete;
    }, stopped);
    const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(ScreenMetrics::Clock::now()-start).count();
    // Separate bounded phase: failure cannot replace a valid element total.
    const auto factories = result.status == ControlCountStatus::Complete ? collectFactories() : FactoryStatistics{};
    NativeVector after{};
    SharedControl afterRoot{};
    if (!readAt(stackAddress, 0x18, &after, sizeof(after)) || after.begin != scenes.begin || after.end != scenes.end ||
        readPointerAt(after.end-24) != scene || readPointerAt(view, 0x50) != tree ||
        !readAt(tree, 8, &afterRoot, sizeof(afterRoot)) || afterRoot.object != root.object || afterRoot.owner != root.owner)
        return {};
    const auto totalElapsed = std::chrono::duration_cast<std::chrono::microseconds>(ScreenMetrics::Clock::now()-start).count();
    return {result, static_cast<std::uint32_t>(std::max<std::int64_t>(0, elapsed)), factories,
        static_cast<std::uint32_t>(std::max<std::int64_t>(0, totalElapsed))};
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
    if (caller != state.base + primaryScreenNameCallerRva ||
        state.screenThread.load(std::memory_order_acquire) != GetCurrentThreadId()) return result;
    static thread_local auto lastSample = ScreenMetrics::Clock::time_point{};
    const auto now = ScreenMetrics::Clock::now();
    if (now-lastSample < std::chrono::milliseconds(250)) return result;
    lastSample = now;
    // Private MSVC string layout verified in the exact native accessor. Never
    // construct/destroy a game string with this DLL's C++ runtime.
    struct NativeString { std::array<char, 16> storage; std::uint64_t size, capacity; };
    static_assert(sizeof(NativeString) == 32);
    NativeString value{};
    std::uintptr_t table{};
    if (result != output || !copyMemory(stack, &table, sizeof(table)) || table != state.base + sceneStackTableRva ||
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
    // Retain copied names/counts only, never a scene/root/control pointer between callbacks.
    static thread_local ScreenMetrics::Name countedName{};
    static thread_local auto countedAt = ScreenMetrics::Clock::time_point{};
    static thread_local CollectedControls controls{};
    const auto currentName = std::string_view(name.data(), static_cast<std::size_t>(value.size));
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
                " total_collection_us=" + std::to_string(controls.totalMicros));
        }
    }
    state.screenMetrics.publish(currentName, now, controls.elements, controls.micros, controls.factories);
    return result;
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
    const bool matchingTable = pointerAt(p) == state.base + callableTableRva;
    state.completion(callable, result);
    {
        std::lock_guard lock(state.mutex);
        if (state.enabled && matchingTable && captured == state.owner &&
            GetCurrentThreadId() == state.gameThread) {
            state.busy = state.uiCompletion.actionInFlight();
            if (state.mode == LegacyReloadMode::LegacyJsonUiOnly) state.uiCompletion.loaderCompleted();
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
    state.frame(currentOwner);
    std::uintptr_t uiContext = 0;
    {
        std::lock_guard lock(state.mutex);
        if (state.enabled && state.automaticFrame && !state.owner && !state.faulted) {
            const bool ready = state.affinity.observe(reinterpret_cast<std::uintptr_t>(currentOwner),
                GetCurrentThreadId(), caller == state.base + frameCallerRva && validOwner(currentOwner));
            if (state.affinity.conflicted()) {
                state.faulted = true;
                diagnostic("automatic_affinity_conflict no_dispatch_until_restart");
            } else if (ready) {
                state.owner = reinterpret_cast<std::uintptr_t>(currentOwner);
                state.gameThread = GetCurrentThreadId();
                diagnostic("automatic_backend_armed verified_frame_caller=0x83274 mode=" + std::string(state.mode == LegacyReloadMode::LegacyJsonUiOnly ? "LegacyJsonUiOnly callback=0xf203e0" : "FullResourceReload resource_entry=0x873a20 flag=1"));
            }
        }
        if (state.enabled && !state.automaticFrame && !state.owner && state.observedThread == GetCurrentThreadId() && validOwner(currentOwner)) {
            state.owner = reinterpret_cast<std::uintptr_t>(currentOwner);
            state.gameThread = GetCurrentThreadId();
            diagnostic("owner_acquired_from_current_frame after_live_reload_thread_observation");
        }
        if (state.enabled && !state.faulted && state.automaticFrame &&
            caller == state.base + frameCallerRva && state.gameThread == GetCurrentThreadId() &&
            state.owner == reinterpret_cast<std::uintptr_t>(currentOwner)) {
            state.screenThread.store(pointerAt(reinterpret_cast<std::uintptr_t>(currentOwner) + 0x9d8) == 1
                ? GetCurrentThreadId() : 0, std::memory_order_release);
        } else {
            state.screenThread.store(0, std::memory_order_release);
        }
        // Reacquire from this natural call; never dereference a cached heap pointer.
        if ((state.automaticFrame && caller != state.base + frameCallerRva) ||
            !state.enabled || state.faulted || !state.pending || state.busy ||
            state.gameThread != GetCurrentThreadId() ||
            state.owner != reinterpret_cast<std::uintptr_t>(currentOwner) || !validOwner(currentOwner)) return;
        if (state.mode == LegacyReloadMode::LegacyJsonUiOnly) {
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
        diagnostic("native_reload_dispatched mode=" + std::string(state.mode == LegacyReloadMode::LegacyJsonUiOnly ? "legacy_json_ui_only" : state.resourceFallback ? "resource_rebuild" : "legacy_ui"));
    }
    // Single RCX method with ignored return, independently confirmed from the original code.
    injectedLoader = true;
    if (uiContext) {
        diagnostic("native_ui_button_callback_entered");
        // RCX/RDX are unused by this exact callback. It constructs/releases its own
        // guard/control wrapper from R8, then runs the complete native UI action.
        reinterpret_cast<UiReloadCallback>(state.base + uiReloadCallbackRva)(nullptr, nullptr,
            reinterpret_cast<void*>(uiContext));
        {
            std::lock_guard lock(state.mutex);
            state.uiCompletion.actionReturned();
            if (state.uiCompletion.ready()) { state.finished = state.generation; state.busy = false; }
            diagnostic("native_ui_button_callback_returned visual_refresh=UNVERIFIED resource_isolation=UNVERIFIED");
        }
        state.wake.notify_all();
    } else if (state.resourceFallback) {
        const auto context = pointerAt(reinterpret_cast<std::uintptr_t>(currentOwner) + 0x228);
        const auto reloadResources = reinterpret_cast<ResourceMethod>(state.base + resourceReloadRva);
        reloadResources(currentOwner, reinterpret_cast<void*>(context), state.resourceFlag);
    } else {
        observeLoader(currentOwner);
    }
    injectedLoader = false;
}

void verifyImage() {
    wchar_t name[32768]{};
    const auto length = GetModuleFileNameW(nullptr, name, 32768);
    if (!length || length == 32768) throw std::runtime_error("Cannot identify game image");
    BCRYPT_ALG_HANDLE algorithm{};
    BCRYPT_HASH_HANDLE hash{};
    auto cleanup = [&] { if (hash) BCryptDestroyHash(hash); if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0); };
    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0) throw std::runtime_error("SHA256 unavailable");
    DWORD objectSize{}, written{};
    if (BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&objectSize), sizeof(objectSize), &written, 0) < 0) { cleanup(); throw std::runtime_error("SHA256 property failed"); }
    std::vector<UCHAR> object(objectSize);
    if (BCryptCreateHash(algorithm, &hash, object.data(), objectSize, nullptr, 0, 0) < 0) { cleanup(); throw std::runtime_error("SHA256 initialization failed"); }
    std::ifstream input(std::filesystem::path(name), std::ios::binary);
    std::array<char, 65536> bytes{};
    bool ok = input.is_open();
    while (ok && input) {
        input.read(bytes.data(), bytes.size());
        if (input.gcount() && BCryptHashData(hash, reinterpret_cast<PUCHAR>(bytes.data()), static_cast<ULONG>(input.gcount()), 0) < 0) ok = false;
    }
    ok = ok && input.eof();
    std::array<UCHAR, 32> digest{};
    ok = ok && BCryptFinishHash(hash, digest.data(), digest.size(), 0) >= 0;
    cleanup();
    constexpr std::array<UCHAR, 32> expected{0x1d,0x6a,0xd5,0xc2,0x17,0xe9,0x30,0x54,0x42,0xc3,0x53,0xba,0x3e,0x9b,0xca,0x62,0x3d,0x57,0x19,0x13,0x4e,0x1c,0xc8,0xa8,0x4f,0xb1,0x23,0x3f,0x71,0x45,0xe8,0x5c};
    if (!ok || digest != expected) throw std::runtime_error("Requires exact verified Bedrock 1.26.60.5 image");
}
void install(const std::filesystem::path& diagnosticLog, bool automaticFrame, LegacyReloadMode mode) {
    std::unique_lock lock(state.mutex);
    if (state.enabled || state.installed) throw std::runtime_error("Prototype is single-use per process; restart to re-enable");
    verifyImage();
    state.diagnostics = std::make_unique<Logger>(diagnosticLog);
    state.base = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
    constexpr std::array<unsigned char, 7> loaderPrefix{0x55,0x41,0x56,0x56,0x57,0x53,0x48};
    constexpr std::array<unsigned char, 7> framePrefix{0x55,0x41,0x56,0x56,0x57,0x53,0x48};
    constexpr std::array<unsigned char, 7> completionPrefix{0x55,0x41,0x57,0x41,0x56,0x56,0x57};
    if (std::memcmp(reinterpret_cast<void*>(state.base + loaderRva), loaderPrefix.data(), loaderPrefix.size()) ||
        std::memcmp(reinterpret_cast<void*>(state.base + frameRva), framePrefix.data(), framePrefix.size()) ||
        std::memcmp(reinterpret_cast<void*>(state.base + completionRva), completionPrefix.data(), completionPrefix.size()))
        throw std::runtime_error("Code is already hooked or incompatible; detach diagnostics first");
    constexpr std::array<unsigned char, 7> uiCallbackPrefix{0x55,0x56,0x57,0x48,0x83,0xec,0x40};
    if (mode == LegacyReloadMode::LegacyJsonUiOnly &&
        std::memcmp(reinterpret_cast<void*>(state.base + uiReloadCallbackRva), uiCallbackPrefix.data(), uiCallbackPrefix.size()))
        throw std::runtime_error("Native UI callback is already hooked or incompatible");
    const auto initialized = MH_Initialize();
    if (initialized != MH_OK) throw std::runtime_error("Cannot initialize private hook library");
    const std::array<std::uintptr_t, 3> rvas{loaderRva, frameRva, completionRva};
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
    state.mode = mode;
    state.automaticFrame = automaticFrame;
    if (automaticFrame) { state.resourceFallback = true; state.resourceFlag = 1; }
    state.enabled = true;
    diagnostic("backend_enabled waiting_for_live_owner_affinity");
    lock.unlock();
    constexpr std::array<unsigned char, 8> screenPrefix{0x56,0x48,0x83,0xec,0x20,0x48,0x8b,0x41};
    const bool screenCompatible = !std::memcmp(reinterpret_cast<void*>(state.base + screenNameRva),
        screenPrefix.data(), screenPrefix.size());
    if (automaticFrame && screenCompatible &&
        MH_CreateHook(reinterpret_cast<void*>(state.base + screenNameRva), reinterpret_cast<void*>(&observeScreenName),
            reinterpret_cast<void**>(&state.screenName)) == MH_OK) {
        if (MH_EnableHook(reinterpret_cast<void*>(state.base + screenNameRva)) == MH_OK) {
            diagnostic("native_screen_observer_installed primary_caller=0x65b0119 sample_ms=250");
        } else {
            diagnostic("native_screen_observer_enable_failed reload_hooks_preserved");
        }
    } else {
        diagnostic("native_screen_observer_unavailable reload_hooks_preserved");
    }
}
class LegacyReloadBackend final : public ReloadBackend {
public:
    explicit LegacyReloadBackend(const std::filesystem::path& diagnosticLog, bool automaticFrame, LegacyReloadMode mode) { install(diagnosticLog, automaticFrame, mode); }
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
        diagnostic("native_reload_queued mode=" + std::string(state.mode == LegacyReloadMode::LegacyJsonUiOnly ? "legacy_json_ui_only" : state.resourceFallback ? "resource_rebuild" : "legacy_ui"));
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
ScreenMetrics& nativeScreenMetrics() noexcept { return state.screenMetrics; }
std::unique_ptr<ReloadBackend> makeLegacyReloadBackend(const std::filesystem::path& diagnosticLog, bool automaticFrame, LegacyReloadMode mode) { return std::make_unique<LegacyReloadBackend>(diagnosticLog, automaticFrame, mode); }
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
bool setResourceFallback(unsigned observedFlag) {
    std::lock_guard lock(state.mutex);
    if (!state.enabled || state.pending || state.busy || state.uiCompletion.inFlight() || observedFlag > 1) return false;
    state.mode = LegacyReloadMode::FullResourceReload;
    state.resourceFlag = observedFlag;
    state.resourceFallback = true;
    diagnostic("resource_fallback_selected observed_flag=" + std::to_string(observedFlag));
    return true;
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
        << ",\"uiOnly\":" << (state.mode == LegacyReloadMode::LegacyJsonUiOnly)
        << ",\"uiActionInFlight\":" << state.uiCompletion.actionInFlight()
        << ",\"resourceFallback\":" << state.resourceFallback << ",\"resourceFlag\":" << state.resourceFlag << ",\"resourceEntryRva\":\"0x873a20\""
        << ",\"natural\":" << state.natural << ",\"submitted\":" << state.submitted
        << ",\"completed\":" << state.completed << "}";
    return out.str();
}
}
