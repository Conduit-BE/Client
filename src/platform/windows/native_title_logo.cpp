#include "platform/windows/native_title_logo.hpp"
#include "core/title_logo.hpp"
#include "minecraft/compatibility.hpp"
#include <windows.h>
#include <MinHook.h>
#include <array>
#include <atomic>
#include <cstring>
#include <string>
#include <string_view>

// Exact-image ABI and deferred-reference ownership:
// docs/reverse-engineering/1.26.60-image-submission.md
namespace conduit::windows {
namespace {
struct Vec2 { float x, y; };
struct Span { const char* data; std::size_t size; };
using SpriteDraw = void(*)(void*,void*,const Vec2*,const Vec2*,const Vec2*,const Vec2*);
using ImageDraw = void(*)(void*,void*,const Vec2*,const Vec2*,const Vec2*,const Vec2*,bool);
using ConstructResource = void*(*)(void*,const Span*);
using GetTexture = void*(*)(void*,void*,const void*,bool);
const minecraft::CompatibilityProfile* profile{};
std::uintptr_t base{};
SpriteDraw originalSprite{};
ImageDraw originalImage{};
std::atomic<bool> enabled{false};
std::atomic<bool> rendered{false};
ActiveTitleView activeGuard{};
VanillaTitleProvenance provenanceGuard{};
std::string imagePath;
// Native allocations and handles are intentionally retained until process exit.
// A disabled callback is not proof that a previously deferred GPU submission has
// consumed its borrowed image reference. The standalone DLL is already pinned.
alignas(16) std::array<std::byte,56> resource{};
alignas(16) std::array<std::uintptr_t,4> texture{};
bool attemptedImport = false;
void* importContextIdentity{};
std::atomic_flag importing = ATOMIC_FLAG_INIT;
struct ViewFrame { void* view; void* context; bool active; };
thread_local std::array<ViewFrame,16> frames{};
thread_local unsigned frameDepth{};
thread_local unsigned overflowDepth{};
thread_local void* drawingSprite{};

template<class T> T read(const void* object, std::size_t offset) noexcept {
    T value{};
    if (object) std::memcpy(&value,static_cast<const std::byte*>(object)+offset,sizeof(value));
    return value;
}
bool readable(const void* address, std::size_t size) noexcept {
    if (!address || !size) return false;
    const auto begin = reinterpret_cast<std::uintptr_t>(address);
    if (begin+size < begin) return false;
    MEMORY_BASIC_INFORMATION region{};
    if (!VirtualQuery(address,&region,sizeof(region)) || region.State != MEM_COMMIT ||
        (region.Protect & (PAGE_NOACCESS|PAGE_GUARD))) return false;
    const auto end = reinterpret_cast<std::uintptr_t>(region.BaseAddress)+region.RegionSize;
    return begin+size <= end;
}
std::string_view nativeString(const void* object) noexcept {
    if (!readable(object,32)) return {};
    const auto length = read<std::size_t>(object,16), capacity = read<std::size_t>(object,24);
    if (length > 1024 || length > capacity) return {};
    const char* data = capacity < 16 ? static_cast<const char*>(object) : read<const char*>(object,0);
    if (!readable(data,length+1) || data[length] != '\0') return {};
    return {data,length};
}
ViewFrame* frameFor(void* context) noexcept {
    if (!frameDepth || overflowDepth) return nullptr;
    auto& frame = frames[frameDepth-1];
    return frame.active && frame.context == context ? &frame : nullptr;
}
bool eligibleSprite(void* sprite, void* image, ViewFrame& frame) noexcept {
    const auto& logo = profile->logo;
    if (!readable(sprite,logo.spriteImage+sizeof(void*)) ||
        read<std::uintptr_t>(sprite,0) != base+logo.spriteTableRva ||
        read<void*>(sprite,logo.spriteImage) != image || !readable(image,logo.imageHeight+4)) return false;
    auto* owner = read<void*>(sprite,logo.spriteOwner);
    if (!readable(owner,profile->layout.controlName+32)) return false;
    const auto* location = static_cast<std::byte*>(sprite)+logo.spriteResource;
    if (!readable(location,logo.resourceSize)) return false;
    return isVanillaTitleImage(frame.active,provenanceGuard && provenanceGuard(),
        nativeString(static_cast<std::byte*>(owner)+profile->layout.controlName),
        read<std::uint32_t>(location,0),nativeString(location+8),
        read<unsigned>(image,logo.imageWidth),read<unsigned>(image,logo.imageHeight));
}
void* replacementImage(void* context) noexcept {
    // One native context type/group is accepted for this session. Avoid using an
    // old group's retained handles after a different native owner appears.
    if (importing.test_and_set(std::memory_order_acquire)) return nullptr;
    struct Unlock { ~Unlock() { importing.clear(std::memory_order_release); } } unlock;
    const auto identity = read<void*>(context,profile->logo.textureGroup);
    if (!attemptedImport) {
        attemptedImport = true;
        importContextIdentity = identity;
        const Span path{imagePath.data(),imagePath.size()};
        reinterpret_cast<ConstructResource>(base+profile->logo.resourceConstructorRva)(resource.data(),&path);
        const std::uint32_t rawPath = 2;
        std::memcpy(resource.data(),&rawPath,sizeof(rawPath));
        const auto hash = resourceLocationHash(2,read<std::uint64_t>(resource.data(),0x28));
        std::memcpy(resource.data()+0x30,&hash,sizeof(hash));
        reinterpret_cast<GetTexture>(base+profile->logo.textureGetterRva)(context,texture.data(),resource.data(),false);
    }
    if (identity != importContextIdentity) return nullptr;
    auto* image = reinterpret_cast<void*>(texture[0]);
    if (!readable(image,profile->logo.imageHeight+4) ||
        read<unsigned>(image,profile->logo.imageWidth) != 2004 ||
        read<unsigned>(image,profile->logo.imageHeight) != 686) return nullptr;
    return image;
}
void onSprite(void* sprite,void* context,const Vec2* position,const Vec2* size,const Vec2* uv,const Vec2* dimensions) {
    void* previous = drawingSprite;
    drawingSprite = enabled.load(std::memory_order_acquire) && frameFor(context) ? sprite : nullptr;
    originalSprite(sprite,context,position,size,uv,dimensions);
    drawingSprite = previous;
}
void onImage(void* context,void* image,const Vec2* position,const Vec2* size,const Vec2* uv,const Vec2* uvSize,bool flag) {
    auto* frame = frameFor(context);
    if (enabled.load(std::memory_order_acquire) && frame && drawingSprite &&
        eligibleSprite(drawingSprite,image,*frame) && readable(position,sizeof(Vec2)) && readable(size,sizeof(Vec2))) {
        if (void* replacement = replacementImage(context)) {
            const auto bounds = containTitleLogo({position->x,position->y,size->x,size->y},2004,686);
            if (bounds) {
                const Vec2 newPosition{bounds->x,bounds->y}, newSize{bounds->width,bounds->height};
                const Vec2 origin{0,0}, extent{1,1};
                rendered.store(true,std::memory_order_relaxed);
                originalImage(context,replacement,&newPosition,&newSize,&origin,&extent,flag);
                return;
            }
        }
    }
    originalImage(context,image,position,size,uv,uvSize,flag);
}
}
bool titleLogoInstalled() noexcept { return enabled.load(std::memory_order_acquire); }
bool titleLogoRendered() noexcept { return rendered.load(std::memory_order_acquire); }
void setTitleLogoGuards(ActiveTitleView active,VanillaTitleProvenance provenance) noexcept {
    if (!enabled.load()) { activeGuard = active; provenanceGuard = provenance; }
}
void beginTitleLogoView(void* view,void* context) noexcept {
    if (overflowDepth || frameDepth == frames.size()) { ++overflowDepth; return; }
    bool active = false;
    if (enabled.load(std::memory_order_acquire) && profile && activeGuard && provenanceGuard &&
        readable(context,profile->logo.contextScene+sizeof(void*)) &&
        read<std::uintptr_t>(context,0) == base+profile->renderer.contextTableRva) {
        if (readable(view,profile->layout.viewTree+sizeof(void*))) {
            const auto* tree = read<void*>(view,profile->layout.viewTree);
            const auto* root = readable(tree,16) ? read<void*>(tree,8) : nullptr;
            if (readable(tree,sizeof(void*)) && read<std::uintptr_t>(tree,0) == base+profile->reload.visualTreeTableRva &&
                readable(root,profile->layout.controlName+32) &&
                nativeString(static_cast<const std::byte*>(root)+profile->layout.controlName) == "start_screen")
                active = activeGuard(view,read<void*>(context,profile->logo.contextScene));
        }
    }
    frames[frameDepth++] = {view,context,active};
}
void endTitleLogoView() noexcept {
    if (overflowDepth) --overflowDepth;
    else if (frameDepth) --frameDepth;
}
bool installTitleLogo(std::filesystem::path imageFile) noexcept {
    if (enabled.load() || originalImage || originalSprite) return false;
    try {
        profile = minecraft::runningCompatibility();
        if (!profile || !profile->features.mainMenuLogo || !activeGuard || !provenanceGuard) return false;
        const auto utf8 = imageFile.u8string();
        imagePath.assign(reinterpret_cast<const char*>(utf8.data()),utf8.size());
        if (imagePath.empty() || imagePath.size() >= 1024 || imagePath.find('\0') != std::string::npos) return false;
        base = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
        const auto& logo = profile->logo;
        if (logo.resourceSize != resource.size()) return false;
        const auto matches = [](std::uintptr_t rva,const auto& prefix) {
            return rva && readable(reinterpret_cast<void*>(base+rva),prefix.size()) &&
                std::memcmp(reinterpret_cast<void*>(base+rva),prefix.data(),prefix.size()) == 0;
        };
        if (!matches(logo.spriteDrawRva,logo.spritePrefix) || !matches(logo.drawImageRva,logo.drawPrefix) ||
            !matches(logo.resourceConstructorRva,logo.constructorPrefix) || !matches(logo.textureGetterRva,logo.getterPrefix)) return false;
        auto* spriteTarget = reinterpret_cast<void*>(base+logo.spriteDrawRva);
        auto* imageTarget = reinterpret_cast<void*>(base+logo.drawImageRva);
        if (MH_CreateHook(spriteTarget,reinterpret_cast<void*>(&onSprite),reinterpret_cast<void**>(&originalSprite)) != MH_OK) return false;
        if (MH_CreateHook(imageTarget,reinterpret_cast<void*>(&onImage),reinterpret_cast<void**>(&originalImage)) != MH_OK) {
            MH_RemoveHook(spriteTarget); originalSprite = nullptr; return false;
        }
        if (MH_EnableHook(spriteTarget) != MH_OK || MH_EnableHook(imageTarget) != MH_OK) {
            MH_DisableHook(spriteTarget); MH_DisableHook(imageTarget);
            // Keep any reachable trampolines resident, matching standalone shutdown.
            return false;
        }
        enabled.store(true,std::memory_order_release);
        return true;
    } catch (...) { return false; }
}
void stopTitleLogo() noexcept {
    enabled.store(false,std::memory_order_release);
    if (profile && base) {
        if (originalSprite) MH_DisableHook(reinterpret_cast<void*>(base+profile->logo.spriteDrawRva));
        if (originalImage) MH_DisableHook(reinterpret_cast<void*>(base+profile->logo.drawImageRva));
    }
}
}
