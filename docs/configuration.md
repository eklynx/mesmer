# Configuration

Two files beside the plugin in `X-Plane 12\Resources\plugins\BrunnerDirect\`
set it up:

- `BrunnerDirect.ini`: startup settings. Read once, when X-Plane starts.
- `BrunnerDirect.profiles.json`: each aircraft's control-surface figures.
  Read again within two seconds of a change, mid-flight.

A third, `BrunnerDirect.feel.ini`, is written by the feel page's *Save*
button. It overlays the drive calibration and the elevator figures you saved
there on top of the two files above, each time X-Plane starts. Delete it to
go back to the files' values.

The release's copies are in this repository's [`config/`](../config) folder.

## BrunnerDirect.ini

One `key=value` per line, whole numbers only, `#` starts a comment. The file
is strict on purpose: an unknown key, a duplicate, a value out of bounds or a
missing required key stops the plugin from starting, and `Log.txt` says why
(`[BrunnerDirect] Configuration invalid; plugin not started: ...`). Without
the file at all the plugin still starts, with every default below, except
that `auto_start` is 0 and the feel page is off.

| Key | Release | Bounds | What it does |
|---|---|---|---|
| `schema_version` | 6 | 2 to 6 | The file's format. Required. |
| `auto_start` | **0** | 0 or 1 | 0: after X-Plane starts, the yoke waits for your *Retry* before it moves for the first time. 1: the plugin centres the yoke by itself as soon as X-Plane is steady and the yoke is still. Either way, after the first centring it looks after the yoke by itself. |
| `force_feel` | **0** | 0 or 1 | Whether force feel is on when X-Plane starts. Pitch and roll work either way. Optional; 0 if missing. |
| `workbench_port` | 8087 | 0, or 1024 to 65535 | The feel page's port on your local network. 0 turns the page off. If the port is taken, the menu says *Feel tuning unavailable: port 8087 in use* and the plugin tries again every 5 seconds. |
| `pitch_force_units_per_lb` | 222 | 50 to 1000 | How hard: the drive's force units per pound at the grip. Worked out from CLS2Sim's display units, not measured. It changes how hard the yoke pushes, never where it rests. |
| `roll_torque_units_per_nm` | 1000 | 100 to 10000 | The same for roll: drive units per N·m at the wheel. Worked out, not measured. |
| `pitch_force_gain_divisor` | 6 | 1 to 6 | The drive's own drag on pitch (its force gain is 1/this). 6 is the drive's own setting; lower values are untested. |
| `pitch_move_back_gain` | 180 | 180 to 720 | The drive's move-back gain on pitch. 180 is its own; higher values are untested. |
| `buffet` | 0 | 0 or 1 | A shake at the stall through the drive's vibration generator. Untested with the motors on. Optional. |
| `buffet_percent` | 10 | 0 to 100 | Buffet's strength, as a share of the plugin's own ceiling for it. Optional. |
| `read_only_roll_reverse` | 0 | 0 or 1 | Read-only mode only ([spec](read-only-mode.md)): 1 flips the roll direction of the learned travel. Optional. |
| `read_only_pitch_reverse` | 0 | 0 or 1 | Likewise for pitch. Optional. |
| `buffet_period` | 80 | 10 to 140 | The vibration generator's raw time setting; its frequency is not confirmed. Optional. |
| `pitch_trim_counts` | 0 | −600 to 600 | A bench-test offset of pitch's neutral, used only by the developer spring test. Leave at 0. Required. |
| `reference_airspeed_knots` | 316 | 40 to 400 | Kept for older files; not used by the surface models. Required. |

Keys from older versions of the plugin (`roll_spring_percent`,
`pitch_spring_percent`, `minimum_spring_percent` and others) are accepted,
ignored, and listed in `Log.txt` as *retired and ignored*.

`allow_unverified_yoke` is one of them: whether to drive a yoke model that is not
verified is now the Yoke (Brunner) menu's **Developer > Allow unverified yoke
models**, saved in `BrunnerDirect.state.json` in the plugin folder and off by
default ([yoke-presets.md](yoke-presets.md)). An `.ini` that still has the line
loads, and the line does nothing.

## BrunnerDirect.profiles.json

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

### How a profile matches an aircraft

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

### Every figure carries its evidence

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

### The elevator's figures

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

### The ailerons' figures

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

## Writing a profile for another aircraft

See [CONTRIBUTING.md](../CONTRIBUTING.md#writing-an-aircraft-profile).
