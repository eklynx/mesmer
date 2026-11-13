# Mesmer

**The control surfaces, simulated. The yoke, solved.**

![Status: alpha](https://img.shields.io/badge/status-alpha_preview-orange)
![Licence: Apache-2.0](https://img.shields.io/badge/licence-Apache--2.0-blue)
![Platform: Windows](https://img.shields.io/badge/platform-Windows_x64-lightgrey)
![Simulator: X-Plane 12](https://img.shields.io/badge/simulator-X--Plane_12-informational)

![The feel page in cruise: the elevator's hinge moments, the force across the yoke's travel and where the yoke rests](media/feel-page-cruise.jpg)

Mesmer is an X-Plane 12 plugin for the Brunner CLS-E force feedback yoke.
It simulates the aircraft's elevator and ailerons from the air X-Plane
reports over them: each surface's weight about its hinge, the air pressing
on it, and the trim tab. Every update it solves for the spring and the
resting position that put those loads in your hands, and sets the yoke's
motors to them. One plugin, nothing else to install: no CLS2Sim, no
bindings, no program running beside X-Plane.

> **Alpha preview.** It works well on the one yoke it has been developed
> on. It has not been tried on yours yet. Read [What works today](#what-works-today)
> and [SAFETY.md](SAFETY.md) before you install it.

## What it does

- **A simulator for each control surface.** X-Plane flies the aircraft.
  Mesmer simulates only what your hand would hold: the hinge moment of the
  elevator and of each aileron. Pitch firms up as the propwash builds on a
  run-up. Slow flight is lighter than cruise. In a bank the wheel's resting
  point leans a little into the turn.
- **A solver for the yoke.** The CLS-E's drive is a spring with an
  adjustable strength and centre. Each update Mesmer sets both, so the yoke
  rests exactly where the simulation says and the force under your hand is
  the simulation's, up to what the motors can give. Trimming moves where the
  yoke comes to rest. [How it works](docs/how-it-works.md).
- **Simple.** One plugin folder. Switch the yoke on and it is X-Plane's
  pitch and roll in every aircraft, with no bindings to set up. Without
  Mesmer, a Brunner yoke reaches X-Plane through CLS2Sim, a separate Windows
  program, and its connector plugin.
- **It looks after itself.** It finds the yoke, centres it, and gives you
  the controls. After a pause, an aircraft change, a power cycle or a
  replugged cable it carries on by itself. When it cannot, one line in the
  corner of the screen says what is wrong and what to do, and one click
  (*Retry*) fixes it.
- **The feel card in the sim.** A small card in X-Plane draws the elevator
  and ailerons as the plugin sees them, and how closely the yoke is matching
  them.
- **The feel page in a browser.** Open it on a tablet or laptop beside the
  cockpit. It shows the simulation live, has every figure as a knob, and
  keeps a large *Stop yoke motors* button at the top.

| In X-Plane | On a phone beside the yoke |
|---|---|
| ![The feel card in X-Plane: pitch and roll, each surface's air and how stiff the yoke is against the model](media/hud-card.png) | ![The feel page at phone width during a run-up](media/feel-page-phone-run-up.jpg) |

<details>
<summary>More pictures</summary>

The feel card in different moments of a flight (parked, take-off roll,
cruise, rolling into a bank, a stalled wing):

![The feel card's states](media/hud-card-states.png)

The feel page on approach, trimmed at 70 kt:

![The feel page on approach](media/feel-page-approach.jpg)

The feel page with the tail plane stalled and the yoke held fully back:

![The feel page with a stalled tail](media/feel-page-tail-stall.jpg)

The pitch card in X-Plane:

![The pitch card in the top right corner of X-Plane](media/hud-card-in-sim.jpg)

</details>

## What works today

This is a preview, so here is exactly where it stands.

| | Today |
|---|---|
| **Operating system** | Windows x64 only. A macOS build (universal, `build_macos.sh`) loads in X-Plane and reads the yoke through IOKit, but that reader has not been run on hardware yet: it is for development, not for flying. |
| **Simulator** | X-Plane 12 only. |
| **Yoke** | The original Brunner CLS-E yoke on USB, with controller firmware **v367**. On any other firmware the plugin never sends it anything that moves it: it runs [read-only](docs/read-only-mode.md), and a controller it cannot make sense of at all reads *Yoke not recognised - see X-Plane's Log.txt*. The USB bridges it opens are `25BB:0081` and `25BB:008B` (the CLS-E NG, which reports firmware 642: it is found and runs read-only, with no force feel); a yoke with a different USB bridge is not found at all and reads *Yoke off or unplugged*. [Tell us about yours](docs/compatibility-report.md). |
| **Tested on** | One yoke. |
| **Pitch and roll** | Every aircraft. |
| **The yoke's buttons and hat** | **Not supported yet.** The plugin does not read them, so they do not reach X-Plane through it. Use another device for buttons for now. |
| **Trim, rudder, throttle** | Not the yoke's job here. Use your usual controls. The plugin reads X-Plane's pitch trim and never changes it. |
| **Force feel** | **Off until you switch it on**, each time X-Plane starts ([First flight](#first-flight)). Every aircraft flies the Cessna 172's figures. The stock Cessna 172 SP gets the most detail (air over its tail and ailerons, propwash, the tail's angle and stall). Other aircraft get the 172's surfaces driven by indicated airspeed. |
| **Force calibration** | How hard the yoke pushes per pound is worked out, not yet measured with a scale. |
| **Buffet at the stall** | In the feel page, off by default, untested with the motors on. |

## Install

Before you start, close X-Plane and CLS2Sim. The plugin needs the yoke's USB
bridge to itself.

1. **By hand.** Download the latest pre-release zip from
   [Releases](https://github.com/antialias/mesmer/releases), check its
   SHA-256 against `SHA256SUMS`, and unzip it so that you have
   `X-Plane 12/Resources/plugins/BrunnerDirect/`. Then move CLS2Sim's own
   X-Plane plugin (`Resources/plugins/XPlane10Connector`) out of the plugins
   folder. Step by step: [docs/install.md](docs/install.md).
2. **With an agent.** Paste the prompt in
   [INSTALL-WITH-AN-AGENT.md](INSTALL-WITH-AN-AGENT.md) into Claude Code,
   Codex or a similar coding agent on your simulator PC. It finds X-Plane,
   checks the download, installs it, asks before it touches CLS2Sim's
   plugin, and talks you through your first flight.
3. **With the PowerShell installer.** The same steps without an agent:

   ```powershell
   powershell -ExecutionPolicy Bypass -File .\install.ps1 -WhatIf   # see what it would do
   powershell -ExecutionPolicy Bypass -File .\install.ps1
   ```

   [install.ps1](install.ps1) takes `-XPlaneRoot` if X-Plane is somewhere
   it cannot find.
4. **Plugin managers and the x-plane.org file library:** coming soon.

To remove it, close X-Plane and delete `Resources/plugins/BrunnerDirect`.
CLS2Sim works again as soon as its connector is back in the plugins folder.

## First flight

1. Switch the yoke on. Start X-Plane and load a flight.
2. The corner line reads **Yoke stopped - Retry to resume**. A fresh install
   waits for you before the yoke moves for the first time.
3. **Take your hands off the yoke.** Click the line, or choose
   *Plugins > Yoke (Brunner) > Retry*. The yoke centres itself (*Yoke
   centring - hands off*), then the line goes away. You now have pitch and
   roll.
4. Fly. With force feel off, the yoke's motors are off and it moves freely.
5. When you want force feel, park, keep a light hand on the yoke and choose
   *Plugins > Yoke (Brunner) > Force feel*.
   *Stop yoke motors* in the same menu turns it off at once.

The full walk-through, with what you should feel at each step:
[docs/first-flight.md](docs/first-flight.md).

## Safety in brief

The yoke has motors that can push hard: the drive's own protection limit is
roughly 20 lb at the grip. Keep your hands off when it centres. To stop force
at once, use *Stop yoke motors* in the menu, bind the command
`brunner_direct/stop_motors` to a button, or press *Stop yoke motors* on the
feel page. Pitch and roll keep working with the motors off. This is alpha
software with no warranty. Read [SAFETY.md](SAFETY.md).

## Help wanted

This preview is published to find out who else wants it, and to find the
people who can take it further. If any of this is you, please say hello.

- **Other Brunner hardware.** The CLS-E NG, the CLS-E Mk II, dual-yoke
  setups, the CLS-P pedals, the joysticks and the rest of Brunner's range.
  The first step is a short
  [compatibility report](docs/compatibility-report.md) from your hardware.
  → [Other Brunner hardware](https://github.com/antialias/mesmer/discussions/categories/other-brunner-hardware)
- **Aircraft profiles.** Every aircraft flies the Cessna 172's figures
  today. A profile is a small JSON block of figures, each with its source:
  elevator and aileron areas, chords, travel, trim tab and balance. If you
  know an aircraft's figures, or have the books, you can write one.
  → [Aircraft profiles](https://github.com/antialias/mesmer/discussions/categories/aircraft-profiles)
- **Other simulators.** The surface models, the solver and the yoke
  supervisor are plain C++ with no X-Plane in them. Microsoft Flight
  Simulator, Prepar3D, DCS and others could use them.
  → [Other simulators](https://github.com/antialias/mesmer/discussions/categories/other-simulators)
- **Bigger cockpits.** Two yokes, pedals, a trim wheel, a full home
  cockpit, several PCs. Tell us what your setup needs and show us what
  you've built.
  → [Cockpit builds](https://github.com/antialias/mesmer/discussions/categories/cockpit-builds)
- **Force feedback beyond Brunner.** The hard part here is modelling what a
  control surface does to the pilot's hand, and that has nothing to do with
  any one brand. Perhaps it belongs in a general open-source home for force
  feedback controls. If you build or own other force-feedback hardware,
  let's talk.
  → [Force feedback beyond Brunner](https://github.com/antialias/mesmer/discussions/categories/force-feedback-beyond-brunner)

Testers are as welcome as developers. A flight report with what you felt is
useful.

## Documentation

- [Install](docs/install.md), [first flight](docs/first-flight.md),
  [troubleshooting](docs/troubleshooting.md)
- [Configuration and aircraft profiles](docs/configuration.md)
- [How it works](docs/how-it-works.md): the elevator and aileron models and
  the solver
- [Safety](SAFETY.md), [changes](CHANGELOG.md)

## Contributing

Bug reports, compatibility reports, profiles and pull requests are all
welcome. Please read [CONTRIBUTING.md](CONTRIBUTING.md) first: the plugin is
developed in a private repository, so a pull request here is reviewed here,
then brought across with you credited as co-author, and closed with a link
to the release that carries it.

Questions go to [Discussions](https://github.com/antialias/mesmer/discussions).
Bugs go to [Issues](https://github.com/antialias/mesmer/issues).

## Unofficial

Mesmer is an independent project. It is **not affiliated with, endorsed
by or supported by Brunner Elektronik AG or Laminar Research**. "Brunner",
"CLS-E" and "CLS2Sim" are named only to say which hardware and software this
works with or replaces. "X-Plane" is a trademark of Laminar Research. Please
don't ask Brunner for support with this plugin; ask here.

Inside X-Plane the plugin keeps its working names: the `BrunnerDirect` folder,
the *Yoke (Brunner)* menu and the `brunner_direct/*` commands and datarefs.

## Licence

[Apache-2.0](LICENSE). See [NOTICE](NOTICE) for third-party notices.
