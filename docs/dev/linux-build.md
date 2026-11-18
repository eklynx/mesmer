# Linux build: specification

`src/scripts/build_linux.sh` is the interface to the Linux build. This page is its
specification; `tests/test-build-linux.sh` checks each statement, and
`tests/test-install-linux.sh` checks [Installing](#installing).

## What it produces

`build/stage/BrunnerDirect/lin_x64/BrunnerDirect.xpl`, with the files the other platforms ship
(`BrunnerDirect.ini`, `BrunnerDirect.profiles.json`, the `yokes/` folder, `skunkcrafts_updater.cfg`,
`README.md`, `LICENSE`, `NOTICE`, `SAFETY.md`) beside it in `BrunnerDirect/`.

- A 64-bit x86-64 ELF shared object (the script fails otherwise). X-Plane 12 on Linux is x86-64 only. Release build, `-Wall -Wextra -Wpedantic -Werror`.
- Exports `XPluginStart`, `XPluginEnable`, `XPluginDisable`, `XPluginStop`, `XPluginReceiveMessage`.
  No XPLM library is linked: X-Plane resolves those symbols when it loads the plugin.
- **Yoke reader: hidraw, not yet run on hardware.** `src/hid/lin/hid_lin.cpp`, behind the seam in
  `src/hid/hid_device.hpp` (spec: [hid-bridge.md](hid-bridge.md)). It needs no library: it opens
  `/dev/hidraw*`, reads the ids and the report descriptor with the `HIDIOCG*` ioctls, and works out
  the usage page, usage and report sizes from the descriptor itself. The build also makes
  `build/brunner_hardware_read_test`, a 15 s check to run first, and `build/brunner_hardware_query`.
- **Text** is drawn with X-Plane's own font (`XPLMDrawString`), as on MacOS: the panel's glyph atlas is a Windows feature.

## Needs

```
sudo apt install build-essential cmake libgl-dev          # always
sudo apt install curl unzip                               # only when the script downloads the SDK
```

CMake 3.20 or later. OpenGL headers are needed for the panel; nothing is needed for USB.

## Usage

```
src/scripts/build_linux.sh [--sdk PATH] [--clean] [--no-test] [--help]
```

Run it from the repository root, `src/` or `src/scripts/` (or from anywhere): it changes to the
root itself, says so when it was started elsewhere, and treats a relative `--sdk` or `$XPLANE_SDK`
as relative to where it was run from.

- `--sdk PATH` (or `$XPLANE_SDK`, which `--sdk` overrides) is an unpacked SDK, the folder with `CHeaders/`.
  With neither, the pinned SDK is downloaded into `build/sdk` and checked against its SHA-256 before it is unpacked.
  The pin equals the one in `release.yml`, `ci.yml` and `build_macos.sh`.
- `--clean` removes `build/` first. `--no-test` skips ctest. `--help` prints usage and builds nothing.
- `build/` is ignored by git.

Failures say what to do: not Linux or not x86-64, a missing tool or `GL/gl.h`, an unknown option, a path
that is not an SDK, a failed download, a checksum that does not match, a build that did not produce the
plugin or produced the wrong kind of file.

## Reading the yoke: permissions

`/dev/hidraw*` is root-only by default. Without a rule the plugin reports the yoke as busy
("Yoke busy or unavailable") and `brunner_hardware_read_test` says how many devices it could not open.
Give your user access to the Brunner bridge only, in `/etc/udev/rules.d/70-brunner.rules`:

```
KERNEL=="hidraw*", SUBSYSTEM=="hidraw", ATTRS{idVendor}=="25bb", TAG+="uaccess"
```

then `sudo udevadm control --reload && sudo udevadm trigger` and replug the yoke. (`uaccess` grants the
logged-in desktop user; with no logind seat use a group rule instead, and note that `plugdev` exists on
Debian and Ubuntu only.) The pilot-facing version, for any distribution, is
[install.md](../install.md#linux-let-your-account-open-the-yoke-once); keep the two in step.

hidraw has no equivalent of MacOS's seize or Windows' share-mode 0. The plugin takes an advisory
`flock` on the device, so a second Mesmer (another X-Plane, `brunner_hardware_read_test`) reads as busy, but a
program that ignores the lock can still read the same reports. Nothing on Linux plays CLS2Sim's role.

## Installing

```
src/scripts/install_linux.sh [--version N] [--xplane-dir PATH] [--force] [--dry-run] [--help]
```

The same behaviour as `install_macos.sh` ([macos-build.md](macos-build.md#installing)): X-Plane's folder is
the first line of `~/.x-plane/x-plane_install_<N>.txt` (the newest N unless `--version`), or `--xplane-dir`;
it must hold `Resources/plugins`; a running `X-Plane-x86_64` stops the install unless `--force`; an existing
plugin moves to `<X-Plane>/Mesmer-backups/BrunnerDirect-<UTC time>`, and the pilot's `BrunnerDirect.ini`,
`BrunnerDirect.feel.ini`, `BrunnerDirect.yokes.json` and any file the new plugin does not ship are carried over.
