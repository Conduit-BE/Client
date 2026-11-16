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
        std::ofstream(configRoot / "config.json") << "{\"enabled\":false,\"resourcePackPath\":17,\"reloadMode\":\"obsolete\"}";
    }
    const auto dll = std::filesystem::absolute(argv[1]);
    const auto runtime = configRoot / "runtime" / std::to_string(GetCurrentProcessId());
    using conduit::standalone::Phase;
    const auto expected = mode == "disabled" ? Phase::Disabled : Phase::Failed;
    HMODULE firstResident{};
    for (int attempt = 0; attempt < 2; ++attempt) {
        if (!LoadLibraryW(dll.c_str())) return 3;
        HMODULE resident{};
        for (unsigned i = 0; i < 1000 && !resident; ++i) {
            std::error_code error;
            for (std::filesystem::directory_iterator it(runtime, error), end; !error && it != end; it.increment(error)) {
                const auto candidate = GetModuleHandleW(it->path().c_str());
                if (!candidate || candidate == firstResident) continue;
                const auto phase = reinterpret_cast<unsigned (*)()>(GetProcAddress(candidate, "ConduitStandalonePhase"));
                if (phase && phase() == static_cast<unsigned>(expected)) {
                    resident = candidate;
                    break;
                }
            }
            if (!resident || GetModuleHandleW(dll.c_str())) Sleep(10);
            else break;
            resident = nullptr;
        }
        if (!resident) { std::cerr << "No fresh resident instance on injection " << attempt + 1 << '\n'; return 5; }
        firstResident = resident;
        const auto logo = FindResourceW(resident, MAKEINTRESOURCEW(104), MAKEINTRESOURCEW(10));
        if (!logo || SizeofResource(resident, logo) != 153289) return 9;
        const auto* png = static_cast<const unsigned char*>(LockResource(LoadResource(resident, logo)));
        if (!png || png[0] != 137 || png[1] != 'P' || png[2] != 'N' || png[3] != 'G') return 10;
    }
    if (std::filesystem::exists(configRoot / "standalone.log") || std::filesystem::exists(configRoot / "client.log") ||
        std::filesystem::exists(configRoot / "native-stages.log")) return 11;
    std::ifstream log(configRoot / "logs/standalone.log");
    const std::string text((std::istreambuf_iterator<char>(log)), {});
    const auto marker = mode == "disabled" ? "no hooks installed" : mode == "invalid" ? "parse_error" : "Unsupported Minecraft executable";
    if (text.find(marker) == std::string::npos) { std::cerr << text; return 7; }
    log.close();
    // Windows keeps the resident image files mapped until this test exits.
    std::error_code error;
    std::filesystem::remove_all(configRoot / "logs", error);
    std::cout << "Automatic initialization " << mode << ": fail-closed across two injections at the same path\n";
}
