# config/ - the configuration files

Everything that reads or writes one of Mesmer's configuration files lives here, one folder per kind of file. Each header says what
happens on the happy path and on every error; read the header, not the implementation. The code is in the `brunner` namespace (or
the one it already had): the folders group files, they do not rename anything.

| Folder | File it handles | Source of truth |
|---|---|---|
| `json.hpp` | (the strict JSON reader and the small writer the folders below share) | `json.hpp` |
| `ini/` | `BrunnerDirect.ini`, the managed defaults ([configuration](../../docs/configuration.md)) | `ini/config.hpp` |
| `state/` | `BrunnerDirect.state.json`, the owner's saved choices and `button_commands`, and `Output/BrunnerDirect/aircraft/<id>.json`, one aircraft's own `button_commands` that override them ([advanced configuration](../../docs/dev/advanced-configuration.md)) | `state/owner_state.hpp`, `state/aircraft_state.hpp`, `state/button_commands.hpp` (the section both share); tests `state/aircraft_state_tests.cpp` |
| `yoke/` | `yokes/*.json` and `BrunnerDirect.yokes.json`, the yoke models, and editing the owner's file from the feel page ([yoke presets](../../docs/dev/yoke-presets.md)) | `yoke/yoke_preset.hpp`, `yoke/yoke_overrides.hpp`; tests `yoke/preset_tests.cpp` |
| `aircraft/` | `BrunnerDirect.profiles.json`, the aircraft profiles | `aircraft/profiles.hpp` |

The plugin (`../plugin.cpp`) does the file reads and writes; these headers turn text into values and values into text.

**Error text lives in its own header, one per folder that has any**: `json_errors.hpp`, `ini/ini_errors.hpp`, `state/state_errors.hpp`,
`yoke/yoke_errors.hpp` and `aircraft/aircraft_errors.hpp`, each in a namespace of its own (`brunner::json_errors`, ...). A message that
never changes is a `constexpr` constant; one that names a key, a place in the file or a value is a small function taking them. The
parser says *when* something is wrong and the errors header says *what is said*, so the wording can be read, reviewed and tested in one
place, and a message is changed without touching the parsing. Other folders get the same treatment when they are reached.
