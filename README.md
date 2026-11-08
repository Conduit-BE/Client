# ConduitClient

C++20 development client for Minecraft Bedrock GDK x64. **v0.4.0-beta — quality-of-life, stability, and maintainability update.** Only the previously verified Windows **1.26.60.5** image (package **1.26.6005.0**) is supported. Other images fail closed.

Ctrl+R uses targeted legacy JSON UI reload. F3 diagnostics retain native rendering, screen detection, element/binding counts, factory statistics and breakdown, FPS, reload counters and latency. Previous versions established targeted reload in-game; this build still needs in-game regression verification.

## Installation

Download `ConduitClient.dll`, `SHA256SUMS`, and `THIRD_PARTY_NOTICES.txt` from the [v0.4.0-beta GitHub pre-release](https://github.com/Conduit-BE/client/releases/tag/v0.4.0-beta). Verify the DLL checksum. For source builds, use [the installation and build guide](docs/standalone-initialization.md). Exit Minecraft first to retire older pinned DLLs. Configure your Windows x64 DLL injector or BedrockOnLinux injector to load only the new standalone `ConduitClient.dll`, then launch Minecraft. The foundation `bin/ConduitClient.dll` is a separate target.

Initialization is automatic. No pack-path or reload-mode setting is needed. Minecraft must already have your development pack active. Save a legacy JSON UI edit, press **Ctrl+R**, then reopen the affected screen. Conduit does not mount packs, install a resource pack, or modify Minecraft's installation.

## Configuration and logs

`%LOCALAPPDATA%/ConduitClient/config.json` defaults to `{}`. Obsolete activation, manual pack path and reload-mode keys are ignored and removed; unknown keys are preserved. Malformed configurations fail startup without overwriting the file. See [configuration and discovery details](docs/configuration.md).

Diagnostic files are appended under `%LOCALAPPDATA%/ConduitClient/logs/`: `standalone.log`, `native-stages.log`, and `client.log`. Old root-level logs are preserved. Filesystem failures are reported through the available log or Windows debugger output.

**Ctrl+K safely deactivates ConduitClient** while Minecraft owns the foreground window: F3 closes, reload requests stop, the client worker joins and native hooks are disabled. The DLL and trampolines remain resident because the existing pinned callback architecture cannot prove safe full unload. Do not force-unload it. `DISABLE` in the data root also stops it; a pre-existing file prevents hooks. Restart Minecraft to reactivate.

## Supported implementation and limits

Targeted JSON UI reload is the only native reload implementation. The obsolete broad fallback has been removed. Single-client context guards, request serialization, exact executable validation and verified thread affinity remain in place. Native work already executing cannot be safely cancelled; shutdown may wait for it. Disk syntax checks cannot guarantee transactional Minecraft rollback or semantic validity.

The compatibility framework selects verified native addresses, structures and optional features for the exact supported executable. It does not claim support for additional builds. [Adding a version profile](docs/reverse-engineering/1.26.60-compatibility-profile.md).

**Main-menu logo replacement remains blocked:** the title-specific image setter and texture ownership ABI have not been verified. The original Minecraft logo remains intact on all screens. [Investigation](docs/reverse-engineering/1.26.60-title-logo.md).

## Development

- [Build and lifecycle contracts](docs/build.md), [standalone setup](docs/standalone-initialization.md).
- [v0.4.0-beta release notes and manual checks](docs/releases/v0.4.0-beta.md), [changelog](CHANGELOG.md).
- [Targeted reload evidence](docs/reverse-engineering/1.26.60-json-ui-only-prototype.md), [project guidance](AGENTS.md), [third-party notices](THIRD_PARTY_NOTICES.txt).

Game binaries, dumps, logs and machine-specific configuration stay outside source control.
