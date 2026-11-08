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
    image.sha256 = versions::bedrock126605.sha256;
    const auto* profile = selectCompatibility(image);
    require(profile && profile->build == "1.26.60.5" && profile->features.targetedReload);
    require(profile->features.screenMetrics && profile->features.nativeRenderer && !profile->features.mainMenuLogo);
    require(profile->reload.uiReloadCallbackRva == 0xf203e0 && profile->renderer.viewRenderRva == 0x4cb3260);
    // A matching advertised version cannot make modified code compatible.
    image.hasFileVersion = true; image.fileVersion = {1,26,60,5}; image.sha256[0] ^= 1;
    require(!selectCompatibility(image));
    image.sha256 = versions::bedrock126605.sha256; image.machine = 0x14c;
    require(!selectCompatibility(image));
    require(!runningCompatibility()); // Pure selection never authorizes native installation.
    std::cout << "Exact image selection, unsupported build/architecture and feature support passed\n";
}
