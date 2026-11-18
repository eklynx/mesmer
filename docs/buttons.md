# The yoke's buttons

Some models of the CLS yokes (such as the original CLS-E) do not expose a
native HID joystick that X-Plane can read, so the plugin handles the button
presses itself. A setting in each yoke's preset, `button_action_method`, says
what the plugin does with the buttons it reads.

## The button methods

| Method | What happens | Available on |
|---|---|---|
| **[vJoy](#vjoy)** | The plugin presses the matching buttons of a virtual joystick, vJoy device 1. X-Plane lists it in its Joystick screen as `vJoy - Virtual Joystick`, and you bind its buttons like any other controller's. | Windows only |
| **[Direct](#direct)** | The plugin runs the X-Plane commands you name in `button_commands`. No binding in X-Plane is needed. | Windows, MacOS and Linux |
| **None** | Turns the buttons off entirely. Use it when your yoke already appears as a joystick in X-Plane and you wish to use that interface. **Not yet implemented.** | Windows, MacOS and Linux (once implemented) |

- **vJoy** uses other software to create a virtual joystick that X-Plane can
  see. On Windows that is the vJoy software included in the CLS2Sim package.
  MacOS may get it in the future, but it would need a separate application
  because of MacOS security entitlements. Linux is also future work. If your
  environment doesn't support `vJoy`, the plugin uses `Direct` instead, and
  `Log.txt` says so.
- **Direct** is the default, and works on every platform now.

Each yoke model's preset says which method it uses (the original CLS-E:
`vJoy`; the CLS-E NG: `Direct`), so there is nothing to set. See
[The button action](dev/advanced-configuration.md#the-button-action), which
also says what each method needs.

Pitch, roll and force feel are not affected by any of this. The buttons work
while X-Plane runs with the plugin, and all of them release whenever the link
to the yoke drops.

## Which control is which

Every control has a name that is the same whichever method you use: `button_N`
(the number of the yoke's own button slot) or, for a hat, a position such as
`hat_n`. A control can also have friendly names.

**The button definitions live in the yoke's config file**: `button_registers`
says which CAN bus register each control is read from, and `button_aliases`
gives the controls friendly names. They are covered in
[advanced configuration](dev/advanced-configuration.md#names). Names are
matched in any case.

**The original CLS-E** (the one yoke tested so far). Its three front panel
buttons are not listed: they are [disabled on purpose](#the-cls-es-front-panel-buttons).


| Control | Name | Also called |
|---|---|---|
| Left grip, index finger | `button_1` | `left_index` |
| Left grip, thumb left | `button_2` | `left_thumb_left` |
| Left grip, thumb right | `button_3` | `left_thumb_right` |
| Left grip rocker, back | `button_4` | `left_rocker_back` |
| Left grip rocker, forward | `button_5` | `left_rocker_forward` |
| Right grip, index finger | `button_7` | `right_index` |
| Right grip, left thumb | `button_8` | `right_thumb_left` |
| Right grip, right thumb | `button_9` | `right_thumb_right` |
| Right grip rocker, back | `button_10` | `right_rocker_back` |
| Right grip rocker, forward | `button_11` | `right_rocker_forward` |

**The CLS-E NG** has one grip, whose first five controls are the CLS-E's left
grip slots (checked on an NG's own register, 2026-11-16). Its hat push and its
hat are in the NG's own button record, which Mesmer reads for the NG.

| Control | Name | Also called |
|---|---|---|
| Trigger | `button_1` | `trigger` |
| Top left | `button_2` | `top_left` |
| Top right | `button_3` | `top_right` |
| Rocker, down | `button_4` | `rocker_down` |
| Rocker, up | `button_5` | `rocker_up` |
| Hat, pushed in | `button_6` | `hat_push` |
| Hat, eight directions | `hat_n`, `hat_ne`, `hat_e`, `hat_se`, `hat_s`, `hat_sw`, `hat_w`, `hat_nw` | |

**The hat.** The original CLS-E has no hat switch. The CLS-E NG has one, and a
CLS-E MK2 is reported to as well (Mesmer has no preset for the MK2 yet).

If your yoke's buttons land elsewhere, please say so in a
[compatibility report](compatibility-report.md).


## vJoy

**vJoy is currently only available on Windows.** It is a Windows driver. MacOS
and Linux may gain it in the future (see
[the button methods](#the-button-methods)). Elsewhere the original CLS-E's
buttons use `Direct`, and `Log.txt` says so.

### Set up vJoy once

1. Install Brunner's build of vJoy, version 2.2.2.0. Brunner signs the
   installer and Microsoft signs the driver, so it loads with Secure Boot
   on. CLS2Sim ships it as `Virtual joystick driver\vJoySetup.exe` in its
   folder, and it is on
   [Brunner's vJoy releases](https://github.com/BrunnerInnovation/vJoy/releases).
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
and Log.txt says why: see
[troubleshooting](troubleshooting.md#the-yokes-buttons-dont-reach-x-plane).

### Which button is which

The plugin presses vJoy button *n* for the control named `button_n` (see
[Which control is which](#which-control-is-which)). On the NG, the hat push is
vJoy button 6, and the hat's eight directions are vJoy buttons 17 to 24 (up
first, then clockwise).

## Direct

Works on Windows, MacOS and Linux. With the `Direct` method each control runs
X-Plane commands that you choose.

One benefit of `Direct` is that you can set defaults and then per-aircraft
overrides, so the buttons can behave differently depending on what you are
flying. The downside is that changes are made in the configuration files, and
they are only picked up when the file is read: the state file when X-Plane
starts, an aircraft's file when the aircraft loads.

### Commands on the yoke buttons

Each control runs the X-Plane commands you write in `button_commands` in
`Output/BrunnerDirect/BrunnerDirect.state.json`, one entry per control:
`"button_1": "sim/view/default_view"`. Nothing is bound until you write it.

- Add `_released` to the name to run something when the control is let go.
- Add `_hold` for a command that should act for as long as it is held (trim,
  panning the view).
- An entry can be one command, a list of them (they run in order), or `null`
  for none.

The names, the yoke's friendly names for its controls and a Cessna 172 example
are in [Button commands](dev/advanced-configuration.md#button-commands).

### Different commands for one aircraft

Put a file named after the aircraft, such as `C172.json`, in the `aircraft/`
folder beside the state file, with `"version": 1` and a `button_commands`
section. Its commands replace the state file's for that aircraft, and only
that one.

- It replaces; it does not add: if both files give `button_1` a command, only
  the aircraft's runs.
- Everything it leaves out keeps your state file's commands, and `null`
  switches one off for that aircraft.
- Pressed (`button_1`), let go (`button_1_released`) and held
  (`button_1_hold`) are separate entries, and each is replaced only by an
  entry of its own. An aircraft that gives `button_1` a command does **not**
  unset your `button_1_released` or `button_1_hold`, which keep running (set
  them to `null` to remove a command).

The file name, what is read and when are in
[Configuration](configuration.md#aircraftidjson).

## Plugin commands

The plugin adds its own X-Plane commands. Like any X-Plane command, they can be
assigned to a yoke button with [Direct](#commands-on-the-yoke-buttons), or
bound in X-Plane itself from *Settings > Joystick* or *Settings > Keyboard*.

| Command | What it does |
|---|---|
| `brunner_direct/stop_motors` | Stops the yoke's motors: force feel off, pitch and roll keep working (see [Safety](../SAFETY.md)). |
| `brunner_direct/look_down` | Looks down a step: ahead, then half down, then the panel. |
| `brunner_direct/look_up` | Looks back up the same steps. |
| `brunner_direct/mark_feel` | Marks this moment on the feel page. |
| `brunner_direct/compare_feel` | Switches the feel page between *Kept* and *Trying*. |

The look steps use X-Plane's own views. Ahead is the default view; half down
and the panel are Quick Looks 2 and 1, which you save once per aircraft: look
where you want, then save it from *Views > Quick Look*, or with the commands
`sim/view/quick_look_1_mem` (half down) and `sim/view/quick_look_0_mem` (the
panel). A view moved with the mouse alone isn't seen; the next step goes from
the last view a command chose.

## The CLS-E's front panel buttons

The original CLS-E has three buttons on its front panel (middle, right and
left; slots 12 to 14). Mesmer **intentionally disables them** because of
compatibility issues: they have no names in the yoke's config file and are not
read, so they can't be bound to anything and don't press a vJoy button.

What was found on the tested yoke:

- They also work the yoke's own menu.
- One press switched the yoke's motors on while the plugin was driving it.
- The plugin then stopped with a fault.


