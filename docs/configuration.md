# Standalone configuration — v1.0.0-stable

The data root is `%LOCALAPPDATA%/ConduitClient/` in Minecraft's Windows/Proton profile. `config.json` is created automatically with the defaults below.

On startup, the obsolete keys `enabled`, `resourcePackPath` and `reloadMode` are ignored regardless of their former values/types and removed through temporary-file replacement. Unknown keys are preserved. The old settings cannot select a broad reload path. Non-object, invalid or oversized (>16 KiB) files fail startup without changing the original. Write/replacement failures fail safely. `DISABLE` remains an operational stop sentinel outside configuration.


## Diagnostics

```json
{
  "ui": {
    "alpha": 0.5,
    "scale": 1.0
  },
  "diagnostics": {
    "factories": {
      "enabled": true,
      "empty": true
    }
  },
  "keybinds": {
    "diagnostics": "F3",
    "tooltip": "F5",
    "eject": "Ctrl+K",
    "reload": "Ctrl+R"
  }
}
```

`ui.scale` controls the font size, spacing, and padding of both the diagnostics and hover tooltip. It accepts 0.25 to 4.0, with 1.0 using Minecraft's native UI scale. `ui.alpha` controls the opacity of both backgrounds, from 0 (transparent) to 1 (opaque). The hover highlight also fades with this setting. Text remains fully opaque at every value.

On startup, Conduit migrates the old `ui_scale` or `diagnostics.scale` to `ui.scale`. If several are present, `ui.scale` takes precedence, followed by `ui_scale`.

`factories.enabled` shows or hides the entire factory section. `factories.empty: false` hides factory rows only when both live roots and controls are zero. These switches affect presentation, not sampling or diagnostic logging. Viewport clipping can still omit rows to keep the panel on screen.

Missing fields receive defaults; unknown fields are preserved. Invalid types or out-of-range scales fail startup with a logged error and leave the original config unchanged.

## Keybinds

Shortcuts accept a letter, digit, or F1–F24, optionally combined with Ctrl, Shift, or Alt using `+`. Matching is case-insensitive and uses the exact modifiers. Each action requires a distinct shortcut.

After editing `config.json`, press the currently active reload shortcut. Conduit applies the new keybinds and diagnostics settings and queues Minecraft’s JSON UI reload. If the config is invalid or cannot be read, Conduit logs the error, keeps the active settings, and still queues the JSON UI reload.

The optional `launcher.version` setting belongs to the launcher. Conduit preserves the launcher section when migrating client settings.

## Native reload admission

The reload shortcut (Ctrl+R by default) dispatches Minecraft's targeted JSON UI reload for its mounted definitions directly. ConduitClient does not scan or validate resource-pack JSON before dispatch, so unrelated files such as `texts/languages.json`, inactive packs and disk parse errors cannot block the shortcut. Minecraft owns definition parsing and error handling; ConduitClient does not promise rollback on native parsing errors. Continuous disk watching remains disabled. No manual resource-pack path is needed.

Executable compatibility, native context/thread-affinity checks, serialization and shutdown guards still apply. The internal discovery and JSON parsing utilities remain available for foundation/development callers.

## Logging

The layout is:

```text
ConduitClient/
  config.json
  logs/
    standalone.log
    native-stages.log
    client.log
```

The logger creates missing parent directories and appends. Previous root-level logs remain untouched; new automatic logs use `logs/`. Foundation automatic logging also uses `logs/conduit.log`; explicit foundation C API clients continue to own their supplied log paths. No automatic rotation/deletion occurs. If a directory cannot be created or a log cannot be opened, initialization fails with debugger output rather than falling back to a root-level file.
