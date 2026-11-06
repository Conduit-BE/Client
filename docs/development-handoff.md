# Development handoff — v0.2.0-beta

The current working milestone is the standalone targeted legacy JSON UI reload for exact Bedrock Windows 1.26.60.5, package 1.26.6005.0, tested through BedrockOnLinux/Proton. The user confirmed edited UI appears without visible chunk/texture refresh and confirmed follow-up testing. A brief freeze remains. Exact repeat counts, individual skin observations and frame-time measurements were not separately supplied.

Normal installation: [standalone guide](standalone-initialization.md). Set `reloadMode` explicitly to `LegacyJsonUiOnly`; missing settings retain `FullResourceReload` for compatibility. Continuous watching is disabled. No Frida controller is required.

The targeted path invokes native callback RVA `0xf203e0` on a validated current client context; that callback handles lifetime references and the full UI action at `0x6583170` (load → category task barrier → collection invalidation). [Evidence, assumptions and tests](reverse-engineering/1.26.60-json-ui-only-prototype.md). Do not replace it with loader-only `0x83a190` or intermediate `0x8c4f40`, which failed earlier visual tests.

The V5 broader fallback at `0x873a20` remains intact. The original alpha and first standalone beta are preserved. The first beta release is now named **v0.1.0-beta**; its original `v0.1.0-beta.1` Git tag remains a compatibility alias to the same commit. No history was rewritten.

Ignored local preservation:

- `.local/re-investigation/legacy-ui-only-20261106/`: raw analysis, native events, configuration backup, checkpoint and confirmed DLL/source snapshot, with hashes in `CONFIRMED_WORKING_RESULT.json`.
- `.local/re-investigation/legacy-prototype-v3-isolated-20261106/v5/`: original confirmed V5 snapshot and traces.
- `.local/build/`, `.local/releases/`: generated builds and verified release assets.
- `.local/frida-proton/`, `.local/tools/`, `.local/deps/`, `.local/wine-conduit/`: development tooling, binaries and environments.
- Temporary Ghidra projects and licensed game executables remain machine-local and do not transfer through Git.

Versioned notes and own-source tools preserve reproducible findings. Recreate analysis from the exact licensed image; never reuse heap addresses or thread IDs from saved runs. Keep raw game artifacts, logs, process dumps and user configuration out of Git. Frida documentation is a developer reference, not normal installation.
