# Yoke presets: specification

What differs between yoke **models**, how it is chosen, and how forces stop at
a limit. The code: `src/config/yoke/yoke_preset.hpp` (the settings and the file; read it
first), `src/brunner/drive.hpp` (what the models and writes actually use),
`src/brunner/readonly.hpp` (the gate), `src/brunner/reader.cpp` (identification and the test
write). `src/config/yoke/preset_tests.cpp`, `src/brunner/drive_tests.cpp` and
`src/brunner/readonly_tests.cpp` check every statement here.

## A model, not a firmware

A preset belongs to a model, found from the USB vendor/product id of the bridge
Mesmer opened. A firmware update never changes a model's values. The ids Mesmer
opens are the ids the presets name, so a model added to the presets file is found
without a rebuild. A Brunner bridge whose id no preset names is opened read-only (see [read-only-mode.md](read-only-mode.md)); another vendor's is never opened.

Built in: `cls-e` (USB `0x25BB:0x0081`, verified on firmware 367) and `cls-e-ng`
(`0x25BB:0x008B`, verified on firmware 642 and 810).

## What a preset holds

| Field | Meaning |
|---|---|
| `name`, `title` | `name` is lower case, digits and `-`; `title` is what the pilot sees |
| `usb` | the bridges that identify the model: `[{"vendorId":"0x25BB","productId":"0x008B"}]`: each id is a hex string with the `0x` prefix (1 to 4 digits, either case, and `0X` works too; written upper case, 4 digits). A decimal number is refused |
| `verified`, `verified_firmware` | see Gates |
| `roll_travel_counts`, `pitch_travel_counts` | travel either way from centre, in encoder counts (1000 to 30000): **the range the controller holds a resting point in**, not necessarily its end stops. The force models never put the centre past it, and a write the controller would clamp is a read-back fault. The NG's are 5300 (pitch, measured: it clamps there) and 7350 (roll, inferred from its homing sweep) |
| `roll_input_low`, `roll_input_high`, `pitch_input_low`, `pitch_input_high` | the encoder's raw counts at each stop (low below zero, high above), which scale the position sent to X-Plane: low reads -1 and high +1, each side by its own span. Leave all out (or 0) for -travel to +travel, the CLS-E's behaviour. The travel above is how far the spring's centre may be written and the force models' scale; these are where the yoke physically stops (the NG's: roll -7641/7649, pitch -5446/5461) |
| `wheel_deg`, `pitch_travel_mm` | wheel travel each way; pitch travel (information only) |
| `pitch_units_per_lb`, `roll_units_per_nm` | drive force units per physical unit (the same on both models so far) |
| `roll_ceiling_units`, `pitch_ceiling_units` | the most force the model's spec allows, in drive force units |
| `margin_fraction` | headroom kept under the ceiling (0.02 to 0.5; the CLS-E's 500 of 4500 is 0.1111) |
| `target_torque` | the only target-torque value the plugin writes (CLS-E 50, NG 25) |
| `centre_drift_percent` | how far the yoke may drift while a centring decides it has settled, as a **decimal percent of the axis's own travel** (default 0.1, 0 to 5; 0 asks for the old fixed 2 counts). A sensitive yoke wanders a little at rest: the centring settles when it holds within this of where the hold began (and within 200 counts of zero) for half a second; leaving it restarts the hold. 0.1 is about ±12 counts on the CLS-E's pitch and ±5 on the NG's (5300). Tune finely, e.g. 0.05 or 0.125 |
| `status_stopped`, `status_ready_mask`, `status_acquired_mask` | the status words the CLS-E flow expects (decimal) |
| `spring_mode` | the force mode (`0x2101:01`) the controllers rest in, without the flow's bits 13, 11, 6 and 5 (decimal; the CLS-E's is 517 = 0x0205, and the NG's 1541 = 0x0605). A controller found in any mode other than this or `power_on_spring_mode` is refused |
| `power_on_spring_mode` | optional: another force mode (same terms as `spring_mode`) a controller may be found in before the plugin has written one, for a model whose `spring_mode` differs from what it powers on in. Omitted or 0: the same as `spring_mode`. The NG's is 517 = 0x0205 (the CLS-E's), see below. The flow still works in `spring_mode` and writes it itself |
| `rest` | what the plugin leaves the controllers at between sessions (after force feel stops, before a centring, in recovery and in **Developer > Reset yoke**): `roll_curve`, `pitch_curve` (9 points each), `roll_spring_percent`, `pitch_spring_percent`, `pitch_gain_denominator`, `pitch_move_back`, `pitch_cutoff` (0: not written) and `mode` (the force mode left at rest, bit 13 included; 0: not written). The CLS-E's are the historical constants; the NG's are its own (survey of 2026-11-12, mode `0x2605`), because writing the CLS-E's to the NG overwrote them and the NG kept them. A **new model that gives no `rest`** has the plugin read these registers when a session starts and write exactly that back, so a yoke is left as it was found (settings it could not write back refuse the session before anything is written) |
| `idle_enabled` | `true` for a model that idles with its motors enabled (the NG). Centring then shuts them down the standard way first instead of refusing; `false` (the CLS-E) keeps the refusal |
| `roll_encoder_sign`, `pitch_encoder_sign` | the model's counts (forward / right positive) are sign x the encoder (+1 or -1). Reading only: it is where the force models and the feel page think the yoke is |
| `roll_spring_sign`, `pitch_spring_sign` | the controller's spring frame (the centre 0x2116 and the constant force) is sign x the encoder (+1 or -1, default -1): a fact about the hardware. Every value the plugin writes to those registers is the model's times `encoder_sign` x `spring_sign`, so it is mirrored only when the two differ. The CLS-E is -1 and -1 (never mirrored); the NG's pitch encoder runs the other way, so it is +1 and -1 and its pitch writes are mirrored (roll is -1 and -1) |
| `button_aliases` | what the yoke's controls are called, as a map of lists keyed like `button_registers`: the key is the control (`button_1` to `button_16`, `hat_n` to `hat_nw`, any case) and the list its friendly names, `"button_1": ["trigger"]`. A control may have several names and each name belongs to one control; a name is 1 to 32 of `a-z`, `0-9`, `_`, not ending in `_released` or `_hold` and not a button or hat name. A key that is not a button or hat name, a key given twice (in any case), a name that is not usable or a name given twice is skipped (the rest of the map and the file still apply), see [Button mappings](#button-mappings-that-cannot-be-used). A control's virtual-joystick button is its button number (the hat positions take 17 to 24). Giving the field in `BrunnerDirect.yokes.json` replaces the model's whole map. See [button-commands.md](button-commands.md) |
| `button_registers` | where each of the yoke's buttons and hat positions is on the bridge, as a map: the key is the control (`button_1` to `button_16`, `hat_n` to `hat_nw`), the value is the controller object that says it is pressed, `{"node": 2, "object": "0x2880", "sub": 3, "mask": "0x01"}` or `{"node": 2, "object": "0x2880", "sub": 4, "value": 1}`. `node` (1 or 2), `object` (hex) and `sub` are required, plus exactly one of `mask` (a hex string: pressed when **all** its bits are set in the value read) and `value` (a whole number: pressed when the value read is exactly it). Only the read-only allowlist is accepted (`0x60FD` sub 0, four bytes; `0x2880` sub 3 or 4, one byte each); an entry naming anything else, a node other than 1 or 2, a mask of 0 or wider than the object, a value wider than the object, a key that is not a button or hat name or a key given twice (in any case) is skipped, see [Button mappings](#button-mappings-that-cannot-be-used). A control with no entry is one the yoke does not have and is never pressed. Each distinct object is read once per sampling cycle, however many controls use it. The original CLS-E's are one mask per slot on `0x60FD` (bits 16 to 31 for slots 1 to 5 and 7 to 14; no hat); the NG's are bits 0 to 5 of `0x2880:3` and the values 1 to 8 of `0x2880:4` for the hat. Giving the field replaces the model's whole map |
| `button_action_method` | what the plugin does with this model's buttons: `"VJoy"` (a virtual joystick X-Plane binds; Windows, elsewhere it is `Direct`) or `"Direct"` (the X-Plane commands in `button_commands`). Any case. Left out, `Direct`; any other word is skipped and the model keeps its own. The original CLS-E's is `VJoy`, the NG's `Direct`. See [button-commands.md](button-commands.md) |
| `roll_input_reverse`, `pitch_input_reverse` | flips the pilot's input direction (the CLS-E reverses roll; the NG reverses roll and pitch) |

**The NG's numbers.** Travel is the midpoint of the controller's own limit flags
(roll -7637/+7647, pitch -5458/+5478). The ceilings are the spec peaks converted
with the CLS-E's units (pitch 190 N / 0.02 N per unit = 9500; roll 11 N.m / 0.001
N.m per unit = 11000). The ready mask is 0x1007 because the NG at rest reads
`0x1427`; its "stopped" word is not yet known and is the CLS-E's until a run shows it.

