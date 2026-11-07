# Experimental F3 diagnostics overlay

Built on the clean `2bddb49` targeted-reload milestone (v0.2.0-beta), not on the older beta 1. The standalone image hash/version guard remains mandatory: Bedrock Windows x64 1.26.60.5, package 1.26.6005.0. The existing reload implementation and configuration are retained. No release is published by this change.

Inject the experimental `standalone/ConduitClient.dll` into a **fresh game process**, using the existing standalone configuration. F3 toggles the panel once per sampled key press while Minecraft has focus. Initial visibility is off; holding F3 does not repeat. Ctrl+R remains the existing independent reload hotkey. No pack files are changed. Creating the existing `DISABLE` file hides/destroys the overlay and follows the existing backend shutdown procedure. The DLL stays pinned; replacing it in a running process is unsupported.

## Fields and meaning

| Field | Source and limitation |
| --- | --- |
| Screen | Native result from the primary player/client stack name request; four samples/second, expires after one second. Legacy identifiers and Ore UI URIs are displayed as returned. See [native screen evidence](reverse-engineering/1.26.60-native-screen.md). |
| Elements | Unique instantiated controls in the selected legacy screen, including its root and invisible descendants. Sampled once/second, and on native name changes. HBUI/unverified roots are `Unavailable`; bounded partial traversals show `>= N (incomplete)`. See [native hierarchy evidence](reverse-engineering/1.26.60-ui-controls.md). |
| Factory instances | Verified factory components attached to controls in the active legacy screen; not globally registered definitions. |
| Generated controls | Unique live active-screen descendants of retained factory-generated roots, including roots. Root count is shown separately; expired/out-of-screen roots are excluded. See [factory evidence](reverse-engineering/1.26.60-ui-factories.md). |
| FPS (DXGI presents) | Rolling reciprocal mean of the most recent 120 successful foreground presentation intervals, at least eight intervals required. This measures DXGI presentation cadence, not a native engine FPS counter or GPU execution time. |
| Successful reloads | This DLL session's backend calls returning `Succeeded`; failures, unsupported requests and validation failures do not increment it. Applies to the configured reload mode; it is not a count of all natural game reloads. |
| Reload latency | Last successful backend request through native completion, including game-thread dispatch wait, excluding preceding file validation. It is not isolated UI render/layout time or just the synchronous native call duration. Unavailable before a successful reload. |

The FPS hook observes `Present`/`Present1` **after** the original returns, rejects test presents and every result other than `S_OK`, filters to the selected game HWND and ignores nested forwarding between Present entry points. It never counts worker/game-update callbacks. Samples reset on failed/occluded presents, focus loss observed at presentation, chain changes, or intervals of at least 500 ms; values expire after 500 ms without another sample. A fixed array stores timings without per-frame allocation. Multiple alternating chains for the same window produce repeated warm-up rather than an aggregate FPS. Rapid secondary-chain presentation remains a limitation; in-game cadence must be checked. No existing native FPS accessor was established in the bounded prior-evidence review.

## Rendering and lifecycle

The panel is a semi-transparent Win32 layered owned window using GDI text, not JSON UI, ImGui, or a D3D render pass. It has `WS_EX_TRANSPARENT`, `WS_EX_NOACTIVATE`, `WS_EX_TOOLWINDOW`, transparent hit testing, and no input controls. It is hidden on focus loss/minimize and follows the game client rectangle at 4 Hz. F3 is polled at approximately 15 ms; extremely short presses between polls can be missed. Existing F3 handling by Minecraft/other software is not suppressed. No Frida is required.

This approach does not touch game graphics state, queues, buffers or device recreation. It requires desktop composition/windowed or borderless presentation; **exclusive-fullscreen and Proton compositor visibility are unverified**. If it is not visible in the actual environment, the compositor path must be revisited at the manual checkpoint rather than enabling the older unconfirmed D3D12 HUD automatically.

Only FPS uses DXGI hooks. A temporary D3D11 chain supplies public COM method entry points; this does not assume Minecraft uses D3D11. If those implementations are not the game's implementations, FPS stays unavailable. The temporary probe does not establish the game's graphics API. Overlay hooks enable/disable only their own addresses, share the already initialized MinHook instance, and never call global uninitialization. Callback code and trampolines remain resident after logical shutdown so in-flight originals remain callable. The old D3D12 renderer is not linked into the standalone target.

