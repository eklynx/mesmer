# Changelog

Versions are `0.1.0-alpha.N` while Mesmer is a preview. Each is a GitHub
pre-release with a Windows zip, its SHA-256 in `SHA256SUMS`, and a build
provenance attestation.

## Unreleased

- **The NG's elevator weight pushes the right way.** Its pitch encoder runs the other way from the CLS-E's,
  but the plugin wrote the model's spring centre and constant force to the controller unchanged, so the
  weight pushed aft and no existing setting could fix it. A model now has `roll_spring_sign` and
  `pitch_spring_sign` (the controller's spring frame against the encoder, default -1); writes are the
  model's times `encoder_sign` x `spring_sign`, which is 1 (unchanged) on the CLS-E and mirrors the NG's
  pitch (`pitch_encoder_sign` is now +1 there).
- **The CLS-E NG inverts pitch by default.** Its model now ships `pitch_input_reverse: true` (roll was
  already reversed). Set it back to `false` for `cls-e-ng` in `BrunnerDirect.yokes.json` to undo it.
- **The saved Developer choice moves to X-Plane's Output folder.** "Allow unverified yoke models" is
  now saved in `Output/BrunnerDirect/BrunnerDirect.state.json`, so updating the plugin does not
  replace it. An existing `BrunnerDirect.state.json` in the plugin folder is read once, saved to the
  new place, and left where it was (delete it if you like).
- **A macOS install script.** `src/scripts/install_macos.sh` installs the staged plugin into
  X-Plane (folder from `~/Library/Preferences/x-plane_install_12.txt`), backing up an existing
  one outside `plugins/` and keeping your ini, feel and yoke overrides. `--dry-run`, `--version`,
  `--xplane-dir` and `--force` (see `docs/macos-build.md`).
- **Read-only mode leaves pitch and roll to X-Plane.** A yoke in read-only mode no longer has the
  plugin override X-Plane's pitch and roll: both override datarefs are set to 0 and X-Plane reads
  the yoke's own USB joystick (the CLS-E NG's shows X and Y). A driven (read-write) yoke is
  unchanged: both overrides at 1 and the plugin feeds pitch and roll. The read-only status line
  still reports how far the learned travel has come.
- **Merged upstream alpha.3.** The yoke's buttons through vJoy, the look-down commands, the
  disabled-USB offer and the HUD changes are in. The Windows-only parts sit in this layout:
  `xplane/look.hpp` and the disabled-bridge check in `hid/win/hid_win.cpp`. The plugin no
  longer reads the drives' control position (0x2106), as upstream. Windows parts not built here.
- **The virtual joystick is Windows-only code.** Everything that presses the yoke's buttons on
  vJoy (`buttons.hpp`, `vjoy_win.hpp`, the new `yoke_buttons.cpp`, and its tests, a Windows-only
  `brunner_buttons_test`) is in `src/hid/win/`, reached through `hid/yoke_buttons.hpp`; the Mac
  build has no button code and no longer reads the button word each cycle. The NG's own
  joystick (6 buttons, hat, X/Y) is X-Plane's and never went through vJoy.
- **Yoke models are shipped files.** `config/yokes/cls-e.json` (the original, exactly what was
  compiled in before) and `cls-e-ng.json` are packaged in the plugin's `yokes/` folder and loaded
  at start, one complete file per model. `BrunnerDirect.yokes.json` stays as the optional
  overrides file (kept by the installer); `BrunnerDirect.yokes.example.json` is gone. A missing
  or refused folder falls back to the compiled-in models (kept equal to the files by a test).
- **Natural drift allowed when centring.** A sensitive yoke wanders a little, so a centring
  now counts as settled when it holds within ±0.1 % of the axis's travel of where the hold began
  (it was a fixed 2 counts between samples). The setting is `centre_drift_percent` in each yoke's
  config (`config/yokes/*.json`): a decimal percent of travel, 0.1 by default, 0 to 5 (0 is the
  old behaviour), so it can be tuned finely per model (0.05, 0.125, ...).
- **A model is left as it was found.** The plugin wrote the CLS-E's resting settings (spring
  curve, spring strength, pitch drag, and a force mode with the reference-at-power-on bit
  cleared) to the CLS-E NG at every rest and stop, and the NG kept them: it stopped
  referencing itself and read-only mode waited on it forever. Presets now carry the model's
  own `rest` settings (the NG's are its factory values); a new model without them has them
  read from the yoke and put back. **Developer > Reset yoke** (new, last item) stops the
  motors and writes the model's resting settings, read back, without a power cycle; it needs
  the Developer toggle like any write. Read-only now says *stopped, not referenced* (and what
  to do) for a quiet unreferenced yoke instead of *homing*.
- **CLS-E NG: pitch no longer refuses to rest.** Force feel started and then faulted with
  `Readback mismatch ... register=2116:0 expected=5468 actual=5300`: the NG holds
  a resting point only up to 5300 counts on pitch, and the preset's travel (5468, its end-stop
  flag) asked for more. The NG's travel is now the range it holds (pitch 5300, roll 7350, the
  latter inferred from its homing sweep), editable in `BrunnerDirect.yokes.json`.