**Directions are unverified.** The CLS-E's `-1` signs have never been confirmed on a
powered yoke, and the NG's are inferred from one log. If the first powered run shows
the rest mirrored or the spring pushing the wrong way, flip `*_spring_sign` (the
hardware's frame) in the presets file; no rebuild.

## The shipped models: the `yokes/` folder

Each model is a file in the plugin's `yokes/` folder (`config/yokes/` in the source):
`cls-e.json` (the original, verified on firmware 367) and `cls-e-ng.json` (verified on firmware 642 and 810). A file is a complete
`{"schema":1,"yokes":[{...}]}` with every field spelled out, so a new model is a copy
and an edit; numbers are decimal (JSON has no hex: `status_stopped` 1057 is 0x0421,
`spring_mode` 1541 is 0x0605). The files are read once at start, each on its own (a
shipped file cannot patch another), and names and USB ids must be unique across the folder.
If the folder is missing or any file does not validate, the **whole folder** is refused,
the reason is in `Log.txt`, and the compiled-in models (equal to the files, pinned by a
test) stand in. Do not edit the shipped files to tune a yoke: an update replaces them.
Put changes in the overrides file below.

## The overrides file

`BrunnerDirect.yokes.json`, next to the profiles file, optional, never replaced by an
update or the installer. Read once at start, after the `yokes/` folder. An
entry whose `name` is a shipped model's changes only the fields it gives. A new name
must give `usb`, both travels, both ceilings and `target_torque`; it is
unverified unless it says otherwise. No two models may claim one USB id.
Every field is bounded; an unknown field or a file that does not validate is
refused **whole**, the built-ins stay in force, and the reason is in `Log.txt`.
The button mappings are the exception, below.

### Button mappings that cannot be used

`button_aliases`, `button_registers` and `button_action_method` never refuse a file. What cannot be used is **skipped** and the rest
applies: a bad `button_registers` entry or alias is left out and its neighbours stay; a bad `button_action_method` keeps the model's own
value; a field of the wrong kind altogether (not an object) leaves the model with its own. Each skipped item is one line in `Log.txt`
(`Yoke config: yoke "cls-e": button_aliases.nope is not a button or hat name ...; skipped`) **and** the first is shown in the corner of the
X-Plane window, with a count of the others, until you click it. A movement warning or a Retry offer takes the corner first, and the
notice comes back after. Every other field still refuses the file as above: those numbers drive the yoke's forces.

## The NG's force mode

The NG works in its own force mode, 0x0605 (0x2605 at rest, with the reference request, so it still references
itself at power-on). The CLS-E's 0x0205, the same without bit 0x0400, was tried on it on 2026-11-14 and the
NG's registers kept it, so the NG preset's `power_on_spring_mode` is 517 (0x0205): a controller found in that
mode is accepted, and the first session writes 0x0605 / 0x2605 back. Any other mode is refused. Once every NG has
been through a session the field can be dropped.

## Forces stop at limits

For each axis the ceiling is the **lower of the preset's** and **the controller's
own limit register** (`0x211b`, read at session start and never written). One
budget for both axes follows from the lower axis, less the margin. The models
**clamp** at it: they do not ask for more and nothing is refused for exceeding it.
The CLS-E's 4500 units and 500 margin are exactly what this gives for the CLS-E.
Travel scales: the spring is defined as 10 units per percent at full travel, so a
model with less travel has a proportionally stiffer spring per count.

## Gates

- **A model that is not `verified`** is read-only unless **Developer > Allow
  unverified yoke models** in the Yoke (Brunner) menu is on (see
  [read-only-mode.md](read-only-mode.md)). The choice is saved in
  `Output/BrunnerDirect/BrunnerDirect.state.json` in X-Plane's folder (an older copy in the plugin folder is read once and saved there), is off by default, and only a literal
  `true` turns it on (the file's other rules: [advanced configuration](advanced-configuration.md#the-state-file)). Flipping it takes effect at once: the reader
  restarts, so turning it off stops force feel and returns to read-only, and turning
  it on starts the test write and then the normal flow (which stops the motors and
  centres the yoke: it moves). The older `allow_unverified_yoke` key in
  `BrunnerDirect.ini` is retired: a file that still has it loads, the line is ignored
  and `Log.txt` says so. The
  first writes to such a model are one setting (the spring strength) written back
  to the value it already has, node by node, with a read-back, before anything
  else is sent. That checks the write is acknowledged and nothing aborts; it
  cannot show that a changed value would take. A refusal ends the session with the
  reason.
- **Firmware never gates.** A firmware outside the preset's `verified_firmware`
  is noted in `Log.txt` and on the Force Monitor's status line, for information only;
  it never makes a yoke read-only, and a firmware update changes nothing.
- **Refusals print the status words** the controller reported, so the preset's
  status fields can be corrected from the log.

## Not yet done

The feel page (`workbench.html`, `elevator.html`, `aileron.html`) and the shared
test vectors still draw CLS-E travel; they are display only and follow in a
separate change. The spring law and the force units on the NG are assumed, not
measured.
