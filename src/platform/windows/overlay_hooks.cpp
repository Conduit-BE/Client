#include "platform/windows/overlay_hooks.hpp"

#include "platform/windows/d3d12_hud.hpp"
#include "core/logger.hpp"
#include "core/notifications.hpp"

#include <MinHook.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <wrl/client.h>

#include <atomic>
#include <algorithm>
#include <mutex>
#include <string>
#include <vector>

namespace conduit::windows {
namespace {
using Microsoft::WRL::ComPtr;
using Present = HRESULT (STDMETHODCALLTYPE*)(IDXGISwapChain*, UINT, UINT);
using Present1 = HRESULT (STDMETHODCALLTYPE*)(IDXGISwapChain1*, UINT, UINT, const DXGI_PRESENT_PARAMETERS*);
using ResizeBuffers = HRESULT (STDMETHODCALLTYPE*)(IDXGISwapChain*, UINT, UINT, UINT, DXGI_FORMAT, UINT);
using ExecuteLists = void (STDMETHODCALLTYPE*)(ID3D12CommandQueue*, UINT, ID3D12CommandList* const*);
using QueueSignal = HRESULT (STDMETHODCALLTYPE*)(ID3D12CommandQueue*, ID3D12Fence*, UINT64);
using QueueWait = HRESULT (STDMETHODCALLTYPE*)(ID3D12CommandQueue*, ID3D12Fence*, UINT64);

struct State {
    std::mutex mutex;
    D3D12Hud hud;
    std::vector<ComPtr<ID3D12CommandQueue>> queues;
    NotificationCenter* notifications = nullptr;
    Logger* logger = nullptr;
    std::atomic<unsigned> callbacks{0};
    std::atomic<bool> enabled{false};
    std::atomic<bool> rendered{false};
    std::atomic<unsigned long long> presents{0};
    std::atomic<unsigned long long> executes{0};
    std::atomic<unsigned long long> signals{0};
    std::atomic<unsigned long long> waits{0};
    std::atomic<unsigned long long> deviceMisses{0};
    std::atomic<unsigned long long> queueMisses{0};
    bool errorReported = false;
    void* presentAddress = nullptr;
    void* present1Address = nullptr;
    void* resizeAddress = nullptr;
    void* executeAddress = nullptr;
    void* signalAddress = nullptr;
    void* waitAddress = nullptr;
    Present originalPresent = nullptr;
    Present1 originalPresent1 = nullptr;
    ResizeBuffers originalResize = nullptr;
    ExecuteLists originalExecute = nullptr;
    QueueSignal originalSignal = nullptr;
    QueueWait originalWait = nullptr;
};
State state;
thread_local bool inOverlay = false;

struct CallbackGuard {
    CallbackGuard() { state.callbacks.fetch_add(1); }
    ~CallbackGuard() { state.callbacks.fetch_sub(1); }
};

void renderIfPossible(IDXGISwapChain* chain, UINT flags) noexcept {
    state.presents.fetch_add(1, std::memory_order_relaxed);
    if (!state.enabled.load(std::memory_order_acquire) || inOverlay ||
        (flags & DXGI_PRESENT_TEST)) { return; }
    CallbackGuard guard;
    if (!state.enabled.load(std::memory_order_acquire)) { return; }
    const std::lock_guard lock(state.mutex);
    if (state.queues.empty() || !state.notifications) { return; }
    try {
        ComPtr<ID3D12Device> device;
        if (FAILED(chain->GetDevice(IID_PPV_ARGS(&device)))) {
            state.deviceMisses.fetch_add(1, std::memory_order_relaxed);
            return;
        }
        ComPtr<IUnknown> identity;
        if (FAILED(device.As(&identity))) { return; }
        ID3D12CommandQueue* matchingQueue = nullptr;
        for (auto queue = state.queues.rbegin(); queue != state.queues.rend(); ++queue) {
            ComPtr<ID3D12Device> queueDevice;
            ComPtr<IUnknown> queueIdentity;
            if (SUCCEEDED((*queue)->GetDevice(IID_PPV_ARGS(&queueDevice))) &&
                SUCCEEDED(queueDevice.As(&queueIdentity)) &&
                identity.Get() == queueIdentity.Get()) {
                matchingQueue = queue->Get();
                break;
            }
        }
        if (!matchingQueue) {
            state.queueMisses.fetch_add(1, std::memory_order_relaxed);
            return;
        }
        inOverlay = true;
        const auto items = state.notifications->snapshot(std::chrono::steady_clock::now());
        const bool success = state.hud.render(chain, matchingQueue, items);
        inOverlay = false;
        if (success && state.hud.ready()) {
            if (!state.rendered.exchange(true) && state.logger) {
                state.logger->write("info", "D3D12 HUD submitted to game swap chain");
            }
        } else if (!state.errorReported && state.logger) {
            state.errorReported = true;
            state.logger->write("error", "D3D12 HUD render failed; game presentation continues");
        }
    } catch (...) {
        inOverlay = false;
        if (!state.errorReported && state.logger) {
            state.errorReported = true;
            state.logger->write("error", "D3D12 HUD callback raised an exception");
        }
    }
}

HRESULT STDMETHODCALLTYPE onPresent(IDXGISwapChain* chain, UINT sync, UINT flags) {
    renderIfPossible(chain, flags);
    return state.originalPresent(chain, sync, flags);
}

HRESULT STDMETHODCALLTYPE onPresent1(IDXGISwapChain1* chain, UINT sync, UINT flags,
                                     const DXGI_PRESENT_PARAMETERS* parameters) {
    renderIfPossible(chain, flags);
    return state.originalPresent1(chain, sync, flags, parameters);
}

HRESULT STDMETHODCALLTYPE onResize(IDXGISwapChain* chain, UINT count, UINT width,
                                   UINT height, DXGI_FORMAT format, UINT flags) {
    CallbackGuard guard;
    if (state.enabled.load()) {
        const std::lock_guard lock(state.mutex);
        state.hud.beforeResize();
    }
    return state.originalResize(chain, count, width, height, format, flags);
}

void observeQueue(ID3D12CommandQueue* queue) {
    if (state.enabled.load(std::memory_order_acquire) && !inOverlay &&
        queue->GetDesc().Type == D3D12_COMMAND_LIST_TYPE_DIRECT) {
        const std::lock_guard lock(state.mutex);
        const auto found = std::find_if(state.queues.begin(), state.queues.end(),
            [queue](const auto& candidate) { return candidate.Get() == queue; });
        if (found == state.queues.end()) {
            state.queues.emplace_back(queue);
            if (state.queues.size() > 8) { state.queues.erase(state.queues.begin()); }
            if (state.logger) { state.logger->write("info", "Observed D3D12 direct command queue"); }
        }
    }
}

void STDMETHODCALLTYPE onExecute(ID3D12CommandQueue* queue, UINT count,
                                 ID3D12CommandList* const* lists) {
    state.executes.fetch_add(1, std::memory_order_relaxed);
    CallbackGuard guard;
    observeQueue(queue);
    state.originalExecute(queue, count, lists);
}

HRESULT STDMETHODCALLTYPE onSignal(ID3D12CommandQueue* queue, ID3D12Fence* fence, UINT64 value) {
    state.signals.fetch_add(1, std::memory_order_relaxed);
    CallbackGuard guard;
    observeQueue(queue);
    return state.originalSignal(queue, fence, value);
}

HRESULT STDMETHODCALLTYPE onWait(ID3D12CommandQueue* queue, ID3D12Fence* fence, UINT64 value) {
    state.waits.fetch_add(1, std::memory_order_relaxed);
    CallbackGuard guard;
    observeQueue(queue);
    return state.originalWait(queue, fence, value);
}

bool getHookAddresses() noexcept {
    ComPtr<ID3D12Device> device;
    ComPtr<ID3D12CommandQueue> queue;
    ComPtr<IDXGIFactory4> factory;
    ComPtr<IDXGISwapChain> chain;
    if (FAILED(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device))) ||
        FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) { return false; }
    D3D12_COMMAND_QUEUE_DESC queueDesc{};
    queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    if (FAILED(device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&queue)))) { return false; }
    WNDCLASSW klass{};
    klass.lpfnWndProc = DefWindowProcW;
    klass.hInstance = GetModuleHandleW(nullptr);
    klass.lpszClassName = L"ConduitD3D12HookProbe";
    if (!RegisterClassW(&klass)) { return false; }
    const HWND window = CreateWindowW(klass.lpszClassName, L"", WS_OVERLAPPED,
                                     0, 0, 8, 8, nullptr, nullptr, klass.hInstance, nullptr);
    bool success = false;
    if (window) {
        DXGI_SWAP_CHAIN_DESC desc{};
        desc.BufferDesc.Width = 8;
        desc.BufferDesc.Height = 8;
        desc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        desc.BufferCount = 2;
        desc.OutputWindow = window;
        desc.Windowed = TRUE;
        desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        if (SUCCEEDED(factory->CreateSwapChain(queue.Get(), &desc, &chain))) {
            ComPtr<IDXGISwapChain1> chain1;
            success = SUCCEEDED(chain.As(&chain1));
            if (success) {
                auto** swapTable = *reinterpret_cast<void***>(chain.Get());
                auto** swap1Table = *reinterpret_cast<void***>(chain1.Get());
                auto** queueTable = *reinterpret_cast<void***>(queue.Get());
                state.presentAddress = swapTable[8];
                state.resizeAddress = swapTable[13];
                state.present1Address = swap1Table[22];
                state.executeAddress = queueTable[10];
                state.signalAddress = queueTable[14];
                state.waitAddress = queueTable[15];
            }
        }
        chain.Reset();
        DestroyWindow(window);
    }
    UnregisterClassW(klass.lpszClassName, klass.hInstance);
    return success;
}
}

