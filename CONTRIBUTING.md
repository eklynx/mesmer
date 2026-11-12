# Contributing

Thank you for looking. Bug reports, flight reports, compatibility reports,
aircraft profiles, documentation fixes and code are all welcome.

## Where the source lives, and how a pull request lands

Mesmer is developed in a **private repository**, alongside the rest of
the cockpit it was built for. This GitHub repository is published from it:
each release replaces this tree with the private repository's, as one commit
named after the release.

So a pull request here goes like this:

1. You open it here, and it is **reviewed here**, in public, as usual.
2. Once it is ready, it is **brought into the private repository** with you
   credited as co-author (`Co-authored-by:` with your GitHub name and
   no-reply address), and tested there.
3. It ships in the **next release**, which appears here as a commit and a
   GitHub pre-release.
4. Your pull request is then **closed with a link** to that release. It
   shows as closed rather than merged; your commit credit is in the release
   commit.

If the project gains regular contributors, it may move here as its home.

## Before you start

- For anything bigger than a small fix, open an issue or a
  [discussion](https://github.com/antialias/mesmer/discussions) first, so
  we can agree the approach before you spend time on it.
- **Anything that moves the yoke is safety-critical.** Changes to what is
  written to the drives (`loading.hpp`, `reference.hpp`, `tuning.hpp`,
  `reader.cpp`, `hid_win.cpp`, `hid_mac.cpp`) need tests and a careful review, and are tried on
  hardware before they ship.
- **No new features in this preview** beyond what the release notes list,
  unless agreed in a discussion. Profiles, fixes, documentation, tests and
  support for other hardware are the exceptions we're looking for.
- Keep anything private out: serial numbers, licence keys, sign-ins, home
  network details.

## Building and testing

The surface models, the solver, the supervisor, the parsers and the feel
page's server are portable C++17 with no X-Plane in them. You can build and
test them on macOS or Windows with CMake 3.20 or later:

```sh
cmake -S src -B build
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

That runs the core, supervisor, feel card, force monitor and feel page
tests, including a real loopback HTTP round trip.

**The feel page without a yoke or X-Plane:** `brunner_workbench_demo` runs
the real feel page against a simulated Cessna 172.

```sh
cmake --build build --target brunner_workbench_demo
build/brunner_workbench_demo --port 8087 --scenario ready \
    --page src/workbench.html --profiles config/BrunnerDirect.profiles.json
```

Then open `http://127.0.0.1:8087/`. Scenarios: `ready`, `force_feel_off`,
`yoke_off`, `fault`, `busy`, `baron`. `--ias KT` and `--yoke PERCENT` hold
the airspeed and the yoke, and `--tail-alpha DEG --tail-stalled SHARE` stand
in for X-Plane's tail angle and stall. For example
`--ias 60 --yoke 95 --tail-alpha -6 --tail-stalled 1` shows a stalled tail
with the yoke fully back.

**The models' pages:** open `src/elevator.html` and `src/aileron.html` in a
browser. If you change a model, change it in both the C++ and the page, and
regenerate the test vectors with Node.js (`node elevator_vectors.mjs --write`, and
the same for the ailerons; without `--write` the script checks the page
against the file). The C++ tests check the plugin against the same file.

**The plugin itself** needs Windows, Visual Studio (x64) and the
[X-Plane SDK](https://developer.x-plane.com/sdk/plugin-sdk-downloads/):

```powershell
cmake -S src -B build -A x64 -DXPLANE_SDK=C:/path/to/SDK
cmake --build build --config Release --clean-first
ctest --test-dir build -C Release --output-on-failure
```

The plugin is `build/Release/BrunnerDirect.xpl`; install it as
`Resources/plugins/BrunnerDirect/win_x64/BrunnerDirect.xpl`. It links the
runtime statically and needs no other DLL. The Windows build also makes
`brunner_hardware_read_test.exe`, a 15-second read-only check of a connected
yoke (X-Plane and CLS2Sim closed); it is not part of `ctest`.

Use `--clean-first` on Windows: MSBuild has been seen to skip rebuilding after
a header-only change.

GitHub Actions builds every release on Windows against an SDK pinned by
SHA-256 and runs the tests.

**The plugin on macOS** is a universal (arm64 and x86_64) `.xpl` that loads in
X-Plane 12 and reads the yoke through IOKit. That reader has **not yet been
run on hardware**; try it with `build/brunner_hardware_read_test` (read-only,
X-Plane and CLS2Sim closed) before anything else. It is for development, not
for flying. Xcode's command line tools and CMake 3.20 or later are enough:

```sh
./build_macos.sh                  # downloads and verifies the pinned SDK
./build_macos.sh --sdk /path/to/SDK --no-test
```

The result is `build/stage/BrunnerDirect/mac_x64/BrunnerDirect.xpl`, ad hoc
signed; copy the `BrunnerDirect` folder into `Resources/plugins/`. What the
script promises and how it fails is in [docs/macos-build.md](docs/macos-build.md);
`tests/test-build-macos.sh` checks it.

## Writing an aircraft profile

Every aircraft flies the Cessna 172's figures until it has its own profile.
A profile is one entry in `BrunnerDirect.profiles.json`; the format and every
figure's meaning and bounds are in [configuration](docs/configuration.md).

1. **Find the aircraft's file.** Its `.acf` path under the X-Plane folder,
   with `/` between folders, for example
   `Aircraft/Laminar Research/Beechcraft Baron 58/Baron_58.acf`.
2. **Gather the figures,** each with its evidence. For the elevator: its
   area (both halves), its chord aft of the hinge, its travel up and down, the
   trim tab's travel, how much yoke travel gives full elevator travel, and its
   balance about the hinge. The same for one aileron, with the wheel's
   travel. The best sources are the type certificate data sheet, the
   maintenance and structural repair manuals, *Jane's All the World's
   Aircraft*, and NACA reports for hinge-moment slopes.
3. **Tag every figure honestly.** `published` if a source states it,
   `derived` if you worked it out from published figures (say how), `guessed`
   if neither. A good guess with a clear source line is far more useful than
   a precise-looking number with none.
4. **Optionally, find the aircraft's X-Plane wing numbers** for the tail,
   ailerons and wing roots (`tail_sections`, `aileron_sections`,
   `wing_root_sections`). These let the plugin read the air, propwash and
   stall over the aircraft's own panels. Without them it uses indicated
   airspeed. Say how you found them.
5. **Try it.** Copy your profile into `BrunnerDirect.profiles.json` while
   X-Plane runs; the plugin rereads it within two seconds. The feel page shows
   whether it matched (or why it was refused) and lets you try figures live.
   You can also check it with the demo above.
6. **Send it** as a pull request to `config/BrunnerDirect.profiles.json`, or
   paste it into an
   [aircraft profile request](https://github.com/antialias/mesmer/issues/new?template=aircraft_profile.yml).
   Say what you flew and what you felt.

Payware aircraft are welcome, but don't copy anything from their files beyond
the `.acf` path and wing numbers.

## Reporting bugs and hardware

- [Bug report](https://github.com/antialias/mesmer/issues/new?template=bug_report.yml):
  the status text and the `[BrunnerDirect]` lines from `Log.txt`.
- [Compatibility report](docs/compatibility-report.md) for hardware the
  plugin doesn't recognise.
- Something unsafe: open an issue with *safety* in the title.

## Code of conduct

Be kind, be specific, assume good faith. Maintainers may remove anything that
isn't.

## Licence

By contributing you agree that your contribution is licensed under the
[Apache License 2.0](LICENSE), as the rest of the project.
