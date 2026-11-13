#include "platform/windows/native_debug_renderer.hpp"
#include "platform/windows/native_title_logo.hpp"
#include <windows.h>
#include "minecraft/compatibility.hpp"
#include <MinHook.h>
#include <atomic>
#include <cstddef>
#include <cstring>
#include <cmath>
#include <cwchar>
#include <cstdio>

// Exact build evidence, ABI and lifetime rules:
// docs/reverse-engineering/1.26.60-native-debug-rendering.md
namespace conduit::windows {
namespace {
const minecraft::CompatibilityProfile* profile{};
struct Rect { float left, right, top, bottom; };
struct Color { float red, green, blue; };
struct Span { const char* data; std::size_t size; };
struct Measure { float scale = 1.0f, spacing = 0; bool a = false, b = false; };
static_assert(sizeof(Span) == 16 && offsetof(Measure, a) == 8);
using ViewRender = void(*)(void*, void*);
using FlushText = void(*)(void*, float, std::uint64_t);
using DrawText = void(*)(void*, void*, const Rect*, const Span*, const Color*, float, unsigned, const Measure*, const int*);
using Fill = void(*)(void*, const Rect*, const Color*, float);
using SetClip = void(*)(void*, const Rect*);
using Width = unsigned(*)(void*, const Span*, float, bool);
std::uintptr_t base{};
ViewRender originalView{};
FlushText originalFlush{};
std::atomic<bool> enabled{false};
std::atomic<unsigned> callbacks{0};
DebugPanelMailbox* mailbox{};
HWND gameWindow{};
InspectHoveredControl inspectHovered{};
float tooltipScale = 1.0f;
std::atomic<bool> hoverActive{false};
struct Call { Call() { callbacks.fetch_add(1); } ~Call() { callbacks.fetch_sub(1); } };
template<class T> T& field(void* object, std::size_t offset) {
    return *reinterpret_cast<T*>(static_cast<std::byte*>(object)+offset);
}
struct Frame {
    Frame* previous{};
    void* context{};
    void* font{};
    std::uint64_t flushFlags = 0;
    bool flushed = false;
};
thread_local Frame* currentFrame{};
// Cache contains only copied text/geometry and a numeric font identity, never a retained native pointer.
thread_local DebugPanelText text;
thread_local DebugPanelLayout layout;
thread_local std::uint64_t layoutRevision = 0;
thread_local float lastWidth = 0, lastHeight = 0;
thread_local std::uintptr_t lastFontIdentity = 0;
void render(Frame& frame, void* view) {
    const bool showPanel = mailbox->tryRead(text) && text.visible;
    if (!frame.flushed || (!showPanel && !hoverActive.load())) return;
    void* context = frame.context;
    auto* screen = field<void*>(context, profile->layout.screenContext);
    auto* gui = field<void*>(screen, profile->layout.screenGui);
    const float width = field<float>(gui, profile->layout.guiWidth), height = field<float>(gui, profile->layout.guiHeight);
    if (!std::isfinite(width) || !std::isfinite(height) || width < 40 || height < 38 || width > 32768 || height > 32768) return;
    // Original ScreenView::render has completed. Never append to unflushed game text.
    const auto begin = field<std::uintptr_t>(context, profile->layout.textBegin);
    if (!begin || field<std::uintptr_t>(context, profile->layout.textEnd) != begin) return;
    if (showPanel && frame.font && (layoutRevision != text.revision || width != lastWidth || height != lastHeight ||
        lastFontIdentity != reinterpret_cast<std::uintptr_t>(frame.font))) {
        const auto measure = [&](const char* value, std::size_t size) {
            const Span span{value, size};
            return reinterpret_cast<Width>(base+profile->renderer.textWidthRva)(frame.font, &span, text.scale, false);
        };
        layout = layoutDebugPanel(text.text.data(), width, height, measure, text.scale);
        layoutRevision = text.revision; lastWidth = width; lastHeight = height;
        lastFontIdentity = reinterpret_cast<std::uintptr_t>(frame.font);
    }
    const auto capacity = field<std::uintptr_t>(context, profile->layout.textCapacity);
    if (capacity < begin) return;
    const auto available = (capacity - begin) / profile->layout.textRecordSize;
    HoveredControl hovered{};
    bool showHover = false;
    POINT cursor{};
    RECT client{};
    if (hoverActive.load() && inspectHovered && GetForegroundWindow() == gameWindow &&
        GetCursorPos(&cursor) && ScreenToClient(gameWindow, &cursor) && GetClientRect(gameWindow, &client) &&
        client.right > 0 && client.bottom > 0 && PtInRect(&client, cursor)) {
        showHover = inspectHovered(view, field<void*>(context, profile->logo.contextScene),
            cursor.x * width / client.right, cursor.y * height / client.bottom, hovered);
    }
    const bool drawPanel = showPanel && frame.font && layout.count && layout.count <= available;
    if (!drawPanel && !showHover) return;
    auto* gpu = field<void*>(screen, profile->layout.screenGpu);
    auto* color = field<void*>(screen, profile->layout.screenColor);
    struct Restore {
        void* color; void* gpu;
        std::byte rgba[17], clip[16];
        Restore(void* c, void* g) : color(c), gpu(g) {
            std::memcpy(rgba, color, sizeof(rgba)); std::memcpy(clip, static_cast<std::byte*>(gpu)+profile->layout.gpuClip, sizeof(clip));
        }
        ~Restore() {
            std::memcpy(color, rgba, sizeof(rgba)); std::memcpy(static_cast<std::byte*>(gpu)+profile->layout.gpuClip, clip, sizeof(clip));
        }
    } restore(color, gpu);
    const Rect viewport{0, width, 0, height};
    reinterpret_cast<SetClip>(base+profile->renderer.setClipRva)(context, &viewport);
    const Color background{0, 0, 0}, foreground{1, 1, 1};
    const auto fill = reinterpret_cast<Fill>(base+profile->renderer.fillRva);
    char details[1536]{};
    DebugPanelLayout tooltip{};
    if (showHover) {
        const auto left = std::min(hovered.x, hovered.x + hovered.width);
        const auto top = std::min(hovered.y, hovered.y + hovered.height);
        const Rect bounds{std::clamp(left, 0.f, width), std::clamp(left + std::abs(hovered.width), 0.f, width),
                          std::clamp(top, 0.f, height), std::clamp(top + std::abs(hovered.height), 0.f, height)};
        const Color blue{0.40f, 0.76f, 1.f};
        fill(context, &bounds, &blue, 0.30f);
        if (frame.font) {
            const int propertyBytes = std::snprintf(details, sizeof(details),
                "Name: %s\nType: %s\nLayer: %lld\nSize: %.1f x %.1f\nPosition: %.1f, %.1f\nVisible: %s",
                hovered.name.data(), hovered.type, static_cast<long long>(hovered.layer),
                hovered.width, hovered.height, hovered.x, hovered.y, hovered.visible ? "true" : "false");
            auto used = static_cast<std::size_t>(std::clamp(propertyBytes, 0, static_cast<int>(sizeof(details) - 1)));
            for (unsigned index = 0; index < hovered.parentCount && used < sizeof(details) - 1; ++index) {
                const int bytes = std::snprintf(details + used, sizeof(details) - used, "\n%s %s",
                    index == 0 ? "Parent:" : "  <-", hovered.parents[index].data());
                if (bytes > 0) used += std::min(static_cast<std::size_t>(bytes), sizeof(details) - used - 1);
            }
            if (hovered.moreParents)
                std::snprintf(details + used, sizeof(details) - used, "\n  ...");
            tooltip = layoutDebugPanel(details, width, height, [&](const char* value, std::size_t size) {
                const Span span{value, size};
                return reinterpret_cast<Width>(base+profile->renderer.textWidthRva)(frame.font, &span, tooltipScale, false);
            }, tooltipScale);
            tooltip.left = std::clamp(bounds.left, 0.f, std::max(0.f, width - tooltip.width));
            tooltip.top = std::clamp(bounds.top >= tooltip.height ? bounds.top - tooltip.height : bounds.bottom,
                                     0.f, std::max(0.f, height - tooltip.height));
        }
    }
    const auto draw = [&](const DebugPanelLayout& box, float opacity) {
        const Rect panel{box.left, box.left+box.width, box.top, box.top+box.height};
        fill(context, &panel, &background, opacity);
        const Measure measure{box.fontScale};
        const int caret = -1;
        for (unsigned index = 0; index < box.count; ++index) {
            const auto& row = box.rows[index];
            const Span span{row.data(), std::strlen(row.data())};
            const float y = panel.top+box.padding+index*box.lineHeight;
            const Rect bounds{panel.left+box.padding, panel.right-box.padding, y, y+10*box.fontScale};
            reinterpret_cast<DrawText>(base+profile->renderer.drawTextRva)(
                context, frame.font, &bounds, &span, &foreground, 1, 0, &measure, &caret);
        }
    };
    const bool drawTooltip = tooltip.count && tooltip.count <= available;
    if (drawPanel && (!drawTooltip || layout.count + tooltip.count <= available)) draw(layout, 0.5f);
    if (drawTooltip) draw(tooltip, 0.75f);
    // Borrowed strings remain alive until this synchronous flush returns. Bit 32 prevents
    // advancing the font animation timer twice; low 32 bits preserve the natural text depth.
    if (drawPanel || drawTooltip) originalFlush(context, 0, frame.flushFlags | (std::uint64_t{1}<<32));
}
void onFlush(void* context, float delta, std::uint64_t flags) {
    Call call;
    auto* frame = currentFrame;
    if (enabled.load() && frame && frame->context == context) {
        const auto begin = field<std::uintptr_t>(context, profile->layout.textBegin), end = field<std::uintptr_t>(context, profile->layout.textEnd);
        if (begin && end >= begin && (end-begin)%profile->layout.textRecordSize == 0) {
            const auto count = std::min<std::uintptr_t>((end-begin)/profile->layout.textRecordSize, 64);
            for (std::uintptr_t i = 0; i < count && !frame->font; ++i) {
                auto* font = *reinterpret_cast<void**>(begin+i*profile->layout.textRecordSize);
                if (font && field<std::uintptr_t>(font, 0) == base+profile->renderer.vanillaFontTableRva) frame->font = font;
            }
        }
        frame->flushFlags = flags; frame->flushed = true;
    }
    originalFlush(context, delta, flags);
}
void onView(void* view, void* context) {
    Call call;
    TitleLogoViewScope titleScope(view,context);
    if (!enabled.load() || !context || field<std::uintptr_t>(context, 0) != base+profile->renderer.contextTableRva) {
        originalView(view, context); return;
    }
    if (!hoverActive.load() && (!mailbox->tryRead(text) || !text.visible)) { originalView(view, context); return; }
    Frame frame{currentFrame, context};
    struct Scope { Frame* previous; ~Scope() { currentFrame = previous; } } scope{currentFrame};
    currentFrame = &frame;
    originalView(view, context);
    if (enabled.load()) render(frame, view);
    // frame's context and borrowed font cease to be accessible here.
}
}
bool NativeDebugRenderer::install(DebugPanelMailbox& panel, const std::filesystem::path& logoFile,
                                  HWND window, InspectHoveredControl inspect, float uiScale) noexcept {
    wchar_t path[32768]{};
    if (!GetModuleFileNameW(nullptr, path, 32768)) return false;
    const auto* name = std::wcsrchr(path, L'\\');
    if (_wcsicmp(name ? name+1 : path, L"Minecraft.Windows.exe") != 0) return false;
    profile = minecraft::runningCompatibility();
    if (!profile || !profile->features.nativeRenderer) return false;
    base = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
    if (!minecraft::matchesMainExecutableCode(profile->renderer.viewRenderRva, profile->signatures.viewPrefix) ||
        !minecraft::matchesMainExecutableCode(profile->renderer.flushTextRva, profile->signatures.flushPrefix)) return false;
    if (MH_CreateHook(reinterpret_cast<void*>(base+profile->renderer.viewRenderRva), reinterpret_cast<void*>(&onView), reinterpret_cast<void**>(&originalView)) != MH_OK) return false;
    if (MH_CreateHook(reinterpret_cast<void*>(base+profile->renderer.flushTextRva), reinterpret_cast<void*>(&onFlush), reinterpret_cast<void**>(&originalFlush)) != MH_OK) return false;
    mailbox = &panel;
    gameWindow = window;
    inspectHovered = inspect;
    tooltipScale = uiScale;
    if (MH_EnableHook(reinterpret_cast<void*>(base+profile->renderer.viewRenderRva)) != MH_OK) return false;
    if (MH_EnableHook(reinterpret_cast<void*>(base+profile->renderer.flushTextRva)) != MH_OK) {
        MH_DisableHook(reinterpret_cast<void*>(base+profile->renderer.viewRenderRva)); return false;
    }
    installed_ = true; enabled.store(true);
    if (!logoFile.empty()) installTitleLogo(logoFile);
    return true;
}
void NativeDebugRenderer::setHoverInspection(bool active) noexcept {
    hoverActive.store(active && inspectHovered != nullptr);
}
NativeDebugRenderer::~NativeDebugRenderer() {
    if (!installed_) return;
    enabled.store(false);
    hoverActive.store(false);
    stopTitleLogo();
    MH_DisableHook(reinterpret_cast<void*>(base+profile->renderer.viewRenderRva));
    MH_DisableHook(reinterpret_cast<void*>(base+profile->renderer.flushTextRva));
    while (callbacks.load()) Sleep(1);
    mailbox = nullptr; // In-flight trampolines stay resident, like the other standalone hooks.
}
}
