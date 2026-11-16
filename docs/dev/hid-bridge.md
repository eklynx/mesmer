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
| `0x25BB` | `0x0081` | The original CLS-E USB Bridge. |
| `0x25BB` | `0x008B` | The CLS-E NG USB Bridge.          
An id in the list is necessary, never sufficient. A device is the bridge only
if **all** of these hold (`select_bridge`):

- its id is in the list **or**, only when no listed bridge is present, its vendor is Brunner's
  (`0x25BB`) with a product id no preset names (an *unknown model*, opened read-only,
  [read-only-mode.md](read-only-mode.md)). A listed yoke always wins, so another Brunner device
  never causes a refusal; with no listed yoke, two unknown Brunner bridges are refused;
- its top-level usage is vendor page `0xFF00`, usage `0xFF02`;
- its input and output reports are 64 bytes (report ID included).

Another vendor's device is never opened, whatever it reports.

Then `brunner/reader.cpp` identifies the model from the device, and before anything that
moves a motor is sent a model whose preset is not verified is
[read-only](read-only-mode.md) unless the owner acknowledges it, and a bridge with no preset
is only read. Firmware is only noted ([yoke-presets.md](yoke-presets.md)).

To support another model, add a preset naming its bridge (in
`BrunnerDirect.yokes.json`, no rebuild); the descriptor checks above and the
verified/acknowledged rule keep a different device from being driven without
the owner's say-so.

The yoke's separate *joystick* interface (usage page `0x01`, usage `0x04`) is not the bridge and is
never selected by `select_bridge`. Mesmer only observes it, shared and read-only; see
[button-commands.md](button-commands.md). The yoke's buttons are also one of the registers read over the bridge: the registers its preset names (node 2's `0x60fd`, or the NG's `0x2880`), in the sampling cycle, when the yoke's `button_read_method` is `CanBus`.

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

## When the link stalls

Any `TransportError` ends the session, the reader closes the bridge and looks again a second later. Each time a
session ends that way the plugin writes **one line to `Log.txt`** (and to the recording when `debug_log=1`),
starting `Yoke link stall:`, which says where it failed and what each side had last done
(`src/brunner/link_diagnostics.hpp`, filled in by `Bridge::exchange`):

| Part of the line | Meaning |
|---|---|
| the error | `HID I/O timed out`, `HID I/O failed` (the device went away or a write failed), `Short HID report`, `No matching controller reply` |
| `while writing a request` / `while waiting for the reply` / `between requests` | The stage the failure was in. Waiting means the request went out and nothing (or nothing right) came back; writing means macOS or Windows refused or timed out the send |
| `request node 2 read of trim position (0x2116:0) [..] sent N ms ago` | What the request in flight was, decoded from its bytes: the controller (node 1 roll, node 2 pitch), `read` or `write` (with the value written), the register's name and its object:sub-index (`status word (sampling)`, `encoder position (sampling)`, `trim position`, `constant force (bias)`, `spring strength`, `centre mode`, ... or `unnamed register`), then its first 14 bytes, and how long ago it was written. The sampling reads say `(sampling)`; the other reads are the live-update confirmation and read-backs |
| `last good reply N ms ago` | How long the yoke had been silent: a short time means it stopped suddenly, a long one that it was slow before |
| `N other reports arrived since it was sent` | Reports that came but were not the reply: 0 is silence, more means the yoke answered other traffic |
| `N requests and M replies this session (K unmatched)` | Totals since this USB session opened, and the session's age |
| `macOS: ...` (macOS only, `HidDevice::describe`) | How many input reports IOKit delivered, how long ago the last arrived, how many writes were made and failed, and whether the removal callback fired (macOS saying the device left the bus) |

Reading it: *removal callback FIRED* means the bus dropped the device; *did not fire* with the last input report
long ago means the yoke stopped sending while macOS still saw it; reports still arriving but unmatched means the
protocol, not the link, is at fault; a failed write names the sending side. The line is only logged on a transport
error, so a healthy session logs nothing extra.

### A silent yoke is tolerated for a while

A request that gets no reply is not necessarily a lost link. At about 237 requests a second the NG now and then drops
one, and now and then goes quiet for a moment (measured: one reply in about 16,660 never came, and one outage of about a
second in a 650 s flight). The sampling loops therefore carry on from the last complete sample, as with a dropped datagram,
for up to **`reply_tolerance_ms`** (state file, default 1000, 0 to 5000): the time since the last *complete* sampling
cycle, checked at each missed reply. The first miss past that ends the session, with the usual stall line. 0 is the old
behaviour: the first missed reply ends it.

