# Legacy JSON UI static-analysis utilities

These are developer analysis tools, separate from ConduitClient. The static tools in the table below do not execute or inject into Minecraft; the separate Frida runtime workflow is described at the end. Keep all inputs and generated output in ignored `.local/` or a private external directory. Consult [the findings](../../docs/reverse-engineering/1.26.60-legacy-reload-path.md) for executable identity, observed coordinates, evidence limitations and the current stopping point.

| Tool | Purpose and limits |
|---|---|
| `prepare_pe_ui_analysis.py` | Creates a separate x64 PE analysis copy with only the exception-directory descriptor cleared. Writes a hash manifest and selected-function TSV from original unwind entries. Refuses existing outputs and refuses to replace the input. Never execute its derived image. |
| `ConduitUiProbe.java` | Ghidra post-script: bounded disassembly and decompilation of TSV-selected original unwind intervals. Output includes assembly and references; synthetic function names and recovered types are not verified native declarations. |
| `LegacyIndirectProbe.java` | Read-only query of the existing 1.26.60.5 project for the first reload-action call's instructions and relevant pointer entries. Coordinates are specific to the documented build and static image base. |
| `find_rip.cpp` | Preserved exploratory byte scanner. Reports candidate 32-bit RIP-relative displacements for literal occurrences or explicit `rva:<hex>` targets. Assumes a well-formed PE and that the first section is code. It is not an instruction decoder; validate every candidate with decoded instructions and original unwind entries. |

## Recreate a targeted Linux Ghidra project

Use Ghidra **12.1.4** and JDK **21**, as verified in the findings. The Ghidra installation is not committed. The example assumes it has been extracted to `.local/tools/ghidra_12.1.4_PUBLIC`; see the findings for the official release and archive checksum.

From the repository root, supply the matching licensed original executable and choose only functions relevant to the next question:

```sh
mkdir -p .local/analysis .local/analysis/config .local/analysis/cache /tmp/conduit-ghidra
python tools/reverse-engineering/prepare_pe_ui_analysis.py /absolute/path/to/Minecraft.Windows.exe \
  .local/analysis/legacy.exe --function-rva 0x6583170 --function-rva 0xf203e0

env XDG_CONFIG_HOME="$PWD/.local/analysis/config" \
  XDG_CACHE_HOME="$PWD/.local/analysis/cache" \
  GHIDRA_HEADLESS_MAXMEM=8G \
  GHIDRA_HEADLESS_JAVA_OPTIONS="-Djava.util.prefs.userRoot=$PWD/.local/analysis/java-prefs" \
  .local/tools/ghidra_12.1.4_PUBLIC/support/analyzeHeadless \
  /tmp/conduit-ghidra LegacyJsonUi \
  -import "$PWD/.local/analysis/legacy.exe" -noanalysis \
  -scriptPath "$PWD/tools/reverse-engineering" \
  -postScript ConduitUiProbe.java \
  "$PWD/.local/analysis/legacy.functions.tsv" "$PWD/.local/analysis/selected.txt" \
  > .local/analysis/headless.log 2>&1
```

Use an absolute project path outside hidden directories: Ghidra rejected `.local` in its project path during the investigation. `-noanalysis` avoids broad analysis; selected boundaries are supplied explicitly from the original PE. The analysis copy retains all original section bytes, including `.text` and `.pdata`, while avoiding automatic markup of 991,803 exception entries.

To reuse a saved project, replace `-import ...` with `-process legacy.exe`. For `LegacyIndirectProbe.java`, use `-readOnly -noanalysis` and pass only the output filename after the script name. It expects the relevant functions already disassembled in that project. Filter saved output locally instead of dumping entire functions into chat.

The optional byte scanner builds without additional libraries:

```sh
c++ -O3 -std=c++20 tools/reverse-engineering/find_rip.cpp -o .local/analysis/find_rip
.local/analysis/find_rip /absolute/path/to/Minecraft.Windows.exe \
  'ui/_ui_defs.json' rva:f091b70 > .local/analysis/reference-candidates.txt
```

Candidate absence does not prove absence of references; SIMD interior loads, absolute pointers and other encodings can require separate checks. No output from these utilities establishes a safe calling convention, object lifetime, runtime table, UI-thread contract or transactional reload behavior.

## Frida on Proton

The separate `frida-proton/` tools validate a Windows Frida server and disposable fixture inside the running game's Wine/runtime context. They default to probe-only validation; Minecraft inspection requires an explicit flag. See [the tested configuration and exact reproduction steps](../../docs/reverse-engineering/1.26.60-frida-proton.md). These are runtime instrumentation tools, unlike the static tools above; do not run them as part of static-only work.

### Legacy UI runtime observation

`frida-proton/monitor-legacy.py` reuses the validated server/probe gate and keeps a single observation session active for manual `/reload all` commands. Matching Node binding is required via `CONDUIT_FRIDA_MODULE`. Use the same required path/PID arguments as `validate.py`, a fresh `--output-dir`, and **no** `--inspect-minecraft` flag. Entry/return hooks in `monitor-legacy-agent.js` are build-specific to the verified 1.26.60.5 image. Create `OUTPUT_DIRECTORY/STOP` to detach and clean up only owned helpers; maximum duration is two hours. Do not run concurrently with another monitor.

`frida-proton/summarize-legacy.py OUTPUT_DIRECTORY` aggregates the saved append-only event stream without target access. Its two-second grouping is a display heuristic, not a command boundary; asynchronous work can cross groups. The authoritative ordering is event sequence plus thread IDs and timestamps. See the [runtime findings](../../docs/reverse-engineering/1.26.60-legacy-reload-path.md) before interpreting names as established API semantics.

The separate opt-in native prototype and lifecycle controller `frida-proton/run-prototype.mjs` are described in [the experimental guide](../../docs/experimental-legacy-reload.md). The working V5 outer native reload is visually confirmed but also refreshes chunks; retain it as a fallback while narrowing JSON UI invalidation. Recovery tools `inspect-prototype.mjs` and `retire-own-prototype.mjs` operate on our own prototype modules; the latter requires exact locally generated COFF symbol/prefix evidence and is not a generic game-call utility.
