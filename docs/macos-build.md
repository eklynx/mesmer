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