**Which requests.** About a third of the requests in force feel are not the four sampling reads: before every live tuning
update the loop confirms both drives with eight read-only reads (status, force bias, trim offset, position mode), and the
first stalls seen with the time-based build were all one of those (2026-11-15, objects 0x2101:4 and 0x2115). They write
nothing and wait one sample period like the sampling reads; a reply missing from them (`TuningNotConfirmed`, a
`ReplyMissed`) skips that cycle's update, counts towards the silence, and the loop tries again next cycle. A reply
missing from a write is handled by the write rule below. Reads that are *not* a sampling read (the read-backs after a
write, and everything in setup and reference) wait 20 ms per attempt and are **sent again** for up to 250 ms
(`retried_read`; a read changes nothing, so a repeat is always safe) before a missing reply is fatal.

**Writes.** The loading writes of the steady loops (`Bridge::load_write`: spring strength, centre, constant force, buffet)
also wait one sample period, `load_write_reply_timeout_ms` (20 ms), for their reply. When it does not come the **same
write is sent again** (every one is an absolute register value, so a repeat of a write that did arrive changes nothing,
and the loading sequences read each value back), for up to `load_write_tolerance_ms` (250 ms) from the first attempt;
only a reply still missing after that is fatal (`ReplyMissed`, with the usual stall line). The stall and miss lines count
`retries` (writes and non-sampling reads sent again). The writes of setup, reference and stop (`ref_write`) are not retried and keep the 250 ms wait: they are
the sequences that enable, centre and stop the motors, where an unconfirmed step must end the session.

`Bridge::exchange` reports a request with no (matching) reply as `ReplyMissed` (a `TransportError` underneath, so anything
that does not handle it still ends the session) and drops what is queued so a late reply is not taken for the next
one. The three steady sampling loops (read-only, idle between requests, and force feel) skip the cycle (in force feel also the
update's confirmation reads, above): the axes keep the
last complete cycle's values and `Log.txt` gets two lines per silence:

- `Yoke stopped answering (the sampling carries on for up to N ms): ...` when it starts, with the same facts as a stall line;
- `Yoke replies resumed after X ms without a complete sample (n requests got no reply)` when a cycle completes again.

While the yoke is silent the force-feel loop **writes nothing** to it (no spring or buffet updates): a write's reply cannot
be skipped, and the yoke keeps the last settings it was given. Writes resume with the first complete sample. Pitch and
roll to X-Plane are also on the last sample meanwhile, so a long tolerance means a long freeze of the controls.

Not tolerated: a failed or removed device, a failed write, a loading write that stays unanswered for 250 ms, and any
missing reply during setup, reference or a stop (those stay fatal, because an unconfirmed step that may have changed the
yoke cannot be skipped).

**The reply timeout is the sample time.** A sampling read waits `sample_reply_timeout_ms` for its reply, the same
20 ms as the loop's period (`sample_period_ms`, `src/brunner/core.hpp`): a reply that has not come within one sample is
missed and the next cycle starts on time. Every other request (writes, setup, reference, stop) still waits
`default_reply_timeout_ms`, 250 ms. The stall and miss
lines include the reply times (`reply times <1 ms: n, <2 ms: n, ...; slowest x ms`), and
`src/hid/mac/latency_probe_mac.cpp` measures them read-only on a free yoke. Measured 2026-11-15 on the NG, X-Plane closed:
median 2.0 ms, 99th percentile 4.0 ms, slowest 14.1 ms over 21,500 reads; in flight the slowest of 135,000 was 17.9 ms.

The lines above are logged whether or not `debug_log` is on (the recording gets them too when it is).

## Windows

`hid/win/hid_win.cpp`: SetupDi enumeration, `CreateFileW` with share mode 0, overlapped
`ReadFile`/`WriteFile`, `HidD_FlushQueue`.

## macOS

`hid/mac/hid_mac.cpp`: `IOHIDManager` enumeration matching every listed id, then
`IOHIDDeviceOpen` with `kIOHIDOptionsTypeSeizeDevice`. Input reports arrive by
callback on a private run-loop thread into a queue; writes use
`IOHIDDeviceSetReportWithCallback` so the timeout holds. Needs the frameworks
IOKit and CoreFoundation.

Paths: X-Plane on macOS hands plugins classic `Volume:Folder:` paths unless
the plugin asks for POSIX ones, so `XPluginStart` calls
`XPLMEnableFeature("XPLM_USE_NATIVE_PATHS",1)` on macOS before any path is read.

`brunner_hardware_read_test` opens the bridge, reads status, encoder and
position from both controllers for 15 s and closes it. It never asks for force
feel. The reader's session start would still disable motors that it finds
already enabled (for example after a power glitch), as the Windows test does.
Close X-Plane, CLS2Sim and Simux first.
