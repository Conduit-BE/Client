#pragma once
#include "minecraft/version.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
namespace conduit::minecraft {
struct FeatureSupport { bool targetedReload, screenMetrics, nativeRenderer, mainMenuLogo; };
struct ReloadAddresses {
    std::uintptr_t loaderRva;
    std::uintptr_t frameRva;
    std::uintptr_t frameCallerRva;
    std::uintptr_t completionRva;
    std::uintptr_t ownerTableRva;
    std::uintptr_t callableTableRva;
    std::uintptr_t uiReloadCallbackRva;
    std::uintptr_t clientTableRva;
    std::uintptr_t clientOwnerGetterRva;
    std::uintptr_t clientCacheGetterRva;
    std::uintptr_t uiWaitRva;
    std::uintptr_t screenNameRva;
    std::uintptr_t sceneStackTableRva;
    std::uintptr_t primaryScreenNameCallerRva;
    std::uintptr_t legacySceneTableRva;
    std::uintptr_t visualTreeTableRva;
    std::uintptr_t factoryComponentTableRva;
    std::uintptr_t factoryComponentMaskRva;
    std::uintptr_t bindingComponentMaskRva;
    std::uintptr_t bindingComponentTableRva;
    std::uintptr_t serviceTableRva;
    std::uintptr_t serviceMethodRva;
    std::uintptr_t contextTableRva;
    std::uintptr_t ownerWaitRva;
    std::uintptr_t playerStackGetterRva;
    std::uintptr_t frontendStackGetterRva;
    std::uintptr_t sceneNameGetterRva;
};
struct RendererAddresses {
    std::uintptr_t viewRenderRva;
    std::uintptr_t flushTextRva;
    std::uintptr_t contextTableRva;
    std::uintptr_t vanillaFontTableRva;
    std::uintptr_t textWidthRva;
    std::uintptr_t setClipRva;
    std::uintptr_t fillRva;
    std::uintptr_t drawTextRva;
};
struct NativeLayout {
    std::size_t textRecordSize;
    std::uintptr_t clientGuardContext;
    std::uintptr_t serviceWaitSlot;
    std::uintptr_t serviceGuard;
    std::uintptr_t serviceGuardContext;
    std::uintptr_t cacheTree;
    std::uintptr_t treeRight;
    std::uintptr_t controlName;
    std::uintptr_t bindingVectors;
    std::uintptr_t stackScenes;
    std::uintptr_t screenContext;
    std::uintptr_t screenGpu;
    std::uintptr_t minimumCacheBytes;
    std::uintptr_t minimumTreeBytes;

    std::uintptr_t ownerLoaderSlot;
    std::uintptr_t ownerService;
    std::uintptr_t ownerContext;
    std::uintptr_t ownerClientCount;
    std::uintptr_t ownerClientMap;
    std::uintptr_t clientMapValue;
    std::uintptr_t clientOwnerSlot;
    std::uintptr_t clientCacheSlot;
    std::uintptr_t clientOwner;
    std::uintptr_t ownerWaitSlot;
    std::uintptr_t clientCache;
    std::uintptr_t treeSentinel;
    std::uintptr_t controlComponents;
    std::uintptr_t factoryRoots;
    std::uintptr_t sceneView;
    std::uintptr_t viewTree;
    std::uintptr_t controlChildren;
    std::uintptr_t playerStackSlot;
    std::uintptr_t frontendStackSlot;
    std::uintptr_t sceneNameSlot;
    std::uintptr_t clientPlayerStack;
    std::uintptr_t ownerFrontendStack;
    std::uintptr_t screenGui;
    std::uintptr_t guiWidth;
    std::uintptr_t guiHeight;
    std::uintptr_t textBegin;
    std::uintptr_t textEnd;
    std::uintptr_t textCapacity;
    std::uintptr_t screenColor;
    std::uintptr_t gpuClip;
};
struct HookSignatures {
    std::array<unsigned char, 7> loaderPrefix;
    std::array<unsigned char, 7> framePrefix;
    std::array<unsigned char, 7> completionPrefix;
    std::array<unsigned char, 7> uiCallbackPrefix;
    std::array<unsigned char, 8> screenPrefix;
    std::array<unsigned char, 3> viewPrefix;
    std::array<unsigned char, 3> flushPrefix;
};
struct TitleLogoAddresses {
    std::uintptr_t spriteDrawRva, drawImageRva, resourceConstructorRva, textureGetterRva, spriteTableRva;
    std::size_t spriteOwner, spriteResource, spriteImage, resourceSize, contextScene, imageWidth, imageHeight, textureGroup;
    std::array<unsigned char, 8> spritePrefix, drawPrefix, constructorPrefix, getterPrefix;
};
struct CompatibilityProfile {
    std::string_view build;
    std::array<std::uint16_t, 4> fileVersion;
    FeatureSupport features;
    ReloadAddresses reload;
    RendererAddresses renderer;
    NativeLayout layout;
    HookSignatures signatures;
    TitleLogoAddresses logo;
};
// Select a native profile by PE architecture and file version.
const CompatibilityProfile* selectCompatibility(const ExecutableIdentity& identity) noexcept;
const CompatibilityProfile* runningCompatibility() noexcept;
const CompatibilityProfile& requireRunningCompatibility();
#ifdef _WIN32
bool matchesMainExecutableCode(std::uintptr_t rva, std::span<const unsigned char> expected) noexcept;
#endif
}
