#include "minecraft/version.hpp"
#include <stdexcept>
#ifdef _WIN32
#include <windows.h>
#include <bcrypt.h>
#include <filesystem>
#include <fstream>
#include <vector>
#include <memory>
namespace conduit::minecraft {
namespace {
struct HashHandles {
    BCRYPT_ALG_HANDLE algorithm{};
    BCRYPT_HASH_HANDLE hash{};
    ~HashHandles() { if (hash) BCryptDestroyHash(hash); if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0); }
};
}
ExecutableIdentity detectRunningExecutable() {
    ExecutableIdentity identity;
    std::array<wchar_t, 32768> path{};
    const auto length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    if (!length || length == path.size()) throw std::runtime_error("Cannot identify game executable");
    identity.path.assign(path.data(), length);
    HashHandles handles;
    if (BCryptOpenAlgorithmProvider(&handles.algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0)
        throw std::runtime_error("SHA256 unavailable");
    DWORD objectSize{}, written{};
    if (BCryptGetProperty(handles.algorithm, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&objectSize), sizeof(objectSize), &written, 0) < 0)
        throw std::runtime_error("SHA256 property failed");
    std::vector<UCHAR> object(objectSize);
    if (BCryptCreateHash(handles.algorithm, &handles.hash, object.data(), objectSize, nullptr, 0, 0) < 0)
        throw std::runtime_error("SHA256 initialization failed");
    std::ifstream input(std::filesystem::path(identity.path), std::ios::binary);
    std::array<char, 65536> bytes{};
    bool ok = input.is_open();
    while (ok && input) {
        input.read(bytes.data(), bytes.size());
        if (input.gcount() && BCryptHashData(handles.hash, reinterpret_cast<PUCHAR>(bytes.data()), static_cast<ULONG>(input.gcount()), 0) < 0) ok = false;
    }
    if (!ok || !input.eof() || BCryptFinishHash(handles.hash, identity.sha256.data(), static_cast<ULONG>(identity.sha256.size()), 0) < 0)
        throw std::runtime_error("Cannot hash game executable");
    // Read architecture from the hashed on-disk image rather than dereferencing an unvalidated module.
    input.clear(); input.seekg(0);
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
