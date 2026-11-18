# Troubleshooting

The plugin says what is going on in one status text. You see it in three
places:

- the top line of *Plugins > Yoke (Brunner)*;
- the **corner line** in the top left of X-Plane, which appears whenever the
  yoke is not simply working and disappears when it is. When it offers
  *Retry*, click it. *Show yoke status on screen* in the menu hides the
  routine messages; a centring warning or a *Retry* always shows;
- the feel page and the feel card.

Every change of status is also written to `Log.txt`, on lines starting
`[BrunnerDirect]`.

> **Paths:** `Log.txt` is in X-Plane's own folder (the one that holds
> `Resources` and `Output`). Folder paths on this page are written with
> Windows `\` separators; on MacOS and Linux they are `/`.

## First things to try

Try these first, in this order:

1. Take your hands off the yoke and click *Retry* (in the corner line or the
   *Plugins > Yoke (Brunner)* menu).
2. Switch the yoke off, wait a few seconds, switch it on, then *Retry*.
3. Check the yoke's USB cable and plug.
4. Quit CLS2Sim, Simux and anything else that talks to the yoke.
5. Look up the status text below, and search `Log.txt` for `BrunnerDirect`.

## Every status, and what to do

### Working

| Status | What it means | What to do |
|---|---|---|
| <a id="ready-force-feel-off"></a>**Ready - force feel off** | The yoke is centred and is X-Plane's pitch and roll. The motors are off. | Nothing. Fly. |
| <a id="ready-force-feel-starting"></a>**Ready - force feel starting** | You switched force feel on; it is starting. | Keep a light hand on the yoke for a moment. |
| <a id="ready-force-feel-on"></a>**Ready - force feel on** | The yoke carries the model's loads. | Fly. |
| <a id="ready-force-feel-switching-off"></a>**Ready - force feel switching off** | Force feel is stopping; the motors stop next. | Nothing. |
| <a id="ready-force-feel-paused-with-x-plane"></a>**Ready - force feel paused with X-Plane** | X-Plane is paused before force feel started, so it waits. (Once force feel is running, a pause holds its forces and shows no message.) | Unpause; it starts by itself. |
| <a id="ready-force-feel-waiting-for-x-plane"></a>**Ready - force feel waiting for X-Plane** | Force feel is on but X-Plane isn't steady yet (loading, or just unpaused). | Wait a moment. |

### Waiting

| Status | What it means | What to do |
|---|---|---|
| <a id="yoke-stopped-retry-to-resume"></a>**Yoke stopped - Retry to resume** | The yoke is held until you ask. This is how a fresh install starts (`auto_start=0`), and also follows the developer stop. | Hands off the yoke, then click the line or choose *Retry* in the menu. |
| <a id="yoke-centring-hands-off"></a>**Yoke centring - hands off** | The yoke is about to move, or is moving, to its centre. | Let go. It takes a few seconds and starts only once the yoke has been still for a second. |
| <a id="yoke-waiting-for-x-plane"></a>**Yoke waiting for X-Plane** | X-Plane is loading or not yet running steadily. | Wait. If it stays, make sure a flight is loaded and running. |
| <a id="yoke-waiting-x-plane-is-paused"></a>**Yoke waiting - X-Plane is paused** | The yoke needs to centre but X-Plane is paused. | Unpause. |
| <a id="yoke-off-or-unplugged-check-its-power-switch-and-usb-cable"></a>**Yoke off or unplugged - check its power switch and USB cable** | The plugin can't find the yoke's USB bridge. The yoke's power switch also switches the bridge off, so the plugin can't tell "off" from "unplugged". | Switch the yoke on and check the USB cable. The plugin looks again every second and carries on by itself. If your yoke is on and plugged in, it may use a USB bridge this version doesn't know: see [compatibility report](compatibility-report.md). |
| <a id="yoke-s-usb-is-off-in-windows-click-here-to-turn-it-on"></a>**Yoke's USB is off in Windows - click here to turn it on** | The yoke is plugged in and on, but its USB bridge is switched off in Windows (Device Manager's *Disable device*). Windows keeps it off through unplugging, power cycles and restarts. | Click the line, or choose *Plugins > Yoke (Brunner) > Turn the yoke's USB on*. Windows may ask to allow its device tool, `pnputil`. By hand: Device Manager > *Human Interface Devices* > the yoke's *USB Input Device* > *Enable device*. |
| <a id="yoke-s-usb-turning-on-wait-a-moment"></a>**Yoke's USB turning on - wait a moment** | Windows is switching the yoke's USB back on after your click. | Wait. The yoke reconnects by itself in a few seconds. If it is still off after 15 seconds, the line above comes back. |
| <a id="yoke-is-in-use-by-another-program-close-cls2sim-and-simux"></a>**Yoke is in use by another program - close CLS2Sim and Simux** | Another program has the yoke's USB bridge open. | Quit CLS2Sim, Simux or anything else that talks to the yoke. The plugin picks the yoke up by itself. **On Linux** the same line also appears when your account may not open `/dev/hidraw*`: see [Linux: yoke is in use but nothing else has it](#linux-yoke-is-in-use-by-another-program-but-nothing-else-has-it). |

### Something needs you

| Status | What it means | What to do |
|---|---|---|
| <a id="yoke-not-recognised-see-x-plane-s-log-txt"></a>**Yoke not recognised - see X-Plane's Log.txt** | The plugin found the yoke but refused to drive it: a setting it does not recognise (protection limits, spring mode), status words that differ from the model's preset, or a refused test write. `Log.txt` says which. Firmware alone never causes this. Nothing was moved. | Send a [compatibility report](compatibility-report.md) with the log line; it is how support for other yokes starts. |
| <a id="unknown-yoke-usb-id-0x25bb-0xnnnn-read-only"></a>**Unknown yoke USB id 0x25BB:0xNNNN: read-only** | A Brunner bridge the plugin has no preset for. It is only read; nothing is written. | Send a [compatibility report](compatibility-report.md) with the file from *Survey yoke registers*. Adding the model yourself is a developer task: [yoke presets](dev/yoke-presets.md). The full line goes on: *Add it to BrunnerDirect.yokes.json to drive it*. |
| <a id="read-only-yoke-stopped-not-referenced-power-cycle-it-or-reset-yoke-and-re-centre"></a>**Read-only: yoke stopped, not referenced. Power-cycle it, or Reset yoke and Re-centre** | The NG's motors are off and it has not referenced itself (status `0x0421`), for example after the plugin stopped it. Read-only never writes, so it stays that way. | Power-cycle the yoke, then **Re-centre**. Or turn on *Developer > Allow unverified yoke models*, then *Reset yoke* and *Re-centre*. Details: [read-only mode](dev/read-only-mode.md). |
| <a id="read-only-the-yoke-is-homing-wait-for-it-to-finish"></a>**Read-only: the yoke is homing; wait for it to finish** | The yoke is still going through its own start-up (for example the power-on self-test). | Wait for it to finish. |
| <a id="read-only-move-the-yoke-to-all-four-stops-roll-n-pitch-n"></a>**Read-only: move the yoke to all four stops (roll N%, pitch N%)** | Read-only mode learns the yoke's travel from you, and has not yet seen all four stops. The percentages show how much it has learned. | Move the yoke to each of its four stops (full left, right, forward, back). |
| <a id="readback-mismatch"></a>**`Readback mismatch ... register=2116:0 expected=N actual=M`** in `Log.txt` | The controller held a resting point at M instead of N (its own limit is lower than the model's travel), so force feel stopped. | Tell us the numbers N and M in a [compatibility report](compatibility-report.md). The fix is a lower travel for that axis in the model's settings: [advanced configuration](dev/advanced-configuration.md). |
| <a id="yoke-fault-retry"></a>**Yoke fault - Retry** | A controller reported a fault, the motors were stopped, and two automatic recoveries didn't clear it. | Hands off, then *Retry*. |
| <a id="yoke-fault-switch-the-yoke-off-and-on-then-retry"></a>**Yoke fault - switch the yoke off and on, then Retry** | The drives refused something only a power cycle clears. Nothing has moved; pitch and roll still work if the yoke was centred. | Switch the yoke off, wait a few seconds, switch it on, then *Retry*. |
| <a id="yoke-fault-switch-the-yoke-off-then-retry"></a>**Yoke fault - switch the yoke off, then Retry** | The plugin could not confirm the motors stopped. | **Switch the yoke off now.** Then switch it on and *Retry*. Please [report it](https://github.com/antialias/mesmer/issues/new/choose). |
| <a id="force-feel-unavailable-retry"></a>**Force feel unavailable - Retry** | Force feel stopped because of a problem with the drives; the yoke still flies the aircraft. The corner line's second line says why. | *Retry* tries force feel again without re-centring. If it says Retry won't help, it is a plugin bug: please report it with `Log.txt`. |
| <a id="force-feel-unavailable-switch-the-yoke-off-and-on"></a>**Force feel unavailable - switch the yoke off and on** | The drives' settings were not what the plugin expects (for example after another program changed them), so it changed nothing. | Switch the yoke off and on, then *Retry*. |
| <a id="ready-force-feel-needs-the-aircraft-profiles-see-x-plane-s-log-txt"></a>**Ready - force feel needs the aircraft profiles; see X-Plane's Log.txt** | `BrunnerDirect.profiles.json` is missing or was refused. Pitch and roll work; force feel waits. | Search `Log.txt` for `profile`. Fix or restore the file (the release's copy is in [`config/`](../config)); the plugin rereads it within two seconds. |
| <a id="ready-force-feel-off-another-plugin-holds-the-pitch-trim"></a>**Ready - force feel off: another plugin holds the pitch trim** | Another plugin owns X-Plane's pitch trim override, which force feel needs. Pitch and roll work. | Find the other add-on (often a control or trim add-on) and disable it, or fly without force feel. |
| <a id="yoke-not-reaching-x-plane-lt-reason-gt"></a>**Yoke not reaching X-Plane - &lt;reason&gt;** | The yoke is centred and being driven (read-write) but X-Plane is not taking its pitch and roll for two seconds, and the line says why: *Axis calibration not verified*, *Run reference and centering before input*, *Yoke data stale or unavailable*, *Controller state changed; reference invalid (...)* or *Simulator controls are not writable*. The same reason is written to X-Plane's Log.txt as "Yoke input not reaching X-Plane: ...". | Follow the reason: *Retry* or re-centre for the reference and controller-state ones; for calibration, run the calibration again; for stale data, check the USB connection. If it is not one of these, see the next row. |
| <a id="yoke-not-reaching-x-plane-another-add-on-may-control-pitch-and-roll"></a>**Yoke not reaching X-Plane - another add-on may control pitch and roll** | The yoke is centred and being driven (read-write) but X-Plane is not taking its pitch and roll, usually because another plugin holds X-Plane's joystick override. | Move CLS2Sim's `XPlane10Connector` and other control plugins out of `Resources\plugins`, then restart X-Plane. |
| <a id="yoke-plugin-is-disabled"></a>**Yoke plugin is disabled** | The plugin is disabled in X-Plane's Plugin Admin. | Enable it in *Plugins > Plugin Admin*. |

### Developer statuses

You'll only see these after using the *Developer* submenu: *Developer test
starting*, *Developer test running*, *Yoke paused for a developer session*,
and the end of a test (for example *Light spring test ended; motors
disabled, resting settings in place*). The plugin goes back to normal
operation by itself when a test ends. After *Stop test and disconnect* it
waits for *Retry*.

## Other problems

**There's no *Yoke (Brunner)* menu.** Search `Log.txt` for `BrunnerDirect`.

- `Configuration invalid; plugin not started: ...` names the line in
  `BrunnerDirect.ini` that is wrong. Fix it or restore the release's copy.
- No mention at all: check the folder layout in [install](install.md). The
  plugin must be at
  `Resources\plugins\BrunnerDirect\<platform>\BrunnerDirect.xpl`, where
  `<platform>` is `win_x64`, `mac_x64` or `lin_x64`.

**The menu says *Feel tuning unavailable: port 8087 in use*.** Another
program uses that port. Set a different `workbench_port` in
`BrunnerDirect.ini` and restart X-Plane.

**The feel page doesn't open from another device.** Use the address in the
menu, with `http://`. Both devices must be on the same local network, and
Windows Firewall must allow X-Plane to accept connections on private
networks. The page refuses addresses outside private networks.

