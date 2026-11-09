#pragma once
#include <filesystem>
namespace conduit::windows {
using ActiveTitleView = bool(*)(void* view, void* scene) noexcept;
using VanillaTitleProvenance = bool(*)() noexcept;
// Guards must be installed before hooks and remain callable while the pinned DLL lives.
void setTitleLogoGuards(ActiveTitleView active, VanillaTitleProvenance provenance) noexcept;
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
