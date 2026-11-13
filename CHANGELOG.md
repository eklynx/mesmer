# Changelog

Versions are `0.1.0-alpha.N` while Mesmer is a preview. Each is a GitHub
pre-release with a Windows zip, its SHA-256 in `SHA256SUMS`, and a build
provenance attestation.

## Unreleased

- **CLS-E NG: centring no longer refuses at idle.** The NG idles with its motors
  enabled (status 0x1427) in a force mode of 0x0605, which the CLS-E flow read as
  a fault and an unknown spring mode. Presets now carry `spring_mode` and
  `idle_enabled`; the NG's stopped word is 0x0421 (all seen on the first attempt
  to drive it). Still unverified on a full run.
- **A macOS development build.** `build_macos.sh` builds a universal
  (arm64 and x86_64) plugin that loads in X-Plane 12 on a Mac. It reads the yoke
  through IOKit, which has not been run on hardware yet, so it is not a
  release. See `docs/macos-build.md`.
- **More than one USB bridge id.** The plugin now opens the bridges listed in
  `src/usb_ids.hpp`: `25BB:0081` as before, and `25BB:008B` (the CLS-E NG).
- **Yoke presets: per-model travel, force limits and settings.** What differs between yoke models (travel counts, force ceilings, target torque, wheel travel, status words, directions) is a preset chosen by the USB id, never the firmware, with the built-in CLS-E and CLS-E NG and an editable `BrunnerDirect.yokes.json` for more. Forces stop at the lower of the preset's ceiling and the controller's own limit register; travel scales the force models. A model that is not verified (the NG) is read-only until **Developer > Allow unverified yoke models** is turned on (saved in `BrunnerDirect.state.json`, off by default; an `allow_unverified_yoke` line in `BrunnerDirect.ini` is retired and ignored), and its first writes are one setting written back unchanged. A firmware outside a preset's verified list is only noted in the log and the Force Monitor. See `docs/yoke-presets.md`.
- **Read-only mode for a yoke the plugin does not drive.** The NG reports
  firmware 642, not the 367 the plugin drives, so it is **read-only** (see
  `docs/read-only-mode.md`): nothing is ever written to it, the position feeds
  X-Plane's pitch and roll through a travel learned as you move the yoke to its
  stops (experimental), and a menu item surveys its registers. Force feel is
  not available on it. The firmware 367 behaviour is unchanged. Read-only input and
  learning use only referenced, fault-free samples, so the NG's power-on homing
  sweep is neither learned nor sent to X-Plane.
- **macOS and the log.** On macOS the plugin finds its own folder (it was given
  classic `Volume:Folder:` paths), and `Log.txt` now says why a yoke was
  refused. The reader is split so the Windows and macOS HID access are small separate files; see
  `docs/hid-bridge.md`.

## 0.1.0-alpha.2

The first release built from the public repository, and so the first with a
build provenance attestation. The plugin flies as alpha.1 does.

- **Check where the zip came from.**
  `gh attestation verify Mesmer-0.1.0-alpha.2.zip --repo antialias/mesmer`
  shows GitHub built it from this source. alpha.1 was built while the
  repository was private and has no attestation.
- **Elevator pages: flying slower than it can fly.** On the elevator page, set
  to flying at about 10 kt or less, the elevator ran to a stop as if the air
  came from behind. The page's stand-in for X-Plane let the tail's angle grow
  without limit as the airspeed went. Under 35 kt it now fades into the
  aircraft with no airspeed: the tail in the prop wash alone, the elevator
  hanging on its weight. The feel page and the stall simulator carry the same
  model text. In the aircraft the plugin reads the tail's angle from X-Plane,
  so the yoke was never affected.
- **The README** leads with what Mesmer is: a simulator for each control
  surface and a solver for the yoke.

## 0.1.0-alpha.1

The first public preview: the plugin as it is flown today, published to find
testers and developers. No features were added for it.

**Requirements.** Windows x64, X-Plane 12, and the original Brunner CLS-E yoke
on USB with controller firmware v367. CLS2Sim must be closed. Tested on one
yoke.

**The yoke as a control**

- Pitch and roll in every aircraft, written straight into X-Plane, with no
  bindings to set up and no program beside X-Plane.
- Finds the yoke and keeps looking, once a second, if it is off or
  unplugged.
- Centres the yoke when X-Plane is running steadily and the yoke has been
  still for a second, after showing *Yoke centring - hands off*.
- Carries on by itself through pause and resume, aircraft changes, the yoke
  switched off and on, and the USB cable pulled and replugged.
- After a controller fault, stops the motors and recovers by itself up to
  twice, then asks for one *Retry*.
- One status text, with what is wrong and what to do, in the menu, a
  clickable corner line, the feel card and the feel page.
- Refuses controllers with other firmware before sending anything that
  moves them.

**Force feel (experimental, off by default)**

- Pitch follows a model of the elevator's hinge moment: its weight, the air
  over the tail including propwash, the trim tab, the tail's angle of attack
  and stall, and air from behind.
- Roll follows a model of both ailerons' hinge moments through the wheel's
  linkage, from the air over each wing's own panels.
- A solver sets the drive each update so the yoke rests exactly where the
  model says, with the model's force under your hand up to the drive's own
  limit, kept about 11% under its protection limit.
- Trim moves the yoke's resting point; X-Plane's trim is read, never written.
- Every aircraft flies the Cessna 172 SP's figures from
  `BrunnerDirect.profiles.json`, each with its value, tag and source. The
  file is reread within two seconds of a change.
- Optional stall buffet, off by default and untested with the motors on.

**Seeing and tuning it**

- The feel card in X-Plane: the elevator and ailerons as the plugin sees them.
- The feel page on port 8087 of the local network: the model live, every
  figure as a knob, Save, Undo, Revert, marks, two minutes of history, and
  *Stop yoke motors*.
- Commands for binding: `brunner_direct/stop_motors`, `recenter`,
  `toggle_force_feel`, `force_feel_on`, `force_feel_off`,
  `toggle_status_line`, `pin_hud`, `mark_feel`, `compare_feel`.

**Release defaults.** The yoke waits for your first *Retry* after X-Plane
starts (`auto_start=0`), and force feel is off (`force_feel=0`).

**Known limits.** The yoke's own buttons and hat are not read. The force
calibration (pounds at the grip) is worked out, not measured. Only one
aircraft has its own profile.
