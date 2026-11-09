# DLL foundation: build and test

For the current **v0.4.2-beta standalone JSON UI reload**, use [the standalone build and setup guide](standalone-initialization.md). Enable `CONDUIT_STANDALONE_LEGACY_RELOAD` and inject its `standalone/ConduitClient.dll` output. The default foundation target described below retains its unsupported native reload backend; it is a different DLL target.

Conduit requires CMake 3.24+, a C++20 compiler with `std::jthread`, and Windows x64 for the DLL. The portable core tests also run natively on Linux. The D3D12 presentation hook uses pinned [MinHook 1.3.4](https://github.com/TsudaKageyu/minhook). JSON syntax validation uses [nlohmann/json 3.12.0](https://github.com/nlohmann/json/tree/v3.12.0). CMake FetchContent downloads these at configure time unless checkouts exist at `.local/deps/minhook` and `.local/deps/json`.

## CachyOS / Arch prerequisites

```sh
sudo pacman -S --needed cmake ninja gcc mingw-w64-gcc
```

For Windows runtime tests on Linux, install `wine`. The commands below work from the repository root in Fish or Bash. Build products and the dedicated Wine prefix are inside the Git-ignored `.local/` directory.

## Build Windows x64

```sh
cmake -S . -B .local/build/windows -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/mingw-x86_64.cmake -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_CROSSCOMPILING_EMULATOR=/usr/bin/wine
cmake --build .local/build/windows
```

The output directory `.local/build/windows/bin/` contains:

- `ConduitClient.dll`: the Windows x64 DLL.
- `ConduitHarness.exe`: a standalone host that resolves the API with `GetProcAddress`; it does not link to the DLL's import library.
- `ConduitCoreTests.exe`: portable module tests compiled for Windows.
- `ConduitLoaderSmoke.exe`: a small host that mimics LeviLauncher's native `LoadLibraryExW` call and explicitly tests the Conduit lifecycle.
- `Minecraft.Windows.exe`: a test host with the same basename as the game, exercising automatic initialization and Ctrl+R feedback.
- `ConduitD3D12HudSmoke.exe`: independent renderer and pixel-readback test.
- `overlay/Minecraft.Windows.exe`: hook test host that loads the DLL, presents a D3D12 frame, and reads back panel pixels.

The MinGW runtime libraries are linked statically, so `libstdc++`, `libgcc`, and `libwinpthread` DLLs do not need to be copied next to the binaries. Windows system DLLs remain dynamically linked. The toolchain file targets the Arch MinGW installation; adjust the compiler/root paths for a different distribution.

If Wine is unavailable, omit `CMAKE_CROSSCOMPILING_EMULATOR`. Windows test executables will still build, but CTest will not register tests it cannot execute. On a real Windows x64 machine, run `ConduitHarness.exe ConduitClient.dll` and `ConduitCoreTests.exe` from the output directory. A native Windows CMake build can also use a suitable MSVC C++20 toolchain; that compiler is not tested here.

## Execute tests

Native Linux:

```sh
cmake -S . -B .local/build/native -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build .local/build/native
ctest --test-dir .local/build/native --output-on-failure
```

Windows tests under Wine (initial launch creates a dedicated prefix):

```sh
env WINEPREFIX="$PWD/.local/wine-conduit" WINEDEBUG=-all WINEDLLOVERRIDES="mscoree,mshtml=" ctest --test-dir .local/build/windows --output-on-failure
```

The DLL harness performs three `LoadLibrary`/`FreeLibrary` cycles. In each cycle it tests API validation, start/stop idempotency, manual reload requests, JSON creation/modification/deletion, concurrent API callers, restart after a failed start, and destroying a running context. It removes its fixtures after unloading, checking that file handles were closed. The core tests cover content changes with preserved timestamps, debounce, recursive monitoring, recovery from a missing directory, concurrent logging, hotkey edges, and the placeholder backend contract. Test failures print diagnostic messages and the Windows harness retains its fixture directory for inspection.

## Project layout

| Path | Responsibility |
| --- | --- |
| `include/conduit/api.h` | Versioned C ABI, configuration, status codes, statistics |
| `src/api.cpp` | Input validation and exception containment at the ABI boundary |
| `src/client.*` | Owned context, worker, lifecycle, and reload scheduling |
| `src/core/logger.*` | Serialized append-only logging with write failure reporting |
| `src/core/file_monitor.*` | Portable recursive JSON monitoring and debounce |
| `src/core/hotkey.hpp` | Input abstraction and rising-edge detection |
| `src/core/reload_manager.*` | Backend interface and Minecraft placeholder |
| `src/platform/windows/` | `DllMain`, Windows key-state polling, DXGI hooks, and D3D12 HUD renderer |
| `tests/` | Native core tests and Windows dynamic-loading harness |
| `cmake/toolchains/` | Windows x64 cross-compilation configuration |

## Lifecycle and API contract

1. `LoadLibrary` the DLL, resolve the seven exports, and check `ConduitGetApiVersion()`.
2. Set `ConduitConfig.size`, `api_version`, UTF-8 watch-directory and log paths. `ConduitCreate` copies the paths and opens the append-only log. It does not start a thread.
3. `ConduitStart` establishes a file baseline, initializes input, and starts one worker. Existing files do not trigger a reload on startup. A missing watch directory fails start and permits a later retry.
4. `ConduitRequestReload` queues a request. The worker coalesces pending requests, file changes, and hotkey presses into batches. A successful API return only acknowledges queuing. Query `ConduitStats` for attempts, successes, unsupported batches, monitor errors, and logging/worker failure flags. Counters persist across stop/start and are a live snapshot, not an atomic transaction across fields.
5. `ConduitStop` cancels and joins the worker before returning. It is safe to call repeatedly and before start. Queued work may be discarded by stop. `ConduitDestroy` also stops the worker and closes owned resources.
6. Destroy every context, finish all concurrent API calls, and only then call `FreeLibrary`. No handle or function pointer is valid after unload.

API calls on a live context can run concurrently, but `Destroy` and module unload require exclusive ownership. Invalid non-null pointers and use-after-destroy are caller errors. Do not call the API from `DllMain` or under the loader lock. In a `Minecraft.Windows.exe` host, the DLL schedules an automatic worker without waiting in `DllMain`; that worker initializes the client after loader-lock release. Other hosts use the explicit API as before. `ConduitAutoStop` stops and joins the automatic worker before explicit unload. Normal process exit only signals it, because joining under the loader lock is unsafe. See [BedrockOnLinux verification](bedrock-on-linux.md).

## Hotkeys, monitoring, and future reload integration

`hotkey_virtual_key = 0` selects Ctrl+R. Other values from 1 through 254 select the letter/key paired with Ctrl; `CONDUIT_HOTKEY_DISABLED` disables keyboard polling. Configuration belongs to a context and is fixed until it is destroyed. `GetAsyncKeyState` samples the current desktop without reserving a system-wide shortcut. A held chord triggers once until released, and a chord already held at start does not trigger. Very short presses between polls can be missed; polling also has Windows desktop/session access limitations. Automated tests verify edge handling and the automatic Ctrl+R dialog. Ctrl+K remains the automatic logical shutdown chord.

The default polling interval is 50 ms and debounce is 150 ms. The monitor fingerprints the contents of regular `.json` files recursively, using case-insensitive extensions, and skips symlinks. This detects same-size edits even when timestamps are unchanged. Directory or read failures preserve the previous snapshot and report errors. Fingerprints are change detectors, not cryptographic integrity checks. Continuous edits defer publication until the tree settles. This is intentionally a simple polling implementation: large trees or slow filesystems increase CPU/I/O use and shutdown latency, since an in-progress filesystem scan is completed before joining.

The coordinator validates the whole watched JSON tree before calling the backend. Comments are accepted; trailing commas are rejected. Each file must contain an object, and `namespace`, when present, must be a string. Reads are bounded to 8 MiB per file and 64 MiB per tree. Parse/read failures retain the last valid disk metadata, log the file and error, and skip backend invocation. Metadata counts top-level object definitions; it does not resolve inheritance, bindings, or factories. Debounce and parsing do not guarantee an editor has finished a multi-step save or that Minecraft accepts the definitions. The placeholder Minecraft backend always reports `Unsupported` and never claims successful reload. A future backend must apply transactionally and dispatch game changes to a verified UI/game thread; the worker is not assumed to be that thread. No game signatures, addresses, offsets, or hooks are guessed. Record the findings that establish those interfaces under `docs/reverse-engineering/`, following `AGENTS.md`.

Each client owns its logger, monitor, input adapter, worker, and backend. Avoid having multiple clients append to the same log file: logging is synchronized per context, not across independent processes or contexts. Logging uses Unix milliseconds and flushes each record; logs have no automatic rotation yet.
