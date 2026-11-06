# BedrockOnLinux foundation/HUD verification

For the working alpha reload DLL, use [the experimental guide](experimental-legacy-reload.md). This page describes the separate foundational DLL and its historical HUD tests.

Build the Windows x64 DLL with [MinGW instructions](build.md). Configure BedrockOnLinux's built-in injector to load:

`.local/build/windows/bin/ConduitClient.dll`

Restart Minecraft after every DLL rebuild. Conduit starts automatically only in a process named `Minecraft.Windows.exe`; it does not need an exported startup call. It does not modify the installed game package.

Ctrl+R displays a Windows confirmation dialog reading **Ctrl+R detected! JSON UI reload requested.** JSON UI reload remains unimplemented. Ctrl+K stops the client and removes its graphics hooks. The injector may retain its DLL reference until game exit; Ctrl+K performs a safe logical shutdown.

F3 toggles disk JSON UI diagnostics when Minecraft owns the foreground window. Copy development JSON UI into `%LOCALAPPDATA%\ConduitClient\ui`. Counters and metadata describe this directory; the DLL does not mount it as a resource pack. F3 does not consume the game's input. Short presses between the 30 ms polls can be missed. `F3 diagnostics shown` / `hidden` log lines confirm key detection; actual overlay visibility still needs an in-game check.

The game-process log is `%LOCALAPPDATA%\ConduitClient\conduit.log`. For the current BedrockOnLinux installation it is:

`$BEDROCK_DATA/compatdata/pfx/drive_c/users/<wine-user>/AppData/Local/ConduitClient/conduit.log`

Wait at least 20 seconds after the game window appears, then inspect the log. `D3D12 HUD submitted to game swap chain` means Conduit submitted its draw call, though a visual check is still required. `HUD diagnostic` reports intercepted Presents and queue submissions. If Presents are zero, the hook missed Minecraft's presentation entry point. If queue submissions are zero, the queue capture hook did not observe the game's queue. If `queue matches missed` rises, no observed direct queue matched the presented swap chain's device. Send these lines with any screenshot when reporting a visibility problem.

If the log does not show DLL attachment, verify the injector path, the actual game process basename, and that Minecraft was restarted. Startup failures before the log opens are sent through `OutputDebugString`. If Ctrl+R does not register, the 15-second diagnostic line shows the foreground and current process IDs; try windowed mode and check Wine's Ctrl and R key mapping. The poll interval is 30 ms, so very short presses can be missed. No input is consumed or hooked.

The independent D3D12 renderer and Present-hook pixel tests run under Wine as part of `ctest`; they do not prove Minecraft visibility. See [HUD status](hud-status.md) for the current live validation result.
