# GDK loading feasibility and smoke test

Research checked 2026-10-19 for Minecraft Bedrock GDK `1.26.5203.0`, Windows x64. This is a loading assessment, not a claim of in-game validation. The Xodus copy under `.local/` contains a protected executable and is not used for execution or analysis.

## Recommended path

Use a legitimate Windows 10/11 installation and [LeviLauncher](https://github.com/LiteLDev/LeviLauncher) to create an isolated, licensed GDK instance. Its [quick start](https://github.com/LiteLDev/LeviLauncher/blob/main/docs/guide/quick-start.md) asks you to install Minecraft from Microsoft Store at least once, sign in with the owning account, install a selected version in LeviLauncher, and start it from **Home → Launch**. [Requirements](https://github.com/LiteLDev/LeviLauncher/blob/main/docs/guide/requirements-installation.md) include Gaming Services, GameInput, and WebView2. Do not start the protected Xodus executable directly or copy files into the installed game package.

LeviLauncher [documents `.dll` mod import](https://github.com/LiteLDev/LeviLauncher/blob/main/docs/guide/mods-integrations.md). Its [GDK native loader source](https://github.com/LiteLDev/LeviLauncher/blob/main/native/levilauncher/src/config/mod_loader.cpp) enumerates mod folders containing `manifest.json` with `"type": "preload-native"`, resolves a relative DLL `entry`, calls `LoadLibraryExW(..., LOAD_WITH_ALTERED_SEARCH_PATH)`, and retains loaded modules until process exit. If an external `preloader.dll` is present, this built-in path is skipped. Use LeviLauncher's mod management UI for import and enablement; avoid editing the installed Minecraft package.

**Updated loading behavior:** the DLL now schedules automatic initialization when the host executable is `Minecraft.Windows.exe`, so loaders that only call `LoadLibraryExW` can activate Ctrl+R polling without calling a Conduit export. Initialization runs on a worker after `DllMain` returns. Ctrl+R displays a message box; JSON UI reload is still unsupported. See [BedrockOnLinux verification](bedrock-on-linux.md). The loader provides no explicit stop callback, so normal process exit cannot guarantee a final shutdown log entry.

Microsoft's [Bedrock Add-On scripting workflow](https://learn.microsoft.com/en-us/minecraft/creator/documents/scripting/introduction?view=minecraft-bedrock-stable) supports JavaScript in behavior packs; the [manifest module types](https://learn.microsoft.com/en-us/minecraft/creator/reference/content/addonsreference/packmanifest?view=minecraft-bedrock-stable) do not include a native DLL plugin. General [PC GDK mod support](https://learn.microsoft.com/en-us/gaming/gdk/docs/features/common/packaging/packaging-mods) makes resources outside the game executable modifiable, but does not define a native DLL lifecycle API for Minecraft.

## Version and ABI gates

| Gate | Current assessment |
| --- | --- |
| Minecraft `1.26.5203.0` | Acquired package version; verify exact version in the *running licensed Windows instance*. LeviLauncher's version catalog and native loader compatibility for this exact build have not been verified on Windows. |
| CPU and runtime | Conduit is Windows x64 PE built by MinGW-w64; public boundary is a versioned C ABI. The build links MinGW runtime libraries statically. No C++ objects cross the DLL boundary. |
| LeviLauncher native loader | The source shows generic `preload-native` DLL loading, without a Minecraft function signature dependency in that loader function. This does not establish that its full launch chain works with `1.26.5203.0`. |
| LeviLamina | [Latest checked releases](https://github.com/LiteLDev/LeviLamina/releases) are `26.51.x`, and its [client build](https://github.com/LiteLDev/LeviLamina/blob/main/xmake.lua) uses `clang-cl` and matching Bedrock runtime data. Its [mod template](https://github.com/LiteLDev/levilamina-mod-template/blob/main/xmake.lua) uses xmake and LeviLamina APIs. Do not assume a `26.51` client loader or MSVC C++ ABI works with Minecraft `1.26.52` or a MinGW C++ plugin. A future adapter should call Conduit's C ABI and can be built separately with the loader's supported toolchain. |

## Local contract test

Build as in [build.md](build.md), then run:

```sh
env WINEPREFIX="$PWD/.local/wine-conduit" WINEDEBUG=-all WINEDLLOVERRIDES="mscoree,mshtml=" ctest --test-dir .local/build/windows --output-on-failure
```

`loader_contract` uses the loader's `LoadLibraryExW` flag in a non-Minecraft host, then exercises the public C ABI. `automatic_ctrl_r` runs a test executable named `Minecraft.Windows.exe`, confirms automatic initialization, sends synthetic Ctrl+R, detects the dialog, and checks logging and explicit shutdown. `dll_lifecycle` separately exercises repeated loading and cleanup. These tests touch no Minecraft process or JSON UI internals; actual BedrockOnLinux physical input and presentation still require manual validation.

## Windows validation required

1. On Windows x64, install and launch the licensed Microsoft Store Minecraft GDK build once. Install LeviLauncher from its [official releases](https://github.com/LiteLDev/LeviLauncher/releases), prepare Gaming Services, GameInput, and WebView2, then sign in through its own interactive flow. Keep important worlds backed up.
2. In LeviLauncher, select an isolated Release instance for the exact `1.26.5203.0` build if the catalog offers it. Launch the vanilla instance first. Record the in-game version and LeviLauncher version; if the catalog or launch fails, stop and report that mismatch rather than substituting a different executable.
3. Run `ConduitLoaderSmoke.exe` with the absolute path to `ConduitClient.dll` on that Windows machine. Its output and log check validate MinGW runtime dependencies and lifecycle independently of Minecraft.
4. To assess only loading, import a local development copy of `ConduitClient.dll` through LeviLauncher's mod management, enable it for the isolated instance, and launch. Check LeviLauncher's own log for a successful native load, and use a normal process module viewer to confirm `ConduitClient.dll` appears under the running Minecraft process. **No Conduit log is expected:** the current loader contract does not call `ConduitStart`. Disable/remove the test mod in LeviLauncher after the check. Do not alter the installed game package.
5. Record launch success/failure, exact version, loader version, Windows architecture, module presence, and any error codes in a new note under `docs/reverse-engineering/` following `AGENTS.md`. Do not attach protected binaries or raw dumps.

## Next engineering milestone

Establish a documented loader lifecycle callback compatible with this exact GDK version, then implement a small adapter that calls `ConduitCreate`/`ConduitStart` **after** loader initialization and calls `ConduitStop`/`ConduitDestroy` **before** unload or process shutdown. Keep the adapter's ABI separate from the MinGW core DLL. If LeviLamina adds verified `26.52` client support, its mod lifecycle is a candidate; use its published SDK and toolchain. No Minecraft internals, hooks, signatures, or offsets are needed for that milestone.
