#include "platform/windows/d3d12_hud.hpp"

#include <d3dcompiler.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <string_view>

namespace conduit::windows {
namespace {
using Microsoft::WRL::ComPtr;
constexpr UINT vertexCapacity = 65536;
struct Vertex { float x; float y; };

// Five-bit rows, MSB at the left. The first renderer intentionally needs no
// font texture, input backend, or extra descriptor heap.
constexpr std::array<std::array<std::uint8_t, 7>, 26> glyphs{{
    {{14,17,17,31,17,17,17}}, {{30,17,17,30,17,17,30}},
    {{14,17,16,16,16,17,14}}, {{30,17,17,17,17,17,30}},
    {{31,16,16,30,16,16,31}}, {{31,16,16,30,16,16,16}},
    {{14,17,16,23,17,17,14}}, {{17,17,17,31,17,17,17}},
    {{31,4,4,4,4,4,31}}, {{7,2,2,2,18,18,12}},
    {{17,18,20,24,20,18,17}}, {{16,16,16,16,16,16,31}},
    {{17,27,21,21,17,17,17}}, {{17,25,21,19,17,17,17}},
    {{14,17,17,17,17,17,14}}, {{30,17,17,30,16,16,16}},
    {{14,17,17,17,21,18,13}}, {{30,17,17,30,20,18,17}},
    {{15,16,16,14,1,1,30}}, {{31,4,4,4,4,4,4}},
    {{17,17,17,17,17,17,14}}, {{17,17,17,17,17,10,4}},
    {{17,17,17,21,21,21,10}}, {{17,17,10,4,10,17,17}},
    {{17,17,10,4,4,4,4}}, {{31,1,2,4,8,16,31}}
}};
constexpr std::array<std::array<std::uint8_t, 7>, 10> digits{{
    {{14,17,19,21,25,17,14}}, {{4,12,4,4,4,4,14}},
    {{14,17,1,2,4,8,31}}, {{30,1,1,14,1,1,30}},
    {{2,6,10,18,31,2,2}}, {{31,16,16,30,1,1,30}},
    {{14,16,16,30,17,17,14}}, {{31,1,2,4,8,8,8}},
    {{14,17,17,14,17,17,14}}, {{14,17,17,15,1,1,14}}
}};

void addPixel(std::vector<Vertex>& out, float x, float y, float size, float w, float h) {
    const auto nx = [w](float px) { return px * 2.0f / w - 1.0f; };
    const auto ny = [h](float py) { return 1.0f - py * 2.0f / h; };
    const float x0 = nx(x), x1 = nx(x + size), y0 = ny(y), y1 = ny(y + size);
    out.insert(out.end(), {{x0,y0},{x1,y0},{x0,y1},{x1,y0},{x1,y1},{x0,y1}});
}

void addText(std::vector<Vertex>& out, std::string_view text, float x, float y,
             float scale, float w, float h) {
    for (const char raw : text) {
        char ch = raw;
        if (ch >= 'a' && ch <= 'z') { ch -= 'a' - 'A'; }
        if ((ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9')) {
            const auto& rows = ch >= '0' && ch <= '9'
                ? digits[static_cast<unsigned>(ch - '0')]
                : glyphs[static_cast<unsigned>(ch - 'A')];
            for (unsigned row = 0; row < 7; ++row) {
                for (unsigned column = 0; column < 5; ++column) {
                    if (rows[row] & (1u << (4 - column))) {
                        addPixel(out, x + column * scale, y + row * scale, scale, w, h);
                    }
                }
            }
        }
        if (ch == '.') { addPixel(out, x + 2 * scale, y + 6 * scale, scale, w, h); }
        if (ch == '-') {
            for (unsigned column = 0; column < 5; ++column) {
                addPixel(out, x + column * scale, y + 3 * scale, scale, w, h);
            }
        }
        x += 6 * scale;
        if (out.size() + 250 > vertexCapacity) { break; }
    }
}

constexpr char shader[] = R"(
struct VSIn { float2 position : POSITION; };
struct VSOut { float4 position : SV_POSITION; };
VSOut VSMain(VSIn input) { VSOut output; output.position = float4(input.position, 0, 1); return output; }
float4 PSMain() : SV_TARGET { return float4(0.95, 0.98, 1.0, 1.0); }
)";

bool sameDevice(ID3D12Device* a, ID3D12Device* b) {
    ComPtr<IUnknown> first, second;
    return a && b && SUCCEEDED(a->QueryInterface(IID_PPV_ARGS(&first))) &&
        SUCCEEDED(b->QueryInterface(IID_PPV_ARGS(&second))) && first.Get() == second.Get();
}
}

D3D12Hud::~D3D12Hud() { shutdown(); }

bool D3D12Hud::createPipeline() noexcept {
    ComPtr<ID3DBlob> vertexShader, pixelShader, signature;
    D3D12_ROOT_SIGNATURE_DESC root{};
    root.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
    if (FAILED(D3D12SerializeRootSignature(&root, D3D_ROOT_SIGNATURE_VERSION_1,
                                           &signature, nullptr)) ||
        FAILED(device_->CreateRootSignature(0, signature->GetBufferPointer(),
                                           signature->GetBufferSize(), IID_PPV_ARGS(&rootSignature_))) ||
        FAILED(D3DCompile(shader, sizeof(shader) - 1, nullptr, nullptr, nullptr,
                          "VSMain", "vs_5_0", 0, 0, &vertexShader, nullptr)) ||
        FAILED(D3DCompile(shader, sizeof(shader) - 1, nullptr, nullptr, nullptr,
                          "PSMain", "ps_5_0", 0, 0, &pixelShader, nullptr))) { return false; }
    D3D12_INPUT_ELEMENT_DESC input{"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0,
                                   D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0};
    D3D12_GRAPHICS_PIPELINE_STATE_DESC desc{};
    desc.pRootSignature = rootSignature_.Get();
    desc.VS = {vertexShader->GetBufferPointer(), vertexShader->GetBufferSize()};
    desc.PS = {pixelShader->GetBufferPointer(), pixelShader->GetBufferSize()};
    desc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    desc.SampleMask = UINT_MAX;
    desc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    desc.DepthStencilState.DepthEnable = FALSE;
    desc.DepthStencilState.StencilEnable = FALSE;
    desc.InputLayout = {&input, 1};
    desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    desc.NumRenderTargets = 1;
    desc.RTVFormats[0] = format_;
    desc.SampleDesc.Count = 1;
    return SUCCEEDED(device_->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(&pipeline_)));
}

