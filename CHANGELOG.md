# Changelog

## v0.6.0-beta — 2026-11-13

- Add an F5 inspector that highlights the topmost visible legacy UI control and shows its properties and parent chain.
- Add a shared `ui_scale` setting for F3 diagnostics and the F5 tooltip. Existing `diagnostics.scale` values migrate on startup.

## v0.5.0-beta

- Add Windows GDK x64 support for Bedrock 1.26.62.1 and 1.26.63.1.
- Select native profiles by executable file version and architecture. Check native instruction signatures before installing hooks.

## v0.4.4-beta — 2026-11-12

- Recognize the exact Bedrock 1.26.60.5 GDK x64 executable patched by LeviLauncher 1.1.0.
- Wait for the Minecraft window before installing the diagnostics renderer, so F3 can initialize when the DLL loads at game startup.

## v0.4.3-beta — 2026-11-09

- Remove standalone disk JSON preflight: unrelated language arrays and inactive pack files no longer block Ctrl+R. Native safety guards remain active.

## v0.4.2-beta — 2026-11-09

- Add `diagnostics.scale` (default `1.0`, native font scale), with proportional panel spacing and padding.
- Add `diagnostics.factories.enabled` and `diagnostics.factories.empty` (both default `true`) for overlay presentation.
- Validate and populate missing diagnostics settings at startup while preserving unknown fields and invalid original files.

## v0.4.1-beta — 2026-11-09

- Fix the missing main-menu logo renderer using the embedded Conduit PNG and verified native texture import.
- Guard replacement by the live frontend scene, title image source, and conservative global-pack metadata checks.
- Preserve logo aspect ratio and center without changing splash text or menu buttons.
- Add private PNG cache extraction/repair, native signature validation, and title eligibility/layout tests.
- Preserve the README redesign pulled from GitHub before release preparation.

The logo implementation was visually verified in-game; remaining native regression limits are listed in [release notes](docs/releases/v0.4.1-beta.md).

## v0.4.0-beta — 2026-11-08

- Automatically initialize targeted JSON UI reload; migrate obsolete activation, pack-path and mode settings.
- Discover disk UI packs in known profile layouts for on-demand preflight validation.
- Append automatic diagnostic logs under the data root's `logs/` directory; retain old logs.
- Remove broad resource reload fallback and its runtime selection API.
- Introduce verified build compatibility profiles, executable version/image validation and optional feature gates.
- Add Ctrl+K safe deactivation; keep DLL/trampolines resident for callback safety.
- Preserve native diagnostics and targeted reload code paths pending in-game regression checks.
- Investigate title-only logo replacement; leave original logo unchanged because the texture/control ABI is unverified.

Automated tests passed; native in-game regression verification remains pending. See [release notes](docs/releases/v0.4.0-beta.md) for known limitations.
