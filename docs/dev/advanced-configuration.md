# Advanced configuration

Settings that are not part of standard use: the yoke models, their overrides, the owner's
saved choice, how a yoke's buttons are read and used, and the `BrunnerDirect.ini` keys that only matter to developers. Standard
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
| `button_commands` | you | X-Plane commands for the yoke's controls ([below](#button-commands)). Absent: nothing runs. |

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

The yoke's controls can each run an X-Plane command. Source of truth: [button-commands.md](button-commands.md). Add a
`button_commands` object to `BrunnerDirect.state.json`; every entry is optional, and anything you leave out (or set to `null` or
`""`) does nothing. It is read when the plugin loads, so restart X-Plane after editing. The commands run when
the yoke's action is `Direct` (the CLS-E NG's; the original CLS-E's is `VJoy` on Windows, so give its preset `"button_action_method": "Direct"` to use commands there, [below](#button-read-and-action-methods)).
**Leave X-Plane's own bindings for these controls unset** when a preset reads the NG's joystick (`HID`): X-Plane sees the same joystick,
and the yoke sometimes sends glitched reports that Mesmer ignores but X-Plane does not.

A press runs the command once; holding does not repeat. To run something when a control is *released*, use the
same key with `_released` added (`hid_button_1_released`, `hat_n_released`): it runs once when the button is let go,
or the hat leaves that position. Every `_released` key is optional and does nothing by default.

For a command that should act **while the control is held** (trim, brakes, push to talk, panning the view), use
the key with `_hold` added (`hid_button_4_hold`, `hat_w_hold`): the command is begun when the control is pressed and
ended when it is let go (`XPLMCommandBegin` / `XPLMCommandEnd`), so it keeps acting in between. The plain key runs
once instead (`XPLMCommandOnce`), which suits flaps and toggles. A control may have all three. If the yoke
disconnects or the plugin is disabled while something is held, the held command is ended. A command that the
new hat position still wants (`hat_n_hold: ["rot_up"]` to `hat_ne_hold: ["rot_up", "rot_right"]`) is left running,
not ended and begun again.

### Names

A control can be named three ways, and all three work whichever way the buttons are read: its NG joystick name (`hid_button_N` or a
hat position), its CAN name (`can_button_N` or `can_hat_*`: the original CLS-E's slots, the NG's own record), and a friendly name from the yoke's
`button_aliases` ([yoke presets](yoke-presets.md)). Names are matched in any case. If two of your keys turn out to be the same
control, **both run** and `Log.txt` says so at start.

The NG's joystick (read with `HID`), and the friendly names the shipped NG file gives. Note that the hat can pass through a
neighbouring position for a moment (22 to 44 ms) when moving to or from a diagonal, so a once-command on a straight direction can
fire on the way to a diagonal; `_hold` entries that list the same commands for the diagonal (as the example below does) hide it:

| Key | Where on the yoke | Also called | Your notes |
|---|---|---|---|
| `hid_button_1` | Trigger | `trigger` | |
| `hid_button_2` | Top left | `top_left` | |
| `hid_button_3` | Top right | `top_right` | |
| `hid_button_4` | Rocker, down | `rocker_down` | |
| `hid_button_5` | Rocker, up | `rocker_up` | |
| `hid_button_6` | Hat, pushed in | `hat_push` | |
| `hat_n` | Hat, up | | |
| `hat_ne` | Hat, up and right | | |
| `hat_e` | Hat, right | | |
| `hat_se` | Hat, down and right | | |
| `hat_s` | Hat, down | | |
| `hat_sw` | Hat, down and left | | |
| `hat_w` | Hat, left | | |
| `hat_nw` | Hat, up and left | | |

