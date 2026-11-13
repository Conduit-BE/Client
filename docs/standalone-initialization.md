# Standalone installation — v0.6.0-beta

The [v0.6.0-beta release](https://github.com/Conduit-BE/client/releases/tag/v0.6.0-beta) supports Bedrock GDK x64 **1.26.60.5**, **1.26.62.1**, and **1.26.63.1**. LeviLauncher lists 1.26.62.1 and 1.26.63.1 as **1.26.62.01** and **1.26.63.01**. Conduit selects profiles by PE architecture and file version, then checks native code signatures before installing hooks.

## Build

```sh
cmake -S . -B .local/build/v0.6.0-beta -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/mingw-x86_64.cmake \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DCMAKE_CROSSCOMPILING_EMULATOR=/usr/bin/wine \
  -DCONDUIT_STANDALONE_LEGACY_RELOAD=ON \
  -DCONDUIT_EXPERIMENTAL_LEGACY_RELOAD=ON
cmake --build .local/build/v0.6.0-beta
WINEPREFIX="$PWD/.local/wine-conduit" WINEDEBUG=-all \
  WINEDLLOVERRIDES=mscoree,mshtml= \
  ctest --test-dir .local/build/v0.6.0-beta --output-on-failure
```

Inject `.local/build/v0.6.0-beta/standalone/ConduitClient.dll`. The `bin/ConduitClient.dll` output is the separate foundation target. MinGW runtimes are statically linked. No Frida controller or external PNG is required for initialization.

## Install and use

Exit Minecraft to retire old pinned modules, configure your x64 injector to load only this standalone DLL, and launch. For LeviLauncher 1.1.0, import the standalone DLL through the instance mod manager as a `preload-native` mod and restart the supported GDK instance. Initialization runs after loader-lock release. Configuration includes diagnostics scale and factory visibility defaults; [obsolete keys migrate automatically](configuration.md). Ensure your development pack is already active in Minecraft. Save a visible JSON UI edit, press Ctrl+R, and reopen the affected screen. F3 toggles diagnostics while Minecraft is focused.

Only targeted native UI reload is implemented. Three matching natural frames establish affinity before dispatch; unknown or multiple-client contexts fail closed. The callback retains definition loading, native task synchronization and collection invalidation. Disk JSON preflight is disabled; Minecraft reloads and parses its mounted definitions directly. See [reload admission](configuration.md). The eight-second coordinator timeout does not cancel native work already running.

Logs are under `%LOCALAPPDATA%/ConduitClient/logs/` in the actual Windows/Proton profile. `standalone.log` records startup and failures; `native-stages.log` records automatic arming, dispatch/completion and guards; `client.log` records keys and reload admission. Old root logs remain intact. Startup logging failures use debugger output.

## Keybinds

| Keybind | Action |
| --- | --- |
| Ctrl+R | Reload JSON UI definitions from active resource packs. |
| F3 | Toggle runtime diagnostics. |
| F5 | Toggle the hover inspector. It highlights the topmost visible legacy control and shows its properties and parent chain. |
| Ctrl+K | Deactivate Conduit for the current Minecraft session. |

## Deactivation

Press Ctrl+K once while Minecraft owns the foreground window. Holding the chord does not repeat it; a chord held at initialization is suppressed. The initialization worker shuts down F3/native rendering and Present measurement, disables reload admission, wakes queued requests, joins the reload worker and disables its hooks. After deactivation, shortcuts no longer operate. Creating `DISABLE` in the data root also stops the client, and an existing file prevents startup.

This is **safe deactivation, not physical DLL unloading**. The existing implementation permanently pins the module and retains MinHook trampolines because callbacks can be inside native calls during shutdown. It cannot prove all threads have returned from DLL code. No FreeLibrary is called from a hook. Resident callback state must remain valid; restart Minecraft to reactivate.

## Manual verification required

Check repeated Ctrl+R edits with no broad chunk/texture refresh; F3 screen/element/binding/factory counters, per-factory breakdown, FPS, reload latency and counters; Ctrl+K with F3 visible and a pending reload; continued normal play and absence of shortcut effects after deactivation. Test DISABLE independently. Confirm unsupported builds fail before hooks. Automated tests do not verify these native game behaviors.

The embedded Conduit PNG is imported through Minecraft’s native texture loader and substituted only for an eligible active title image draw. Its aspect ratio, splash text and menu layout are preserved. Conservative global-pack metadata checks suppress replacement for unknown/customized profiles. See [logo findings and verification limits](reverse-engineering/1.26.60-title-logo.md).
