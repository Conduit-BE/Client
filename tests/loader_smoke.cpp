#include "conduit/api.h"

#include <windows.h>

#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

namespace fs = std::filesystem;

namespace {
template<class Function>
Function symbol(HMODULE module, const char* name) {
    const auto address = GetProcAddress(module, name);
    if (!address) { throw std::runtime_error(name); }
    static_assert(sizeof(Function) == sizeof(address));
    Function result;
    std::memcpy(&result, &address, sizeof(result));
    return result;
}

void require(bool value, const char* message) {
    if (!value) { throw std::runtime_error(message); }
}

std::string utf8(const fs::path& path) {
    const auto bytes = path.u8string();
    return {bytes.begin(), bytes.end()};
}
}

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "Usage: ConduitLoaderSmoke.exe ConduitClient.dll\n";
        return 2;
    }
    const auto root = fs::temp_directory_path() /
        ("conduit-loader-smoke-" + std::to_string(GetCurrentProcessId()));
    HMODULE module = nullptr;
    ConduitClient* client = nullptr;
    decltype(&ConduitDestroy) destroy = nullptr;
    decltype(&ConduitStop) stop = nullptr;
    try {
        fs::create_directories(root / "ui");
        const std::string bytes(argv[1]);
        const fs::path dll(std::u8string(bytes.begin(), bytes.end()));
        // Match LeviLauncher's native mod loader contract: load and retain the DLL.
        module = LoadLibraryExW(fs::absolute(dll).c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
        if (!module) {
            throw std::runtime_error("LoadLibraryExW failed with Windows error " +
                                     std::to_string(GetLastError()));
        }
        require(!fs::exists(root / "client.log"), "DLL performed work during load");

        const auto version = symbol<decltype(&ConduitGetApiVersion)>(module, "ConduitGetApiVersion");
        const auto create = symbol<decltype(&ConduitCreate)>(module, "ConduitCreate");
        const auto start = symbol<decltype(&ConduitStart)>(module, "ConduitStart");
        const auto stats = symbol<decltype(&ConduitGetStats)>(module, "ConduitGetStats");
        stop = symbol<decltype(&ConduitStop)>(module, "ConduitStop");
        destroy = symbol<decltype(&ConduitDestroy)>(module, "ConduitDestroy");
        require(version() == CONDUIT_API_VERSION, "API version mismatch");
        const auto watch = utf8(root / "ui");
        const auto log = utf8(root / "client.log");
        ConduitConfig config{sizeof(config), CONDUIT_API_VERSION, watch.c_str(), log.c_str(),
                             CONDUIT_HOTKEY_DISABLED, 50, 150};
        require(create(&config, &client) == CONDUIT_OK && client, "Create failed");
        require(start(client) == CONDUIT_OK, "Start failed");
        ConduitStats snapshot{};
        snapshot.size = sizeof(snapshot);
        require(stats(client, &snapshot) == CONDUIT_OK && snapshot.running == 1,
                "Client did not initialize");
        require(stop(client) == CONDUIT_OK, "Stop failed");
        snapshot = {};
        snapshot.size = sizeof(snapshot);
        require(stats(client, &snapshot) == CONDUIT_OK && snapshot.running == 0,
                "Client did not stop");
        destroy(client);
        client = nullptr;
        require(FreeLibrary(module) != 0, "FreeLibrary failed");
        module = nullptr;
        std::ifstream file(root / "client.log");
        const std::string contents((std::istreambuf_iterator<char>(file)),
                                   std::istreambuf_iterator<char>());
        require(contents.find("Client started") != std::string::npos, "Start log missing");
        require(contents.find("Client stopped") != std::string::npos, "Stop log missing");
        file.close();
        fs::remove_all(root);
        std::cout << "Loader contract smoke passed: passive load, explicit start/log/stop/unload\n";
        return 0;
    } catch (const std::exception& error) {
        if (client && stop && destroy) { stop(client); destroy(client); }
        if (module) { FreeLibrary(module); }
        std::cerr << error.what() << "\nFixtures retained at " << root << '\n';
        return 1;
    }
}
