#pragma once

#include "core/notifications.hpp"

#include <d3d12.h>
#include <dxgi1_4.h>
#include <wrl/client.h>

#include <cstdint>
#include <chrono>
#include <vector>

namespace conduit::windows {
class D3D12Hud {
public:
    D3D12Hud() = default;
    ~D3D12Hud();
    D3D12Hud(const D3D12Hud&) = delete;
    D3D12Hud& operator=(const D3D12Hud&) = delete;

    bool render(IDXGISwapChain* chain, ID3D12CommandQueue* queue,
                const std::vector<Notification>& items) noexcept;
    void beforeResize() noexcept;
    void shutdown() noexcept;
    bool ready() const noexcept { return ready_; }
private:
    struct Frame {
        Microsoft::WRL::ComPtr<ID3D12CommandAllocator> allocator;
        Microsoft::WRL::ComPtr<ID3D12Resource> buffer;
        Microsoft::WRL::ComPtr<ID3D12Resource> vertices;
        D3D12_VERTEX_BUFFER_VIEW vertexView{};
        std::uint64_t fenceValue = 0;
    };
    bool initialize(IDXGISwapChain* chain, ID3D12CommandQueue* queue) noexcept;
    bool createBuffers(IDXGISwapChain* chain) noexcept;
    bool createPipeline() noexcept;
    bool waitForFrame(Frame& frame) noexcept;
    Microsoft::WRL::ComPtr<ID3D12Device> device_;
    Microsoft::WRL::ComPtr<ID3D12CommandQueue> queue_;
    Microsoft::WRL::ComPtr<IDXGISwapChain3> chain_;
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> rtvHeap_;
    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> commandList_;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> pipeline_;
    Microsoft::WRL::ComPtr<ID3D12Fence> fence_;
    std::vector<Frame> frames_;
    HANDLE fenceEvent_ = nullptr;
    DXGI_FORMAT format_ = DXGI_FORMAT_UNKNOWN;
    UINT width_ = 0;
    UINT height_ = 0;
    UINT descriptorSize_ = 0;
    std::uint64_t nextFence_ = 1;
    bool ready_ = false;
    std::chrono::steady_clock::time_point firstPresent_{};
};
}
