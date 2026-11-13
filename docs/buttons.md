# The yoke's buttons

The yoke's own USB connection carries no game controller, so X-Plane can't
see its buttons by itself. The plugin reads them and presses the matching
buttons of a virtual joystick, vJoy device 1. X-Plane lists that joystick in
its Joystick screen as `vJoy - Virtual Joystick`, and you bind its buttons
like any other controller's. Nothing runs beside X-Plane: vJoy is a driver,
and the plugin feeds it.

**This is Windows only.** vJoy is a Windows driver, so the macOS build has no button code:
it neither reads the yoke's buttons nor logs about them (`src/hid/yoke_buttons.hpp` is the
seam). Pitch, roll and force feel are not affected. The CLS-E NG needs none of this: its USB
shows an ordinary joystick (6 buttons, a hat and X/Y), which X-Plane lists by itself.

The buttons work while X-Plane runs with the plugin. All of them release
whenever the link to the yoke drops.

## Set up vJoy once

1. Install Brunner's build of vJoy, version 2.2.2.0. Brunner signs the
   installer and Microsoft signs the driver, so it loads with Secure Boot
   on. CLS2Sim ships it as `Virtual joystick driver\vJoySetup.exe` in its
   folder, and it is on [Brunner's vJoy releases](https://github.com/BrunnerInnovation/vJoy/releases).
   Windows may want a restart afterwards.
2. Set device 1 to 32 buttons and no axes. In an administrator command
   prompt:

   ```bat
   "C:\Program Files\vJoy\x64\vJoyConfig.exe" 1 -f -a -b 32
   ```

   `-a` with nothing after it means no axes. Without it vJoy gives the
   device eight, and X-Plane asks you to calibrate each one.
3. Start X-Plane with the yoke on. X-Plane's Log.txt says
   `Yoke buttons: pressing vJoy device 1's 32 buttons`.
4. In *Settings > Joystick*, pick `vJoy - Virtual Joystick`, press a yoke
   button, and bind the button X-Plane highlights.

Without vJoy the yoke flies exactly as before. Only the buttons are missing,
and Log.txt says why: see [troubleshooting](troubleshooting.md#the-yokes-buttons-dont-reach-x-plane).

## Which button is which

On the one yoke tested so far, an original CLS-E, the plugin presses vJoy
button *n* for the yoke's button in slot *n*:

| Control | vJoy button |
|---|---|
| Left grip, index finger | 1 |
| Left grip, thumb left | 2 |
| Left grip, thumb right | 3 |
| Left grip rocker, back | 4 |
| Left grip rocker, forward | 5 |
| Right grip, index finger | 7 |
| Right grip, left thumb | 8 |
| Right grip, right thumb | 9 |
| Right grip rocker, back | 10 |
| Right grip rocker, forward | 11 |
| Panel buttons: middle, right, left | 12, 13, 14 |

**Leave the three panel buttons alone in flight, and don't bind them.**
They also work the yoke's own menu. On the tested yoke, one press switched
its motors on while the plugin was driving it, and the plugin stopped with a
fault.

If your yoke's buttons land elsewhere, please say so in a
[compatibility report](compatibility-report.md).

## Commands worth a button

Any X-Plane command can go on a yoke button. The plugin adds these:

| Command | What it does |
|---|---|
| `brunner_direct/stop_motors` | Stops the yoke's motors: force feel off, pitch and roll keep working (see [Safety](../SAFETY.md)). |
| `brunner_direct/look_down` | Looks down a step: ahead, then half down, then the panel. |
| `brunner_direct/look_up` | Looks back up the same steps. |
| `brunner_direct/mark_feel` | Marks this moment on the feel page. |
| `brunner_direct/compare_feel` | Switches the feel page between *Kept* and *Trying*. |

The look steps use X-Plane's own views. Ahead is the default view; half down
and the panel are Quick Looks 2 and 1, which you save once per aircraft:
look where you want, then save it from *Views > Quick Look*, or with the
commands `sim/view/quick_look_1_mem` (half down) and
`sim/view/quick_look_0_mem` (the panel). A view moved with the mouse alone
isn't seen; the next step goes from the last view a command chose.
