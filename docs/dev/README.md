# Developer documentation

Everything here is for people building, releasing or extending Mesmer. Standard use (installing,
a first flight, settings, troubleshooting) is in [`docs/`](..). Code headers name these files as
their source of truth.

| Document | Source of truth for |
|---|---|
| [source-layout.md](source-layout.md) | Where things are in `src/`, the threads, the models written twice |
| [yoke-presets.md](yoke-presets.md) | What differs between yoke models: the preset files, overrides, force modes, gates |
| [read-only-mode.md](read-only-mode.md) | A yoke the plugin does not drive: the survey, the learned travel |
| [hid-bridge.md](hid-bridge.md) | Which USB devices are the yoke's bridge, and how it is opened |
| [button-commands.md](button-commands.md) | The yoke's joystick buttons and hat: reading the reports, dropping glitched ones, running X-Plane commands |
| [advanced-configuration.md](advanced-configuration.md) | Developer-only settings and files, retired keys, adding a model |
| [macos-build.md](macos-build.md) | Building, installing and testing the macOS plugin |
| [release-packaging.md](release-packaging.md) | What the release zip contains and the guarantees of its script |
| [release-signing.md](release-signing.md) | The contract with the separate macOS signing repository |
