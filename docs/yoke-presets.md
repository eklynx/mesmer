# Yoke presets: specification

What differs between yoke **models**, how it is chosen, and how forces stop at
a limit. The code: `src/brunner/yoke_preset.hpp` (the settings and the file; read it
first), `src/brunner/drive.hpp` (what the models and writes actually use),
`src/brunner/readonly.hpp` (the gate), `src/brunner/reader.cpp` (identification and the test
write). `src/brunner/preset_tests.cpp`, `src/brunner/drive_tests.cpp` and
`src/brunner/readonly_tests.cpp` check every statement here.

## A model, not a firmware

A preset belongs to a model, found from the USB vendor/product id of the bridge
Mesmer opened. A firmware update never changes a model's values. The ids Mesmer
opens are the ids the presets name, so a model added to the presets file is found
without a rebuild. A Brunner bridge whose id no preset names is opened read-only (see [read-only-mode.md](read-only-mode.md)); another vendor's is never opened.

Built in: `cls-e` (USB `25BB:0081`, verified on firmware 367) and `cls-e-ng`
(`25BB:008B`, not verified).

## What a preset holds

| Field | Meaning |
|---|---|
| `name`, `title` | `name` is lower case, digits and `-`; `title` is what the pilot sees |
| `usb` | the bridges that identify the model: `[{"vendor":9659,"product":139}]` (decimal) |
| `verified`, `verified_firmware` | see Gates |
| `roll_travel_counts`, `pitch_travel_counts` | travel either way from centre, in encoder counts (1000 to 30000): **the range the controller holds a resting point in**, not necessarily its end stops. The force models never put the centre past it, and a write the controller would clamp is a read-back fault. The NG's are 5300 (pitch, measured: it clamps there) and 7350 (roll, inferred from its homing sweep) |
| `wheel_deg`, `pitch_travel_mm` | wheel travel each way; pitch travel (information only) |
| `pitch_units_per_lb`, `roll_units_per_nm` | drive force units per physical unit (the same on both models so far) |
| `roll_ceiling_units`, `pitch_ceiling_units` | the most force the model's spec allows, in drive force units |
| `margin_fraction` | headroom kept under the ceiling (0.02 to 0.5; the CLS-E's 500 of 4500 is 0.1111) |
| `target_torque` | the only target-torque value the plugin writes (CLS-E 50, NG 25) |
| `status_stopped`, `status_ready_mask`, `status_acquired_mask` | the status words the CLS-E flow expects (decimal) |
| `spring_mode` | the force mode (`0x2101:01`) the controllers rest in, without the flow's bits 13, 11, 6 and 5 (decimal; the CLS-E's is 517 = 0x0205, the NG's 1541 = 0x0605). Any other mode is refused |
| `rest` | what the plugin leaves the controllers at between sessions (after force feel stops, before a centring, in recovery and in **Developer > Reset yoke**): `roll_curve`, `pitch_curve` (9 points each), `roll_spring_percent`, `pitch_spring_percent`, `pitch_gain_denominator`, `pitch_move_back`, `pitch_cutoff` (0: not written) and `mode` (the force mode left at rest, bit 13 included; 0: not written). The CLS-E's are the historical constants; the NG's are its own (survey of 2026-11-12, mode `0x2605`), because writing the CLS-E's to the NG overwrote them and the NG kept them. A **new model that gives no `rest`** has the plugin read these registers when a session starts and write exactly that back, so a yoke is left as it was found (settings it could not write back refuse the session before anything is written) |
| `idle_enabled` | `true` for a model that idles with its motors enabled (the NG). Centring then shuts them down the standard way first instead of refusing; `false` (the CLS-E) keeps the refusal |
| `roll_encoder_sign`, `pitch_encoder_sign` | the spring's frame is sign x encoder (+1 or -1) |
| `roll_input_reverse`, `pitch_input_reverse` | flips the pilot's input direction |

**The NG's numbers.** Travel is the midpoint of the controller's own limit flags
(roll -7637/+7647, pitch -5458/+5478). The ceilings are the spec peaks converted
with the CLS-E's units (pitch 190 N / 0.02 N per unit = 9500; roll 11 N.m / 0.001
N.m per unit = 11000). The ready mask is 0x1007 because the NG at rest reads
`0x1427`; its "stopped" word is not yet known and is the CLS-E's until a run shows it.

**Directions are unverified.** The CLS-E's `-1` encoder signs have never been
confirmed on a powered yoke, and the NG's are unknown. If the first powered run
shows the rest mirrored or the spring pushing the wrong way, flip the sign in the
presets file; no rebuild.

## The shipped models: the `yokes/` folder

Each model is a file in the plugin's `yokes/` folder (`config/yokes/` in the source):
`cls-e.json` (the original, verified) and `cls-e-ng.json`. A file is a complete
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
  `BrunnerDirect.state.json` in the plugin folder, is off by default, and anything
  doubtful in that file means off. Flipping it takes effect at once: the reader
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
