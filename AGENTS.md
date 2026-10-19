# Project guidance

This project is a C++20 development client for Minecraft Bedrock GDK. Its primary feature is hot reloading JSON UI during development. REA is used to investigate game behavior and interfaces. The repository is currently a scaffold; paths below describe the intended layout and should be adjusted when the implementation establishes a different one.

## Coding conventions

- Use C++20. Prefer standard library facilities, RAII, and explicit ownership over raw owning pointers or manual resource cleanup.
- Keep platform and game-specific code behind small interfaces. Separate JSON parsing, file watching, reload coordination, and game integration so each can be reasoned about independently.
- Use descriptive `PascalCase` type names and `camelCase` function and variable names. Match an established local convention if one emerges. Name constants clearly and avoid unexplained numeric offsets or addresses.
- Keep headers self-contained, minimize includes, and use `const`/`constexpr`, `std::string_view`, and references where appropriate. Check errors at file, parsing, and game API boundaries; log enough context to diagnose a failed reload.
- Treat file watcher callbacks and game/UI callbacks as potentially different threads. Make synchronization and lifetime rules explicit; dispatch UI mutations to the appropriate game thread.
- Keep a failed JSON parse or reload from replacing the last known good UI state. Avoid partial updates, and make repeated reloads safe.
- Avoid unrelated formatting changes. Add focused tests for pure parsing/reload logic when a test harness exists, and verify behavior in the target GDK build for integration changes.

## Project structure

When adding files, prefer this layout unless existing build tooling requires another:

- `src/` — client implementation, with separate modules for JSON UI, reload orchestration, and GDK integration.
- `include/` — public or shared headers; keep private declarations beside their implementation.
- `tests/` — focused tests and fixtures for parsing and reload behavior.
- `assets/` — development JSON UI files and representative fixtures.
- `docs/reverse-engineering/` — versioned REA findings and supporting notes.
- `tools/` — development scripts and utilities.

Keep generated binaries, dumps, and machine-specific configuration out of source control. Do not commit proprietary game binaries, extracted assets, credentials, or raw memory dumps.

## Reverse-engineering notes

Record findings under `docs/reverse-engineering/` as Markdown, grouped by subsystem or feature. For each finding, include:

1. The tested Bedrock/GDK version, platform, architecture, and date. State how the version was identified.
2. The question investigated, REA method used, and enough reproducible steps or search terms to repeat the investigation.
3. Observations separate from interpretations. Cite relevant symbols, strings, call sites, offsets, signatures, or pseudocode references with module and version context. Do not present an address as stable across builds.
4. Evidence supporting the conclusion, confidence level, and any alternative explanation or unresolved uncertainty.
5. Impact on client code: assumptions, affected files, expected behavior, and a practical validation procedure.

Update or supersede notes when a new game version changes a finding. Link code that depends on a reverse-engineered detail to the corresponding note, and mark unverified behavior clearly. Prefer concise excerpts and references over copied disassembly or large proprietary dumps.
