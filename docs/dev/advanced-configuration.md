# Advanced configuration

Settings that are not part of standard use: the yoke models, their overrides, the owner's
saved choice, and the `BrunnerDirect.ini` keys that only matter to developers. Standard
settings are in [configuration](../configuration.md).

## Yoke models and overrides

- `yokes/*.json`, beside the plugin, are the shipped yoke models. Do not edit them: an update
  replaces them.
- `BrunnerDirect.yokes.json`, beside the plugin, is your optional overrides file. It is never
  replaced by an update or the installer. An entry whose `name` is a shipped model's changes only
  the fields it gives; a new name adds a model. The format, every field and its bounds, and the
  gates are in [yoke presets](yoke-presets.md).
- Adding a model for new hardware: run **Developer > Survey yoke registers** (read-only models
  only, [read-only mode](read-only-mode.md)), send the file in a
  [compatibility report](../compatibility-report.md), and start from a copy of the closest shipped
  model.

## The owner's saved choice

Whether to drive a model that is not verified is the Yoke (Brunner) menu's **Developer > Allow
unverified yoke models**. It is saved in `Output/BrunnerDirect/BrunnerDirect.state.json` in
X-Plane's folder, so updating the plugin keeps it, and it is off by default
([yoke presets](yoke-presets.md), "Gates").

## `BrunnerDirect.ini` keys that are developer-only or retired

| Key | Default | Range | Meaning |
|---|---|---|---|
| `read_only_roll_reverse` | 0 | 0 or 1 | Read-only mode only ([read-only mode](read-only-mode.md)): 1 flips the roll direction of the learned travel. Optional. |
| `read_only_pitch_reverse` | 0 | 0 or 1 | Likewise for pitch. Optional. |
| `centre_drift_permille` | – | – | No longer used: the centring's allowed drift is the yoke model's `centre_drift_percent` ([yoke presets](yoke-presets.md)). An ini that still has it keeps loading; the value is ignored. |
| `allow_unverified_yoke` | – | – | Retired: replaced by the Developer menu choice above. An ini that still has the line loads, and `Log.txt` says it is ignored. |

## Fixing a travel the controller will not hold

`Readback mismatch ... register=2116:0 expected=N actual=M` in `Log.txt` means the controller
held a resting point at M instead of N: its own limit is lower than the model's travel, so force
feel stopped. Lower that axis's `pitch_travel_counts` or `roll_travel_counts` for the model in
`BrunnerDirect.yokes.json` to M or less (and tell us the number).
