# Local Minecraft for Windows acquisition with Xodus

This workflow is for a developer using their own Microsoft/Xbox account and valid Minecraft for Windows entitlement. It never checks in or uploads game files. The [official Xodus repository](https://github.com/xodus-gaming/xodus) is an independent project, not a Microsoft product. Its [README](https://github.com/xodus-gaming/xodus/blob/main/README.md) documents login, MSIXVC download, and license acquisition for GDK games. The [Xbox store listing](https://www.xbox.com/en-us/games/store/Minecraft-for-Windows/9NBLGGH2JHXJ) identifies Minecraft for Windows as product `9NBLGGH2JHXJ`.

## What this version of Xodus supports

The workflow is pinned to Xodus commit `95f482fe37f83e6be31d74c642acd2f2f0503e9a`. Xodus has no upstream release artifact for this repository, so `setup` checks out that commit and builds `xodus-cli` with its lockfile. The CLI source defines `login`, `download`, and `streaming`; `streaming <product> <destination>` resolves the current Windows desktop package, checks the account license, and extracts it. It does not offer a historical version catalog or a version-selecting option. `versions` therefore reports that limitation and lists only local downloads. `download --expected-version` verifies the result after download; it cannot make Microsoft serve an older release.

The upstream README marks on-demand `.exe` decryption as unfinished. A package download may therefore lack a PE executable that REA or Hopper can analyze. The `identify` command checks PE headers and x64 architecture before printing a path and SHA-256. A successful package download alone is **not** proof of an analysis-ready executable. No tool here removes protection from an EXE or attempts to bypass an entitlement check.

## Prerequisites on CachyOS / Arch

Use a graphical Linux session with a working Secret Service keyring, such as GNOME Keyring or KWallet with Secret Service support. Xodus's [login implementation](https://github.com/xodus-gaming/xodus/blob/main/docs/xodus/login.md) uses an interactive Microsoft login webview, **not device-code login**. On Linux its default credential backend is D-Bus Secret Service; do not build with Xodus's optional `key-chain-file` feature. The credentials stay in the user's keyring, outside this repository. The project scripts never accept or print passwords or tokens and set `XODUS_LOG=off` when invoking the CLI. Do not capture login output in shared logs.

Install Rust 1.98 or later, Cargo, Git, `pkg-config`, Protocol Buffers compiler, CMake, and WebKitGTK 4.1 development files using your normal package manager. [Xodus's build instructions](https://github.com/xodus-gaming/xodus/blob/main/README.md) specify Rust 1.98 and `protoc`; [wry's Linux instructions](https://docs.rs/wry/latest/wry/#linux-dependencies) specify `webkit2gtk-4.1` on Arch. On Arch, the package names are generally `rustup`, `git`, `pkgconf`, `protobuf`, `cmake`, and `webkit2gtk-4.1`; select Rust stable through rustup and verify `rustc --version` before building. `setup` reports missing prerequisites without installing system packages. A display and unlocked keyring are needed for `login`.

## Commands

Run these from the repository root in Fish, Bash, or another shell. Python 3 is used only for the local wrapper and PE checks.

```text
python3 tools/minecraft_binary.py check-git
python3 tools/minecraft_binary.py setup
python3 tools/minecraft_binary.py login
python3 tools/minecraft_binary.py versions
python3 tools/minecraft_binary.py download
python3 tools/minecraft_binary.py download --expected-version 1.2.3.4
python3 tools/minecraft_binary.py finalize <staging-uuid>
python3 tools/minecraft_binary.py local
python3 tools/minecraft_binary.py identify 1.2.3.4
```

`setup` clones the official repository into `.local/xodus-source/`, checks its origin and clean state, pins the commit, then runs `cargo build --release --locked -p xodus-cli`. It never replaces a locally modified checkout. `login` opens Xodus's Microsoft webview; sign in there yourself. An account without the necessary entitlement may be refused by Xodus/Microsoft. Never provide account secrets to this repository or an assistant.

`download` uses Xodus `streaming` and places the returned current package in a unique `.local/minecraft-binaries/_staging/` directory. It reads `MicrosoftGame.Config` or `appxmanifest.xml` (case-insensitively) to identify the package version, then moves a successful result to `.local/minecraft-binaries/<version>/x86_64/`. If an expected version differs, metadata is missing, or the destination already exists, the staging directory stays local for manual inspection. Existing version directories are never intentionally overwritten. If a run fails, inspect the staging directory before retrying; the wrapper does not delete it. Xodus may leave an MSIXVC cache there as well. After a completed download whose wrapper failed to identify metadata, `finalize <staging-uuid>` checks Xodus's completion marker and moves that staged package without downloading again.

`identify` lists each structurally valid Windows x64 PE executable and its SHA-256. If there are several, inspect the package's `MicrosoftGame.config` entry point to select the game executable. An EXE with an invalid or protected header is not reported as analysis-ready. Keep the version, architecture, hash, and local path in your reverse-engineering note; paths may be machine-specific.

## REA and Hopper

After `identify` reports a suitable game executable, give its **absolute local path** to REA:

```text
open_binary({"path":"/absolute/path/to/Conduit-Client/.local/minecraft-binaries/<version>/x86_64/<game-executable>.exe","provider_id":"hopper"})
binary_session({})
binary_overview({})
```

`open_binary` should report PE and x86_64. `binary_session` should show `analysis_provider_binding` as Hopper; `binary_overview` confirms that Hopper completed static analysis. Do not infer this merely from Hopper being installed. Do not start investigating JSON UI until a valid binary is open and its identity has been recorded. Follow [AGENTS.md](../AGENTS.md) for versioned evidence notes in `docs/reverse-engineering/`; notes contain observations and references, never game binaries, extracted assets, memory dumps, or large disassembly copies.

## Git and licensing checks

`.gitignore` excludes **all** of `.local/`. `check-git` verifies the ignore rule and checks for tracked or staged `.local` paths. Before any future commit, also inspect `git status --short` and `git diff --cached --name-only`. Git ignore rules reduce accidental commits; they are **not a legal exemption or a guarantee against copyright claims**. Keep all proprietary game packages, executables, and assets local. Do not redistribute or upload them to external services. The only intended project changes are original scripts and documentation.

## Troubleshooting

- **Build prerequisites missing:** install the named prerequisites, then rerun `setup`. The pinned source is stored locally; no upstream binary release is assumed.
- **Login window does not open:** start a graphical session and verify WebKitGTK 4.1 and an unlocked Secret Service keyring. Xodus does not provide a device-code fallback at this pinned revision.
- **No entitlement or package:** verify the account owns Minecraft for Windows in the relevant market. Xodus checks package and license availability through its own authenticated flow; do not substitute unofficial mirrors.
- **Different version returned:** Xodus resolves the current catalog package. `--expected-version` rejects mismatches after download; it cannot fetch arbitrary historical builds.
- **No valid x64 PE:** the package may retain an encrypted executable because Xodus's on-demand EXE handling is incomplete. Use a legitimately installed Windows copy that exposes an analysis-ready PE, or wait for supported Xodus functionality. Do not patch licensing or extract secrets from another app.
- **`cache ended before cached_len`:** Xodus's streaming cache can fail while parsing NTFS metadata. A fresh retry succeeded on this machine. The failed staging directory stays under `.local/`; do not treat it as a complete package. [Xodus tracks streaming error handling](https://github.com/xodus-gaming/xodus/issues/130).
- **REA cannot open it:** confirm the local path, PE header, architecture, and SHA-256 with `identify`; then inspect `binary_session` for Hopper availability. Keep the original binary local.
