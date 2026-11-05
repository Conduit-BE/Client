# Conduit Client

C++20 Windows x64 DLL foundation for a Minecraft Bedrock GDK development client.

The DLL self-starts when loaded into `Minecraft.Windows.exe`. Ctrl+R requests a JSON UI reload and displays a Windows confirmation dialog; the Minecraft reload backend currently returns `Unsupported`. Ctrl+K performs logical shutdown.

F3 toggles disk JSON UI diagnostics in the D3D12 overlay while the game is focused: validated files, top-level definitions, first namespace, validation status, reload counters, and watcher errors. The watched directory is `%LOCALAPPDATA%\ConduitClient\ui`. These are disk diagnostics, not the live game's control tree. The 1.26.60 investigation is recorded in [the versioned REA note](docs/reverse-engineering/1.26.60-json-ui.md).

- [Build and test the DLL](docs/build.md), including the standalone `LoadLibrary` harness and Wine testing on CachyOS/Arch.
- [Public C API](include/conduit/api.h).
- [Local Minecraft acquisition with Xodus](docs/xodus.md).
- [GDK loading assessment and smoke test](docs/gdk-integration.md).
- [BedrockOnLinux Ctrl+R verification](docs/bedrock-on-linux.md).
- [D3D12 HUD implementation and live validation status](docs/hud-status.md).
- [Project conventions and reverse-engineering notes](AGENTS.md).
- [Development handoff and local artifacts](docs/development-handoff.md).

