# The yoke's HID bridge: specification

How Mesmer finds and talks to the yoke's USB bridge, on every platform. The
code it describes: `src/hid/usb_ids.hpp` (which devices), `src/hid/hid_device.hpp`
(the seam; read it first), `src/hid/hid_select.cpp` (which one), `src/brunner/bridge.hpp`
(requests and replies), `src/brunner/reader.cpp` (the supervised reader), and one HID
implementation per operating system: `src/hid/win/hid_win.cpp` and `src/hid/mac/hid_mac.cpp`.
`src/brunner/bridge_tests.cpp` checks every statement in the first four sections.

Mesmer talks to the bridge directly. CLS2Sim is not needed and must be closed
(see Exclusivity).

## Which devices

The bridge ids Mesmer will open are the ids the yoke presets name ([yoke-presets.md](yoke-presets.md); `hid/usb_ids.hpp` holds the built-in models' ids):

| Vendor | Product | Notes |
|---|---|---|
| `0x25BB` | `0x0081` | The original CLS USB Bridge. |
| `0x25BB` | `0x008B` | The CLS-E NG. Found and opened, and it answers the same read requests, but both controllers report firmware **642**, so its preset notes the firmware, and it is read-only until the owner acknowledges the unverified model. Seen on hardware 2026-11-12. |

An id in the list is necessary, never sufficient. A device is the bridge only
if **all** of these hold (`select_bridge`):

- its id is in the list;
- its top-level usage is vendor page `0xFF00`, usage `0xFF02`;
- its input and output reports are 64 bytes (report ID included).

Then `brunner/reader.cpp` identifies the model from the device, and before anything that
moves a motor is sent a model whose preset is not verified is
[read-only](read-only-mode.md) unless the owner acknowledges it. Firmware is only
noted ([yoke-presets.md](yoke-presets.md)).

To support another model, add a preset naming its bridge (in
`BrunnerDirect.yokes.json`, no rebuild); the descriptor checks above and the
verified/acknowledged rule keep a different device from being driven without
the owner's say-so.

## Selecting among several

- None qualifies: `BridgeAbsent` ("Expected one original CLS USB Bridge"). The
  status line reads *Yoke off or unplugged*.
- More than one qualifies, even with two different listed ids:
  `std::runtime_error` "...found several". Nothing is opened; Mesmer will not
  guess which yoke to drive.

## Exclusivity

The bridge is opened exclusively. If it cannot be (CLS2Sim, Simux or another
program has it, or the operating system refuses access) the result is
`BridgeBusy`: *Yoke busy or unavailable; close CLS2Sim and Simux*. The reader
looks again every second.

## Transport

`HidDevice` moves one 64-byte report at a time; byte 0 is report ID `0x3f`.

| Call | Happy path | Failure |
|---|---|---|
| `open_bridge()` | exactly one bridge, opened exclusively, stale input discarded | `BridgeAbsent`, `BridgeBusy`, "found several", `TransportError` |
| `write(report, ms)` | the report is accepted within `ms` (the bridge layer uses 100) | `TransportError` ("HID I/O failed", "timed out") |
| `read(report, ms)` | the next report arrives within `ms` | `TransportError` ("timed out", "Short HID report", device removed) |
| `flush()` | queued input is discarded | `TransportError` |

`Bridge::exchange` sends a request, then reads until a reply matches or 250 ms
pass ("No matching controller reply"). It checks the stop flag between
transfers (`OperationCancelled`). Any `TransportError` ends the session; the
reader closes the bridge and starts over, and an unconfirmed motor stop is
reported, never retried automatically.

## Windows

`hid/win/hid_win.cpp`: SetupDi enumeration, `CreateFileW` with share mode 0, overlapped
`ReadFile`/`WriteFile`, `HidD_FlushQueue`.

## macOS

`hid/mac/hid_mac.cpp`: `IOHIDManager` enumeration matching every listed id, then
`IOHIDDeviceOpen` with `kIOHIDOptionsTypeSeizeDevice`. Input reports arrive by
callback on a private run-loop thread into a queue; writes use
`IOHIDDeviceSetReportWithCallback` so the timeout holds. Needs the frameworks
IOKit and CoreFoundation.

**Checked on hardware (a CLS-E NG, 25BB:008B, 2026-11-12):** enumeration, the
exclusive open, a firmware read request and its reply all work. macOS reports
the vendor interface as usage page `0xFF00`, usage `0xFF02`, input and output
64 bytes (so the report ID is included), and reports carry the report ID in
byte 0. Not yet checked: motor commands, and a yoke with firmware v367 (the
only firmware the plugin will drive). The reader still accepts a reported
size of 63 as 64, in case another macOS version excludes the ID byte.

Paths: X-Plane on macOS hands plugins classic `Volume:Folder:` paths unless
the plugin asks for POSIX ones, so `XPluginStart` calls
`XPLMEnableFeature("XPLM_USE_NATIVE_PATHS",1)` on macOS before any path is read.

macOS may also ask for the *Input Monitoring* permission for the program that
opens the device (X-Plane, or the Terminal running the test). Without it,
enumeration fails; that reads as *Yoke busy or unavailable*.

`brunner_hardware_read_test` opens the bridge, reads status, encoder and
position from both controllers for 15 s and closes it. It never asks for force
feel. The reader's session start would still disable motors that it finds
already enabled (for example after a power glitch), as the Windows test does.
Close X-Plane, CLS2Sim and Simux first.