bool D3D12Hud::createBuffers(IDXGISwapChain* chain) noexcept {
    DXGI_SWAP_CHAIN_DESC desc{};
    if (FAILED(chain->GetDesc(&desc)) || desc.BufferCount == 0 || desc.BufferCount > 8 ||
        desc.BufferDesc.Width == 0 || desc.BufferDesc.Height == 0) { return false; }
    width_ = desc.BufferDesc.Width;
    height_ = desc.BufferDesc.Height;
    format_ = desc.BufferDesc.Format;
    D3D12_DESCRIPTOR_HEAP_DESC heap{};
    heap.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    heap.NumDescriptors = desc.BufferCount;
    if (FAILED(device_->CreateDescriptorHeap(&heap, IID_PPV_ARGS(&rtvHeap_)))) { return false; }
    descriptorSize_ = device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    frames_.resize(desc.BufferCount);
    D3D12_HEAP_PROPERTIES upload{};
    upload.Type = D3D12_HEAP_TYPE_UPLOAD;
    D3D12_RESOURCE_DESC resource{};
    resource.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    resource.Width = vertexCapacity * sizeof(Vertex);
    resource.Height = 1;
    resource.DepthOrArraySize = 1;
    resource.MipLevels = 1;
    resource.SampleDesc.Count = 1;
    resource.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    for (UINT index = 0; index < desc.BufferCount; ++index) {
        auto& frame = frames_[index];
        if (FAILED(chain->GetBuffer(index, IID_PPV_ARGS(&frame.buffer))) ||
            FAILED(device_->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                                    IID_PPV_ARGS(&frame.allocator))) ||
            FAILED(device_->CreateCommittedResource(&upload, D3D12_HEAP_FLAG_NONE, &resource,
                   D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&frame.vertices)))) {
            return false;
        }
        auto handle = rtvHeap_->GetCPUDescriptorHandleForHeapStart();
        handle.ptr += index * descriptorSize_;
        device_->CreateRenderTargetView(frame.buffer.Get(), nullptr, handle);
        frame.vertexView.BufferLocation = frame.vertices->GetGPUVirtualAddress();
        frame.vertexView.StrideInBytes = sizeof(Vertex);
        frame.vertexView.SizeInBytes = static_cast<UINT>(resource.Width);
    }
    return true;
}

