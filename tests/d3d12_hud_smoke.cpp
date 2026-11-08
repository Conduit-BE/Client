#include "platform/windows/d3d12_hud.hpp"

#include <windows.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <wrl/client.h>

#include <array>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <filesystem>
#include <fstream>
#include <thread>
#include <chrono>

#if defined(CONDUIT_HOOK_SMOKE)
namespace fs = std::filesystem;
std::string readLog(const fs::path& path) {
    std::ifstream input(path);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}
bool waitForLog(const fs::path& path, const char* marker) {
    for (int i = 0; i < 200; ++i) {
        if (readLog(path).find(marker) != std::string::npos) { return true; }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    return false;
}
#endif

using Microsoft::WRL::ComPtr;

namespace {
void require(bool okay, const char* message) {
    if (!okay) { throw std::runtime_error(message); }
}

std::array<unsigned char, 4> readPixel(ID3D12Device* device, ID3D12CommandQueue* queue,
                                        IDXGISwapChain3* chain, UINT x, UINT y, UINT index) {
    ComPtr<ID3D12Resource> buffer, readback;
    ComPtr<ID3D12CommandAllocator> allocator;
    ComPtr<ID3D12GraphicsCommandList> commands;
    ComPtr<ID3D12Fence> fence;
    require(SUCCEEDED(chain->GetBuffer(index, IID_PPV_ARGS(&buffer))),
            "GetBuffer failed");
    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_READBACK;
    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Width = 256;
    desc.Height = 1;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    require(SUCCEEDED(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
        D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&readback))), "Readback allocation failed");
    require(SUCCEEDED(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
        IID_PPV_ARGS(&allocator))), "Readback allocator failed");
    require(SUCCEEDED(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
        allocator.Get(), nullptr, IID_PPV_ARGS(&commands))), "Readback list failed");
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = buffer.Get();
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
    commands->ResourceBarrier(1, &barrier);
    D3D12_TEXTURE_COPY_LOCATION source{};
    source.pResource = buffer.Get();
    source.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    D3D12_TEXTURE_COPY_LOCATION destination{};
    destination.pResource = readback.Get();
    destination.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    destination.PlacedFootprint.Footprint.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    destination.PlacedFootprint.Footprint.Width = 1;
    destination.PlacedFootprint.Footprint.Height = 1;
    destination.PlacedFootprint.Footprint.Depth = 1;
    destination.PlacedFootprint.Footprint.RowPitch = 256;
    const D3D12_BOX box{x, y, 0, x + 1, y + 1, 1};
    commands->CopyTextureRegion(&destination, 0, 0, 0, &source, &box);
    std::swap(barrier.Transition.StateBefore, barrier.Transition.StateAfter);
    commands->ResourceBarrier(1, &barrier);
    require(SUCCEEDED(commands->Close()), "Readback close failed");
    ID3D12CommandList* lists[]{commands.Get()};
    queue->ExecuteCommandLists(1, lists);
    require(SUCCEEDED(device->CreateFence(0, D3D12_FENCE_FLAG_NONE,
        IID_PPV_ARGS(&fence))), "Fence creation failed");
    require(SUCCEEDED(queue->Signal(fence.Get(), 1)), "Fence signal failed");
    const HANDLE event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    require(event != nullptr, "Fence event failed");
    require(SUCCEEDED(fence->SetEventOnCompletion(1, event)) &&
            WaitForSingleObject(event, 5000) == WAIT_OBJECT_0, "Readback timed out");
    CloseHandle(event);
    void* mapped = nullptr;
    require(SUCCEEDED(readback->Map(0, nullptr, &mapped)), "Readback map failed");
    std::array<unsigned char, 4> pixel{};
    std::memcpy(pixel.data(), mapped, 4);
    readback->Unmap(0, nullptr);
    return pixel;
}
}

