# Development handoff — first alpha

The permanent working checkpoint is tag **v0.1.0-alpha.1**. The V5 experimental DLL has human-confirmed Ctrl+R native resource reload on Bedrock Windows 1.26.60.5 through BedrockOnLinux/Proton. It invokes RVA **0x873a20** using the current owner/context on the observed game thread. It also reloads unrelated resources/chunks, can freeze briefly and may hide player skins. JSON UI-only reload is not implemented.

The stable foundation target retains its unsupported backend by default. Do not replace the working V5 fallback while narrowing the JSON UI operation. Begin with [the current runtime result](reverse-engineering/1.26.60-legacy-reload-path.md) and [the experimental guide](experimental-legacy-reload.md). Earlier loader-only and intermediate resource-stage calls failed visual tests despite rereading edited JSON and completing callbacks.

Use [build.md](build.md) for tests and dependencies. Generated files remain ignored:

- `.local/build/`, `.local/release-verification/`, `.local/releases/`: local builds and verification outputs.
- `.local/re-investigation/legacy-prototype-v3-isolated-20261106/v5/`: exact working source/DLL snapshot, hashes, native traces and checkpoint.
- `.local/frida-proton/`: matching external server, disposable probe, environments and logs.
- `.local/tools/`, `.local/deps/`, `.local/wine-conduit/`: analysis tools, optional dependency checkouts and test prefix.
- Other `.local/re-investigation/` and `.local/hopper-diagnostics/` files: private analysis captures and derived images.
- Temporary Ghidra projects and the licensed game installation are machine-local and do not transfer through Git.

Preserve local checkpoints separately; they are not release assets. The versioned notes and committed tools retain reproducible findings, not raw game code/dumps. Recreate analysis only from your matching licensed executable. Acquire current object/thread observations for every new game process; do not reuse old heap pointers or thread IDs.
