# Standalone initialization — v0.1.0-beta.1

**Experimental Beta 1: standalone in-game visual confirmation is pending.** Published at the user’s explicit request before that confirmation. The confirmed alpha.1 V5 DLL and tag remain unchanged. This work only replaces the initialization/controller dependency; it does not implement JSON UI-only reload.

## Build and configuration

Build separately so neither the foundation DLL nor the preserved V5 artifact is overwritten:

```sh
cmake -S . -B .local/build/standalone-alpha2 -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/mingw-x86_64.cmake \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DCMAKE_CROSSCOMPILING_EMULATOR=/usr/bin/wine \
  -DCONDUIT_STANDALONE_LEGACY_RELOAD=ON \
  -DCONDUIT_EXPERIMENTAL_LEGACY_RELOAD=ON
cmake --build .local/build/standalone-alpha2
```

Download `ConduitClient.dll`, `SHA256SUMS`, and `THIRD_PARTY_NOTICES.txt` from [Beta 1](https://github.com/zelisce/Conduit-Client/releases/tag/v0.1.0-beta.1), or build from this tag. Standalone build output: `.local/build/standalone-alpha2/standalone/ConduitClient.dll`. The `bin/ConduitClient.dll` output is still the older foundation target; do not confuse them. No extra runtime library or Frida component is needed by the standalone output.

Create `%LOCALAPPDATA%\ConduitClient\config.json` inside Minecraft's Windows/Proton user profile:

```json
{
  "enabled": true,
  "resourcePackPath": "C:\\actual\\profile\\development_resource_packs\\your-pack"
}
```

Use an absolute Windows pack-root path containing `manifest.json` and `ui/`, not just the `ui` directory. Custom referenced JSON files elsewhere in the pack are included. The DLL does not guess which installed pack is active. If configuration is missing, it writes this template with an empty path and logs instructions, installing no hooks. Set the path and restart Minecraft before injecting again. Configuration errors, unsupported images, or incompatible hooks fail closed.

## Manual verification procedure

1. Exit Minecraft to retire earlier pinned prototypes. Stop previous Frida monitors/controllers. Configure BedrockOnLinux's built-in DLL injector (or a Windows x64 `LoadLibrary` injector) to inject **only the standalone DLL** above. Launch the tested Minecraft build and enter a backed-up world with the configured pack active.
2. No Frida server, controller, process ID, thread ID, or manual activation is needed. Do not run an old prototype controller alongside it.
3. Check `%LOCALAPPDATA%\ConduitClient\standalone.log` for startup errors and the selected pack path. In `native-stages.log`, require `automatic_backend_armed verified_frame_caller=0x83274 resource_entry=0x873a20 flag=1` before testing. This records the automatically acquired thread.
4. Save an obvious reversible legacy JSON UI edit, press Ctrl+R, and close/reopen its screen. The user must confirm that the change is visible. Then repeat with another edit. Do not use `/reload all` as part of this test.
5. `client.log` distinguishes Ctrl+R and JSON validation. Native stages distinguish queue, dispatch, loader and completion. Completion is not proof of a visual update. If the game becomes unstable, stop testing.

The V5 limitations remain: broad resource/chunk reload, possible one-second freezes and invisible player skins; exact Bedrock Windows **1.26.60.5 / package 1.26.6005.0** only. Continuous file watching is disabled: no startup JSON scan or background pack scans run. Ctrl+R validates the configured JSON tree on demand before requesting native reload.

## Safe disabling

Create `%LOCALAPPDATA%\ConduitClient\DISABLE`. The independent initialization worker checks this every 250 ms, prevents new work, wakes any pending reload, joins the client worker and disables its own three hooks. Callback code/trampolines stay resident and pinned until game exit. A pre-existing DISABLE file or `"enabled": false` prevents hook installation. Do not force-unload the DLL. Remove DISABLE or enable configuration, then restart Minecraft to re-enable; initialization is deliberately single-use per process. If configuration/logging cannot be opened, debugger output reports the failure.

## Initialization and evidence boundary

Alpha.1 required an external controller to load the DLL, call `ConduitPrototypeStartAsync`, select `ConduitPrototypeSetResourceFallback(1)`, and provide a current process/thread observation via `ConduitPrototypeArmFromObservation`. These diagnostic exports and their manual path remain available in the separate prototype target.

The standalone `DllMain` schedules a worker and closes its thread handle; it does not install hooks, inspect game memory, hash files, parse configuration, or wait under loader lock. After loader-lock release the worker pins the module, reads configuration, initializes logging and the core, and installs the same V5 backend with automatic frame acquisition enabled. A single atomic initialization gate prevents duplicate workers. Terminal errors/disable cannot restart initialization in the same module.

Prior evidence in [the versioned reload note](reverse-engineering/1.26.60-legacy-reload-path.md) records frame entry **0x879030** repeatedly receiving the reload owner and returning to **0x83274** on the natural reload/completion thread. Saved events in `legacy-prototype-20261106/events.jsonl` include three matching samples at agent times 1793922258054, 1793922258055 and 1793922258058. The previously verified V5 dispatch boundary is retained.

Automatic mode requires three matching current calls from that exact module-relative return site, with consistent owner/thread and all original owner/service/context/vtable checks. It neither infers affinity from the injection thread nor dereferences a cached heap pointer for dispatch. Dispatch additionally requires that exact call site, the acquired thread, the current matching owner and the existing validity checks. Ambiguous acquisition latches a fault. The exact image SHA-256 and entry prefixes are still checked before hooks become reachable. No native argument, calling convention or reload operation was changed: **0x873a20(currentOwner, currentOwner's +0x228 context, 1)** remains the V5 call.

Request serialization, eight-second completion timeout/fault, invalid-JSON rejection and logical disable are retained. Automated tests cover initialization races and terminal states, affinity rejection/activation, real automatic DLL startup on unsupported hosts, disabled/config-error startup, duplicate DLL loads, and client request coalescing/worker shutdown. They do not exercise successful Minecraft hook installation or prove standalone visual refresh; those require the pending manual test.
