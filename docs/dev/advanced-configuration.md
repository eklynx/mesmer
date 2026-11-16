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
it). Source of truth: `src/config/state/owner_state.hpp`. The two startup overrides, `auto_start` and
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
`""`) does nothing. It is read when the plugin loads, so restart X-Plane after editing. The commands run when the yoke's action is
`Direct` (the CLS-E NG's; the original CLS-E's is `VJoy` on Windows, so give its preset `"button_action_method": "Direct"` to use
commands there, [below](#the-button-action)).

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

### Names

A control can be named two ways, and both work: its own name (`button_N`, or a hat position) and a friendly name from the yoke's
`button_aliases` ([yoke presets](yoke-presets.md)). Names are matched in any case. If two of your keys turn out to be the same
control, **both run** and `Log.txt` says so at start.

The CLS-E NG, with the friendly names its shipped file gives:

| Key | Where on the yoke | Also called | Your notes |
|---|---|---|---|
| `button_1` | Trigger | `trigger` | |
| `button_2` | Top left | `top_left` | |
| `button_3` | Top right | `top_right` | |
| `button_4` | Rocker, down | `rocker_down` | |
| `button_5` | Rocker, up | `rocker_up` | |
| `button_6` | Hat, pushed in | `hat_push` | |
| `hat_n` | Hat, up | | |
| `hat_ne` | Hat, up and right | | |
| `hat_e` | Hat, right | | |
| `hat_se` | Hat, down and right | | |
| `hat_s` | Hat, down | | |
| `hat_sw` | Hat, down and left | | |
| `hat_w` | Hat, left | | |
| `hat_nw` | Hat, up and left | | |

The NG reads its six buttons and its hat from its own record `0x2880` ([button-commands.md](button-commands.md#what-each-yoke-has)); a model's `button_registers` says where each control is ([yoke presets](yoke-presets.md)).

The original CLS-E has 14 buttons (the slots are bits 16 to 31 of the register `0x60fd`) and no hat switch at all; a CLS-E MK2, reported
to have one, has no preset yet:

| Key | Control on the original CLS-E | Also called |
|---|---|---|
| `button_1` | Left grip, index finger | `left_index` |
| `button_2` | Left grip, thumb left | `left_thumb_left` |
| `button_3` | Left grip, thumb right | `left_thumb_right` |
| `button_4` | Left grip rocker, back | `left_rocker_back` |
| `button_5` | Left grip rocker, forward | `left_rocker_forward` |
| `button_7` | Right grip, index finger | `right_index` |
| `button_8` | Right grip, left thumb | `right_thumb_left` |
| `button_9` | Right grip, right thumb | `right_thumb_right` |
| `button_10` | Right grip rocker, back | `right_rocker_back` |
| `button_11` | Right grip rocker, forward | `right_rocker_forward` |
| `button_12` | Panel, middle | `panel_middle` |
| `button_13` | Panel, right | `panel_right` |
| `button_14` | Panel, left | `panel_left` |

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
    "button_1": "sim/operation/contact_atc",
    "button_2_hold": "sim/autopilot/CWSA",
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

### Button commands for one aircraft

The owner's description is in [Configuration](../configuration.md#aircraftidjson); this is the full rule. Each aircraft can change what the controls do for it. Put a file in `Output/BrunnerDirect/aircraft/` in X-Plane's folder (beside
`BrunnerDirect.state.json`, so an update never replaces it), one per aircraft, named by the id X-Plane gives it. The plugin tries, in order:

1. the name of the **`.acf` file** X-Plane loaded, `<name>.json` (`Cessna_172SP.json` for `Cessna_172SP.acf`), then
2. the aircraft's **ICAO type code**, `<ICAO>.json` (X-Plane's `sim/aircraft/view/acf_ICAO`: `C172.json`, `A320.json`), for an aircraft with no file of its own,

and uses the first that exists. A character in the id that is not a letter, a digit, `_`, `-` or `.` is written `_` in the file name, so an
id can never name a path. `Log.txt` says which file was used (`Aircraft button commands: aircraft/C172.json (3 entries)`). The file is
read when X-Plane loads or changes the aircraft: edit it, then reload the aircraft.

```json
{
  "version": 1,
  "button_commands": {
    "button_1": "sim/operation/contact_atc",
    "hat_n_hold": ["sim/general/rot_up"],
    "button_5_hold": null
  }
}
```

`"version"` is required and is `1` (as in the state file): a file without it, or with another number, is ignored whole and `Log.txt` says
so. The `button_commands` section is exactly the state file's (the same names, the same string, list or `null` values). Nothing else
belongs in the file: any other key is ignored and named in `Log.txt`.

**An aircraft's entries override the state file's, they do not add to them.** For each control and variant (the plain entry, `_released`,
`_hold`) the aircraft names, what the state file gives that control and variant is dropped and the aircraft's commands run in its place, so
the same press never runs both. `null`, `""` or `[]` means *no command* for that control on this aircraft, which switches off one the state
file binds. Everything the aircraft does not name keeps the state file's commands, and an entry for a control the state file does not bind
is simply used. **The three variants are independent**: an aircraft's `button_1` replaces only the state file's press command, never its
`button_1_released` or `button_1_hold`, which keep running; each is replaced, or switched off with `null`, only by its own entry
(`button_1_released`, `button_1_hold`). Two names of one control (`trigger` and `button_1`) are one control, whichever file names them. `Log.txt` says how many
controls an aircraft replaced.

## The button action

A field of a yoke's preset ([yoke presets](yoke-presets.md)), `button_action_method`, chooses what the plugin does with the yoke's
buttons. Source of truth: [button-commands.md](button-commands.md), `src/buttons/methods.hpp` and `src/config/yoke/yoke_preset.hpp`. Its
value is a word, in any case (`vjoy`, `Direct`, `VJOY` all count); anything else refuses the presets file, naming the field. Leave it
out of a model for the default (`Direct`). What the shipped models say:

| Model | `button_action_method` |
|---|---|
| Original CLS-E | `VJoy` |
| CLS-E NG | `Direct` |

| `button_action_method` | The plugin |
|---|---|
| `VJoy` | presses the buttons of a virtual joystick, which you bind in X-Plane's Joystick screen ([The yoke's buttons](../buttons.md)). Windows only for now: elsewhere a model asking for it gets `Direct`, and `Log.txt` says so. |
| `Direct` | runs the X-Plane commands in [`button_commands`](#button-commands). |

A virtual joystick's button numbers are the controls' button numbers (hat positions take 17 to 24). Nothing is read unless something
uses the buttons: `VJoy`, or at least one command. When the yoke's model is known, `Log.txt` says which action is in use. macOS and
Linux gain `VJoy` when a virtual joystick is written for them.
