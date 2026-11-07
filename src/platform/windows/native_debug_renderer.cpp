#include "platform/windows/native_debug_renderer.hpp"
#include <windows.h>
#include <MinHook.h>
#include <atomic>
#include <cstddef>
#include <cstring>
#include <cmath>
#include <cwchar>

// Exact build evidence, ABI and lifetime rules:
// docs/reverse-engineering/1.26.60-native-debug-rendering.md
namespace conduit::windows {
namespace {
constexpr std::uintptr_t viewRenderRva = 0x4cb3260, flushTextRva = 0x6c0b230;
constexpr std::uintptr_t contextTableRva = 0xf2d18c0, vanillaFontTableRva = 0xf0ea8a0;
constexpr std::size_t textRecordSize = 104;
struct Rect { float left, right, top, bottom; };
struct Color { float red, green, blue; };
struct Span { const char* data; std::size_t size; };
struct Measure { float scale = DebugPanelLayout::fontScale, spacing = 0; bool a = false, b = false; };
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
void render(Frame& frame) {
    if (!frame.font || !frame.flushed || !mailbox->tryRead(text) || !text.visible) return;
    void* context = frame.context;
    auto* screen = field<void*>(context, 0x10);
    auto* gui = field<void*>(screen, 0xa0);
    const float width = field<float>(gui, 0x50), height = field<float>(gui, 0x54);
    if (!std::isfinite(width) || !std::isfinite(height) || width < 40 || height < 38 || width > 32768 || height > 32768) return;
    // Original ScreenView::render has completed. Never append to unflushed game text.
    const auto begin = field<std::uintptr_t>(context, 0x80);
    if (!begin || field<std::uintptr_t>(context, 0x88) != begin) return;
    if (layoutRevision != text.revision || width != lastWidth || height != lastHeight ||
        lastFontIdentity != reinterpret_cast<std::uintptr_t>(frame.font)) {
        const auto measure = [&](const char* value, std::size_t size) {
            const Span span{value, size};
            return reinterpret_cast<Width>(base+0xe6be40)(frame.font, &span, DebugPanelLayout::fontScale, false);
        };
        layout = layoutDebugPanel(text.text.data(), width, height, measure);
        layoutRevision = text.revision; lastWidth = width; lastHeight = height;
        lastFontIdentity = reinterpret_cast<std::uintptr_t>(frame.font);
    }
    if (!layout.count) return;
    const auto capacity = field<std::uintptr_t>(context, 0x90);
    if (capacity < begin || capacity-begin < layout.count*textRecordSize) return;
    auto* gpu = field<void*>(screen, 0x10);
    auto* color = field<void*>(screen, 0x30);
    struct Restore {
        void* color; void* gpu;
        std::byte rgba[17], clip[16];
        Restore(void* c, void* g) : color(c), gpu(g) {
            std::memcpy(rgba, color, sizeof(rgba)); std::memcpy(clip, static_cast<std::byte*>(gpu)+0x94, sizeof(clip));
        }
        ~Restore() {
            std::memcpy(color, rgba, sizeof(rgba)); std::memcpy(static_cast<std::byte*>(gpu)+0x94, clip, sizeof(clip));
        }
    } restore(color, gpu);
    const Rect viewport{0, width, 0, height};
    reinterpret_cast<SetClip>(base+0x6c11670)(context, &viewport);
    const Rect panel{layout.left, layout.left+layout.width, layout.top, layout.top+layout.height};
    const Color background{0, 0, 0}, foreground{1, 1, 1};
    reinterpret_cast<Fill>(base+0x6c11440)(context, &panel, &background, 0.5f);
    const Measure measure{};
    const int caret = -1;
    for (unsigned index = 0; index < layout.count; ++index) {
        const auto& row = layout.rows[index];
        const Span span{row.data(), std::strlen(row.data())};
        const float y = panel.top+DebugPanelLayout::padding+index*DebugPanelLayout::lineHeight;
        const Rect bounds{panel.left+DebugPanelLayout::padding, panel.right-DebugPanelLayout::padding, y, y+10*DebugPanelLayout::fontScale};
        reinterpret_cast<DrawText>(base+0x6c0ac90)(context, frame.font, &bounds, &span, &foreground, 1, 0, &measure, &caret);
    }
    // Borrowed strings remain alive until this synchronous flush returns. Bit 32 prevents
    // advancing the font animation timer twice; low 32 bits preserve the natural text depth.
    originalFlush(context, 0, frame.flushFlags | (std::uint64_t{1}<<32));
}
void onFlush(void* context, float delta, std::uint64_t flags) {
    Call call;
    auto* frame = currentFrame;
    if (enabled.load() && frame && frame->context == context) {
        const auto begin = field<std::uintptr_t>(context, 0x80), end = field<std::uintptr_t>(context, 0x88);
        if (begin && end >= begin && (end-begin)%textRecordSize == 0) {
            const auto count = std::min<std::uintptr_t>((end-begin)/textRecordSize, 64);
            for (std::uintptr_t i = 0; i < count && !frame->font; ++i) {
                auto* font = *reinterpret_cast<void**>(begin+i*textRecordSize);
                if (font && field<std::uintptr_t>(font, 0) == base+vanillaFontTableRva) frame->font = font;
            }
        }
        frame->flushFlags = flags; frame->flushed = true;
    }
    originalFlush(context, delta, flags);
}
void onView(void* view, void* context) {
    Call call;
    if (!enabled.load() || !context || field<std::uintptr_t>(context, 0) != base+contextTableRva) {
        originalView(view, context); return;
    }
    if (!mailbox->tryRead(text) || !text.visible) { originalView(view, context); return; }
    Frame frame{currentFrame, context};
    struct Scope { Frame* previous; ~Scope() { currentFrame = previous; } } scope{currentFrame};
    currentFrame = &frame;
    originalView(view, context);
    if (enabled.load()) render(frame);
    // frame's context and borrowed font cease to be accessible here.
}
}
bool NativeDebugRenderer::install(DebugPanelMailbox& panel) noexcept {
    wchar_t path[32768]{};
    if (!GetModuleFileNameW(nullptr, path, 32768)) return false;
    const auto* name = std::wcsrchr(path, L'\\');
    if (_wcsicmp(name ? name+1 : path, L"Minecraft.Windows.exe") != 0) return false;
    base = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
    // Supplemental entry guards; the standalone backend has already verified the complete image hash.
    constexpr unsigned char viewPrefix[]{0x55,0x41,0x57};
    constexpr unsigned char flushPrefix[]{0x55,0x41,0x57};
    if (std::memcmp(reinterpret_cast<void*>(base+viewRenderRva), viewPrefix, sizeof(viewPrefix)) ||
        std::memcmp(reinterpret_cast<void*>(base+flushTextRva), flushPrefix, sizeof(flushPrefix))) return false;
    if (MH_CreateHook(reinterpret_cast<void*>(base+viewRenderRva), reinterpret_cast<void*>(&onView), reinterpret_cast<void**>(&originalView)) != MH_OK) return false;
    if (MH_CreateHook(reinterpret_cast<void*>(base+flushTextRva), reinterpret_cast<void*>(&onFlush), reinterpret_cast<void**>(&originalFlush)) != MH_OK) return false;
    mailbox = &panel;
    if (MH_EnableHook(reinterpret_cast<void*>(base+viewRenderRva)) != MH_OK) return false;
    if (MH_EnableHook(reinterpret_cast<void*>(base+flushTextRva)) != MH_OK) {
        MH_DisableHook(reinterpret_cast<void*>(base+viewRenderRva)); return false;
    }
    installed_ = true; enabled.store(true); return true;
}
NativeDebugRenderer::~NativeDebugRenderer() {
    if (!installed_) return;
    enabled.store(false);
    MH_DisableHook(reinterpret_cast<void*>(base+viewRenderRva));
    MH_DisableHook(reinterpret_cast<void*>(base+flushTextRva));
    while (callbacks.load()) Sleep(1);
    mailbox = nullptr; // In-flight trampolines stay resident, like the other standalone hooks.
}
}