The CAN slots (read with `CanBus`; on the original CLS-E bits 16 to 31 of the register `0x60fd`), as the original CLS-E has them. The
shipped NG file links each of the NG's `hid_button_1` to `hid_button_6` to `can_button_1` to `can_button_6` and each hat position to its
`can_hat_*` twin, and reads them from its own record `0x2880` ([button-commands.md](button-commands.md#what-each-yoke-has)), so on the
NG a command under any name fires with either read method. The original CLS-E has no hat switch at all; a CLS-E MK2, reported to have
one, has no preset yet:

| Key | Control on the original CLS-E | Also called |
|---|---|---|
| `can_button_1` | Left grip, index finger | `left_index` |
| `can_button_2` | Left grip, thumb left | `left_thumb_left` |
| `can_button_3` | Left grip, thumb right | `left_thumb_right` |
| `can_button_4` | Left grip rocker, back | `left_rocker_back` |
| `can_button_5` | Left grip rocker, forward | `left_rocker_forward` |
| `can_button_7` | Right grip, index finger | `right_index` |
| `can_button_8` | Right grip, left thumb | `right_thumb_left` |
| `can_button_9` | Right grip, right thumb | `right_thumb_right` |
| `can_button_10` | Right grip rocker, back | `right_rocker_back` |
| `can_button_11` | Right grip rocker, forward | `right_rocker_forward` |
| `can_button_12` | Panel, middle | `panel_middle` |
| `can_button_13` | Panel, right | `panel_right` |
| `can_button_14` | Panel, left | `panel_left` |

Slot 6 and slots 15 and 16 have no control on the CLS-E. **Leave the three panel buttons alone in flight, and don't bind them**: they
also work the yoke's own menu.

Format: `"key": "command"`, `"key": ["command", "another"]` or `"key": null`, inside `"button_commands": { ... }`,
with `"version":1` still required. A control can run **several commands**: give a list, and they run in the order
written (a one-item list is the same as the string). A value that is not a string, a list of strings or `null`, a
list item that is not a string, or a key that is not a name of a control of the connected yoke, is ignored and named in `Log.txt`. A command X-Plane does not know is reported on its first use.

### Example: a Cessna 172

A starting point, not a default (the plugin ships with every control unbound). It uses X-Plane 12's own
commands. The diagonals of the hat combine the directions as there is no diagonal rotate commands.

```json
{
  "version": 1,
  "allow_unverified_yoke": false,
  "button_commands": {
    "hid_button_1": "sim/operation/contact_atc",
    "hid_button_2_hold": "sim/autopilot/CWSA ",
    "hid_button_3": "sim/autopilot/ap_disc_trim_interrupt",
    "hid_button_4_hold": "sim/flight_controls/pitch_trim_up",
    "hid_button_5_hold": "sim/flight_controls/pitch_trim_down",
    "hid_button_6": "sim/view/default_view",
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

## Button read and action methods

Two fields of a yoke's preset ([yoke presets](yoke-presets.md)) choose where the yoke's buttons are read from and what the plugin
does with them. Source of truth: [button-commands.md](button-commands.md), `src/buttons/methods.hpp` and `src/brunner/yoke_preset.hpp`. Their
values are words, in any case (`hid`, `Hid`, `canbus`, `VJOY` all count); anything else refuses the presets file, naming the field.
Leave them out of a model for the defaults (`CanBus` and `Direct`). What the shipped models say:

| Model | `button_read_method` | `button_action_method` |
|---|---|---|
| Original CLS-E | `CanBus` | `VJoy` |
| CLS-E NG | `CanBus` | `Direct` |

| `button_read_method` | The buttons are read |
|---|---|
| `CanBus` | over the yoke's bridge, from the registers its model names (the original CLS-E's `0x60fd`; the NG's `0x2880`, which has its hat too), in every sampling cycle. Works on the original CLS-E and the NG, and needs no second interface. Names: `can_button_N`, `can_hat_*`. |
| `HID` | from the NG's own joystick HID reports, shared with X-Plane and read-only. Needs a yoke whose USB has a joystick interface (the CLS-E NG). Names: `hid_button_N` and the hat positions. |

| `button_action_method` | The plugin |
|---|---|
| `VJoy` | presses the buttons of a virtual joystick, which you bind in X-Plane's Joystick screen ([The yoke's buttons](../buttons.md)). Windows only for now: elsewhere a model asking for it gets `Direct`, and `Log.txt` says so. |
| `Direct` | runs the X-Plane commands in [`button_commands`](#button-commands). |

Every combination works where the operating system allows: `CanBus` with `Direct` runs commands from the original CLS-E's slots,
`HID` with `VJoy` feeds the virtual joystick from the NG's reports, and a virtual joystick's button numbers are the same either way
(a control's CAN slot number; controls with none take 17 and up). Nothing is read, and nothing is opened, unless something uses
the buttons: `VJoy`, or at least one command. When the yoke's model is known, `Log.txt` says which read method and action are in use.
macOS and Linux gain `VJoy` when a virtual joystick is written for them.

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
