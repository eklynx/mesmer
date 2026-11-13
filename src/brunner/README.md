# brunner/ - the yoke

Everything about the Brunner yoke itself: its two controllers, the USB frames
(`core.hpp`, `reference.hpp`, `loading.hpp`, `vibration.hpp`), the request/reply layer
(`bridge.hpp`, over the HID seam in `../hid/`), the reader thread (`reader.hpp/.cpp`),
the supervisor and its status texts (`yoke.hpp`), and what differs between models
(`yoke_preset.hpp`, `drive.hpp`, `readonly.hpp`, `owner_state.hpp`).

Sources of truth: `yoke_preset.hpp` (models), `drive.hpp` (the numbers the force models
use), `readonly.hpp` (a yoke the plugin does not drive), `bridge.hpp` (the controller
transport). Specs: `docs/yoke-presets.md`, `docs/read-only-mode.md`, `docs/hid-bridge.md`.
Tests: `*_tests.cpp` here, and `../tests.cpp`.
