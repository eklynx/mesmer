# Release packaging: specification

How the release zip is made from the per-platform builds. This page is the source of truth;
`src/scripts/package_release.sh` implements it, `tests/test-package-release.sh` checks each
statement, and `.github/workflows/ci.yml` and `release.yml` use it.

## The fat plugin

One zip, `Mesmer-<version>.zip`, with one folder, `BrunnerDirect/`, holding every platform's
plugin side by side (X-Plane loads the one for its own platform):

```
BrunnerDirect/
    win_x64/BrunnerDirect.xpl     Windows (a 64-bit PE DLL)
    mac_x64/BrunnerDirect.xpl     macOS (universal: arm64 + x86_64)
    lin_x64/BrunnerDirect.xpl     Linux (a 64-bit ELF object); absent until Linux builds
    BrunnerDirect.ini, BrunnerDirect.profiles.json, skunkcrafts_updater.cfg
    README.md, LICENSE, NOTICE, SAFETY.md
    yokes/                        the shipped yoke presets
```

The shared files come from the repository, never from a platform build, so every platform's
zip content is the same. `SHA256SUMS` beside it has one line, `<sha256>  Mesmer-<version>.zip`.
`install.ps1` (Windows) needs only `win_x64\BrunnerDirect.xpl` and the shared files, so extra
platform folders do not change how it installs.

## The script

```
src/scripts/package_release.sh --version V --out DIR [--win DIR] [--mac DIR] [--linux DIR] [--help]
```

Each platform option names a folder holding that platform's `BrunnerDirect.xpl`. At least one
platform is required. A failure prints `package_release.sh: <reason>` to stderr, exits non-zero
and writes nothing (the zip is built in a temporary folder and moved into `--out` last).

| Cause | Reason says |
|---|---|
| Unknown option, option without a value | `unknown option: X`, `X needs a value` |
| No `--version` or `--out`; version not `X.Y.Z` or `X.Y.Z-(alpha\|beta\|rc).N` | `--version is required`, `is not X.Y.Z ...` |
| No platform given | `no platform given` |
| A platform folder missing, with no `BrunnerDirect.xpl`, or an empty one | `no such folder`, `has no BrunnerDirect.xpl`, `is empty` |
| Wrong object format: not a 64-bit PE DLL (Windows); not a universal Mach-O with both `arm64` and `x86_64` (macOS); not a 64-bit ELF (Linux) | `not a 64-bit Windows DLL`, `not a universal arm64 + x86_64 Mach-O`, `not a 64-bit ELF object` |
| A shared file missing from the repository | `the repository has no X` |

The macOS plugin's ad hoc signature is inside the Mach-O file, so zipping keeps it; it is
checked where the binary is built (`build_macos.sh`, `codesign --verify`), not here.

## Workflows

- **`ci.yml`** (every push and pull request): `core` (Linux portable tests and this script's
  tests), `windows`, `macos` (`build_macos.sh` plus the macOS script tests), `linux` (a stub) and
  `installer`.
- **`release.yml`** (a tag `v*`): `plan` checks the tag and `skunkcrafts_updater.cfg` and decides
  which platforms to build; `build-windows`, `build-macos` and `build-linux` (a stub) each build,
  test and upload their `BrunnerDirect.xpl`; `package` downloads them, runs this script, attests
  and publishes the pre-release.
- **`package` publishes only if every platform that was enabled built and passed.** A build
  skipped because its platform is switched off is fine; one that was meant to run and did not
  succeed stops the release.

### Switching a platform off or on

Repository variables (Settings, Secrets and variables, Actions, Variables). Both workflows read
them; none is needed for the default behaviour.

| Variable | Builds when |
|---|---|
| `WINDOWS_BUILD` | it is anything but exactly `false` |
| `MACOS_BUILD` | it is anything but exactly `false` |
| `LINUX_BUILD` | it is exactly `true` (off by default) |

A release made with a platform off has no folder for it. With `WINDOWS_BUILD=false` the zip has no
`win_x64/`, which `install.ps1` refuses to install: that is an emergency switch, not a normal
release.

### Linux (not built yet)

`ci.yml`'s `linux` and `release.yml`'s `build-linux` are stubs behind `LINUX_BUILD`; setting it
to `true` before the build exists fails them on purpose (`TODO(linux)` marks the spot). When the
build is written, `build-linux` uploads `BrunnerDirect.xpl` as the artifact `plugin-lin`;
`package` and this script already take it.
