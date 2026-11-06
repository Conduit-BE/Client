# Standalone installation — v0.2.0-beta

Target: exact Bedrock Windows **1.26.60.5**, GDK x64, package **1.26.6005.0**, tested through BedrockOnLinux/Proton. Targeted legacy JSON UI updates and observed chunk/texture isolation were confirmed in-game, with a successful user follow-up. This remains an experimental pre-release; back up worlds. See [evidence and boundaries](reverse-engineering/1.26.60-json-ui-only-prototype.md).

## Download and inject

Download `ConduitClient.dll`, `SHA256SUMS`, and `THIRD_PARTY_NOTICES.txt` from [v0.2.0-beta](https://github.com/zelisce/Conduit-Client/releases/tag/v0.2.0-beta). Verify the checksum. Configure Minecraft's Windows/Proton profile at `%LOCALAPPDATA%\ConduitClient\config.json`:

```json
{
  "enabled": true,
  "resourcePackPath": "C:\\actual\\profile\\development_resource_packs\\your-pack",
  "reloadMode": "LegacyJsonUiOnly"
}
```

Use the active pack **root**, containing `manifest.json` and `ui/`, with an absolute Windows path. Referenced custom JSON definitions outside `ui/` are included. The pack must already be active. Conduit does not guess the active pack, mount packs, edit pack files or patch the game executable.

Exit Minecraft to retire older pinned prototypes. Configure BedrockOnLinux's built-in DLL injector or a Windows x64 `LoadLibrary` injector to load only this release's DLL, then launch Minecraft. No Frida server/controller, manual PID/thread discovery or backend activation is required. Enter a backed-up world, save a visible legacy JSON UI change, press Ctrl+R and reopen the affected screen. Further reloads do not require restarting Minecraft or the world.

If config is missing, the DLL writes a template with an empty pack path and installs no hooks. Fill the path **and add `reloadMode` as above**, then restart and inject again. There is no automatic config migration: upgrading from the previous beta requires adding the mode field.

## Modes

`LegacyJsonUiOnly` invokes the complete native legacy UI button callback, including definition loading, resource-task synchronization and collection invalidation. It supports the verified single-client context and refuses unsupported/multiple contexts. A short pause remains; user testing reported UI updates without visible chunk or texture reload.

`FullResourceReload` preserves the V5 native operation at RVA `0x873a20`. It affects unrelated resources/chunks, may cause a roughly one-second freeze and may hide skins. It remains the default **when `reloadMode` is omitted** for compatibility. Switch explicitly for fallback testing and restart Minecraft; never confuse it with targeted mode. Unknown mode names fail startup.

Continuous file watching and startup JSON scans are disabled. Ctrl+R validates the JSON tree on demand. JSONC syntax errors prevent dispatch and retain prior validation metadata; Minecraft semantic validation and transactional UI rollback are not guaranteed. The modes share exact-image checks, owner/vtable/context validation, game-thread dispatch, request serialization and timeout protection. No hardcoded runtime heap address or thread ID is used.

## Verify startup and troubleshoot

All logs are in `%LOCALAPPDATA%\ConduitClient\` within the **actual Minecraft profile**, which may be a nested Proton prefix. Inspect `standalone.log` for `Selected reload mode: LegacyJsonUiOnly` and errors. `native-stages.log` should show `automatic_backend_armed` with `mode=LegacyJsonUiOnly callback=0xf203e0`. It later distinguishes native dispatch, loader completion and callback return. `client.log` records key detection and JSON validation failures. Native completion alone is not visual proof.

- No startup log: check the injector path/profile. Do not inject the older foundation DLL or run an old controller alongside the standalone DLL. Failures before the log opens use Windows debugger output.
- Empty/missing pack configuration: supply the absolute active pack root, then restart. Do not point to Conduit's old staging folder.
- `targeted_context_guard_failed`: dispatch was refused. Do not bypass checks; preserve the log and restart before further diagnostics.
- Timeout/fault or initialization failure: no automatic retries; inspect logs and restart. The eight-second coordinator timeout cannot safely interrupt the native category-wait routine already running.
- No visible update: verify the saved file belongs to the active pack, reopen the affected screen and inspect JSON errors. Do not infer success from a pause or completion counter.

## Disable and rollback

Create `%LOCALAPPDATA%\ConduitClient\DISABLE`. The independent worker checks every 250 ms, prevents new work, wakes pending coordinator work, joins the client worker and disables its own hooks. The module/trampolines stay pinned until process exit; do not force-unload. This cannot cancel native work already executing. Stop testing if Minecraft becomes unstable.

A pre-existing DISABLE file or `"enabled": false` prevents hook installation. Remove DISABLE/enable config and restart to re-enable. Configuration and mode changes require a restart. To roll back, use the preserved [v0.1.0-beta](https://github.com/zelisce/Conduit-Client/releases/tag/v0.1.0-beta) DLL and `FullResourceReload`, accepting its broader side effects.

## Build and test from source

```sh
git checkout v0.2.0-beta
cmake -S . -B .local/build/standalone -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/mingw-x86_64.cmake \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DCMAKE_CROSSCOMPILING_EMULATOR=/usr/bin/wine \
  -DCONDUIT_STANDALONE_LEGACY_RELOAD=ON \
  -DCONDUIT_EXPERIMENTAL_LEGACY_RELOAD=ON
cmake --build .local/build/standalone
WINEPREFIX="$PWD/.local/wine-conduit" WINEDEBUG=-all \
  WINEDLLOVERRIDES=mscoree,mshtml= \
  ctest --test-dir .local/build/standalone --output-on-failure
```

Use `.local/build/standalone/standalone/ConduitClient.dll`. The `bin/ConduitClient.dll` output is the older foundation target, **not** this standalone backend. Linux core/policy tests also run natively; prerequisites and API contracts are in [build.md](build.md). MinGW runtime libraries are linked statically; no additional runtime deployment or Frida dependency is required.

Initialization is scheduled after loader-lock release. The worker pins the DLL, reads config, hashes the exact image, installs private hooks and starts the core. Three matching natural frame calls establish affinity. The targeted callback requires additional current client/owner/service/cache checks. Automated tests cover mode selection, completion ordering, unsupported-host rejection, disabled/config-error startup, duplicate loads, synchronization and shutdown. Visual behavior is established separately by the user tests.
