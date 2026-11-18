# Configuration

Two files beside the plugin in `X-Plane 12\Resources\plugins\BrunnerDirect\`
set it up:

- `BrunnerDirect.ini`: startup settings. Read once, when X-Plane starts.
- `BrunnerDirect.profiles.json`: each aircraft's control-surface figures.
  Read again within two seconds of a change, mid-flight.

The yoke models (`yokes/*.json`) and your optional overrides of them are
advanced settings, usually only changed during development: see
[advanced configuration](dev/advanced-configuration.md).

A third, `BrunnerDirect.feel.ini`, lives next to `BrunnerDirect.ini` and is
written by the feel page's *Save* button. It overlays the drive calibration and the elevator figures you saved
there on top of the two files above, each time X-Plane starts. Delete it to
go back to the files' values.

The release's copies are in this repository's [`config/`](../config) folder.

Your own startup choices that an update must not replace go in
[`BrunnerDirect.state.json`](#brunnerdirectstatejson), in X-Plane's `Output`
folder, and what each aircraft's yoke buttons do can be changed per aircraft
in [`aircraft/<id>.json`](#aircraftidjson) beside it.

> **Paths:** use `\` on Windows and `/` on MacOS and Linux, whichever way a
> path is written here. `Log.txt` is in X-Plane's own folder.

| File | Where | Kept across updates? | Edit it by hand? |
|---|---|---|---|
| [`BrunnerDirect.ini`](#brunnerdirectini) | Plugin directory | Only if you copy it back ([updating](install.md#updating)) | Yes: startup settings |
| [`BrunnerDirect.profiles.json`](#brunnerdirectprofilesjson) | Plugin directory | Only if you copy it back | Only to add or tune an aircraft |
| `BrunnerDirect.feel.ini` | Plugin directory | Only if you copy it back | No: the feel page's *Save* writes it; delete it to reset |
| `BrunnerDirect.yokes.json`, `yokes/*.json` | Plugin directory | Only if you copy them back | Advanced: see [advanced configuration](dev/advanced-configuration.md) |
| [`BrunnerDirect.state.json`](#brunnerdirectstatejson) | Output directory | Yes | Yes, optional: your own startup choices |
| [`aircraft/<id>.json`](#aircraftidjson) | Output directory | Yes | Yes, optional: per-aircraft button commands |

## Plugin directory

Files in this section, and `BrunnerDirect.feel.ini`, live in the plugin
installation directory `Resources/plugins/BrunnerDirect/`. Blindly updating the plugin would
overwrite these files: see [updating](install.md#updating).

### BrunnerDirect.ini

One `key=value` per line, whole numbers only, `#` starts a comment. The file
is strict on purpose: an unknown key, a duplicate, a value out of bounds or a
missing required key stops the plugin from starting, and `Log.txt` says why
(`[BrunnerDirect] Configuration invalid; plugin not started: ...`). Without
the file at all the plugin still starts, with every default below, except
that `auto_start` is 0 and the feel page is off. Which keys are required
depends on the file's schema version; the *Required* column gives version 6's.

| Key | Release | Bounds | Required | What it does |
|---|---|---|---|---|
| <a id="ini-schema_version"></a>`schema_version` | 6 | 2 to 6 | Y | The file's format version. |
| <a id="ini-auto_start"></a>`auto_start` | **0** | 0 or 1 | Y | 0: after X-Plane starts, the yoke waits for your *Retry* before it moves for the first time. 1: the plugin centres the yoke by itself as soon as X-Plane is steady and the yoke is still. Either way, after the first centring it looks after the yoke by itself. [`BrunnerDirect.state.json`](#brunnerdirectstatejson) can override it. |
| <a id="ini-force_feel"></a>`force_feel` | **0** | 0 or 1 |  | Whether force feel is on when X-Plane starts. Pitch and roll work either way. Optional; 0 if missing. [`BrunnerDirect.state.json`](#brunnerdirectstatejson) can override it. |
| <a id="ini-workbench_port"></a>`workbench_port` | 8087 | 0, or 1024 to 65535 | Y | The feel page's port on your local network. 0 turns the page off. If the port is taken, the menu says *Feel tuning unavailable: port 8087 in use* and the plugin tries again every 5 seconds. |
| <a id="ini-pitch_force_units_per_lb"></a>`pitch_force_units_per_lb` | 222 | 50 to 1000 | Y | How hard: the drive's force units per pound at the grip. Worked out from CLS2Sim's display units with the CLS-E, not measured. It changes how hard the yoke pushes, never where it rests. |
| <a id="ini-roll_torque_units_per_nm"></a>`roll_torque_units_per_nm` | 1000 | 100 to 10000 | Y | The same for roll: drive units per N·m at the wheel. Worked out, not measured. |
| <a id="ini-pitch_force_gain_divisor"></a>`pitch_force_gain_divisor` | 6 | 1 to 6 | Y | The drive's own drag on pitch (its force gain is 1/this). 6 is the drive's own setting; lower values are untested. |
| <a id="ini-pitch_move_back_gain"></a>`pitch_move_back_gain` | 180 | 180 to 720 | Y | The drive's move-back gain on pitch. 180 is its own; higher values are untested. |
| <a id="ini-debug_log"></a>`debug_log` | **0** | 0 or 1 |  | 1: turns on debug logging. This includes force feedback session recording (`ffb-*.jsonl` files in `Output/BrunnerDirect`, 16 MB each, the newest 16 kept) and debug entries in `Log.txt`. They are used to diagnose a problem; turn it on when asked to send one. Optional; 0 if missing. |
| <a id="ini-buffet"></a>`buffet` | 0 | 0 or 1 |  | A shake at the stall through the drive's vibration generator. Untested with the motors on. Optional. |
| <a id="ini-buffet_percent"></a>`buffet_percent` | 10 | 0 to 100 |  | Buffet's strength, as a share of the plugin's own ceiling for it. Optional. |
| <a id="ini-buffet_period"></a>`buffet_period` | 80 | 10 to 140 |  | The vibration generator's raw time setting; its frequency is not confirmed. Optional. |
| <a id="ini-read_only_roll_reverse"></a>`read_only_roll_reverse` | 0 | 0 or 1 |  | Reverses roll's direction. Only used in read-only mode, when the plugin reads the yoke but does not drive it (an unknown or unverified yoke model): set it to 1 if turning the wheel right moves X-Plane's ailerons left. It has no effect when the plugin drives the yoke: there, directions are set in the yoke's preset, `roll_input_reverse` for what X-Plane reads and `roll_spring_sign` for the force's direction ([yoke presets](dev/yoke-presets.md)). Optional; 0 if missing. |
| <a id="ini-read_only_pitch_reverse"></a>`read_only_pitch_reverse` | 0 | 0 or 1 |  | The same for pitch: set it to 1 if pulling back moves X-Plane's elevator down. Read-only mode only; for a driven yoke use `pitch_input_reverse` and `pitch_spring_sign` in its preset. Optional; 0 if missing. |
| <a id="ini-pitch_trim_counts"></a>`pitch_trim_counts` | 0 | −600 to 600 | Y | A bench-test offset of pitch's neutral, used only by the developer spring test. Leave at 0. |
| <a id="ini-reference_airspeed_knots"></a>`reference_airspeed_knots` | 316 | 40 to 400 | Y | Kept for older files; not used by the surface models. |

> **Do not change `pitch_force_units_per_lb`, `roll_torque_units_per_nm`,
> `pitch_force_gain_divisor` or `pitch_move_back_gain` unless you know what
> you're doing.** They are set for the drive's hardware, wrong values change
> how hard the yoke pushes, and some values are untested. Read
> [SAFETY.md](../SAFETY.md) first. They may also move to the yoke settings in a
> future version.

Keys from older versions of the plugin (`roll_spring_percent`,
`pitch_spring_percent`, `minimum_spring_percent` and others) are accepted,
ignored, and listed in `Log.txt` as *retired and ignored* (see [advanced configuration](dev/advanced-configuration.md)).

### BrunnerDirect.profiles.json

Every aircraft flies one profile: its own, if a profile names its aircraft
file, otherwise the default. The release has one profile, `c172`, the stock
Laminar Research Cessna 172 SP, and it is the default. So **every aircraft
flies the Cessna 172's elevator and ailerons** until someone writes its own
profile.

```json
{
  "schema": 2,
  "default": "c172",
  "profiles": {
    "c172": {
      "aircraft": ["Aircraft/Laminar Research/Cessna 172 SP/Cessna_172SP.acf"],
      "tail_sections": [8, 9],
      "aileron_sections": [2, 3],
      "wing_root_sections": [0, 1],
      "elevator": {
        "area_m2": {"value": 1.35, "tag": "published", "source": "Elevator area 14.5 ft², both halves: Jane's All the World's Aircraft 1977-78, Cessna 172"},
        "...": "every elevator figure below"
      },
      "aileron": {
        "...": "every aileron figure below"
      }
    }
  }
}
```

#### How a profile matches an aircraft

`aircraft` lists `.acf` files as paths under the X-Plane folder, with `/`
between folders. An aircraft matches when its path is the same, or ends with
`/` followed by the listed path, so `Cessna 172 SP/Cessna_172SP.acf` matches
the stock 172 wherever X-Plane is. The first profile that names the aircraft
wins; anything else flies `default`.

The `*_sections` keys name X-Plane's own "wings" (the flight model's
surfaces, numbered 0 to 9 in the aircraft's file) whose air belongs to each
control surface, two each:

- `tail_sections`: the horizontal tail's left and right halves. The plugin
  reads the air over these panels, including propwash, and the tail's angle
  of attack and stall.
- `aileron_sections`: the left wing's aileron panels, then the right's.
- `wing_root_sections`: the inner wing panels, whose stall drives pitch's
  buffet.

These are read only on the profile's **own** aircraft. An aircraft flying
the default profile gets the 172's figures with the air estimated from
indicated airspeed, with no propwash, angle of attack or stall.

#### Every figure carries its evidence

Each figure is an object with exactly three keys:

- `value`: the number, within the bounds below;
- `tag`: `published` (from a manual, a type certificate or a reference
  book), `derived` (worked out from published figures), or `guessed`;
- `source`: where it came from, in at most 600 characters.

The feel page shows each figure with its tag and source, and lets you try a
different value live. The plugin refuses a file that is over 256 KiB, is not
valid JSON, has an unknown key or a missing figure, has a value out of
bounds, or has no default profile. A refused file changes nothing: the
figures already being flown stay, the feel page shows why, and `Log.txt`
records it. Without any usable file the yoke still centres and gives pitch
and roll, and the line reads *Ready - force feel needs the aircraft profiles;
see X-Plane's Log.txt*.

#### The elevator's figures

The release's Cessna 172 SP values. Figures marked *page only* are used by
the feel page's free-elevator animation and never reach the yoke; they are
optional.

| Figure | Unit | Bounds | C172 | Tag | Source |
|---|---|---|---|---|---|
| `area_m2` | m² | 0.05–20 | 1.35 | published | Elevator area 14.5 ft², both halves: Jane's All the World's Aircraft 1977-78, Cessna 172 |
| `chord_m` (aft of the hinge) | m | 0.02–3 | 0.427 | derived | 17 in, from the elevator's area and span |
| `hinge_slope_per_rad` | /rad | 0–5 | 0.45 | derived | Hinge-moment slope read from NASA TN D-7149 Fig. 39a, a similar Cessna-type high-wing elevator; no 172 figure was found |
| `up_deg` | ° | 1–60 | 28 | published | Cessna 172 elevator travel up, Cessna's maintenance figure; not yet checked against a copy |
| `down_deg` | ° | 1–60 | 23 | published | Cessna 172 elevator travel down, as above |
| `gearing_rad_per_in` (elevator per inch of yoke) | rad/in | 0.01–2 | 0.137 | guessed | 7.8° per inch: the elevator's 51° of travel over a guessed 6.5 in of yoke travel |
| `unbalance_inlb` (weight about the hinge, trailing edge heavy positive) | in·lb | −200–200 | 30 | guessed | Within Cessna's 0 to 43 in·lb trailing-edge-heavy allowance for both halves (Single Engine Structural Repair Manual 51-60-00, Table 1); no measured elevator was found |
| `tab_ratio` (how far the trim tab floats the elevator against it) | | 0–3 | 0.6 | guessed | An estimate, no source recorded |
| `tab_up_deg` | ° | 0–60 | 22 | published | Cessna 172 trim tab travel up; no citation recorded, not yet checked against a copy |
| `tab_down_deg` | ° | 0–60 | 19 | published | Cessna 172 trim tab travel down; as above |
| `alpha_slope_per_rad` (float with the tail's angle of attack) | /rad | −2–2 | 0.2 | guessed | No C172 figure found; a little under half the hinge slope, the share plain flaps of this chord show in NACA TR 721 and Report 868. The 172's horn balances may make it smaller, perhaps zero or negative |
| `stall_factor` (share of the air's moments a stalled tail keeps) | | 0–1 | 0.4 | guessed | No published hinge moments past a tail's stall |
| `tau` (the elevator's share of the tail section's angle) | | 0–1 | 0.5 | published | Raymer, *Aircraft Design*, flap-effectiveness chart from NACA plain-flap data, for an elevator about 40% of the tail's chord (that share guessed from drawings) |
| `stall_deg` (the tail section's own stall, angle plus tau × deflection; 0 turns it off) | ° | 0–60 | 0 | guessed | Off in this release: near its onset the yoke's rest can jump between two balances. 13 is the value to try on the feel page: a thin symmetric section stalls at about 12–14° (NACA 0009 and 0012, Abbott and von Doenhoff) |
| `stall_width_deg` (from `stall_deg` to all of the tail stalled) | ° | 0–30 | 4 | guessed | An estimate |
| `k_rev` (with air from behind, the share of the deflection's moment that turns over) | | 0–3 | 1.0 | guessed | No published figure |
| `inertia_kgm2` (page only) | kg·m² | 0–100 | 0.4 | guessed | Below a flat-plate estimate (Roskam; NASA CR) |
| `wash_mps` (page only) | m/s | 0–100 | 30 | guessed | Prop wash over the tail at full power parked, a rough slipstream estimate |
| `damping_ratio` (page only) | | 0–10 | 0.4 | guessed | A rough estimate |

#### The ailerons' figures

Each aileron's figures (the two are mirror images).

| Figure | Unit | Bounds | C172 | Tag | Source |
|---|---|---|---|---|---|
| `area_m2` | m² | 0.05–10 | 0.85 | published | 9.15 ft² each, from 18.30 ft² total: Jane's All the World's Aircraft 1977-78, Cessna 172; not yet checked against a copy |
| `chord_m` (aft of the hinge) | m | 0.02–2 | 0.28 | guessed | 11 in, from a 64 in wing chord and a Frise hinge set back |
| `hinge_slope_per_rad` | /rad | 0–5 | 0.30 | guessed | A Frise aileron's range in NACA Report 868 and TR 721, not a 172's |
| `alpha_slope_per_rad` | /rad | −2–2 | 0.15 | guessed | As above |
| `up_deg` | ° | 1–60 | 20 | published | Cessna 172 aileron travel up, Service Manual figure as commonly quoted; not yet checked against a copy |
| `down_deg` | ° | 1–60 | 15 | published | Aileron travel down, as above |
| `wheel_deg` (the wheel's travel each way) | ° | 10–180 | 86 | derived | The CLS-E's own wheel travel: its soft limits at CLS2Sim's degrees per count |
| `rim_m` (grip radius of the wheel) | m | 0.05–0.5 | 0.15 | guessed | To be measured |
| `unbalance_inlb` | in·lb | −200–200 | 0 | guessed | The 172's ailerons carry balance weights; Cessna's balance limits are in the Structural Repair Manual 51-60-00, Table 1 |
| `stall_factor` | | 0–1 | 0.4 | guessed | No published post-stall hinge data |
| `k_rev` | | 0–3 | 1.0 | guessed | No published figure |
| `inertia_kgm2` (page only) | kg·m² | 0–100 | 0.1 | guessed | Each aileron about its hinge, rough |
| `wheel_inertia_kgm2` (page only) | kg·m² | 0–10 | 0.01 | guessed | The wheel's own, rough |
| `damping_ratio` (page only) | | 0–10 | 0.4 | guessed | Rough |
| `wash_mps` (page only) | m/s | 0–100 | 5 | guessed | Prop wash over the ailerons at full power parked; they sit outside most of the slipstream |

The linkage between wheel and ailerons is modelled as a smooth curve through
neutral and both stops, using `up_deg`, `down_deg` and `wheel_deg`.

#### Writing a profile for another aircraft

See [CONTRIBUTING.md](../CONTRIBUTING.md#writing-an-aircraft-profile).

## Output directory

Files in this section live in the output directory `Output/BrunnerDirect/`.
Because they are outside the plugin directory, they are preserved between
updates. To erase all settings and start clean, delete this directory and
install a clean version of the plugin.

### BrunnerDirect.state.json

Your own startup choices, in `X-Plane 12/Output/BrunnerDirect/BrunnerDirect.state.json`. Unlike
`BrunnerDirect.ini`, this file is not in the plugin folder, so updating or reinstalling the plugin
never replaces it. It is optional and you write it by hand; the plugin reads it once, when X-Plane
starts, after the ini.

```json
{"version":1,"auto_start":true,"force_feel":true}
```

| Key | Values | What it does |
|---|---|---|
| <a id="state-version"></a>`version` | 1 | Required. A file without `"version":1` is ignored whole. |
| <a id="state-auto_start"></a>`auto_start` | `true` or `false` | Overrides the ini's `auto_start`. With `true` the plugin centres the yoke by itself soon after X-Plane starts, so **keep clear of the yoke and read [SAFETY.md](../SAFETY.md) first**. |
| <a id="state-force_feel"></a>`force_feel` | `true` or `false` | Overrides the ini's `force_feel`: whether force feel is on when X-Plane starts. |
| <a id="state-button_commands"></a>`button_commands` | an object | What each control of the yoke does, as X-Plane commands: `"button_1": "sim/view/default_view"`. Nothing is bound until you write it. See [The yoke's buttons](buttons.md#commands-on-the-yoke-buttons). |

A key you leave out keeps the ini's value, so the ini is still where the defaults are and the state
file only changes what you name. The values are the words `true` and `false`, not 1, 0 or text.
A wrong value, or a key this version does not know, is ignored on its own and the other keys still
apply; `Log.txt` says which, for example
`BrunnerDirect.state.json: auto_start must be true or false; ignored`. When an override is in effect
the log says so: `auto_start 1 from BrunnerDirect.state.json (BrunnerDirect.ini: 0)`.

The plugin never changes your two settings: switching force feel on or off in the menu is for
this session only. The file also holds a developer setting the plugin writes itself; it keeps your
keys when it does ([advanced configuration](dev/advanced-configuration.md)).

### `aircraft/<id>.json`

Located under `X-Plane 12/Output/BrunnerDirect/aircraft/`. You create these
files yourself, one per aircraft, for personal customisation. For now they
only override the yoke's button commands.

The file is named after the aircraft. The plugin looks for, in this order, and uses the first it finds:

1. the name of the aircraft's **`.acf` file**, such as `Cessna_172SP.json` for `Cessna_172SP.acf`, then
2. the aircraft's **ICAO type code** as X-Plane reports it, such as `C172.json` or `A320.json`.

So a file for one aircraft wins over a file for its whole type: with both `Cessna_172SP.json` and `C172.json`, that Cessna uses the first, and
any other Cessna 172 add-on falls through to `C172.json`.

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

| Key | Values | What it does |
|---|---|---|
| <a id="aircraft-version"></a>`version` | 1 | Required. A file without `"version":1` is ignored whole, and `Log.txt` says so. |
| <a id="aircraft-button_commands"></a>`button_commands` | an object | Written exactly like the one in `BrunnerDirect.state.json`. See [The yoke's buttons](buttons.md#commands-on-the-yoke-buttons). |

**An aircraft's commands replace yours, they do not add to them.** Say your state file makes `button_1` run `sim/view/default_view`. An
aircraft file that gives `button_1` a command makes it run *that* instead, on that aircraft, and not both. Anything the aircraft file does
not mention stays as your state file has it, and a control the state file does not bind can be given a command here. To switch a command off
for one aircraft, give it `null` (or `""`, or `[]`).

**Pressed, let go and held are three separate entries, and each is replaced only by an entry of its own.** A control can have a command for
being pressed (`button_1`), one for being let go (`button_1_released`) and one for being held (`button_1_hold`). An aircraft entry changes
just the one it names, and **never unsets or replaces the other two**:

| Your state file has | The aircraft file gives | On that aircraft |
|---|---|---|
| `button_1`, `button_1_released`, `button_1_hold` | `button_1` | the aircraft's command when pressed; **your released and held commands still run** |
| the same three | `button_1_hold` | the aircraft's held command; your pressed and released commands still run |
| the same three | `button_1_released: null` | nothing when let go; your pressed and held commands still run |
| `button_1` only | `button_1_hold` | your pressed command **and** the aircraft's held command (the aircraft added one the state file did not have) |

So to give an aircraft a completely different button, name all three that you use: replace the ones it should change and set the others to
`null`. Naming only the pressed command leaves any `_released` or `_hold` command of yours in place.

The plugin reads the file when X-Plane loads or changes the aircraft, so after editing it, reload the aircraft. `Log.txt` says
which file was used, for example `Aircraft button commands: aircraft/C172.json (3 entries)`. Anything it cannot use is ignored on its own and
named there. A key other than `version` and `button_commands` is ignored entirely.
