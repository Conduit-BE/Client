#include "minecraft/version.hpp"
#include <stdexcept>
#ifdef _WIN32
#include <windows.h>
#include <filesystem>
#include <fstream>
#include <vector>
namespace conduit::minecraft {
ExecutableIdentity detectRunningExecutable() {
    ExecutableIdentity identity;
    std::array<wchar_t, 32768> path{};
    const auto length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    if (!length || length == path.size()) throw std::runtime_error("Cannot identify game executable");
    identity.path.assign(path.data(), length);
    std::ifstream input(std::filesystem::path(identity.path), std::ios::binary);
    IMAGE_DOS_HEADER dos{}; input.read(reinterpret_cast<char*>(&dos), sizeof(dos));
    if (!input || dos.e_magic != IMAGE_DOS_SIGNATURE || dos.e_lfanew < 0) throw std::runtime_error("Invalid game PE header");
    input.seekg(dos.e_lfanew);
    DWORD signature{}; IMAGE_FILE_HEADER file{};
    input.read(reinterpret_cast<char*>(&signature), sizeof(signature));
    input.read(reinterpret_cast<char*>(&file), sizeof(file));
    if (!input || signature != IMAGE_NT_SIGNATURE) throw std::runtime_error("Invalid game PE signature");
    identity.machine = file.Machine;
    DWORD ignored{};
    const DWORD size = GetFileVersionInfoSizeW(identity.path.c_str(), &ignored);
    if (size) {
        std::vector<unsigned char> version(size);
        if (GetFileVersionInfoW(identity.path.c_str(), 0, size, version.data())) {
            VS_FIXEDFILEINFO* info{}; UINT infoSize{};
            if (VerQueryValueW(version.data(), L"\\", reinterpret_cast<void**>(&info), &infoSize) &&
                infoSize >= sizeof(*info) && info->dwSignature == 0xfeef04bd) {
                identity.fileVersion = {HIWORD(info->dwFileVersionMS), LOWORD(info->dwFileVersionMS),
                    HIWORD(info->dwFileVersionLS), LOWORD(info->dwFileVersionLS)};
                identity.hasFileVersion = true;
            }
        }
    }
    return identity;
}
}
#else
namespace conduit::minecraft {
ExecutableIdentity detectRunningExecutable() { throw std::runtime_error("Minecraft GDK executable detection requires Windows"); }
}
#endif
