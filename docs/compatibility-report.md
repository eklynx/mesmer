# Compatibility report

This version drives the original Brunner CLS-E yoke on USB (verified on
controller firmware **v367**; another firmware is driven too, with a note in the
log). The CLS-E NG (`25BB:008B`) is read-only until you turn on the Developer
toggle. A Brunner bridge the plugin has no preset for is opened read-only as an
*Unknown yoke*, so nothing is written to it. A controller whose settings it does
not recognise reads **Yoke not recognised - see X-Plane's Log.txt**, before
anything that could move the yoke is sent. A device whose USB vendor is not Brunner's
is not found at all and reads **Yoke off or unplugged - check its power switch and
USB cable**. The bridges this version opens by id are `25BB:0081` and `25BB:008B`
([the list](hid-bridge.md)).

A short report from your hardware is the first step to supporting it. It
takes ten minutes and nothing in it moves the yoke.

## What to send

[Open a compatibility report](https://github.com/antialias/mesmer/issues/new?template=compatibility_report.yml)
with:

1. **The model.** CLS-E, CLS-E NG, CLS-E Mk II, a joystick, pedals, or
   something else; USB or Ethernet; roughly when you bought it.
2. **Its firmware, from CLS2Sim.** With X-Plane closed, start CLS2Sim and
   connect to the yoke. Note the controller firmware version it shows for
   each axis (pitch and roll), and CLS2Sim's own version.
3. **The status text** Mesmer showed, exactly.
4. **The plugin's lines from `Log.txt`.** Close CLS2Sim, start X-Plane with
   the yoke on, wait a minute, then copy every line containing
   `[BrunnerDirect]` from `X-Plane 12\Log.txt`.
5. **The USB IDs.** In Windows' Device Manager, find the yoke under *Human
   Interface Devices* (it appears and disappears as you switch the yoke on and
   off), open *Properties > Details > Hardware Ids*, and copy the lines that
   start with `HID\VID_`.
6. **What else you'd want it to do**: dual yokes, pedals, another simulator.

Leave out serial numbers and licence keys; we don't need them.

## What happens next

We can't promise support for any hardware we don't have. A report tells us
how far your hardware is from the one this plugin knows, and whether someone
with that hardware can help develop and test it. If you're happy to test
builds on your hardware, say so in the report.

Discussion of other hardware is in
[Other Brunner hardware](https://github.com/antialias/mesmer/discussions/categories/other-brunner-hardware).
