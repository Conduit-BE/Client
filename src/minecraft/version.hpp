#pragma once
#include <array>
#include <cstdint>
#include <string>
namespace conduit::minecraft {
struct ExecutableIdentity {
    std::uint16_t machine = 0;
    std::array<std::uint16_t, 4> fileVersion{};
    bool hasFileVersion = false;
    std::wstring path;
};
// Throws if the executable cannot be read or has an invalid PE header.
ExecutableIdentity detectRunningExecutable();
}
