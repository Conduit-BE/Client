<p align="center">
  <img src="./assets/logo.png" alt="Conduit Client Logo">
</p>

<h1></h1>

<p align="center">
  A DLL-based Client made for Minecraft Bedrock, with its purpose being helping you create JSON UI projects faster, easier and more optimized!
</p>

<div align="center">
  <a href="https://discord.gg/bestack">
    <img src="https://img.shields.io/discord/1402649772271210566?logo=discord&logoColor=white&color=5865F2&style=for-the-badge" alt="Discord">
  </a>
  <img src="https://img.shields.io/github/stars/Conduit-BE/client?style=for-the-badge" alt="GitHub Stars">
</div>

<h2>Introduction</h2>

**Conduit Client** is a Minecraft: Bedrock Edition DLL client that helps you build **JSON User Interfaces** easier, faster and more optimized with features like JSON UI reload, debug menu and more coming soon!

## Install and configure

Download `ConduitClient.dll`, `SHA256SUMS`, and `THIRD_PARTY_NOTICES.txt` from the release. Verify the DLL checksum.

1. Create `%LOCALAPPDATA%\ConduitClient\config.json` inside Minecraft's Windows/Proton profile:

   ```json
   {
     "enabled": true,
     "resourcePackPath": "C:\\actual\\profile\\development_resource_packs\\your-pack",
     "reloadMode": "LegacyJsonUiOnly"
   }
   ```

   Use an absolute Windows path to the active **pack root**, containing `manifest.json` and `ui/`. Custom referenced JSON files outside `ui/` are included. The pack must already be active in Minecraft; Conduit does not mount it or edit its files. **Keep `reloadMode` explicitly set as above:** omitting it retains the broader compatibility fallback.
2. Exit Minecraft to retire older pinned DLLs. Configure BedrockOnLinux's built-in injector or your Windows x64 `LoadLibrary` injector to load **only this release's DLL**, then launch Minecraft. Normal use needs no Frida components or exported startup call.
3. Enter your backed-up world, save a visible JSON UI change, press **Ctrl+R**, and close/reopen its screen. Subsequent edits need no world or game restart.

[Full setup, build instructions, logs and troubleshooting](docs/standalone-initialization.md).

## Modes and limitations

- **`LegacyJsonUiOnly`**: confirmed targeted native UI action, including definition loading, task synchronization and collection invalidation. No visible chunk/texture refresh was reported in testing. The first experimental implementation supports one client context; split-screen/multiple clients fail closed.
- **`FullResourceReload`**: preserved V5 fallback. It reloads unrelated client resources/chunks, may freeze for roughly one second and may make player skins invisible. It remains the compatibility default if `reloadMode` is omitted; select it deliberately for fallback testing.

Both modes require the exact supported image and acquire the live owner/game thread automatically. Requests cannot overlap. Unknown contexts, unsupported builds and unexpected completion timeouts disable dispatch rather than guessing. The native UI action can wait for outstanding work; Conduit cannot safely cancel an operation already executing.

## Keybinds

Conduit Client provides keyboard shortcuts to simplify your JSON UI development workflow.

| Keybind | Action | Description |
|---|---|---|
| <code>Ctrl&nbsp;+&nbsp;R</code> | Reload JSON UI | Reloads the active development resource pack's JSON UI definitions using the configured reload mode. |
| `F3` | Toggle&nbsp;Diagnostics&nbsp; | Shows or hides the diagnostics interface, displaying information about the current screen. |
| <code>Ctrl&nbsp;+&nbsp;K</code> | Eject Client | Ejects the client from the current Minecraft instance. |

## Logs and disabling

Logs are under `%LOCALAPPDATA%\ConduitClient\`:

- `standalone.log`: configuration, selected mode and startup failures.
- `native-stages.log`: automatic arming, native queue/dispatch/completion and faults.
- `client.log`: Ctrl+R detection and JSON validation errors.

Create `%LOCALAPPDATA%\ConduitClient\DISABLE` to prevent new requests, stop the client worker and retire its hooks. The DLL remains pinned; do not force-unload it. A pre-existing DISABLE file or `"enabled": false` prevents startup. To re-enable, change modes/configuration or recover from an initialization failure, restart Minecraft after updating settings.

## Development and history

- [Standalone installation and build](docs/standalone-initialization.md); [foundation API and tests](docs/build.md).
- [Targeted native sequence and visual evidence](docs/reverse-engineering/1.26.60-json-ui-only-prototype.md).
- [Release provenance](docs/releases/v0.2.0-beta.md); [development handoff](docs/development-handoff.md).
- [Reverse-engineering tools](tools/reverse-engineering/README.md); [historical Frida/controller workflow](docs/experimental-legacy-reload.md).
- [Earlier broad standalone beta](https://github.com/zelisce/Conduit-Client/releases/tag/v0.1.0-beta); [original V5 alpha](https://github.com/zelisce/Conduit-Client/releases/tag/v0.1.0-alpha.1).
- [Project guidance](AGENTS.md), [foundation C API](include/conduit/api.h), [third-party notices](THIRD_PARTY_NOTICES.txt).

The older foundation `bin/ConduitClient.dll` is a different build target; use the downloaded DLL or the standalone build output. Frida is retained only for development. Builds, logs, game executables, dumps, machine-specific configurations and analysis environments remain excluded from Git.
