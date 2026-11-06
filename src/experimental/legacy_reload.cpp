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
    std::unique_ptr<Logger> diagnostics;
    std::uintptr_t owner = 0, base = 0;
    DWORD gameThread = 0, observedThread = 0;
    std::uint64_t submitted = 0, completed = 0, natural = 0, generation = 0, finished = 0;
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
            state.busy = false;
            if (state.submitted > state.completed) {
                state.completed = state.submitted;
                state.finished = state.generation;
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
                diagnostic("automatic_backend_armed verified_frame_caller=0x83274 resource_entry=0x873a20 flag=1");
            }
        }
        if (state.enabled && !state.automaticFrame && !state.owner && state.observedThread == GetCurrentThreadId() && validOwner(currentOwner)) {
            state.owner = reinterpret_cast<std::uintptr_t>(currentOwner);
            state.gameThread = GetCurrentThreadId();
            diagnostic("owner_acquired_from_current_frame after_live_reload_thread_observation");
        }
        // Reacquire from this natural call; never dereference a cached heap pointer.
        if ((state.automaticFrame && caller != state.base + frameCallerRva) ||
            !state.enabled || state.faulted || !state.pending || state.busy ||
            state.gameThread != GetCurrentThreadId() ||
            state.owner != reinterpret_cast<std::uintptr_t>(currentOwner) || !validOwner(currentOwner)) return;
        state.pending = false;
        state.busy = true;
        ++state.submitted;
        diagnostic("native_reload_dispatched mode=" + std::string(state.resourceFallback ? "resource_rebuild" : "legacy_ui"));
    }
    // Single RCX method with ignored return, independently confirmed from the original code.
    injectedLoader = true;
    if (state.resourceFallback) {
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
void install(const std::filesystem::path& diagnosticLog, bool automaticFrame) {
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
    // This DLL has its own MinHook instance and owns exactly these three hooks.
    const auto activation = MH_EnableHook(MH_ALL_HOOKS);
    lock.lock();
    if (activation != MH_OK) {
        state.enabled = false;
        lock.unlock();
        MH_DisableHook(MH_ALL_HOOKS);
        throw std::runtime_error("Native hook activation failed");
    }
    state.automaticFrame = automaticFrame;
    if (automaticFrame) { state.resourceFallback = true; state.resourceFlag = 1; }
    state.enabled = true;
    diagnostic("backend_enabled waiting_for_live_owner_affinity");
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
        if (state.busy || state.pending) return ReloadResult::Failed;
        const auto generation = ++state.generation;
        state.pending = true;
        diagnostic("native_reload_queued mode=" + std::string(state.resourceFallback ? "resource_rebuild" : "legacy_ui"));
        if (!state.wake.wait_for(lock, std::chrono::seconds(8), [&] { return !state.enabled || state.finished >= generation; })) {
            state.pending = false;
            state.faulted = true;
            diagnostic("native_reload_timeout backend_disabled_until_restart"); // No retry after an unexpected or missing completion.
            return ReloadResult::Failed;
        }
        return state.enabled && state.finished >= generation ? ReloadResult::Succeeded : ReloadResult::Failed;
    }
};
}
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
bool setResourceFallback(unsigned observedFlag) {
    std::lock_guard lock(state.mutex);
    if (!state.enabled || state.pending || state.busy || observedFlag > 1) return false;
    state.resourceFlag = observedFlag;
    state.resourceFallback = true;
    diagnostic("resource_fallback_selected observed_flag=" + std::to_string(observedFlag));
    return true;
}
void disableLegacyReload() noexcept {
    std::lock_guard lock(state.mutex);
    state.enabled = false;
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
        << ",\"resourceFallback\":" << state.resourceFallback << ",\"resourceFlag\":" << state.resourceFlag << ",\"resourceEntryRva\":\"0x873a20\""
        << ",\"natural\":" << state.natural << ",\"submitted\":" << state.submitted
        << ",\"completed\":" << state.completed << "}";
    return out.str();
}
}
