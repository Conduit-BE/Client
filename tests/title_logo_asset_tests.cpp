#include "standalone/title_logo_asset.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <windows.h>
void require(bool value) { if (!value) throw std::runtime_error("Title asset assertion failed"); }
int main() {
    namespace fs = std::filesystem;
    const auto root = fs::temp_directory_path()/("conduit-title-test-"+std::to_string(GetCurrentProcessId()));
    try {
        const auto image = conduit::standalone::prepareTitleLogoFile(root);
        require(fs::file_size(image) == 153289);
        std::ofstream(image,std::ios::binary|std::ios::trunc) << "broken";
        require(conduit::standalone::prepareTitleLogoFile(root) == image && fs::file_size(image) == 153289);
        const auto local = root/"local", roaming = root/"roaming";
        require(!conduit::standalone::vanillaGlobalPacks(local,roaming));
        const auto profile = roaming/"Minecraft Bedrock/Users/123/games/com.mojang";
        fs::create_directories(profile/"minecraftpe");
        std::ofstream(profile/"minecraftpe/options.txt") << "test";
        require(conduit::standalone::vanillaGlobalPacks(local,roaming));
        std::ofstream(profile/"global_resource_packs.json") << "[]";
        require(conduit::standalone::vanillaGlobalPacks(local,roaming));
        std::ofstream(profile/"global_resource_packs.json") << "[{\"pack_id\":\"custom\"}]";
        require(!conduit::standalone::vanillaGlobalPacks(local,roaming));
        std::ofstream(profile/"global_resource_packs.json") << "{invalid";
        require(!conduit::standalone::vanillaGlobalPacks(local,roaming));
        fs::remove_all(root);
        std::cout << "Embedded PNG extraction, cache repair and conservative pack customization checks passed\n";
    } catch (const std::exception& e) { std::cerr << e.what(); fs::remove_all(root); return 1; }
}