The native screen callback copies its caller-owned result on the verified game thread; the overlay never accesses native UI objects. The presentation lock protects only fixed timing data; it is never held during GDI rendering, native reload calls, or waiting for the game thread. Overlay initialization failures leave the reload worker running.

## Build and validation

```sh
cmake -S . -B .local/build/f3-debug \
  -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/mingw-x86_64.cmake \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DCONDUIT_STANDALONE_LEGACY_RELOAD=ON \
  -DCMAKE_CROSSCOMPILING_EMULATOR=/usr/bin/wine
cmake --build .local/build/f3-debug -j 4
ctest --test-dir .local/build/native --output-on-failure
env WINEPREFIX="$PWD/.local/wine-conduit" WINEDEBUG=-all \
  WINEDLLOVERRIDES="mscoree,mshtml=" \
  ctest --test-dir .local/build/f3-debug --output-on-failure
```

Tests cover deterministic rate averaging/wraparound/staleness, F3 edges, actual Win32 panel visibility and held-key behavior, preserved foreground focus, successful DXGI presentation sampling in a D3D11 test host, and backend success/failure/unsupported accounting. Existing DLL lifecycle, Ctrl+R, unsupported-image and standalone configuration tests remain registered. These tests do not establish visibility, FPS accuracy, native reload success or stability inside Minecraft.

Manual checkpoint: restart Minecraft, inject this build, press F3 twice to open/close, compare FPS behavior across menus/gameplay and focus changes, confirm click-through and Ctrl+R edited-UI reloads, then report visibility, latency, performance and stability. Verify screen changes between gameplay, Pause, Inventory, chest/BeHub and menus. Element counting remains unavailable pending a separate hierarchy investigation. Do not publish or describe this as game-verified before that checkpoint.

Implementation checkpoint (2026-11-06): MinGW RelWithDebInfo DLL built; 3/3 portable tests and 14/14 Wine regression tests passed. After strengthening the FPS assertion, one early smoke run failed during asynchronous setup; the test now waits for overlay initialization before sending F3. The final overlay/FPS smoke passed three consecutive runs. The four standalone image/configuration guard tests also passed during the follow-up run. Minecraft manual verification remains pending.

Milestone A update: the native screen accessor was observed across HUD, Pause, Inventory, the BeHub chest replacement and Settings. The UI screen result can be `small_chest_screen` for a custom BeHub replacement, or `ui://./hbui/index.html` for Ore UI. These are actual native values, not JSON namespace claims. The current DLL adds this field binding without changing the overlay renderer. User confirmation of Milestone A is still required before element counting.

The standalone Milestone A build was visually checked in Minecraft without Frida: HUD, Pause, Inventory and the BeHub chest replacement displayed their native identifiers. Title/Play menus remain `Unavailable` because the selected player-stack request is inactive there; a separate menu-stack selection still needs verification. Split-screen is also unavailable. The UI/renderer and FPS path were preserved. Automated results: 4/4 native, 15/15 Wine, plus a final 4/4 image/configuration-guard rerun. This does not replace the requested user confirmation before Milestone B.

## Milestone B checkpoint (2026-11-06)

Milestone A was manually accepted by the user before this work. Its name selection, overlay renderer and reload hooks remain intact. The existing verified UI-thread name callback now traverses the selected legacy scene's owned control hierarchy and publishes a copied count; DXGI Present never reads game UI objects. Collection uses fixed scratch space, unique-object detection and a soft 5 ms deadline, with limits of 8,192 controls and 32,768 edges. Invisible instantiated descendants are included. Missing/unverified roots and HBUI remain unavailable.

Standalone checks without Frida displayed HUD 1,033, Pause 799, Inventory 4,643, and the BeHub chest replacement 770. These are observations from this development pack, not constants. First sampled collection costs were 0.703 ms (HUD), 0.598 ms (Pause), and 3.353 ms (Inventory); these individual samples do not establish worst-case overhead or FPS impact. Ctrl+R completed twice, including with the custom screen open, and count sampling continued. No resource files were edited, so this check establishes dispatch/completion rather than correctness of an edited resource.

