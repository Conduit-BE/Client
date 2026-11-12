#include "minecraft/compatibility.hpp"
#include "minecraft/versions/bedrock_1_26_60_5.hpp"
#include <iostream>
#include <stdexcept>
using namespace conduit::minecraft;
void require(bool result) { if (!result) throw std::runtime_error("Compatibility assertion failed"); }
int main() {
    ExecutableIdentity image;
    require(!selectCompatibility(image));
    image.machine = 0x8664;
    require(!selectCompatibility(image));
    image.hasFileVersion = true;
    image.fileVersion = {1,26,60,5};
    const auto* profile = selectCompatibility(image);
    require(profile && profile->build == "1.26.60.5" && profile->features.targetedReload);
    require(profile->features.screenMetrics && profile->features.nativeRenderer && profile->features.mainMenuLogo);
    require(profile->reload.uiReloadCallbackRva == 0xf203e0 && profile->renderer.viewRenderRva == 0x4cb3260);
    image.fileVersion = {1,26,62,1};
    profile = selectCompatibility(image);
    require(profile && profile->build == "1.26.62.1" && profile->features.targetedReload);
    require(profile->reload.uiReloadCallbackRva == 0xf202a0 && profile->renderer.viewRenderRva == 0x4cb3430);
    image.fileVersion = {1,26,63,1};
    profile = selectCompatibility(image);
    require(profile && profile->build == "1.26.63.1" && profile->features.targetedReload);
    require(profile->reload.uiReloadCallbackRva == 0xf20000 && profile->renderer.viewRenderRva == 0x4cb3190);
    image.fileVersion = {1,26,62,2};
    require(!selectCompatibility(image));
    image.fileVersion = {1,26,62,1};
    image.machine = 0x14c;
    require(!selectCompatibility(image));
    require(!runningCompatibility()); // Pure selection never authorizes native installation.
    std::cout << "File version selection, unsupported build/architecture and feature support passed\n";
}
