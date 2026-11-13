# Troubleshooting

The plugin says what is going on in one status text. You see it in three
places:

- the top line of *Plugins > Yoke (Brunner)*;
- the **corner line** in the top left of X-Plane, which appears whenever the
  yoke is not simply working and disappears when it is. When it offers
  *Retry*, click it. *Show yoke status on screen* in the menu hides the
  routine messages; a centring warning or a *Retry* always shows;
- the feel page and the feel card.

Every change of status is also written to `X-Plane 12\Log.txt`, on lines
starting `[BrunnerDirect]`.

## Every status, and what to do

### Working

| Status | What it means | What to do |
|---|---|---|
| **Ready - force feel off** | The yoke is centred and is X-Plane's pitch and roll. The motors are off. | Nothing. Fly. |
| **Ready - force feel starting** | You switched force feel on; it is starting. | Keep a light hand on the yoke for a moment. |
| **Ready - force feel on** | The yoke carries the model's loads. | Fly. |
| **Ready - force feel switching off** | Force feel is stopping; the motors stop next. | Nothing. |
| **Ready - force feel paused with X-Plane** | X-Plane is paused before force feel started, so it waits. (Once force feel is running, a pause holds its forces and shows no message.) | Unpause; it starts by itself. |
| **Ready - force feel waiting for X-Plane** | Force feel is on but X-Plane isn't steady yet (loading, or just unpaused). | Wait a moment. |

### Waiting

| Status | What it means | What to do |
|---|---|---|
| **Yoke stopped - Retry to resume** | The yoke is held until you ask. This is how a fresh install starts (`auto_start=0`), and also follows the developer stop. | Hands off the yoke, then click the line or choose *Retry* in the menu. |
| **Yoke centring - hands off** | The yoke is about to move, or is moving, to its centre. | Let go. It takes a few seconds and starts only once the yoke has been still for a second. |
| **Yoke waiting for X-Plane** | X-Plane is loading or not yet running steadily. | Wait. If it stays, make sure a flight is loaded and running. |
| **Yoke waiting - X-Plane is paused** | The yoke needs to centre but X-Plane is paused. | Unpause. |
| **Yoke off or unplugged - check its power switch and USB cable** | The plugin can't find the yoke's USB bridge. The yoke's power switch also switches the bridge off, so the plugin can't tell "off" from "unplugged". | Switch the yoke on and check the USB cable. The plugin looks again every second and carries on by itself. If your yoke is on and plugged in, it may use a USB bridge this version doesn't know: see [compatibility report](compatibility-report.md). |
| **Yoke's USB is off in Windows - click here to turn it on** | The yoke is plugged in and on, but its USB bridge is switched off in Windows (Device Manager's *Disable device*). Windows keeps it off through unplugging, power cycles and restarts. | Click the line, or choose *Plugins > Yoke (Brunner) > Turn the yoke's USB on*. Windows may ask to allow its device tool, `pnputil`. By hand: Device Manager > *Human Interface Devices* > the yoke's *USB Input Device* > *Enable device*. |
| **Yoke's USB turning on - wait a moment** | Windows is switching the yoke's USB back on after your click. | Wait. The yoke reconnects by itself in a few seconds. If it is still off after 15 seconds, the line above comes back. |
| **Yoke is in use by another program - close CLS2Sim and Simux** | Another program has the yoke's USB bridge open. | Quit CLS2Sim, Simux or anything else that talks to the yoke. The plugin picks the yoke up by itself. |

### Something needs you