bool D3D12Hud::initialize(IDXGISwapChain* chain, ID3D12CommandQueue* queue) noexcept {
    if (!chain || !queue || FAILED(chain->QueryInterface(IID_PPV_ARGS(&chain_))) ||
        FAILED(chain->GetDevice(IID_PPV_ARGS(&device_)))) { return false; }
    ComPtr<ID3D12Device> queueDevice;
    if (FAILED(queue->GetDevice(IID_PPV_ARGS(&queueDevice))) ||
        !sameDevice(device_.Get(), queueDevice.Get()) ||
        queue->GetDesc().Type != D3D12_COMMAND_LIST_TYPE_DIRECT) { return false; }
    queue_ = queue;
    if (!createBuffers(chain) || !createPipeline() ||
        FAILED(device_->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
               frames_[0].allocator.Get(), pipeline_.Get(), IID_PPV_ARGS(&commandList_))) ||
        FAILED(commandList_->Close()) ||
        FAILED(device_->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence_)))) { return false; }
    fenceEvent_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!fenceEvent_) { return false; }
    firstPresent_ = std::chrono::steady_clock::now();
    ready_ = true;
    return true;
}

bool D3D12Hud::waitForFrame(Frame& frame) noexcept {
    if (!frame.fenceValue || fence_->GetCompletedValue() >= frame.fenceValue) { return true; }
    return SUCCEEDED(fence_->SetEventOnCompletion(frame.fenceValue, fenceEvent_)) &&
        WaitForSingleObject(fenceEvent_, 250) == WAIT_OBJECT_0;
}

