# Safety

Mesmer drives the motors of a force-feedback yoke. Please read this page
before you install it.

## The yoke can push hard

The CLS-E's motors are strong enough to hurt a finger caught between the yoke
and the panel, or to pull a wrist that is braced the wrong way. The drive has
its own protection limit, roughly **20 lb (about 90 N) at the grip**. That
figure is worked out from Brunner's display units, not measured. The plugin
keeps everything it asks of the drive below about 18 lb, but the drive can
still deliver forces of that size.

- **Hands off when it centres.** Each time the yoke centres, the line in the
  corner of X-Plane reads *Yoke centring - hands off* before anything
  moves, and the yoke must have been still for a full second first. Let go and
  let it finish. It takes a few seconds.
- **The yoke moves by itself during flight with force feel on.** That is the
  point: it rests where the aircraft's elevator and ailerons would put it,
  and that place changes with airspeed, power and trim. Parked with the
  engine off, the Cessna's elevator weight pushes the yoke forward.
- **Keep the area around the yoke clear**: cables, cups, children and pets.

## How to stop at once

Each of these switches force feel off and stops the motors. Pitch and roll
keep working, so you can keep flying with a free yoke.

- **The menu:** *Plugins > Yoke (Brunner) > Stop yoke motors*.
- **A button:** bind the X-Plane command `brunner_direct/stop_motors`
  (*Stop yoke motors: force feel off, pitch and roll keep working*) to a key
  or a joystick button in X-Plane's control settings. We recommend doing
  this before your first force-feel flight. (The yoke's own buttons don't
  reach X-Plane yet, so use another device.)
- **The feel page:** the red *Stop yoke motors* button at the top.
- **The yoke's power switch.** Switching the yoke off always stops it. The
  plugin notices, waits, and centres it again when you switch it back on.

If a stop cannot be confirmed by the drive, the line reads
*Yoke fault - switch the yoke off, then Retry*. Do that.

## What the release does by default

- **The yoke waits for you.** After you start X-Plane the line reads *Yoke
  stopped - Retry to resume*, and nothing moves until you click it or choose
  *Retry* (*Re-centre yoke (hands off)* when it isn't stopped) in the menu.
  After that first centring the plugin looks after the yoke by itself for the
  rest of the session: it centres it again, without asking, after a power
  cycle or a replugged cable. You can change this with `auto_start` in
  [configuration](docs/configuration.md).
- **Force feel is off.** With force feel off the motors are off after
  centring and the yoke moves freely. Force feel comes on only when you
  switch it on, and it is off again at the next X-Plane start.
- **The motors stop** when force feel is switched off, when X-Plane pauses,
  when you change aircraft, and when a controller reports a fault. After a
  fault the plugin tries to recover at most twice by itself; after that it
  waits for you to press *Retry*.
- **The feel page listens on port 8087** on your local network only. It
  answers private network addresses and refuses the internet. Anyone on your
  local network can open it, see the yoke and switch force feel on or off.
  If you share your network, set `workbench_port=0` in `BrunnerDirect.ini`
  to turn it off.

## What is untested

- **Your yoke.** One CLS-E has been flown with this plugin.
- **Force feel at full strength.** In cruise the model asks for more
  stiffness than the drive can give, so the drive runs at its own limit.
  That range has had little time on the hardware.
- **The aileron model** is newer than the elevator model and has had less
  flying.
- **Buffet** at the stall is untested with the motors on and off by default.
- **What the drive does if the PC crashes, the USB cable is pulled or
  Windows hangs while the motors are running.** The plugin cannot act then;
  what the drive does on its own has not been verified. Keep the power
  switch within reach.
- **How hard "1 lb" is.** The pound figures on the feel page are worked out
  from Brunner's display units and have not been checked with a scale.
- **Other firmware.** Firmware is not a gate: a model is driven whatever its
  firmware, and the log notes when it is one the model's preset was not verified
  on. Before anything moves, the plugin checks the protection limits and spring
  mode against the model's, and, for a model nobody has verified, writes one
  setting back unchanged and reads it back. A yoke it cannot identify is only read.

## No warranty

Mesmer is alpha software, provided under the
[Apache License 2.0](LICENSE) "as is", without warranties or conditions of any
kind. You use it at your own risk. It is not made, endorsed or supported by
Brunner Elektronik AG, and using it may affect your support from Brunner.

Found something unsafe? Please [open an issue](https://github.com/antialias/mesmer/issues/new/choose)
with *safety* in the title.
