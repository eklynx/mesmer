# Source layout

Developer reference for where things are in `src/`. The usage view of the same system is
[how it works](../how-it-works.md). Everything here is in the source tree; each folder has
a short README naming its source-of-truth headers.

The source is in [`src/`](../../src), grouped by idea (each folder has a short
README naming its source-of-truth headers):

| Folder | What is in it |
|---|---|
| `brunner/` | The yoke: its controllers, the USB frames, the reader, and what differs between models |
| `force/` | Forces calculated from the simulation: the elevator and aileron models, solvers, profiles, buffet |
| `hid/` | The USB HID seam, with `win/` and `mac/` implementations |
| `xplane/` | The X-Plane datarefs (read, write and published) |
| `ui/menu/`, `ui/view/`, `ui/workbench/` | The menu, the windows, and the feel page |

Everything except the Windows USB reader and the files under `xplane/`,
`ui/menu/`, `ui/view/startup_notice.*`, `ui/view/panel_win.hpp` and `plugin.cpp` is portable
C++17 with tests that run on macOS and Windows.

| File | What it holds |
|---|---|
| `force/elevator.hpp` | The elevator's hinge moment and its solver (`solve_pitch`) |
| `force/aileron.hpp` | The ailerons, the linkage and roll's solver |
| `force/flight.hpp` | Reading X-Plane's panel air into each surface's inputs; the startup settings |
| `force/tuning.hpp` | The drive's limits: the strongest spring that stays within the margin anywhere in the travel |
| `brunner/loading.hpp` | Writing a solution to the drive safely, in an order whose every step is within the margin |
| `force/buffet.hpp` | The optional stall buffet |
| `brunner/yoke.hpp` | The supervisor and every pilot-facing status text |
| `config/` | Everything that reads or writes a configuration file, one folder per kind ([src/config/README.md](../../src/config/README.md)): `json.hpp` (the strict JSON reader/writer), `ini/config.hpp` (`BrunnerDirect.ini`), `state/owner_state.hpp` (`BrunnerDirect.state.json`) and `state/aircraft_state.hpp` (an aircraft's own `button_commands`), `yoke/` (the yoke model files and the overrides editor, with their tests) and `aircraft/profiles.hpp` (`BrunnerDirect.profiles.json`) |
| `brunner/core.hpp`, `brunner/reference.hpp` | The USB frames the plugin reads and writes, validated against an allowlist |
| `config/yoke/yoke_preset.hpp`, `brunner/drive.hpp` | What differs between yoke models (the preset format and loader), and the numbers the force models use ([spec](yoke-presets.md)) |
| `brunner/readonly.hpp` | What happens with a yoke the plugin does not drive (an unknown model, or an unverified one): only reads, a learned travel for pitch and roll ([spec](read-only-mode.md)) |
| `brunner/reader.cpp`, `brunner/bridge.hpp` | The reader thread and its request/reply layer; the same on every platform |
| `hid/hid_device.hpp`, `hid/usb_ids.hpp`, `hid/hid_select.cpp` | The seam to the operating system and the USB ids Mesmer opens ([spec](hid-bridge.md)) |
| `hid/win/hid_win.cpp`, `hid/mac/hid_mac.cpp`, `hid/lin/hid_lin.cpp` | The Windows, macOS and Linux HID access |
| `buttons/controls.*`, `buttons/commands.*`, `buttons/edges.*`, `buttons/hold.*`, `buttons/hub.*` | The yoke's controls and what the buttons mean: names and aliases, `button_commands` resolved through them, press and release edges, per-command holds, and the hub the reader delivers to ([spec](button-commands.md)) |
| `buttons/input/` | `can_layout.hpp` (where a model's buttons are on the bridge) and `can_source.hpp` (reading them in the reader's sampling cycle) |
| `buttons/actions/` | The two actions: `direct_action.*` (X-Plane commands) and `vjoy_action.*` with `vjoy_presser.hpp` (a virtual joystick; the Windows driver is `win/vjoy_win.hpp`) |
| `buttons/service.*`, `buttons/methods.hpp` | `ButtonService`, the one object the plugin talks to, and the action setting a yoke's preset gives (`button_action_method`) |
| `plugin.cpp` | The X-Plane plugin's glue: commands, the flight loop, start and stop, and what the menu, the corner line and the datarefs show |
| `xplane/sim.hpp`, `xplane/published.hpp` | Every X-Plane dataref the plugin reads or writes, and the `brunner_direct/...` datarefs it publishes |
| `ui/menu/menu.hpp` | The Yoke (Brunner) menu and its Developer submenu |
| `ui/view/startup_notice.hpp`, `ui/view/hud.hpp`, `ui/view/tail.hpp`, `ui/view/panel*.hpp` | The corner notice, the feel card and the force monitor windows |
| `ui/workbench/workbench*.hpp/.cpp`, `workbench.html` | The feel page and its small HTTP server |
| `elevator.html`, `aileron.html` | Stand-alone pages that animate each model; open them in a browser |

**The models are written twice and kept equal.** `elevator.html` and
`aileron.html` carry a JavaScript copy of each model for the pages, and
`elevator_vectors.json` and `aileron_vectors.json` hold test vectors that both
the C++ and the JavaScript must reproduce.

**Threads.** The USB reader runs on its own thread and makes no X-Plane SDK
calls. Everything that touches X-Plane runs on the simulator's thread.

**Building and testing:** see [CONTRIBUTING.md](../../CONTRIBUTING.md).
