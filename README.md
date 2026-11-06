# ConduitClient

An experimental C++20 development client for **Minecraft Bedrock Windows 1.26.60.5** (GDK x64, package **1.26.6005.0**), tested through **BedrockOnLinux/Proton on Linux**.

## v0.1.0-beta.1

**Use [Beta 1](https://github.com/zelisce/Conduit-Client/releases/tag/v0.1.0-beta.1) for new installations.** The standalone Windows x64 DLL initializes automatically after injection. The alpha's Frida server and external controller are no longer required.

The standalone beta lets **Ctrl+R trigger a native resource reload**, making edited legacy JSON UI visible without manually running `/reload all` for each edit. The result has been visually confirmed in-game. Close and reopen the affected screen after reloading.

**This is a broader resource reload, not JSON UI-only hot reload.** It can refresh chunk rendering, freeze the game for roughly one second, and make player skins invisible. Other resource effects are not fully characterized. Targeted JSON UI-only reload remains under development. Back up your worlds and use this experimental beta at your own risk.

Features:

- Ctrl+R with JSONC validation before native dispatch.
- On-demand JSON validation, including custom definitions referenced outside `ui/`.
- Automatic initialization and game-thread/live-owner acquisition with build checks.
- Persistent resource-pack configuration, diagnostic logs, and safe disabling.
- Linux development and automated testing through Wine/Proton.

Continuous resource-pack watching and startup JSON scans are disabled in this beta. Ctrl+R validates the configured JSON tree on demand before native dispatch. Scanning/validation is not Minecraft's semantic JSON UI validation.

## Install and enable

Download **`ConduitClient.dll`, `SHA256SUMS`, and `THIRD_PARTY_NOTICES.txt` from [v0.1.0-beta.1](https://github.com/zelisce/Conduit-Client/releases/tag/v0.1.0-beta.1). Verify the DLL's SHA-256 against `SHA256SUMS`.

1. Create `%LOCALAPPDATA%\ConduitClient\config.json` inside Minecraft's Windows/Proton user profile:

   ```json
   {
     "enabled": true,
     "resourcePackPath": "C:\\actual\\profile\\development_resource_packs\\your-pack"
   }
   ```

   Use an absolute Windows path to the active **pack root** containing `manifest.json` and `ui/`, not just the UI directory. The pack must already be active in Minecraft; Conduit does not mount it.
2. Exit Minecraft to retire older pinned prototypes and stop old Frida monitors/controllers. Inject **only the beta's standalone DLL** into a fresh Minecraft process using BedrockOnLinux's built-in DLL injector or a Windows x64 `LoadLibrary` injector.
3. Enter a backed-up world with the configured pack active, save a JSON UI edit, press **Ctrl+R**, and close/reopen the affected screen.

**No Frida server, external controller, process/thread configuration, or manual backend activation is required.** Follow the [Beta 1 setup and troubleshooting guide](https://github.com/zelisce/Conduit-Client/blob/v0.1.0-beta.1/docs/standalone-initialization.md) for configuration, injection, logs, and disabling. Only the exact tested Minecraft build is supported.

If configuration is missing, the DLL writes a template with an empty pack path and installs no hooks. Set the path and restart Minecraft before injecting again. Logs are under `%LOCALAPPDATA%\ConduitClient\`; check `standalone.log` for startup errors and `native-stages.log` for backend activation.

To disable, create `%LOCALAPPDATA%\ConduitClient\DISABLE`. A pre-existing DISABLE file or `"enabled": false` prevents hook installation. Do not force-unload the DLL; it remains resident until game exit. Remove DISABLE or enable configuration, then restart Minecraft to re-enable. Initialization failures also require a game restart.

The [alpha release](https://github.com/zelisce/Conduit-Client/releases/tag/v0.1.0-alpha.1) and its Frida/controller setup are retained for historical reference. They are not required for the beta. When building from source, use the beta tag's standalone build instructions; the older foundation `bin/ConduitClient.dll` is a different target.

## Development

- [Build and tests](docs/build.md) — portable Linux core and Windows/Wine tests.
- [Beta standalone initialization and build guide](https://github.com/zelisce/Conduit-Client/blob/v0.1.0-beta.1/docs/standalone-initialization.md).
- [Historical experimental backend, configuration and C exports](docs/experimental-legacy-reload.md).
- [Versioned native reload evidence](docs/reverse-engineering/1.26.60-legacy-reload-path.md).
- [Alpha artifact provenance](docs/releases/v0.1.0-alpha.1.md).
- [Development handoff](docs/development-handoff.md).
- [Public foundation C API](include/conduit/api.h), [project guidance](AGENTS.md), [third-party notices](THIRD_PARTY_NOTICES.txt).

Build outputs, game binaries, logs, dumps, Frida environments and analysis databases are excluded from Git. Never publish proprietary game files.