bool installOverlayHooks(NotificationCenter& notifications, Logger& logger) noexcept {
    if (!getHookAddresses()) {
        logger.write("error", "D3D12/DXGI probe failed; HUD disabled");
        return false;
    }
    if (MH_Initialize() != MH_OK) {
        logger.write("error", "MinHook initialization failed; HUD disabled");
        return false;
    }
    const bool created =
        MH_CreateHook(state.presentAddress, reinterpret_cast<void*>(&onPresent),
                      reinterpret_cast<void**>(&state.originalPresent)) == MH_OK &&
        MH_CreateHook(state.present1Address, reinterpret_cast<void*>(&onPresent1),
                      reinterpret_cast<void**>(&state.originalPresent1)) == MH_OK &&
        MH_CreateHook(state.resizeAddress, reinterpret_cast<void*>(&onResize),
                      reinterpret_cast<void**>(&state.originalResize)) == MH_OK &&
        MH_CreateHook(state.executeAddress, reinterpret_cast<void*>(&onExecute),
                      reinterpret_cast<void**>(&state.originalExecute)) == MH_OK &&
        MH_CreateHook(state.signalAddress, reinterpret_cast<void*>(&onSignal),
                      reinterpret_cast<void**>(&state.originalSignal)) == MH_OK &&
        MH_CreateHook(state.waitAddress, reinterpret_cast<void*>(&onWait),
                      reinterpret_cast<void**>(&state.originalWait)) == MH_OK;
    if (!created) {
        MH_Uninitialize();
        logger.write("error", "DXGI/D3D12 hook creation failed; HUD disabled");
        return false;
    }
    state.notifications = &notifications;
    state.logger = &logger;
    state.errorReported = false;
    state.enabled.store(true, std::memory_order_release);
    if (MH_EnableHook(MH_ALL_HOOKS) != MH_OK) {
        state.enabled.store(false);
        MH_Uninitialize();
        state.notifications = nullptr;
        state.logger = nullptr;
        logger.write("error", "DXGI/D3D12 hook enable failed; HUD disabled");
        return false;
    }
    logger.write("info", "DXGI Present/Present1/ResizeBuffers and D3D12 queue Execute/Signal/Wait hooks active");
    return true;
}

void removeOverlayHooks() noexcept {
    state.enabled.store(false, std::memory_order_release);
    MH_DisableHook(MH_ALL_HOOKS);
    while (state.callbacks.load() != 0) { Sleep(1); }
    {
        const std::lock_guard lock(state.mutex);
        state.hud.shutdown();
        state.queues.clear();
        state.notifications = nullptr;
        state.logger = nullptr;
    }
    MH_Uninitialize();
}

bool overlayHasRendered() noexcept { return state.rendered.load(); }

void logOverlayDiagnostics(Logger& logger) noexcept {
    logger.write("info", "HUD diagnostic: presents=" + std::to_string(state.presents.load()) +
        ", queue submissions=" + std::to_string(state.executes.load()) +
        ", queue signals=" + std::to_string(state.signals.load()) +
        ", queue waits=" + std::to_string(state.waits.load()) +
        ", swap-chain device misses=" + std::to_string(state.deviceMisses.load()) +
        ", queue matches missed=" + std::to_string(state.queueMisses.load()) +
        ", rendered=" + std::to_string(state.rendered.load()));
}
}
