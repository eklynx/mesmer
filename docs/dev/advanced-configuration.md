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
| `reply_tolerance_ms` | you | How long, in milliseconds, the yoke may go without completing a sample before the link counts as lost: a whole number from 0 to 5000, default 1000 ([hid-bridge](hid-bridge.md#a-silent-yoke-is-tolerated-for-a-while)). Meanwhile the controls stay on the last sample and no force updates are written. 0 is the old behaviour: the first missed reply ends the session. Anything else is ignored and named in `Log.txt`. |
| `button_commands` | you | X-Plane commands for the yoke's buttons and hat ([below](#button-commands)). Absent: nothing runs. |

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
  flipping the toggle never erases them, nor a `button_commands` map. The menu never writes `auto_start` or `force_feel`.
- An older install's copy in the plugin folder is read once when the new place has none, and saved
  to the new place.

## Button commands

The CLS-E NG's six buttons and eight hat positions can each run an X-Plane command. Source of truth:
[button-commands.md](button-commands.md). Add a `button_commands` object to `BrunnerDirect.state.json`; every
entry is optional, and anything you leave out (or set to `null` or `""`) does nothing. It is read when the plugin
loads, so restart X-Plane after editing. **Leave X-Plane's own bindings for these controls unset**: X-Plane sees
the same joystick, and the yoke sometimes sends glitched reports that Mesmer ignores but X-Plane does not.

A press runs the command once; holding does not repeat. To run something when a control is *released*, use the
same key with `_released` added (`button_1_released`, `hat_n_released`): it runs once when the button is let go,
or the hat leaves that position. Every `_released` key is optional and does nothing by default.

For a command that should act **while the control is held** (trim, brakes, push to talk, panning the view), use
the key with `_hold` added (`button_4_hold`, `hat_w_hold`): the command is begun when the control is pressed and
ended when it is let go (`XPLMCommandBegin` / `XPLMCommandEnd`), so it keeps acting in between. The plain key runs
once instead (`XPLMCommandOnce`), which suits flaps and toggles. A control may have all three. If the yoke
disconnects or the plugin is disabled while something is held, the held command is ended. A command that the
new hat position still wants (`hat_n_hold: ["rot_up"]` to `hat_ne_hold: ["rot_up", "rot_right"]`) is left running,
not ended and begun again.

| Key | Where on the yoke | Your notes |
|---|---|---|
| `button_1` | Trigger | |
| `button_2` | Top left | |
| `button_3` | Top right | |
| `button_4` | Rocker, down | |
| `button_5` | Rocker, up | |
| `button_6` | Hat, pushed in | |
| `hat_n` | Hat, up | |
| `hat_ne` | Hat, up and right | |
| `hat_e` | Hat, right | |
| `hat_se` | Hat, down and right | |
| `hat_s` | Hat, down | |
| `hat_sw` | Hat, down and left | |
| `hat_w` | Hat, left | |
| `hat_nw` | Hat, up and left | |

Format: `"key": "command"`, `"key": ["command", "another"]` or `"key": null`, inside `"button_commands": { ... }`,
with `"version":1` still required. A control can run **several commands**: give a list, and they run in the order
written (a one-item list is the same as the string). A value that is not a string, a list of strings or `null`, a
list item that is not a string, or a key that is not in the table, is ignored and named in `Log.txt`. A command X-Plane does not know is reported on its first press.

### Example: a Cessna 172

A starting point, not a default (the plugin ships with every control unbound). It uses X-Plane 12's own
commands. The diagonals of the hat combine the directions as there is no diagonal rotate commands.

```json
{
  "version": 1,
  "allow_unverified_yoke": false,
  "button_commands": {
    "button_1": "sim/operation/contact_atc",
    "button_2_hold": "sim/autopilot/CWSA ",
    "button_3": "sim/autopilot/ap_disc_trim_interrupt",
    "button_4_hold": "sim/flight_controls/pitch_trim_up",
    "button_5_hold": "sim/flight_controls/pitch_trim_down",
    "button_6": "sim/view/default_view",
    "hat_e_hold": "sim/general/rot_right",
    "hat_n_hold": "sim/general/rot_up",
    "hat_ne_hold": [
      "sim/general/rot_up",
      "sim/general/rot_right"
    ],
    "hat_nw_hold": [
      "sim/general/rot_left",
      "sim/general/rot_up"
    ],
    "hat_s_hold": "sim/general/rot_down",
    "hat_se_hold": [
      "sim/general/rot_right",
      "sim/general/rot_down"
    ],
    "hat_sw_hold": [
      "sim/general/rot_down",
      "sim/general/rot_left"
    ],
    "hat_w_hold": "sim/general/rot_left"
  }
}
```

## `BrunnerDirect.ini` keys that are developer-only or retired

| Key | Default | Range | Meaning |
|---|---|---|---|
| `read_only_roll_reverse` | 0 | 0 or 1 | Read-only mode only ([read-only mode](read-only-mode.md)): 1 flips the roll direction of the learned travel. Optional. |
| `read_only_pitch_reverse` | 0 | 0 or 1 | Likewise for pitch. Optional. |
| `allow_unverified_yoke` | – | – | Retired: replaced by the Developer menu choice in the state file above. An ini that still has the line loads, and `Log.txt` says it is ignored. |

## Fixing a travel the controller will not hold

`Readback mismatch ... register=2116:0 expected=N actual=M` in `Log.txt` means the controller
held a resting point at M instead of N: its own limit is lower than the model's travel, so force
feel stopped. Lower that axis's `pitch_travel_counts` or `roll_travel_counts` for the model in
`BrunnerDirect.yokes.json` to M or less (and tell us the number).