**The yoke centres every time I switch it on.** That is intended: switching
the yoke off loses its position reference, so it must find its centre again.

**Pitch or roll are reversed.** Please report it with your yoke's model and
firmware.

### Linux: "Yoke is in use by another program" but nothing else has it

Your account probably isn't allowed to open `/dev/hidraw*`.

| Check | Result | What to do |
|---|---|---|
| `lsusb \| grep 25bb` | No Brunner device listed | Check the USB cable and the yoke's power switch. |
| `ls -l /dev/hidraw*` | `root root` and `crw-------` | Add the udev rule: [Permissions for Linux](install.md#4-permissions-for-linux). |
| `ls -l /dev/hidraw*` | Still `root root` after the rule | Run `sudo udevadm control --reload` and `sudo udevadm trigger`, then unplug and replug the yoke. |
| `ls -l /dev/hidraw*` | Your account or group can write, and the line stays | Another program may have the yoke open. Quit it. If X-Plane runs in Flatpak, Snap or a container, give the sandbox access to `/dev/hidraw*`. |

### MacOS: "BrunnerDirect.xpl can't be opened" or "can't be verified"

The release's `.xpl` is signed, so this should be rare. It usually means the
zip was damaged or modified, or MacOS has quarantined the files.

1. Check the zip's hash against `SHA256SUMS` ([install, step 1](install.md#1-download-the-package-from-the-project-page)). If it doesn't match, download it again.
2. If it matches, clear the quarantine flag and restart X-Plane:
   `xattr -dr com.apple.quarantine "X-Plane 12/Resources/plugins/BrunnerDirect"`
3. Still blocked? [Report it](https://github.com/antialias/mesmer/issues/new/choose) with the exact message and your MacOS version.

### The yoke's buttons don't reach X-Plane

Search `Log.txt` for `Yoke buttons`. When the yoke's model is known, a line
says which action its buttons use (`Yoke buttons: Brunner CLS-E: action VJoy`),
as its preset says
([advanced configuration](dev/advanced-configuration.md#the-button-action)).

- With the action `Direct`, the buttons run the commands in
  `button_commands`. If nothing runs, check that it has entries (`Log.txt`
  says `no button_commands configured` when it has none) and that its names
  are controls of your yoke (it notes each one that is not).
- `Yoke buttons not read:` is the controller refusing the register.

The rest of this section is about the virtual joystick (`VJoy`), the original
CLS-E's action on Windows.

While they work it reads `Yoke buttons: pressing vJoy device 1's 32 buttons`;
otherwise `Yoke buttons off:` says why, and the plugin tries again every
5 seconds.

| `Yoke buttons off:` ... | What to do |
|---|---|
| `vJoy is not installed` | Install Brunner's vJoy driver: [set it up](buttons.md#set-up-vjoy-once). |
| `vJoy is installed but switched off` | In an administrator command prompt: `"C:\Program Files\vJoy\x64\vJoyConfig.exe" enable on`. |
| `vJoy device 1 is not set up` or `has no buttons` | Set device 1 to 32 buttons and no axes: [step 2](buttons.md#set-up-vjoy-once). |
| `vJoy device 1 is in use by another program` | Close the program that feeds vJoy, often CLS2Sim. |
| anything else | Please [report it](https://github.com/antialias/mesmer/issues/new/choose) with `Log.txt`. |

If the log says the buttons are being pressed but X-Plane does nothing, bind
them: *Settings > Joystick*, `vJoy - Virtual Joystick`.

## Reporting a problem

[Open a bug report](https://github.com/antialias/mesmer/issues/new/choose)
with:

- the status text, exactly as shown;
- the `[BrunnerDirect]` lines from `Log.txt` (copy `Log.txt` before
  restarting X-Plane, which overwrites it);
- what you did and what the yoke did;
- if force feel was on, a screenshot of the feel page.

To pull the `[BrunnerDirect]` lines out of `Log.txt` into a file you can attach,
run this in X-Plane's folder, before restarting X-Plane:

**Windows (PowerShell):**

```powershell
Select-String -SimpleMatch '[BrunnerDirect]' Log.txt | ForEach-Object Line | Set-Content mesmer-log.txt
```

**MacOS and Linux:**

```sh
grep '\[BrunnerDirect\]' Log.txt > mesmer-log.txt
```

The plugin also records each session in `Output\BrunnerDirect\`.
Attach the newest file there if we ask for it.
