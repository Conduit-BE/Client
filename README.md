# ConduitClient

An experimental C++20 development client for **Minecraft Bedrock Windows 1.26.60.5** (GDK x64, package **1.26.6005.0**), tested through **BedrockOnLinux/Proton on Linux**.

## v0.1.0-alpha.1

The V5 prototype lets **Ctrl+R trigger a native resource reload**, making edited legacy JSON UI visible without manually running `/reload all` for each edit. The result has been visually confirmed in-game. Close and reopen the affected screen after reloading.

**This is a broader resource reload, not JSON UI-only hot reload.** It can refresh chunk rendering, freeze the game for roughly one second, and make player skins invisible. Other resource effects are not fully characterized. Targeted JSON UI-only reload remains under development. Back up your worlds and use this alpha at your own risk.

Features:

- Ctrl+R with JSONC validation before native dispatch.
- File monitoring, including custom definitions referenced outside `ui/`.
- An opt-in native backend with game-thread dispatch and build checks.
- Linux development and automated testing through Wine/Proton.

Automatic native reload on file changes is disabled in this alpha. Scanning/validation is not Minecraft's semantic JSON UI validation.

## Install and enable

Download the **Windows x64 DLL**, checksum file and third-party notices from [v0.1.0-alpha.1](https://github.com/zelisce/Conduit-Client/releases/tag/v0.1.0-alpha.1). Verify its SHA-256.

This developer alpha requires explicit initialization through the tested **Windows Frida server 17.23.2 inside Proton** and the provided controller. **Injecting the DLL alone does not enable the backend.** Clone this tag for the scripts, configure the actual active resource-pack root and an ignored log directory, and obtain fresh process/thread observations for your running game. One-time setup can require a manual control reload; subsequent edits use Ctrl+R.

Follow [the alpha setup and rollback guide](docs/experimental-legacy-reload.md) and [Frida/Proton setup](docs/reverse-engineering/1.26.60-frida-proton.md). The pack must already be active in Minecraft; Conduit does not mount it. Do not use another Minecraft build or attach with Frida's Linux local device.

The released DLL is the separate experimental V5 target. The foundational `ConduitClient.dll` target still has its default unsupported reload backend; its autostart dialog/F3 overlay is a different path.

To disable an active experimental run, create `DISABLE` in its controller output directory. The controller calls `ConduitPrototypeDisable`, joins the worker and disables its hooks. Modules remain pinned until process exit; do not forcibly unload them. Stopping diagnostics alone does not disable the backend.

## Development

- [Build and tests](docs/build.md) — portable Linux core and Windows/Wine tests.
- [Experimental backend, configuration and C exports](docs/experimental-legacy-reload.md).
- [Versioned native reload evidence](docs/reverse-engineering/1.26.60-legacy-reload-path.md).
- [Alpha artifact provenance](docs/releases/v0.1.0-alpha.1.md).
- [Development handoff](docs/development-handoff.md).
- [Public foundation C API](include/conduit/api.h), [project guidance](AGENTS.md), [third-party notices](THIRD_PARTY_NOTICES.txt).

Build outputs, game binaries, logs, dumps, Frida environments and analysis databases are excluded from Git. Never publish proprietary game files.
