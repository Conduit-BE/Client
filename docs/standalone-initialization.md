# Standalone installation — v0.4.4-beta

Supported target: exact Windows Bedrock **1.26.60.5**, GDK x64, package **1.26.6005.0**. The compatibility layer reads the executable file version and checks exact image SHA-256, architecture and native guards. The verified images include the BedrockOnLinux reference and the exact LeviLauncher 1.1.0 patched executable. Unsupported hosts install no game hooks. This experimental beta still requires in-game regression verification. Download the standalone DLL, checksums, and notices from the [GitHub pre-release](https://github.com/Conduit-BE/client/releases/tag/v0.4.4-beta).

## Build

```sh
cmake -S . -B .local/build/v0.4.4-beta -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/mingw-x86_64.cmake \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DCMAKE_CROSSCOMPILING_EMULATOR=/usr/bin/wine \
  -DCONDUIT_STANDALONE_LEGACY_RELOAD=ON \
  -DCONDUIT_EXPERIMENTAL_LEGACY_RELOAD=ON
cmake --build .local/build/v0.4.4-beta
WINEPREFIX="$PWD/.local/wine-conduit" WINEDEBUG=-all \
  WINEDLLOVERRIDES=mscoree,mshtml= \
  ctest --test-dir .local/build/v0.4.4-beta --output-on-failure
```

Inject `.local/build/v0.4.4-beta/standalone/ConduitClient.dll`. The `bin/ConduitClient.dll` output is the separate foundation target. MinGW runtimes are statically linked. No Frida controller or external PNG is required for initialization.

## Install and use

Exit Minecraft to retire old pinned modules, configure your x64 injector to load only this standalone DLL, and launch. For LeviLauncher 1.1.0, import the standalone DLL through the instance mod manager as a `preload-native` mod and restart the 1.26.60.05 GDK instance. Initialization runs after loader-lock release. Configuration includes diagnostics scale and factory visibility defaults; [obsolete keys migrate automatically](configuration.md). Ensure your development pack is already active in Minecraft. Save a visible JSON UI edit, press Ctrl+R, and reopen the affected screen. F3 toggles diagnostics while Minecraft is focused.

Only targeted native UI reload is implemented. Three matching natural frames establish affinity before dispatch; unknown or multiple-client contexts fail closed. The callback retains definition loading, native task synchronization and collection invalidation. Disk JSON preflight is disabled; Minecraft reloads and parses its mounted definitions directly. See [reload admission](configuration.md). The eight-second coordinator timeout does not cancel native work already running.

Logs are under `%LOCALAPPDATA%/ConduitClient/logs/` in the actual Windows/Proton profile. `standalone.log` records startup and failures; `native-stages.log` records automatic arming, dispatch/completion and guards; `client.log` records keys and reload admission. Old root logs remain intact. Startup logging failures use debugger output.

## Deactivation

Press Ctrl+K once while Minecraft owns the foreground window. Holding the chord does not repeat it; a chord held at initialization is suppressed. The initialization worker shuts down F3/native rendering and Present measurement, disables reload admission, wakes queued requests, joins the reload worker and disables its hooks. After deactivation, shortcuts no longer operate. Creating `DISABLE` in the data root also stops the client, and an existing file prevents startup.

This is **safe deactivation, not physical DLL unloading**. The existing implementation permanently pins the module and retains MinHook trampolines because callbacks can be inside native calls during shutdown. It cannot prove all threads have returned from DLL code. No FreeLibrary is called from a hook. Resident callback state must remain valid; restart Minecraft to reactivate.

## Manual verification required

Check repeated Ctrl+R edits with no broad chunk/texture refresh; F3 screen/element/binding/factory counters, per-factory breakdown, FPS, reload latency and counters; Ctrl+K with F3 visible and a pending reload; continued normal play and absence of shortcut effects after deactivation. Test DISABLE independently. Confirm unsupported builds fail before hooks. Automated tests do not verify these native game behaviors.

The embedded Conduit PNG is imported through Minecraft’s native texture loader and substituted only for an eligible active title image draw. Its aspect ratio, splash text and menu layout are preserved. Conservative global-pack metadata checks suppress replacement for unknown/customized profiles. See [logo findings and verification limits](reverse-engineering/1.26.60-title-logo.md).