Build/tests: 5/5 portable and 16/16 Windows/Wine tests passed, including synthetic graph counting and snapshot invalidation. Experimental DLL: `.local/build/f3-debug/standalone/ConduitClient.dll`. User verification of counts, dynamic changes, performance and stability is still required. Title/Play and split-screen remain unsupported by the existing screen selection. No release has been published.

## Factory discovery checkpoint (2026-11-07)

The existing panel now adds Factory instances and Generated controls (with root count), using the existing native screen snapshot. These metrics describe the selected screen, including invisible controls, and refresh at the existing one-second cadence. Generated subtrees are deduplicated across nested/shared roots. They are not global registered definitions, cumulative instantiations, or per-control allocation attribution. A separate bounded factory sampling failure leaves the element count intact and reports factory fields unavailable. Bindings are not inspected yet; the next milestone waits for user factory verification.

Portable tests: 6/6; Windows/Wine: 17/17, including synthetic factory provenance graphs, snapshot expiry/invalidation, real overlay text, and existing reload/lifecycle tests. Native generation and per-factory associations were separately observed using temporary Frida; normal DLL operation needs none. See the versioned evidence note for actual observations and limitations.

## Per-instance factory details (2026-11-07)

The summary panel is unchanged. While F3 is visible and Minecraft has focus, changed copied factory snapshots are written to the existing **standalone.log**, coalesced to one block per two seconds. Search for `factory_breakdown` and `factory_instance`. Rows include native factory name, native owner-control name, a screen-scoped structural position ID, `instances=1`, roots, controls including roots, and descendants excluding roots. A known empty name is logged as `name="" name_available=1`; unavailable labels and truncated labels/row sets are explicit. Up to 32 instance rows are retained; aggregate totals continue to cover all inspected components. IDs describe a structural position, so tree reordering can change them. A successful Ctrl+R produces another log snapshot even if counts stay equal. No file I/O or formatting occurs on the UI/Present callbacks.

For the investigated BeHub screen, two inherited `common.screen_panel` popup components each have zero roots; `container_item_factory` owns all seven `item_switcher` roots and their 280-control union (273 descendants excluding roots). Those 280 controls are already included in Elements: 770. The two popup components have empty native factory names, with owners both named `popup_dialog_factory`, and are distinguished by their structural positions. See [the attribution evidence](reverse-engineering/1.26.60-ui-factories.md#per-factory-attribution-follow-up--2026-11-07).

## Factory rows in F3 (2026-11-07)

The existing per-instance snapshot now also renders under Factory instances in the same F3 panel. Each row shows the native name, roots and controls including roots. A missing/empty name falls back to the copied owner name and structural position ID; no popup-specific names or counts are hardcoded. Zero-root rows remain visible. Existing aggregate counts and log output retain their semantics.

The panel retains its font, colors, opacity, click-through behavior and F3 toggle. Width is bounded to 560 pixels and the visible intersection of Minecraft's client area with its monitor work area; height follows the measured text lines. Long labels are ellipsized to leave room for counts. If available height or the existing 32-row snapshot limit prevents a full list, an explicit remaining-instance count is shown. Narrow/tiny windows can clip remaining summary lines rather than extend the panel off-screen. All layout/text formatting stays on the overlay worker at 4 Hz, without native hierarchy access or extra Present work.

The overlay smoke test covers named and unnamed zero-root rows, updates, automatic height, long labels, many rows in a smaller game viewport, explicit omitted rows, unavailable snapshots, and restoring the original viewport. The native factory sampler is unchanged by this presentation update.

Presentation checkpoint: 6/6 portable tests and the 16 other Windows/Wine regressions passed. The expanded overlay smoke passed on isolated Xvfb (1280x1024), exercising an actual constrained viewport; the desktop tiling WM had ignored earlier resize requests. The test now bases the omission assertion on actual client dimensions. A fresh standalone process's public F3 window text was compared to the same process's `standalone.log`: `container_item_factory` 7 roots / 280 controls, plus the two unnamed position IDs at 0 / 0. With long fallback owner labels, the panel may use just the structural ID to preserve numeric columns. The user's visual layout verification remains pending.

Experimental DLL: `/home/zelisce/Projects/Conduit-Client/.local/build/f3-debug/standalone/ConduitClient.dll`; SHA-256 `32fc6936738ee4e23c0d792faf3874889de39919cd5698de605fc346103888e9`. No native sampler changes, additional hooks, commits, tags, pushes or releases were part of this presentation update.
