 
<p align="center">
  <img src="./assets/logo.png" alt="Conduit Client Logo">
</p>

<h1></h1>

<p align="center">
  A DLL-based development client for Minecraft: Bedrock Edition, designed to make JSON UI development faster, easier, and more efficient.
</p>

<div align="center">
  <a href="https://discord.gg/bestack">
    <img src="https://img.shields.io/discord/1402649772271210566?logo=discord&logoColor=white&color=5865F2&style=for-the-badge" alt="Discord">
  </a>
  <a href="https://github.com/Conduit-BE/Client">
    <img src="https://img.shields.io/github/stars/Conduit-BE/client?style=for-the-badge" alt="GitHub Stars">
  </a>
</div>

<h2>Introduction</h2>

**Conduit Client** is a Minecraft: Bedrock Edition DLL-based development tool that simplifies the process of creating, testing, and debugging **JSON User Interfaces**.

With targeted JSON UI hot reloading, real-time diagnostics, screen detection, and native Minecraft integration, Conduit Client helps developers iterate faster without repeatedly restarting Minecraft.

The project is actively developed, with more debugging, profiling, and development tools planned for future releases.

## Features

- **JSON UI Hot Reload** — Reload JSON UI definitions without restarting Minecraft or rejoining a world.
- **Developer Diagnostics** — Inspect runtime information through an in-game debug overlay.
- **UI Hover Inspector** — Highlight the topmost visible legacy UI control and inspect its runtime properties and ancestors.
- **Element Statistics** — Monitor runtime UI element counts and factory statistics.

## Configuration

```json
{
  "diagnostics": {
    "factories": {
      "empty": true,
      "enabled": true
    }
  },
  "keybinds": {
    "diagnostics": "F3",
    "eject": "Ctrl+K",
    "reload": "Ctrl+R",
    "tooltip": "F5"
  },
  "ui": {
    "alpha": 0.5,
    "scale": 1.0
  }
}
```

You can change any of the provided settings under `/%LOCALAPPDATA%/ConduitClient/config.json`. The reload feature also reloads the client's settings.

## Installation

1. Download the latest compatible `ConduitClient.dll` from the [latest release](https://github.com/Conduit-BE/Client/releases/latest).
2. Configure your DLL injector to load `ConduitClient.dll`. Use the dedicated [Conduit Launcher](https://github.com/Conduit-BE/Launcher) or your own solution.
3. Launch a supported Minecraft: Bedrock Edition version.
4. Open a world or join a server with your resource pack enabled.

[Full installation, building, and troubleshooting documentation](docs/standalone-initialization.md).

## Keybinds

Conduit Client provides keyboard shortcuts to simplify your JSON UI development workflow.

| Default keybind | Action | Description |
|---|---|---|
| <code>Ctrl&nbsp;+&nbsp;R</code> | Reload JSON UI and settings | Reloads mounted JSON UI definitions and applies changes to Conduit’s config. |
| `F3` | Toggle&nbsp;Diagnostics | Shows or hides the debug overlay with screen information, UI statistics, and performance metrics. |
| `F5` | Toggle&nbsp;UI&nbsp;Inspector | Toggles the blue highlight and tooltip for the topmost visible legacy UI control under the cursor. |
| <code>Ctrl&nbsp;+&nbsp;K</code> | Eject Client | Deactivates Conduit Client so it can be injected again. |

## Diagnostics and tooltip

The in-game diagnostics overlay provides real-time information about Minecraft's UI environment and Conduit Client's operations.

Available diagnostics include:

- Current active screen.
- Runtime UI element and binding count.
- UI factory statistics and per-factory breakdown.
- Frames per second (FPS).

Press `F3` to toggle the diagnostics overlay.

The hover-tooltip displays information about the most-top currently hovered UI element, showing its various properties and highlighting the element in blue.

Press `F5` to toggle the hover inspector. The tooltip shows the control name, type, layer, size, position, visibility, and up to six ancestors.

## Version Compatibility

Each supported Minecraft version requires a verified compatibility profile containing the necessary native addresses, structures, and feature availability.

**Supported Bedrock GDK x64 builds:** `1.26.60.5`, `1.26.62.1`, `1.26.63.1`, and `1.26.65.1`.

## Configuration and Logs

Conduit Client stores its configuration and diagnostic logs under:

`%LOCALAPPDATA%\ConduitClient\`

```text
ConduitClient/
├── config.json
├── cache/
│   └── logo-3d2b75a9f89f0a0c.png
└── logs/
    ├── standalone.log
    ├── native-stages.log
    └── client.log
```

### Log files

- `standalone.log` — Initialization, compatibility checks, and startup errors.
- `native-stages.log` — Native integration, dispatch operations, and internal failures.
- `client.log` — Client operations, keyboard shortcuts, and reload admission/results.

The `logs` directory is created automatically when required. The `cache` PNG is generated from the embedded DLL asset; no external PNG or resource-pack installation is required.

## Development

Conduit Client is actively developed with a focus on stability, performance, and developer experience.

Useful resources:

- [Standalone installation and build](docs/standalone-initialization.md)
- [Native JSON UI reverse-engineering research](docs/reverse-engineering/1.26.60-json-ui-only-prototype.md)
- [Development handoff](docs/development-handoff.md)
- [Reverse-engineering tools](tools/reverse-engineering/README.md)
- [Project guidance](AGENTS.md)
- [Third-party notices](THIRD_PARTY_NOTICES.txt)

## Disclaimer

Conduit Client is an independent, experimental development tool and is not affiliated with, endorsed by, or associated with Mojang Studios or Microsoft.

Minecraft and Minecraft: Bedrock Edition are trademarks of their respective owners.
