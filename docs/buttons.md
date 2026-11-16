# The yoke's buttons

X-Plane can't see the yoke's buttons by itself: the yoke's own USB connection carries them in a form X-Plane does not read. The
plugin reads them and does one of two things with them, depending on your yoke:

| Action | What happens | Where |
|---|---|---|
| **vJoy** | The plugin presses the matching buttons of a virtual joystick, vJoy device 1. X-Plane lists it in its Joystick screen as `vJoy - Virtual Joystick`, and you bind its buttons like any other controller's. | the original CLS-E, on Windows |
| **Direct** | The plugin runs the X-Plane commands you name in `button_commands`, no binding in X-Plane needed: [Commands on the yoke buttons](#commands-on-the-yoke-buttons), and [different ones for one aircraft](configuration.md#aircraftidjson). | the CLS-E NG, and any yoke on a system with no virtual joystick |

Each yoke model's preset says which action it uses ([yoke presets](dev/yoke-presets.md)); there is nothing to set. See
[The button action](dev/advanced-configuration.md#the-button-action), which also says what each needs.

**vJoy is a Windows driver**, so it only works there for now; elsewhere the CLS-E's buttons use `Direct` too and `Log.txt` says so. Pitch, roll and
force feel are not affected by any of this. The buttons work while X-Plane runs with the plugin, and all of them release whenever the
link to the yoke drops.

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

**On the vJoy joystick.** On the original CLS-E, the one yoke tested so far, the plugin presses vJoy button *n* for the yoke's button in
slot *n*. The same numbers are the names `button_n` for `button_commands`:

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

**The hat.** The original CLS-E has no hat switch. The CLS-E NG has one, and a CLS-E MK2 is reported to as well (Mesmer has no preset for the MK2 yet).

**The NG** has one grip, whose first five controls are the CLS-E's left grip slots (checked on an NG's own register, 2026-11-16): trigger
1, top left 2, top right 3, rocker down 4, rocker up 5, the same vJoy numbers. Its hat push (6) and its hat are in the NG's own button
record, which Mesmer reads for the NG. The hat push is vJoy button 6 and the hat's eight directions are vJoy buttons 17 to 24 (up
first, then clockwise).

**Leave the three panel buttons alone in flight, and don't bind them.**
They also work the yoke's own menu. On the tested yoke, one press switched
its motors on while the plugin was driving it, and the plugin stopped with a
fault.

If your yoke's buttons land elsewhere, please say so in a
[compatibility report](compatibility-report.md).

## Commands on the yoke buttons

With the `Direct` action (the CLS-E NG's), each control runs the X-Plane commands you write in `button_commands` in
`Output/BrunnerDirect/BrunnerDirect.state.json`, one entry per control: `"button_1": "sim/view/default_view"`. Nothing is bound until you
write it. Add `_released` to the name to run something when the control is let go, and `_hold` for a command that should act for as long
as it is held (trim, panning the view). An entry can be one command, a list of them (they run in order), or `null` for none. The names, the
yoke's friendly names for its controls and a Cessna 172 example are in [Button commands](dev/advanced-configuration.md#button-commands).

**Different commands for one aircraft.** Put a file named after the aircraft, such as `C172.json`, in the `aircraft/` folder beside the
state file, with `"version": 1` and a `button_commands` section, and its commands replace the state file's for that aircraft, and only
that one. It replaces; it does not add: if both files give `button_1` a command, only the aircraft's runs. Everything it leaves out keeps
your state file's commands, and `null` switches one off for that aircraft. Pressed (`button_1`), let go (`button_1_released`) and held
(`button_1_hold`) are separate entries and each is replaced only by an entry of its own: an aircraft that gives `button_1` a command does
**not** unset your `button_1_released` or `button_1_hold`, which keep running (set them to `null` there to stop them). The file name, what is read and when are in
[Configuration](configuration.md#aircraftidjson).

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