bool D3D12Hud::render(IDXGISwapChain* chain, ID3D12CommandQueue* queue,
                      const std::vector<Notification>& items) noexcept {
    if (ready_ && (chain != chain_.Get() || queue != queue_.Get())) {
        shutdown();
    }
    if (!ready_) {
        shutdown();
        if (!initialize(chain, queue)) { shutdown(); return false; }
    }
    DXGI_SWAP_CHAIN_DESC desc{};
    if (FAILED(chain->GetDesc(&desc))) { return false; }
    if (desc.BufferDesc.Width != width_ || desc.BufferDesc.Height != height_ ||
        desc.BufferDesc.Format != format_ || desc.BufferCount != frames_.size()) {
        beforeResize();
        return false;
    }
    const UINT index = chain_->GetCurrentBackBufferIndex();
    if (index >= frames_.size()) { return false; }
    auto& frame = frames_[index];
    if (!waitForFrame(frame)) { return false; }

    std::vector<Vertex> vertices;
    vertices.reserve(16384);
    struct Panel { D3D12_RECT rect; };
    std::vector<Panel> panels;
    const bool startup = std::chrono::steady_clock::now() - firstPresent_ < std::chrono::seconds(60);
    const auto addPanel = [&](std::string_view title, std::string_view description, unsigned row) {
        if (width_ < 300 || height_ < 90 || 20 + row * 65 + 55 > height_) { return; }
        const float x = static_cast<float>(width_ - 280);
        const float y = static_cast<float>(20 + row * 65);
        panels.push_back({{static_cast<LONG>(x), static_cast<LONG>(y),
                          static_cast<LONG>(x + 260), static_cast<LONG>(y + 55)}});
        addText(vertices, title, x + 12, y + 10, 2, static_cast<float>(width_), static_cast<float>(height_));
        if (!description.empty()) {
            addText(vertices, description.substr(0, 32), x + 12, y + 31, 1,
                    static_cast<float>(width_), static_cast<float>(height_));
        }
    };
    unsigned row = 0;
    if (startup) { addPanel("Conduit Client Loaded", {}, row++); }
    for (const auto& item : items) {
        addPanel(item.title, item.description, row++);
    }
    if (panels.empty() || vertices.size() > vertexCapacity) { return true; }
    void* mapped = nullptr;
    D3D12_RANGE noRead{0, 0};
    if (FAILED(frame.vertices->Map(0, &noRead, &mapped))) { return false; }
    std::memcpy(mapped, vertices.data(), vertices.size() * sizeof(Vertex));
    D3D12_RANGE written{0, vertices.size() * sizeof(Vertex)};
    frame.vertices->Unmap(0, &written);
    if (FAILED(frame.allocator->Reset()) ||
        FAILED(commandList_->Reset(frame.allocator.Get(), pipeline_.Get()))) { return false; }
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = frame.buffer.Get();
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
    commandList_->ResourceBarrier(1, &barrier);
    auto handle = rtvHeap_->GetCPUDescriptorHandleForHeapStart();
    handle.ptr += index * descriptorSize_;
    const float background[4]{0.04f, 0.06f, 0.09f, 1.0f};
    for (const auto& panel : panels) {
        commandList_->ClearRenderTargetView(handle, background, 1, &panel.rect);
    }
    commandList_->OMSetRenderTargets(1, &handle, FALSE, nullptr);
    D3D12_VIEWPORT viewport{0, 0, static_cast<float>(width_), static_cast<float>(height_), 0, 1};
    D3D12_RECT scissor{0, 0, static_cast<LONG>(width_), static_cast<LONG>(height_)};
    commandList_->RSSetViewports(1, &viewport);
    commandList_->RSSetScissorRects(1, &scissor);
    commandList_->SetGraphicsRootSignature(rootSignature_.Get());
    commandList_->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    commandList_->IASetVertexBuffers(0, 1, &frame.vertexView);
    commandList_->DrawInstanced(static_cast<UINT>(vertices.size()), 1, 0, 0);
    std::swap(barrier.Transition.StateBefore, barrier.Transition.StateAfter);
    commandList_->ResourceBarrier(1, &barrier);
    if (FAILED(commandList_->Close())) { return false; }
    ID3D12CommandList* lists[]{commandList_.Get()};
    queue_->ExecuteCommandLists(1, lists);
    const auto fenceValue = nextFence_++;
    if (FAILED(queue_->Signal(fence_.Get(), fenceValue))) { return false; }
    frame.fenceValue = fenceValue;
    return true;
}

void D3D12Hud::beforeResize() noexcept {
    if (fence_ && fenceEvent_) {
        for (auto& frame : frames_) { waitForFrame(frame); }
    }
    frames_.clear();
    rtvHeap_.Reset();
    commandList_.Reset();
    rootSignature_.Reset();
    pipeline_.Reset();
    fence_.Reset();
    if (fenceEvent_) { CloseHandle(fenceEvent_); fenceEvent_ = nullptr; }
    chain_.Reset();
    queue_.Reset();
    device_.Reset();
    ready_ = false;
}

void D3D12Hud::shutdown() noexcept { beforeResize(); }
}