int main(int argc, char** argv) {
    try {
#if defined(CONDUIT_HOOK_SMOKE)
        require(argc == 2, "DLL path required");
        wchar_t local[32768]{};
        require(GetEnvironmentVariableW(L"LOCALAPPDATA", local, 32768) > 0,
                "LOCALAPPDATA unavailable");
        const auto log = fs::path(local) / L"ConduitClient" / L"logs" / L"conduit.log";
        std::error_code ignored;
        fs::remove(log, ignored);
        const std::string bytes(argv[1]);
        const fs::path dll(std::u8string(bytes.begin(), bytes.end()));
        const HMODULE module = LoadLibraryW(dll.c_str());
        require(module != nullptr, "DLL load failed");
        require(waitForLog(log, "hooks active"), "D3D12 hooks did not initialize");
#else
        (void)argc;
        (void)argv;
#endif
        WNDCLASSW klass{};
        klass.lpfnWndProc = DefWindowProcW;
        klass.hInstance = GetModuleHandleW(nullptr);
        klass.lpszClassName = L"ConduitD3D12HudSmoke";
        require(RegisterClassW(&klass) != 0, "Window class failed");
        const HWND window = CreateWindowW(klass.lpszClassName, L"Conduit HUD Smoke",
            WS_OVERLAPPEDWINDOW, 100, 100, 640, 360, nullptr, nullptr, klass.hInstance, nullptr);
        require(window != nullptr, "Window creation failed");
        ShowWindow(window, SW_SHOW);
        ComPtr<ID3D12Device> device;
        ComPtr<ID3D12CommandQueue> queue;
        ComPtr<IDXGIFactory4> factory;
        ComPtr<IDXGISwapChain1> chain1;
        ComPtr<IDXGISwapChain3> chain;
        require(SUCCEEDED(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0,
            IID_PPV_ARGS(&device))), "D3D12 device failed");
        D3D12_COMMAND_QUEUE_DESC queueDesc{};
        queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
        require(SUCCEEDED(device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&queue))),
                "D3D12 queue failed");
        require(SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))), "DXGI factory failed");
        DXGI_SWAP_CHAIN_DESC1 desc{};
        desc.Width = 640;
        desc.Height = 360;
        desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        desc.BufferCount = 2;
        desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        require(SUCCEEDED(factory->CreateSwapChainForHwnd(queue.Get(), window, &desc,
            nullptr, nullptr, &chain1)) && SUCCEEDED(chain1.As(&chain)), "Swap chain failed");
        {
#if defined(CONDUIT_HOOK_SMOKE)
            ComPtr<ID3D12CommandAllocator> allocator;
            ComPtr<ID3D12GraphicsCommandList> list;
            require(SUCCEEDED(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                IID_PPV_ARGS(&allocator))), "Queue capture allocator failed");
            require(SUCCEEDED(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
                allocator.Get(), nullptr, IID_PPV_ARGS(&list))) && SUCCEEDED(list->Close()),
                "Queue capture list failed");
            ID3D12CommandList* lists[]{list.Get()};
            queue->ExecuteCommandLists(1, lists);
            const UINT presentedIndex = chain->GetCurrentBackBufferIndex();
            require(SUCCEEDED(chain->Present(0, 0)), "Present failed");
            require(waitForLog(log, "HUD submitted"), "Present hook did not submit HUD");
            const auto panelPixel = readPixel(device.Get(), queue.Get(), chain.Get(),
                                              365, 25, presentedIndex);
            const auto textPixel = readPixel(device.Get(), queue.Get(), chain.Get(),
                                             374, 30, presentedIndex);
            require(panelPixel[0] < 40 && panelPixel[1] < 40 && panelPixel[2] < 40,
                    "Present hook panel pixel is not dark");
            require(textPixel[0] > 200 && textPixel[1] > 200 && textPixel[2] > 200,
                    "Present hook text pixel is not bright");
            const auto autoStop = reinterpret_cast<void (*)()>(GetProcAddress(module, "ConduitAutoStop"));
            require(autoStop != nullptr, "ConduitAutoStop missing");
            autoStop();
            require(FreeLibrary(module) != 0, "DLL unload failed");
#else
            conduit::windows::D3D12Hud hud;
            require(hud.render(chain.Get(), queue.Get(), {}), "HUD render failed");
            const UINT currentIndex = chain->GetCurrentBackBufferIndex();
            const auto background = readPixel(device.Get(), queue.Get(), chain.Get(), 365, 25, currentIndex);
            const auto text = readPixel(device.Get(), queue.Get(), chain.Get(), 374, 30, currentIndex);
            require(background[0] < 40 && background[1] < 40 && background[2] < 40,
                    "HUD panel pixel is not dark");
            require(text[0] > 200 && text[1] > 200 && text[2] > 200,
                    "HUD text pixel is not bright");
            hud.beforeResize();
            require(SUCCEEDED(chain->ResizeBuffers(2, 800, 450, DXGI_FORMAT_R8G8B8A8_UNORM, 0)),
                    "ResizeBuffers failed");
            require(hud.render(chain.Get(), queue.Get(), {}), "HUD render after resize failed");
            hud.shutdown();
#endif
        }
        chain.Reset(); chain1.Reset(); queue.Reset(); device.Reset(); factory.Reset();
        DestroyWindow(window);
        UnregisterClassW(klass.lpszClassName, klass.hInstance);
#if defined(CONDUIT_HOOK_SMOKE)
        std::cout << "D3D12 Present hook submitted HUD\n";
#else
        std::cout << "D3D12 HUD pixels and resize passed\n";
#endif
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
