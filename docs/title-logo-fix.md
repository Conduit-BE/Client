# Title-logo fix — v0.4.1-beta

2026-11-09, exact Bedrock GDK x64 1.26.60.5. This update fixes
the missing renderer in the published v0.4.0-beta build. The PNG is embedded,
materialized into the application's private cache and imported with Minecraft's
verified native texture loader. No resource pack or installation edit is needed.

The fix replaces only an eligible image draw in the active main title scene.
Original bounds/center, aspect ratio, splash and menu layout are preserved.
Other draws remain unchanged. A conservative global-pack metadata check keeps
customized or unidentified profiles on the original rendering path. Imported
native references remain in the pinned module until process exit for safety.
See [scope, evidence and limitations](reverse-engineering/1.26.60-title-logo.md).

Release build output: `.local/build/v0.4.1-beta/standalone/ConduitClient.dll`.
Earlier experimental DLL loaded for live verification (before the release version bump):
`.local/build/v0.4.0-beta/standalone/ConduitLogoFix.dll`.
SHA-256: `8dff3413242a975c2e84952fd47b406cd997a723476a06bbcd355b3833b60bdd`.

11/11 portable tests pass. All 24 Windows/Wine checks passed, with the
focus-sensitive overlay test rerun on an isolated Xvfb display. The compiled DLL
was visually checked rendering the correct logo after Frida detached. World,
loading, pause, custom-title, Ctrl+R/F3 and new-module deactivation regressions
still require normal in-game verification. Automated tests do not establish them.

The fix ships in v0.4.1-beta; the original v0.4.0-beta release is preserved. Restart Minecraft before
normal injection to avoid multiple pinned client versions. During the live verification session, the old client was deactivated before the experimental fix was loaded.
