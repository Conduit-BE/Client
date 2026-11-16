#pragma once
#include <filesystem>
namespace conduit::windows {
using ActiveLogoView = bool(*)(void* view, void* scene) noexcept;
using VanillaTitleProvenance = bool(*)() noexcept;
// Guards must be installed before hooks and remain callable while the pinned DLL lives.
void setTitleLogoGuards(ActiveLogoView title, ActiveLogoView world, VanillaTitleProvenance provenance) noexcept;
inline constexpr unsigned titleLogoImportLimit = 32;
// Generation zero is prepared at startup. Subsequent files are prepared by the worker.
unsigned requestedTitleLogoGeneration() noexcept;
bool supplyTitleLogoFile(unsigned generation, const std::filesystem::path& imageFile);
bool installTitleLogo(std::filesystem::path imageFile) noexcept;
void stopTitleLogo() noexcept;
bool titleLogoInstalled() noexcept;
bool titleLogoRendered() noexcept;
void beginTitleLogoView(void* view, void* context) noexcept;
void endTitleLogoView() noexcept;
class TitleLogoViewScope {
public:
    TitleLogoViewScope(void* view, void* context) noexcept { beginTitleLogoView(view, context); }
    ~TitleLogoViewScope() { endTitleLogoView(); }
    TitleLogoViewScope(const TitleLogoViewScope&) = delete;
    TitleLogoViewScope& operator=(const TitleLogoViewScope&) = delete;
};
}
