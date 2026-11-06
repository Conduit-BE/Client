#include <windows.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include "standalone/initialization.hpp"
int main(int argc, char** argv) {
    if (argc != 3) return 2;
    const auto root = std::filesystem::temp_directory_path() / ("conduit-standalone-test-" + std::to_string(GetCurrentProcessId()) + "-" + std::to_string(GetTickCount64()));
    const auto configRoot = root / "ConduitClient";
    std::filesystem::create_directories(configRoot);
    const auto pack = root / "pack";
    std::filesystem::create_directories(pack / "ui");
    std::ofstream(pack / "manifest.json") << "{}";
    SetEnvironmentVariableW(L"LOCALAPPDATA", root.c_str());
    const std::string mode(argv[2]);
    if (mode == "disabled") std::ofstream(configRoot / "DISABLE") << "";
    else if (mode == "invalid") std::ofstream(configRoot / "config.json") << "{bad";
    else {
        auto path = pack.generic_string(); // Forward slashes are valid Windows paths and JSON text.
        std::ofstream(configRoot / "config.json") << "{\"resourcePackPath\":\"" << path << "\"}";
    }
    const auto module = LoadLibraryA(argv[1]);
    if (!module) return 3;
    const auto phase = reinterpret_cast<unsigned (*)()>(GetProcAddress(module, "ConduitStandalonePhase"));
    if (!phase) return 4;
    using conduit::standalone::Phase;
    const auto expected = mode == "disabled" ? Phase::Disabled : Phase::Failed;
    bool done = false;
    for (unsigned i = 0; i < 1000; ++i) {
        if (phase() == static_cast<unsigned>(expected)) { done = true; break; }
        Sleep(10);
    }
    if (!done) { std::cerr << "Unexpected initialization phase " << phase() << '\n'; return 5; }
    const auto duplicate = LoadLibraryA(argv[1]);
    if (duplicate != module || phase() != static_cast<unsigned>(expected)) return 6;
    std::ifstream log(configRoot / "standalone.log");
    const std::string text((std::istreambuf_iterator<char>(log)), {});
    const auto marker = mode == "disabled" ? "no hooks installed" : mode == "invalid" ? "parse_error" : "exact verified Bedrock 1.26.60.5";
    if (text.find(marker) == std::string::npos) { std::cerr << text; return 7; }
    log.close();
    FreeLibrary(duplicate); FreeLibrary(module);
    std::error_code error;
    for (unsigned i = 0; i < 100; ++i) {
        std::filesystem::remove_all(root, error);
        if (!error) break;
        Sleep(10); // The asynchronous worker may still be closing its log.
    }
    if (error) { std::cerr << error.message(); return 8; }
    std::cout << "Automatic initialization " << mode << ": fail-closed, duplicate load safe\n";
}
