# brunner/ - the yoke

Everything about the Brunner yoke itself: its two controllers, the USB frames
(`core.hpp`, `reference.hpp`, `loading.hpp`, `vibration.hpp`), the request/reply layer
(`bridge.hpp`, over the HID seam in `../hid/`), the reader thread (`reader.hpp/.cpp`),
the supervisor and its status texts (`yoke.hpp`), and what differs between models
(`drive.hpp`, `readonly.hpp`; the model format and the saved state are in `../config/`).

Sources of truth: `../config/yoke/yoke_preset.hpp` (the model format and loader; the models themselves are the repository's `config/yokes/*.json`), `drive.hpp` (the numbers the force models
use), `readonly.hpp` (a yoke the plugin does not drive), `bridge.hpp` (the controller
transport). Specs: `docs/dev/yoke-presets.md`, `docs/dev/read-only-mode.md`, `docs/dev/hid-bridge.md`.
Tests: `*_tests.cpp` here, and `../tests.cpp`.
