# Installing by hand

This takes about five minutes. You need X-Plane 12 and a Brunner CLS family
yoke connected via USB. The `CLS-E` (not Mk2) and `CLS-E NG` are currently
supported, but with the correct configuration file any model should work.

**On this page:**
[1. Download](#1-download-the-package-from-the-project-page) ·
[2. Unzip](#2-unzip-into-x-planes-plugins-folder) ·
[3. Move CLS2Sim's plugin](#3-move-cls2sims-x-plane-plugin-aside) ·
[4. Linux permissions](#4-permissions-for-linux) ·
[5. Start X-Plane](#5-start-x-plane) ·
[Updating](#updating) ·
[Uninstalling](#uninstalling)

## 1. Download the package from the project page

From [Releases](https://github.com/antialias/mesmer/releases), download the
zip file of the version you want: the newest release, or a pre-release build.
Optionally, download the file containing the release's SHA256 hashes and
verify that the value matches the hash of the zip file.

**Windows (PowerShell):**

```powershell
Get-FileHash .\mesmer-*.zip -Algorithm SHA256
```

**MacOS (Terminal):**

```sh
shasum -a 256 mesmer-*.zip
```

**Linux (shell):**

```sh
sha256sum mesmer-*.zip
```

`SHA256SUMS` has one line per file: the hash, then the file name.

```
<64 hex characters>  mesmer-<version>.zip
```

The hash you computed must match the zip's line. Ignore upper and lower case:
PowerShell prints the hash in capitals, the others in lower case. If it
doesn't match, don't install it; download again, and open an issue if it still
doesn't match.

**You should see:** the hash you computed matches the zip's line in `SHA256SUMS`.

Each release is built from this repository's source by GitHub Actions and
carries a build provenance attestation, so you can check where the binary came
from with `gh attestation verify <zip> --repo antialias/mesmer`.

## 2. Unzip into X-Plane's plugins folder

The zip holds one folder, `BrunnerDirect`, with a plugin for each supported
platform (`win_x64`, `mac_x64`, `lin_x64`). Put it in
`X-Plane 12\Resources\plugins\`, so that you have:

```
X-Plane 12\
  Resources\
    plugins\
      BrunnerDirect\
        BrunnerDirect.ini              startup settings
        BrunnerDirect.profiles.json    aircraft force profile values
        yokes\                         hardware profiles
          cls-e.json                   yoke model: CLS-E
          cls-e-ng.json                yoke model: CLS-E NG
        win_x64\
          BrunnerDirect.xpl            the plugin (Windows)
        mac_x64\
          BrunnerDirect.xpl            the plugin (MacOS)
        lin_x64\
          BrunnerDirect.xpl            the plugin (Linux)
        skunkcrafts_updater.cfg        configuration for the Skunkcrafts updater
        README.md                      the main README for the project
        SAFETY.md                      safety warnings about force feedback
        LICENSE                        the licence the plugin is distributed under
        NOTICE                         the copyright notice for the plugin
```

X-Plane loads the `BrunnerDirect.xpl` that matches its own platform.

Not sure where X-Plane is? This file lists every X-Plane 12 folder on the
computer, one per line:

| OS      | File                                            |
| ------- | ----------------------------------------------- |
| Windows | `%LOCALAPPDATA%\x-plane_install_12.txt`         |
| MacOS   | `~/Library/Preferences/x-plane_install_12.txt`  |
| Linux   | `~/.x-plane/x-plane_install_12.txt`             |

**You should see:** `Resources\plugins\BrunnerDirect\` contains
`BrunnerDirect.ini` and a `BrunnerDirect.xpl` in the folder for your platform.

> **Note:** If you are upgrading, keep your own `BrunnerDirect.ini` and
> `BrunnerDirect.feel.ini` (the feel page's saved settings) if you changed
> them, and likewise `BrunnerDirect.yokes.json` and the files in `yokes\` if
> you edited them. Replace the rest.

## 3. Move CLS2Sim's X-Plane plugin aside

CLS2Sim installs its own plugin into X-Plane:
`X-Plane 12\Resources\plugins\XPlane10Connector`. With Mesmer you don't
need it, and if it is loaded it may hold X-Plane's pitch and roll. Mesmer's
status line then reads *Pitch or roll is owned by another plugin*.

Move the whole `XPlane10Connector` folder **out of** the `plugins` folder,
for example to `X-Plane 12\Mesmer-backups\XPlane10Connector`. Don't
just rename it inside `plugins`: X-Plane loads every plugin folder it finds
there, whatever its name.

To go back to CLS2Sim later, move it back.

**You should see:** no `XPlane10Connector` folder left in `plugins`.

## 4. Permissions for Linux

*Linux only. Windows and MacOS users can skip to step 5.*

On Linux the plugin talks to the yoke through the kernel's `/dev/hidraw*`
devices, and those belong to root by default. Until your account is allowed to
open the yoke, the status line reads *Yoke is in use by another program* even
when nothing else has it. This is a **one-time setup per computer**, needed for
X-Plane or any other program or utility that needs access to the yoke.

<details>
<summary>Show the Linux setup steps</summary>

These steps assume you are comfortable making system changes in the terminal.

### Check the permissions

With the yoke plugged in, `lsusb | grep 25bb` should list a Brunner device.
If `ls -l /dev/hidraw*` shows your own account (or a group you are in) able to
write, you are done and can move on to starting X-Plane.

If it shows `root root` and `crw-------`, continue below.

### Change the permissions

#### Distributions using udev

This covers any distribution using udev (Ubuntu, Debian, Fedora, Arch,
openSUSE and the others that use systemd, and most that don't). As root (that
is, with `sudo`), create `/etc/udev/rules.d/70-brunner.rules` containing the
following single line:

```
KERNEL=="hidraw*", SUBSYSTEM=="hidraw", ATTRS{idVendor}=="25bb", TAG+="uaccess"
```

Reload the rules and re-apply them to the plugged-in yoke:

```sh
sudo udevadm control --reload
sudo udevadm trigger
```

If `ls -l /dev/hidraw*` still shows `root root`, unplug the yoke and plug it
in again. The rule only touches Brunner devices (vendor `25bb`), not your
keyboard or mouse.

#### X-Plane in a sandbox (Flatpak, Snap, a container)

The sandbox has to be allowed to see `/dev/hidraw*`. That is a setting of the
package, not of Mesmer; see your sandbox's documentation for how to grant it
access to USB devices.

#### Running as root

Running X-Plane as root also works, but is **highly** discouraged.

</details>

**You should see:** `ls -l /dev/hidraw*` shows your account, or a group you
are in, able to write to the yoke's device.

Still seeing *Yoke is in use by another program*? See
[troubleshooting](troubleshooting.md#linux-yoke-is-in-use-by-another-program-but-nothing-else-has-it).

## 5. Start X-Plane

If you have CLS2Sim or any other software that takes control of the yoke,
make sure it is fully closed. Also check that it isn't still running in the
taskbar or menu bar; if it is, right-click its icon and quit it.

Switch the yoke on, start X-Plane and load a flight. Check that
*Plugins > Yoke (Brunner)* exists. Then follow
[the first flight](first-flight.md).

If the menu is missing, open `X-Plane 12\Log.txt` and search for
`BrunnerDirect`. See [troubleshooting](troubleshooting.md).

# Updating

Updating is similar to installing. Before you start, copy the
`BrunnerDirect.ini` and `BrunnerDirect.feel.ini` files to a safe place. If you
have edited them, also copy `BrunnerDirect.yokes.json` and the files in the
`yokes\` folder. After installing the updated version, copy these files from
the previous version into the updated folder to preserve your settings.

# Uninstalling

1. Quit X-Plane.
2. Delete `X-Plane 12\Resources\plugins\BrunnerDirect`.
3. Optionally delete `X-Plane 12\Output\BrunnerDirect` (the plugin's session
   recordings and preserved session state files).
4. To use CLS2Sim again, move `XPlane10Connector` back into
   `Resources\plugins` and start CLS2Sim.

The plugin changes nothing else in X-Plane: no joystick bindings, no
preferences, no aircraft files. It does not store anything in the yoke's
controllers that survives switching the yoke off.
