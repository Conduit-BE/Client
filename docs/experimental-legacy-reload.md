# Experimental legacy JSON UI reload prototype

Target: exact Windows x64 Bedrock 1.26.60.5 image (package1.26.6005.0). Linux/BedrockOnLinux/Proton is supported through the documented Windows Frida server transport. This is a separate DLL; the stable ConduitClient backend remains unsupported by default. **V5 is human-confirmed to refresh edited legacy JSON UI with Ctrl+R, but it also reloads chunks. JSON UI-only reload is not achieved. A completion counter alone does not prove a refreshed screen.** Roughly one-second freezes can occur and player skins may become invisible.

## Build and isolation

```sh
cmake -S . -B .local/build/windows -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/mingw-x86_64.cmake \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DCMAKE_CROSSCOMPILING_EMULATOR=/usr/bin/wine \
  -DCONDUIT_EXPERIMENTAL_LEGACY_RELOAD=ON \
  -DCONDUIT_EXPERIMENTAL_DLL_NAME=ConduitReloadPrototype
cmake --build .local/build/windows
```

The option defaults OFF. A process that already loaded a prototype needs a fresh DLL name for development iterations; do not overwrite a mapped DLL. All outputs stay in `.local`. DLLs pin themselves for safe callback lifetime; disabling removes their own hooks but does not unload their code.

## Native operation

Ctrl+R passes through the existing key-edge detector, file monitor and JSONC validation. The backend queues a request; a recurring native frame boundary (RVA0x879030) reacquires the current owner and runs the queued operation on the observed reload thread. The owner/table, resource service/table and current resource context/table are checked for this exact build. No heap address or thread ID is compiled into the backend.

The default experimental operation is legacy loader RVA0x83a190. Runtime testing established that this rereads modified definitions and completes, but does **not** make the tested edit visible. The intermediate resource task submitter RVA0x8c4f40 also failed visual testing. The current broader fallback invokes outer resource operation RVA0x873a20 with the current owner, its current +0x228 context and the observed third-argument byte. The outer function performs invalidation/cache resets before invoking the task submitter. See [the evidence](reverse-engineering/1.26.60-legacy-reload-path.md). This fallback is broader than JSON UI alone; do not describe it as a UI-only reload or as a chat command.

Only one request is pending/in flight. The worker waits for the matching native legacy completion callback; an eight-second timeout latches a fault instead of retrying. Automatic file-triggered native reload remains disabled during controlled validation; changes are scanned and Ctrl+R validates the whole configured tree before dispatch. JSON parse failures skip native dispatch and retain the previous validation metadata. That is not a guarantee of transactional behavior inside Minecraft.

Watch the pack root so `@namespace/interfaces/...` definitions are included. The experimental client explicitly skips `.git` and `.vscode` recursion. Default stable-client scanning is unchanged. Prefer the game's short `C:\...` pack path rather than a long `Z:\home\...` alias: repository metadata under the latter exceeded Windows path lengths in this installation. Do not modify the user's pack to work around path errors.

## Reproduction and control

Download the alpha DLL, `SHA256SUMS`, and third-party notices. Clone tag `v0.1.0-alpha.1` for the tooling. Install the matching Node binding locally (for example `npm install --prefix .local/frida-node frida@17.23.2`), and set `CONDUIT_FRIDA_MODULE` to the absolute path of `.local/frida-node/node_modules/frida/build/src/frida.js`. Obtain the official Windows x64 server and build the disposable fixture using the [Frida guide](reverse-engineering/1.26.60-frida-proton.md). This alpha is developer tooling, not a drop-in autostart injector.

First use `tools/reverse-engineering/frida-proton/monitor-legacy.py` with `CONDUIT_AGENT_PATH` pointing to `prototype-validation-agent.js`; its server/probe gate and required parameters match the Frida guide. For example, after setting the current Linux PID, licensed original image path, and matching server/probe paths:

```sh
export CONDUIT_AGENT_PATH="$PWD/tools/reverse-engineering/frida-proton/prototype-validation-agent.js"
python tools/reverse-engineering/frida-proton/monitor-legacy.py \
  --game-linux-pid "$GAME_LINUX_PID" --game-exe "$GAME_EXE" \
  --server-exe "$FRIDA_SERVER_EXE" --probe-exe "$PROBE_EXE" \
  --output-dir "$PWD/.local/alpha-server-run"
```

Leave this server-owning monitor running. Its `game-prefix-context.json` contains the loopback endpoint; `READY` contains the current Windows process ID. Observe a natural initialization or perform one control `/reload all`, then inspect `events.jsonl` for the `resource_parent` entry's `tid`. Use that fresh thread ID in the configuration below; the confirmed third-argument value is `1` for the tested build. The controller validates that the observed thread belongs to the current process and reacquires its owning object from a current frame call.

The launcher passes `--directory` with an isolated local extraction directory to prevent stale Frida agent collisions after interrupted runs. Startup diagnostics use no global NT file hooks; snapshots are nonblocking and startup runs asynchronously on an owned DLL thread.

Obtain a fresh natural reload observation for the current process (or observe natural initialization when loading early). Record Windows PID and reload thread; never reuse another run's IDs. The actual call arguments and their static register provenance must match the versioned note before selecting the fallback.

Create an ignored run configuration:

```json
{
  "dll": "Z:\\absolute\\path\\to\\ConduitClient-v0.1.0-alpha.1-windows-x64.dll",
  "watchDirectory": "C:\\actual\\profile\\development_resource_packs\\pack",
  "logFile": "Z:\\absolute\\ignored\\run\\prototype-native.log",
  "windowsPid": 0,
  "observedThread": 0,
  "observedFlag": 1
}
```

Replace both zero IDs and paths with current observations. `retireModule` is an optional **own prototype DLL** name for a development iteration; the helper refuses to replace a busy prototype. With the matching Frida Node binding in `CONDUIT_FRIDA_MODULE`, run:

```sh
node tools/reverse-engineering/frida-proton/run-prototype.mjs \
  127.0.0.1:CURRENT_PORT .local/RUN/config.json .local/RUN
```

The helper loads only our DLL, calls its explicit C exports, configures the observed fallback, and preserves fsynced state records. The native worker handles Ctrl+R independently of Frida. The controller performs no internal game calls. Confirm enabled/armed/resourceFallback state and native logs before testing.

To disable safely while this helper is running:

```sh
touch .local/RUN/DISABLE
```

This stops the client, cancels queued work and disables its hooks through the explicit `ConduitPrototypeDisable` export. Keep the module resident. `CONTROL_STOP` ends just the helper and leaves an enabled native prototype running; do not confuse stopping diagnostics with disabling the backend. Keep the server-owning monitor alive while connected helpers need it.

## Validation

Use a backed-up test world and an active pack. Make a reversible, obvious edit, save, press Ctrl+R, close/reopen the affected page, and obtain human confirmation. Repeat with another edit. Never infer visual success from lag, file reads, or completion counts. Do not continue if the game becomes unstable.

Native stage logs distinguish queueing, dispatch, loader entry and callback return. Client logs identify key detection, JSON validation and backend results. Scoped file-read evidence was collected separately; Conduit's own watcher reads must be excluded when attributing reads to Minecraft. The controller's snapshots intentionally avoid blocking during startup to prevent reentrant diagnostic lock cycles.
