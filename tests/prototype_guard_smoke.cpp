#include <windows.h>
#include <array>
#include <iostream>
#include <string>
int main(int argc, char** argv) {
    if (argc != 2) return 2;
    HMODULE module = LoadLibraryA(argv[1]);
    if (!module) return 3;
    using Start = int (*)(const char*, const char*);
    using Disable = void (*)();
    using Snapshot = unsigned (*)(char*, unsigned);
    const auto start = reinterpret_cast<Start>(GetProcAddress(module, "ConduitPrototypeStart"));
    const auto disable = reinterpret_cast<Disable>(GetProcAddress(module, "ConduitPrototypeDisable"));
    const auto snapshot = reinterpret_cast<Snapshot>(GetProcAddress(module, "ConduitPrototypeSnapshot"));
    if (!start || !disable || !snapshot) return 4;
    if (start(nullptr, nullptr) != 0 || start(".", "prototype-guard.log") != 0) return 5;
    std::array<char, 1024> text{};
    snapshot(text.data(), static_cast<unsigned>(text.size()));
    const std::string result(text.data());
    if (result.find("exact verified Bedrock 1.26.60.5") == std::string::npos) {
        std::cerr << result << '\n'; return 6;
    }
    const auto asyncStart = reinterpret_cast<Start>(GetProcAddress(module, "ConduitPrototypeStartAsync"));
    if (!asyncStart || asyncStart(".", "prototype-guard.log") != 1) return 7;
    bool done = false;
    for (unsigned i = 0; i < 500; ++i) {
        text.fill(0);
        snapshot(text.data(), static_cast<unsigned>(text.size()));
        if (std::string(text.data()).find("ERROR:") == 0) { done = true; break; }
        Sleep(10);
    }
    if (!done) return 8;
    disable();
    FreeLibrary(module);
    std::cout << "Unverified host rejected before hooks or game calls\n";
}
