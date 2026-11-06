# D3D12 HUD status

Historical foundation renderer only. The current experimental standalone F3 overlay uses a separate layered-window path; see [debug overlay](debug-overlay.md). The older Ctrl+R/unsupported-backend statements below do not describe the v0.2.0 standalone target.

Conduit contains a small D3D12 renderer and DXGI presentation hooks. It draws a dark rectangle with `Conduit Client Loaded` in the top-right of a swap chain, using its own command list and a built-in bitmap font. It does not depend on Minecraft JSON UI or a separate desktop window.

The renderer and hook passed independent Wine tests that read back dark panel and bright text pixels from a D3D12 swap chain. **Visibility in Minecraft has not yet been confirmed.** The first BedrockOnLinux test showed no panel. Its log confirmed DLL startup and hook installation, but no successful frame submission. The next build adds counters for intercepted Presents, command queue submissions, device lookup failures, and unmatched queues. Those counters appear in `conduit.log` every 15 seconds and should identify the integration gap.

The hooks target `IDXGISwapChain::Present`, `IDXGISwapChain1::Present1`, `IDXGISwapChain::ResizeBuffers`, and `ID3D12CommandQueue::ExecuteCommandLists`. A temporary D3D12 device supplies function addresses after loader lock. MinHook 1.3.4 is pinned by commit in the CMake build. The renderer matches a direct queue to the presented swap chain's device, waits for its own submitted work before reusing per-buffer resources, and releases render resources on resize or shutdown. It does not change game logic or input.

Ctrl+R displays a Windows confirmation dialog and queues a notification; JSON UI reloading remains unsupported. Ctrl+K stops the client and removes hooks. An injector-held DLL reference can remain until the game exits.

## Live validation

Inject `.local/build/windows/bin/ConduitClient.dll`, restart Minecraft, and leave the game window open for at least 20 seconds. Look for the top-right panel and inspect `%LOCALAPPDATA%\ConduitClient\conduit.log` in the BedrockOnLinux prefix. A successful frame logs `D3D12 HUD submitted to game swap chain`. If absent, compare the `HUD diagnostic` counters. Zero Presents means the game calls a different presentation entry point than the test host; zero queue submissions means the queue capture hook did not observe Minecraft's queue; queue matches missed indicates a device or queue mismatch. Report the log and whether any panel appeared before treating HUD support as verified.