| Status | What it means | What to do |
|---|---|---|
| **Yoke not recognised - see X-Plane's Log.txt** | The plugin found the yoke but refused to drive it: a setting it does not recognise (protection limits, spring mode), status words that differ from the model's preset, or a refused test write. `Log.txt` says which. Firmware alone never causes this. Nothing was moved. | Send a [compatibility report](compatibility-report.md) with the log line; it is how support for other yokes starts. |
| **Unknown yoke USB id 25BB:xxxx: read-only** | A Brunner bridge the plugin has no preset for. It is only read; nothing is written. | Add a preset for its USB id ([yoke presets](yoke-presets.md)); run *Survey yoke registers* first and send the file. |
| **Read-only: the yoke is stopped, not referenced.** | The NG's motors are off and it has not referenced itself (status `0x0421`), for example after the plugin stopped it. Read-only never writes, so it stays that way. | Power-cycle the yoke, or turn on **Developer > Allow unverified yoke models**, then **Developer > Reset yoke** and **Re-centre**. |
| **`Readback mismatch ... register=2116:0 expected=N actual=M`** in `Log.txt` | The controller held a resting point at M instead of N (its own limit is lower than the model's travel), so force feel stopped. | Lower that axis's `pitch_travel_counts` / `roll_travel_counts` for the model in `BrunnerDirect.yokes.json` to M or less and tell us the number. |
| **Yoke fault - Retry** | A controller reported a fault, the motors were stopped, and two automatic recoveries didn't clear it. | Hands off, then *Retry*. |
| **Yoke fault - switch the yoke off and on, then Retry** | The drives refused something only a power cycle clears. Nothing has moved; pitch and roll still work if the yoke was centred. | Switch the yoke off, wait a few seconds, switch it on, then *Retry*. |
| **Yoke fault - switch the yoke off, then Retry** | The plugin could not confirm the motors stopped. | **Switch the yoke off now.** Then switch it on and *Retry*. Please [report it](https://github.com/antialias/mesmer/issues/new/choose). |
| **Force feel unavailable - Retry** | Force feel stopped because of a problem with the drives; the yoke still flies the aircraft. The corner line's second line says why. | *Retry* tries force feel again without re-centring. If it says Retry won't help, it is a plugin bug: please report it with `Log.txt`. |
| **Force feel unavailable - switch the yoke off and on** | The drives' settings were not what the plugin expects (for example after another program changed them), so it changed nothing. | Switch the yoke off and on, then *Retry*. |
| **Ready - force feel needs the aircraft profiles; see X-Plane's Log.txt** | `BrunnerDirect.profiles.json` is missing or was refused. Pitch and roll work; force feel waits. | Search `Log.txt` for `profile`. Fix or restore the file (the release's copy is in [`config/`](../config)); the plugin rereads it within two seconds. |
| **Ready - force feel off: another plugin holds the pitch trim** | Another plugin owns X-Plane's pitch trim override, which force feel needs. Pitch and roll work. | Find the other add-on (often a control or trim add-on) and disable it, or fly without force feel. |
| **Yoke not reaching X-Plane - another add-on may control pitch and roll** | The yoke is centred but X-Plane is not taking its pitch and roll, usually because another plugin holds X-Plane's joystick override. | Move CLS2Sim's `XPlane10Connector` and other control plugins out of `Resources\plugins`, then restart X-Plane. |
| **Yoke plugin is disabled** | The plugin is disabled in X-Plane's Plugin Admin. | Enable it in *Plugins > Plugin Admin*. |

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
  plugin must be at `Resources\plugins\BrunnerDirect\win_x64\BrunnerDirect.xpl`.

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

### The yoke's buttons don't reach X-Plane

Search `Log.txt` for `Yoke buttons`. While they work it reads
`Yoke buttons: pressing vJoy device 1's 32 buttons`; otherwise
`Yoke buttons off:` says why, and the plugin tries again every 5 seconds.

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
- the `[BrunnerDirect]` lines from `X-Plane 12\Log.txt` (copy `Log.txt` before
  restarting X-Plane, which overwrites it);
- what you did and what the yoke did;
- if force feel was on, a screenshot of the feel page.

The plugin also records each session in `X-Plane 12\Output\BrunnerDirect\`.
Attach the newest file there if we ask for it.
