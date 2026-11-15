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

## The state file

`Output/BrunnerDirect/BrunnerDirect.state.json` in X-Plane's folder (so updating the plugin keeps
it). Source of truth: `src/brunner/owner_state.hpp`. The two startup overrides, `auto_start` and
`force_feel`, are standard use and documented in [configuration](../configuration.md#brunnerdirectstatejson);
this section is the format and the rules.

```json
{"version":1,"allow_unverified_yoke":false,"auto_start":true,"force_feel":true}
```

| Key | Written by | Meaning |
|---|---|---|
| `version` | the plugin | Required, 1. |
| `allow_unverified_yoke` | the plugin (Developer > Allow unverified yoke models) | Whether to drive a yoke model that is not verified ([yoke presets](yoke-presets.md), "Gates"). Off by default; only a literal `true` turns it on. |
| `auto_start`, `force_feel` | you | Startup overrides of the same-named `BrunnerDirect.ini` keys. Absent: the ini's value. |

Rules:
- **Per key.** Every key but `version` is optional and judged alone: a value that is not a literal
  `true` or `false`, or a key this version does not know, is ignored and named in `Log.txt`, and the
  other keys still apply. (Before the overrides existed one wrong or unknown key switched the whole
  file off; now a valid `allow_unverified_yoke: true` still counts when another key is wrong.)
- **Whole file.** Not JSON, not an object, or not `"version":1`: everything is off, the ini applies,
  and one line in the log says the file was ignored.
- **Precedence.** State file, then `BrunnerDirect.ini`, then the built-in default (0). The state file
  is read after the ini; each override in effect is logged with the ini's value.
- **Saving.** Only the Developer toggle makes the plugin write the file (atomically: a temp file,
  then a rename). It writes `allow_unverified_yoke` and the two overrides exactly as it read them, so
  flipping the toggle never erases them. The menu never writes `auto_start` or `force_feel`.
- An older install's copy in the plugin folder is read once when the new place has none, and saved
  to the new place.

## `BrunnerDirect.ini` keys that are developer-only or retired

| Key | Default | Range | Meaning |
|---|---|---|---|
| `read_only_roll_reverse` | 0 | 0 or 1 | Read-only mode only ([read-only mode](read-only-mode.md)): 1 flips the roll direction of the learned travel. Optional. |
| `read_only_pitch_reverse` | 0 | 0 or 1 | Likewise for pitch. Optional. |
| `centre_drift_permille` | – | – | No longer used: the centring's allowed drift is the yoke model's `centre_drift_percent` ([yoke presets](yoke-presets.md)). An ini that still has it keeps loading; the value is ignored. |
| `allow_unverified_yoke` | – | – | Retired: replaced by the Developer menu choice in the state file above. An ini that still has the line loads, and `Log.txt` says it is ignored. |

## Fixing a travel the controller will not hold

`Readback mismatch ... register=2116:0 expected=N actual=M` in `Log.txt` means the controller
held a resting point at M instead of N: its own limit is lower than the model's travel, so force
feel stopped. Lower that axis's `pitch_travel_counts` or `roll_travel_counts` for the model in
`BrunnerDirect.yokes.json` to M or less (and tell us the number).
