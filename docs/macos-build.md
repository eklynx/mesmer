# macOS build: specification

`src/scripts/build_macos.sh` is the interface to the macOS build. This page is its
specification; `tests/test-build-macos.sh` checks each statement.

## What it produces

A universal plugin, `build/stage/BrunnerDirect/mac_x64/BrunnerDirect.xpl`,
with the files the Windows release ships (`BrunnerDirect.ini`,
`BrunnerDirect.profiles.json`, the `yokes/` folder, `skunkcrafts_updater.cfg`, `README.md`,
`LICENSE`, `NOTICE`, `SAFETY.md`) beside it in `BrunnerDirect/`.

- Slices: exactly `arm64` and `x86_64`.
- Deployment target: macOS 11.0. Release build.
- Ad hoc signed (`codesign --sign -`), and verified.
- Exports `XPluginStart`, `XPluginEnable`, `XPluginDisable`, `XPluginStop`,
  `XPluginReceiveMessage`; links `XPLM.framework` at X-Plane's own path.
- **Yoke reader: IOKit, not yet run on hardware.** Mesmer reads the yoke over
  USB HID itself (no CLS2Sim). On macOS that is `src/hid/mac/hid_mac.cpp`, behind the
  seam in `src/hid/hid_device.hpp` (spec: [hid-bridge.md](hid-bridge.md)). The build
  also makes `build/brunner_hardware_read_test`, a 15 s check to run first.

## Usage

```
src/scripts/build_macos.sh [--sdk PATH] [--clean] [--no-test] [--help]
```

Run it from the repository root, `src/` or `src/scripts/` (or from anywhere): it changes to the
root itself, says so when it was started elsewhere, and treats a relative `--sdk` or `$XPLANE_SDK`
as relative to where you ran it.

| Option | Meaning |
|---|---|
| `--sdk PATH` | Unpacked X-Plane SDK (the directory holding `CHeaders/`). Wins over `XPLANE_SDK`. |
| `--clean` | Delete `build/` first. |
| `--no-test` | Skip `ctest`. |
| `--help` | Print usage, exit 0, build nothing. |

The SDK comes from `--sdk`, else `$XPLANE_SDK`, else the pinned download
(`XPSDK430.zip`, into `build/sdk/`). The download is verified against its
SHA-256 before it is unpacked, and the pin equals the one in
`.github/workflows/release.yml`.

`build/` is ignored by git. The tests (`ctest`) are the portable core tests and
run on the host's architecture.

## Failures

Every failure prints `build_macos.sh: <reason>` to stderr and exits non-zero.
No build starts after one of the first four.

| Cause | Reason says |
|---|---|
| Unknown option / `--sdk` without a value | `unknown option: X` / `--sdk needs a path` |
| Not macOS, or a tool is missing (`cmake lipo codesign curl shasum unzip`) | which one |
| `--sdk`/`XPLANE_SDK` is not an SDK, or lacks `Libraries/Mac/XPLM.framework` | `not an X-Plane SDK directory ...` |
| Download fails | `could not download ... pass --sdk PATH or set XPLANE_SDK` |
| Download's SHA-256 differs from the pin | `checksum ... does not match the pin`; the file is deleted |
| Compiler warning | the build stops (`-Werror`, as on Windows) |
| The `.xpl` lacks either slice | `not universal (slices: ...)` |
| A test fails | `ctest` output, non-zero exit |

## Notes

- If Xcode's `make` path contains a space (Xcode on an external volume), the
  script hands CMake a space-free symlink to it; the generated Makefiles
  otherwise fail to recurse.
- Tests of the script: `tests/test-build-macos.sh` runs the failure paths
  offline. Set `MESMER_TEST_SDK` to an unpacked SDK to also run the full build
  and check slices, signature, exports and deployment target.
- `MESMER_SDK_URL` and `MESMER_SDK_SHA256` override the pin; they exist for
  that test, not for users.

## Installing

`src/scripts/install_macos.sh` copies the staged plugin (`build/stage/BrunnerDirect`, from
`build_macos.sh`) into X-Plane. Tests: `tests/test-install-macos.sh` (a sandbox: fake home, fake
X-Plane, fake stage).

```
src/scripts/install_macos.sh [--version N] [--xplane-dir PATH] [--force] [--dry-run] [--help]
```

- **Where.** X-Plane's folder is the first line of `~/Library/Preferences/x-plane_install_N.txt`,
  where N is X-Plane's major version: the highest N found when there are several (numerically, so 12
  beats 9), or the one `--version N` names.
  `--xplane-dir PATH` names the folder directly (a leading `~` is expanded). If the file is missing or
  empty and no `--xplane-dir` is given, the install stops: it never guesses. Either way the folder must
  exist and hold `Resources/plugins`, checked before anything is changed.
- **Checks first, nothing changed if one fails:** the stage is complete (`mac_x64/BrunnerDirect.xpl`,
  `BrunnerDirect.ini`, `BrunnerDirect.profiles.json`, `yokes/cls-e.json`, `yokes/cls-e-ng.json`), and
  X-Plane is not running (`--force` installs anyway; X-Plane loads plugins at start only).
- **Reinstall.** An existing `Resources/plugins/BrunnerDirect` moves to
  `<X-Plane>/Mesmer-backups/BrunnerDirect-<UTC time>` (outside `plugins/`, which X-Plane loads
  entirely). The pilot's `BrunnerDirect.ini`, `BrunnerDirect.feel.ini` and `BrunnerDirect.yokes.json`,
  and any other file in the old folder that the new plugin does not ship (saved state), are copied
  into the new one, as `install.ps1` does on Windows.
- `--dry-run` says what would happen and changes nothing. Every failure prints
  `install_macos.sh: <reason>` to stderr and exits non-zero.
