# Installing by hand

This takes about five minutes. You need Windows x64, X-Plane 12 and a
Brunner CLS-E yoke on USB. If you'd rather not do it by hand, use
[the agent prompt](../INSTALL-WITH-AN-AGENT.md) or [install.ps1](../install.ps1).

## 1. Close X-Plane and CLS2Sim

Quit X-Plane. Then quit CLS2Sim: right-click its icon in the notification
area (bottom right of the taskbar) and exit it, or end `CLS2Sim` in Task
Manager. If CLS2Sim starts with Windows, turn that off in its settings or in
*Task Manager > Startup apps*, or quit it before each flight.

**Why:** the plugin opens the yoke's USB bridge exclusively. While another
program holds the bridge, the plugin cannot reach the yoke and its status line
reads *Yoke is in use by another program - close CLS2Sim and Simux*. The same
goes for Simux and any other program that talks to the yoke.

## 2. Download and check

From [Releases](https://github.com/antialias/mesmer/releases), download
the latest pre-release's zip and its `SHA256SUMS`. Check the zip in
PowerShell:

```powershell
Get-FileHash .\mesmer-*.zip -Algorithm SHA256
```

The hash must match the zip's line in `SHA256SUMS`. If it doesn't, don't
install it; download again, and open an issue if it still doesn't match.

Each release is built from this repository's source by GitHub Actions and
carries a build provenance attestation, so you can check where the binary came
from with `gh attestation verify <zip> --repo antialias/mesmer`.

## 3. Unzip into X-Plane's plugins folder

The zip holds one folder, `BrunnerDirect`, with a plugin for each supported platform (`win_x64`, `mac_x64`). Put it in
`X-Plane 12\Resources\plugins\`, so that you have:

```
X-Plane 12\
  Resources\
    plugins\
      BrunnerDirect\
        BrunnerDirect.ini              startup settings
        BrunnerDirect.profiles.json    aircraft figures
        yokes\
          cls-e.json                   yoke model: CLS-E
          cls-e-ng.json                yoke model: CLS-E NG
        win_x64\
          BrunnerDirect.xpl            the plugin (Windows)
        mac_x64\
          BrunnerDirect.xpl            the plugin (macOS; X-Plane loads the one for its platform)
```

Not sure where X-Plane is? `%LOCALAPPDATA%\x-plane_install_12.txt` lists every
X-Plane 12 folder on the PC, one per line.

If you are upgrading, keep your own `BrunnerDirect.ini` and
`BrunnerDirect.feel.ini` (the feel page's saved settings) if you changed
them, and replace the rest.

## 4. Move CLS2Sim's X-Plane plugin aside

CLS2Sim installs its own plugin into X-Plane:
`X-Plane 12\Resources\plugins\XPlane10Connector`. With Mesmer you don't
need it, and if it is loaded it may hold X-Plane's pitch and roll. Then
Mesmer's line reads *Pitch or roll is owned by another plugin*.

Move the whole `XPlane10Connector` folder **out of** the `plugins` folder,
for example to `X-Plane 12\Mesmer-backups\XPlane10Connector`. Don't
just rename it inside `plugins`: X-Plane loads every plugin folder it finds
there, whatever its name.

To go back to CLS2Sim later, move it back.

## 5. Start X-Plane

Switch the yoke on, start X-Plane and load a flight. Check
*Plugins > Yoke (Brunner)* exists. Then follow [the first flight](first-flight.md).

If the menu is missing, open `X-Plane 12\Log.txt` and search for
`BrunnerDirect`. See [troubleshooting](troubleshooting.md).

## Removing it

1. Quit X-Plane.
2. Delete `X-Plane 12\Resources\plugins\BrunnerDirect`.
3. Optionally delete `X-Plane 12\Output\BrunnerDirect` (the plugin's session
   recordings).
4. To use CLS2Sim again, move `XPlane10Connector` back into
   `Resources\plugins` and start CLS2Sim.

The plugin changes nothing else in X-Plane: no joystick bindings, no
preferences, no aircraft files. It does not store anything in the yoke's
controllers that survives switching the yoke off.