- **Source layout.** `src/` is grouped by idea: `brunner/` (the yoke),
  `force/` (the simulation's force models), `hid/` (with `win/` and `mac/`),
  `xplane/` (datarefs), and `ui/` (`menu/`, `view/`, `workbench/`). `plugin.cpp`
  keeps the commands, the flight loop and start/stop, and calls the
  new modules. No behaviour change; a build on Windows is the first check.
- **A pause no longer ends force feel.** The motors stay on and hold the last
  forces through a pause, so there is no delay on resume (it used to rebuild
  the whole session). Real lapses still release the motors. Not yet run on
  hardware.
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
  `src/hid/usb_ids.hpp`: `25BB:0081` as before, and `25BB:008B` (the CLS-E NG),
  and, read-only, any other Brunner (`25BB`) bridge when no listed yoke is present.
- **Yoke presets: per-model travel, force limits and settings.** What differs between yoke models (travel counts, force ceilings, target torque, wheel travel, status words, directions) is a preset chosen by the USB id, never the firmware, with the built-in CLS-E and CLS-E NG and an editable `BrunnerDirect.yokes.json` for more. Forces stop at the lower of the preset's ceiling and the controller's own limit register; travel scales the force models. A model that is not verified (the NG) is read-only until **Developer > Allow unverified yoke models** is turned on (saved in `BrunnerDirect.state.json`, off by default; an `allow_unverified_yoke` line in `BrunnerDirect.ini` is retired and ignored), and its first writes are one setting written back unchanged. A firmware outside a preset's verified list is only noted in the log and the Force Monitor. See `docs/yoke-presets.md`.
- **Read-only mode for a yoke the plugin does not drive.** Read-only (see
  `docs/read-only-mode.md`) applies to a Brunner bridge no preset names (an
  *Unknown yoke*, whatever its firmware) and to a model that is not verified until
  the Developer toggle is on. Nothing is ever written to it, the position feeds
  X-Plane's pitch and roll through a travel learned as you move the yoke to its
  stops (experimental), and a menu item surveys its registers. Firmware never
  decides this: a firmware update changes nothing but a note, and the learned
  range is kept per USB id (`read-only-travel.json` is now version 3; an old file
  is relearned once). Read-only input and learning use only referenced,
  fault-free samples, so the NG's power-on homing sweep is neither learned nor
  sent to X-Plane.
- **macOS and the log.** On macOS the plugin finds its own folder (it was given
  classic `Volume:Folder:` paths), and `Log.txt` now says why a yoke was
  refused. The reader is split so the Windows and macOS HID access are small separate files; see
  `docs/hid-bridge.md`.

## 0.1.0-alpha.3

The yoke's buttons now reach X-Plane through vJoy. When Windows has switched the yoke's USB off, the plugin now says so and turns it back on when you click.

- **The yoke's buttons, through vJoy.** The plugin reads the yoke's buttons and presses the matching buttons of vJoy device 1. Bind them in *Settings > Joystick* under `vJoy - Virtual Joystick`. You need to install Brunner's vJoy 2.2.2.0 and set device 1 to 32 buttons and no axes, once. [The yoke's buttons](docs/buttons.md) has the steps and says which button is which. Without vJoy the yoke flies as before, and Log.txt gives the reason after `Yoke buttons off:` ([troubleshooting](docs/troubleshooting.md#the-yokes-buttons-dont-reach-x-plane)).
- **Leave the three panel buttons alone in flight.** Panel buttons 12 to 14 also work the yoke's own menu. On the tested yoke, one press switched its motors on while the plugin was driving it, and the plugin stopped with a fault. Don't bind them.
- **Look down from the yoke.** The new commands `brunner_direct/look_down` and `brunner_direct/look_up` step between ahead, half down and the panel. Ahead is X-Plane's default view. Half down and the panel are Quick Looks 2 and 1, which you save once per aircraft. [The yoke's buttons](docs/buttons.md#commands-worth-a-button) lists these with the plugin's other commands.
- **The yoke's USB switched off in Windows.** Windows keeps a disabled USB bridge off through unplugging, power cycles and restarts. When the yoke's bridge is disabled, the status line now reads *Yoke's USB is off in Windows - click here to turn it on* instead of *Yoke off or unplugged*, even with routine status hidden. Click the line, choose *Plugins > Yoke (Brunner) > Turn the yoke's USB on*, or run `brunner_direct/usb_on`; Windows may ask to allow `pnputil`. While the USB comes back the line reads *Yoke's USB turning on - wait a moment*, and if it is still off after 15 seconds, the offer to turn it on comes back.
- **The HUD's pitch card draws the float.** At rest in cruise, the card drew about 9.5 lb on the yoke where the model, the feel page and the yoke hold none. It now shows the elevator's float in the feel page's green, with words such as "floats 9.7 lb forward". Only the drawing changed; what the plugin sends the yoke did not.
- **The HUD's roll card says yoke.** It used to say wheel. Its lines now read "Saturated · yoke 23% as stiff", "Saturated · yoke under 1% as stiff" and "Force feel off · the yoke is free", the same words as the pitch card.
- **The README on SkunkCrafts Updater.** The updater (version 4 or later) keeps an installed Mesmer current and never overwrites `BrunnerDirect.ini`; the profiles file is replaced with each release. Run updates with X-Plane closed. With X-Plane open, the update stops at the plugin file and leaves the old plugin working; close X-Plane and run the update again.
- **Building from source.** On Windows the plugin now also links cfgmgr32, shell32 and advapi32. You don't need vJoy to build: the plugin loads `vJoyInterface.dll` at run time from the folder vJoy's installer records. The reader reads the button word in every cycle and no longer reads the drives' control position, so the Developer menu's Roll and Pitch lines and the once-a-second Log.txt line no longer show it.

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
