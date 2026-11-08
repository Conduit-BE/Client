# Changelog

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
