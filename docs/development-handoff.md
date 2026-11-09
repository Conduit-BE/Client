# Development handoff — v0.4.1-beta

The standalone target uses only the verified targeted legacy JSON UI callback for exact Bedrock Windows 1.26.60.5, package 1.26.6005.0. Prior user tests established visible UI changes without visible chunk/texture refresh. This preparation build has not been verified in-game. See [release notes](releases/v0.4.1-beta.md) and [installation](standalone-initialization.md).

Version-specific native addresses/layouts/signatures reside in `src/minecraft/versions/bedrock_1_26_60_5.hpp`; runtime selection requires the exact original image hash and AMD64. No additional build is supported. The broad V5 implementation and selection API are removed. Automatic pack discovery replaces the manual path setting; old activation/path/mode keys migrate away. Automatic logs append in `logs/` under the data root.

Ctrl+K safely deactivates; permanent pinning and retained callback trampolines prevent proven full unload. The title-logo renderer now imports the embedded PNG through the verified native loader and substitutes guarded active-title draws. See [logo fix evidence](title-logo-fix.md); broader native transition regression checks remain pending.

The targeted path invokes callback RVA `0xf203e0` on a validated current client context. That callback manages lifetime references and the complete UI action (load → category task barrier → collection invalidation). [Evidence](reverse-engineering/1.26.60-json-ui-only-prototype.md). Loader-only and intermediate task submission experiments failed earlier visual tests; do not substitute them.

Ignored local preservation remains under `.local/re-investigation/`, `.local/build/`, `.local/releases/`, `.local/frida-proton/`, `.local/tools/`, `.local/deps/`, and `.local/wine-conduit/`. These contain historical artifacts and machine-specific evidence, not additional current runtime features. Licensed game executables, dumps, raw logs and analysis databases must not enter Git. Never reuse recorded heap addresses or thread IDs.

The user authorized publication of v0.4.1-beta after build/test. Native in-game regression verification remains pending; publication does not establish native validation.
