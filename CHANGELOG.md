# Changelog

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
