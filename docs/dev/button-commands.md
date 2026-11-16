# The yoke's buttons: specification

How Mesmer reads the yoke's buttons and hat and what it does with them. The code it describes lives in `src/buttons/`
(read the interface headers, not the implementations; each says what happens on the happy path and on every error):

| File | What it holds |
|---|---|
| `buttons/methods.hpp` | the two settings, their parsing and per-OS defaults |
| `buttons/controls.hpp` | the canonical input names, friendly names and the `ControlMap` built from a yoke's `button_aliases` |
| `buttons/state.hpp`, `buttons/hub.hpp` | what a read method hands over (`ButtonState`, `ButtonSink`) and the one place both read methods deliver to and both actions take from (`ButtonHub`) |
| `buttons/edges.hpp`, `buttons/hold.hpp`, `buttons/commands.hpp` | press and release edges, the per-command hold tracking, `button_commands` resolved through the controls |
| `buttons/input/` | the read methods: `can_source.hpp` (CanBus), `hid_source.hpp` and `hid_profile.hpp` (HID) |
| `buttons/actions/` | the actions: `direct_action.hpp` (X-Plane commands), `vjoy_action.hpp` with `vjoy_presser.hpp` (a virtual joystick), `win/vjoy_win.hpp` (Brunner's vJoy driver) |
| `buttons/service.hpp` | `ButtonService`, the one object the plugin talks to |

Around it: `src/hid/joystick_device.hpp` and `src/hid/report_decoder.hpp` (the HID interface and its reports, with
`hid/mac/joystick_mac.cpp` and `hid/win/joystick_win.cpp`), `src/brunner/owner_state.hpp` (the `button_commands` key),
`src/brunner/yoke_preset.hpp` (`button_aliases`), `src/config.hpp` (the two settings) and `src/plugin.cpp` (the glue).
Tests: `buttons/buttons_tests.cpp`, `buttons/vjoy_tests.cpp`, `hid/joystick_tests.cpp`, `brunner/readonly_tests.cpp` (the
reader's CanBus read), `brunner/preset_tests.cpp` and `tests.cpp` (the settings). Terms are in
[GLOSSARY.md](../../GLOSSARY.md). Setting it up is in [advanced configuration](advanced-configuration.md#button-commands).

## Two settings

Each yoke model's preset ([yoke presets](yoke-presets.md)) gives both, in any case; a model that gives none is `CanBus` + `Direct`.

| Preset field | Values | Meaning |
|---|---|---|
| `button_read_method` | `CanBus`, `HID` | where the buttons are **read** from |
| `button_action_method` | `VJoy`, `Direct` | what the plugin **does** with them |

The shipped models: the original CLS-E is `CanBus` + `VJoy`, the NG is `CanBus` + `Direct`. They are settings of the connected yoke:
`ButtonService::set_yoke` applies them when its model becomes known (and again if another model connects), switching the action and
starting or stopping the HID source as needed.

- **CanBus** reads the registers the model's `button_registers` names over the bridge, CANopen requests made in every sampling cycle.
  It works on the original CLS-E and on the NG, needs no second interface, and gives the buttons as `can_button_1` to `can_button_16`
  and the hat as `can_hat_*`.
- **HID** observes the NG's own joystick HID reports (read-only, shared with X-Plane) and gives them as `hid_button_1` to
  `hid_button_6` and the hat positions.
- **VJoy** presses the buttons of a virtual joystick that X-Plane binds. **Direct** runs the X-Plane commands in `button_commands`.

**Every combination works wherever the OS provides the pieces.** The virtual joystick is per operating system
(`make_virtual_joystick()`): only Windows has one today. A model asking for `VJoy` elsewhere gets `Direct` and `Log.txt` says so
once per model; macOS and Linux gain it by implementing that one factory. `HID` on a yoke with no joystick interface (the original CLS-E)
logs that and the buttons do nothing; `CanBus` still works.

**When the buttons are read at all.** The buttons are only read when something uses them: the `VJoy` action, or at
least one command in `button_commands`. With neither, no register is read (no bus traffic) and the joystick interface is not
opened, and `Log.txt` says so once at start. `HID` never costs bus traffic; `CanBus` costs one read, about 2 ms of the 20 ms
sampling cycle, only while it is wanted.

## Controls and names

A **control** is one physical thing on the yoke. The **canonical inputs** are what a read method can see:

| Name | Meaning |
|---|---|
| `hid_button_1` to `hid_button_6` | the NG joystick's buttons: 1 trigger, 2 top left, 3 top right, 4 rocker down, 5 rocker up, 6 hat push |
| `hat_n`, `hat_ne`, `hat_e`, `hat_se`, `hat_s`, `hat_sw`, `hat_w`, `hat_nw` | the HID hat's positions (1 up, clockwise) |
| `can_button_1` to `can_button_16` | the CAN slots: on the original CLS-E bits 16 to 31 of `0x60fd` (CLS2Sim's mapping); on the NG `can_button_1` to `can_button_6` are bits 0 to 5 of its own record `0x2880:3` |
| `can_hat_n`, `can_hat_ne`, `can_hat_e`, `can_hat_se`, `can_hat_s`, `can_hat_sw`, `can_hat_w`, `can_hat_nw` | the hat as the bridge reports it (`0x2880:4` on the NG): the CAN twins of the `hat_*` positions |

Which registers a model's buttons come from is the model's `button_registers` ([yoke presets](yoke-presets.md)); see
[What each yoke has](#what-each-yoke-has). The original CLS-E reads `0x60fd`. The NG's own record `0x2880` has all six of its
buttons and its hat, so the NG reads that: every one of its controls works with `CanBus`. The yoke's `button_aliases`
([yoke presets](yoke-presets.md)) says which names are the same control and gives controls friendly names: a map of lists,
`"trigger": ["hid_button_1", "can_button_1"]`, each key and the names in its list being one control, chains included. One
structure carries both the HID-to-CAN links and the custom names, and a built-in name may be a key (the shipped NG file has
`"hat_n": ["can_hat_n"]`). A control has at most one HID input and one CAN input (a `can_button` or a `can_hat`); friendly names are
`a-z`, `0-9`, `_`, not starting `hid_` or `can_`, not ending `_released` or `_hold`. The shipped files link all of the NG's HID
inputs to their CAN twins (verified on the NG, 2026-11-16) and name the CLS-E's slots.

**A press is matched on every name of its control**, so a command written under any of them fires under either read method.
`ButtonCommands::resolve` runs when the connected yoke's model becomes known (and again if it changes), since the map is the
model's.

**vJoy numbering is the same under both read methods:** a control's virtual-joystick button is its CAN slot number; a control
with no CAN slot number gets the next free number from 17 up, HID buttons first, then the hat positions. Under `CanBus` on the CLS-E that
is slot n to button n, as it always was. On the NG the six buttons are 1 to 6 and the hat's eight positions 17 to 24 (the hat push is
button 6, a CAN slot, so it is no longer 17). A control that finds all 32 taken has no virtual button.

## Read method: CanBus

`can_source.hpp`: in each of the reader's sampling cycles, on its thread, the registers the yoke's `button_registers` names are read
with the sampling timeout (one attempt, 20 ms) and published together as a `ButtonState` in CAN numbering; the slots are also kept in
`Snapshot::buttons`. The original CLS-E has one read, node 2's `0x60fd` (bits 16 to 31). The NG has two, its own `0x2880:3` (buttons 1
to 6, bits 0 to 5) and `0x2880:4` (the hat, 0 to 8; a value above 8 reads as centred), about 2 ms of the 20 ms cycle each. Only
registers on `brunner::button_register`'s read-only allowlist can be asked for (`0x60fd:0`, `0x2880:3`, `0x2880:4`); a preset naming
another is refused. If the hat's reply is missed the whole cycle is dropped, so a half-read state is never published. Nothing is dropped as a glitch (the rules below are the NG joystick's; none has been seen here). A read the
controller refuses, or answers with nonsense, leaves the buttons unread with the reason in `Snapshot::button_refusal`, releases
everything at the sink, and is retried from 5 s later; the buttons never stop pitch, roll or force feel. A missed reply is the
reader's to tolerate like any sampling read ([hid-bridge.md](hid-bridge.md)). When the USB session ends the reader releases
everything at the sink. A press shorter than one cycle can fall between two reads: the register is polled, not event-driven.

## Read method: HID

The NG's USB presents two HID interfaces ([hid-bridge.md](hid-bridge.md) covers the first):

| Interface | Usage | Mesmer |
|---|---|---|
| Bridge | vendor page `0xFF00` / `0xFF02`, 64-byte reports | Seized; the controller protocol |
| Joystick | Generic Desktop `0x01` / Joystick `0x04`, 6-byte reports, no report ID | Observed: shared (`kIOHIDOptionsTypeNone` on macOS, `GENERIC_READ` shared both ways on Windows), read-only, never written. X-Plane keeps reading the same reports |

The joystick interface is found by Brunner's vendor id (`0x25BB`) **and** its top-level usage, in the OS's own match, so the
bridge is never touched by the search; the search's shared open is released again before the joystick device itself is opened.
More than one match is refused ("found several"); none, or no permission, is not an error: Mesmer looks again every two seconds
and `Log.txt` says why when the OS says.

On Windows only opening and reading differ (`src/hid/win/joystick_win.cpp`). The interface is found through the HID walk the
bridge uses (`src/hid/win/hid_common_win.hpp`) and opened `GENERIC_READ`, shared both ways: Windows gives every program that opens a
joystick its own copy of each report. Windows gives no program the raw report descriptor, so the layout comes from Windows' own
parse: each input usage is written all ones into an empty report (`HidP_SetUsages`, `HidP_SetUsageValue`) and the bits that change
are that usage's. `layout_from_fields` (`src/hid/report_decoder.hpp`) turns those bits into the same `ReportLayout`
`parse_report_layout` gets from the descriptor; on the NG they match bit for bit (checked with `brunner_joystick_probe`). Windows
starts every report with a report-ID byte, 0 when the device uses none, and the payload is what follows it.

**Report layout** (from the descriptor, not fixed offsets; the NG's, captured with `src/hid/mac/joystick_probe_mac.cpp`, 97 bytes):

| Bits of the 6-byte report | Meaning |
|---|---|
| byte 0, bits 0 to 5 | Buttons 1 to 6 (bit n-1 is button n) |
| byte 0, bits 6 and 7 | Padding, always 0 in a genuine report |
| byte 1, low nibble | Hat: 0 centred, 1 to 8 clockwise from up (1 N, 2 NE, 3 E, 4 SE, 5 S, 6 SW, 7 W, 8 NW) |
| byte 1, high nibble | Padding |
| bytes 2 to 3, 4 to 5 | X and Y (little endian); not read |

A hat value outside the descriptor's range decodes as centred. A report shorter than the layout is skipped.

**The hat passes through neighbouring positions.** The NG reports about 45 times a second, and its hat is not always read as one clean
step: moving to or from a diagonal, or letting go of one, it can report a straight direction for a report or two (22 to 44 ms) before or
after the diagonal (measured 2026-11-16: `w` then `sw` 22 and 43 ms apart, `n` then `ne` 44 ms, `se` then `e` then centre on release).
A clean push often goes straight to the diagonal, and a slow sweep through the positions is unaffected. Each such position is a real
press and release as far as Mesmer can tell, so a once-command bound to it fires. The plugin does not hide it: an entry that must not
fire on the way to a diagonal should be a `_hold` entry, which the per-command hold tracking already makes quiet when the diagonal lists
the same command, or should accept the extra press.

**Glitched reports.** On macOS the NG sometimes sent reports that no real yoke state produces. Measured over 2,470 reports 15%
were `7d 00 xx xx 00 00`, with buttons 1, 3, 4, 5 and 6 set, a padding bit set and X/Y collapsed (a later hub showed none in 278).
A report is dropped whole (the previous genuine state stays, so nothing is pressed or released by it) when the HID profile
(`hid_profile.hpp`) says so. The default profile, used for every model, drops a report with:

- buttons 4 and 5 both set (the rocker's two sides, which cannot be pressed together; buttons 5 and 6, rocker up with the hat
  pushed in, can), or
- any padding bit set.

Every dropped report is counted by rule, and at most every 10 seconds `Log.txt` says
`Joystick buttons: dropped N glitched reports (rule: count, ...)`, **whether or not `debug_log` is on**, and nothing in a period
with no drops. Because Mesmer cannot stop X-Plane seeing the same reports, **leave X-Plane's own bindings for these buttons and
the hat unset** and use `button_commands` or the virtual joystick instead. When the device goes away (removed, a failure,
stopping) the source releases everything at the sink.

## Presses, releases and holds

The hub turns each source's states into **edges** of controls (`edges.hpp`). The first state only sets the baseline, so a button
held when the plugin starts runs no press (its release is seen). After that a command runs **once per press edge**: a control going
from released to pressed, or the hat moving onto a position. Holding does not repeat. The matching *release* edge (a control let go,
or the hat moving off a position, to centre or to another position) runs the entry named `<name>_released`. A hat moving straight
from one position to another releases the old one, then presses the new one. In one state the releases come first, then the
presses. A glitched report causes neither edge.

Three entries per name, any combination, all optional:

| Entry | Runs | X-Plane call |
|---|---|---|
| `<name>` | on the press edge | `XPLMCommandOnce` |
| `<name>_released` | on the release edge | `XPLMCommandOnce` |
| `<name>_hold` | begun on the press edge, ended on the release edge | `XPLMCommandBegin`, then `XPLMCommandEnd` |

Use `_hold` for commands that act while they are held (trim, brake, push to talk, panning the view) and the plain entry for ones
that act once (flaps, toggles). Holds are kept **per command**, not per control (`HoldTracker`). After each flight-loop callback
the commands that should be held are those of every `_hold` entry whose control is pressed, and only the difference from what is
begun now is done. So the hat moving from `hat_n_hold: ["rot_up"]` to `hat_ne_hold: ["rot_up", "rot_right"]` begins `rot_right`
and leaves `rot_up` running; moving back ends only `rot_right`. A command that two pressed controls share stays begun until both let
go. Within a callback every end comes before any begin, and the once-commands run first, in the order they happened. A control
pressed and released within one callback (a tap) still begins and ends its commands, after the other ends, except those held anyway.
The one case that cannot be smoothed over is a hat move whose release and press land in different callbacks (0.1 s apart); the yoke
reports both in one report, so this is rare, and then the shared command ends and begins again.

**Nothing stays held:** when a source goes away it releases everything it had pressed, and when the plugin is disabled the Direct
action ends every held command itself. A release is delivered at the next flight-loop callback (0.1 s), so a tap shorter than that
begins and ends in the same callback.

## Actions

**Direct** (`direct_action.hpp`): each flight-loop callback, on X-Plane's thread, takes the hub's edges and runs the commands through
a `CommandRunner` the plugin implements with the SDK (`XPLMFindCommand` the first time a name is needed, since other plugins' and
aircraft's commands may not exist at start; a name X-Plane does not know is said once and skipped). Commands run whatever the
force-feel, read-only or verified-yoke state is: they are input only. The source threads make no X-Plane SDK calls and do not log.

**VJoy** (`vjoy_action.hpp`): each callback takes the hub's level (`vjoy_word`: bit n-1 for virtual-joystick button n) and gives it to
the feeder: it takes the device when it can, lets every button up, then presses and lets up buttons as the word changes (only
changes are sent). Every change and every refusal is logged. Everything is up whenever nothing is known: before the first state,
after a source is lost, and, for `CanBus` only (a polled register, read every cycle), when the last state is older than 250 ms; a
HID state stays until the next report, since a joystick that is not touched sends none. No driver, the driver switched off, no buttons
on device 1 or another program holding it is said once (`Yoke buttons off: <why>; trying again every 5 seconds`) and retried every
5 seconds; a button the driver refuses lets all up and gives the device back. The yoke flies exactly as before in every case. The
setup of Brunner's vJoy is in [buttons.md](../buttons.md).

## `button_commands`

An optional object in `BrunnerDirect.state.json`, read once at plugin load (restart X-Plane after editing). A key is a name of a
control (any canonical name or a friendly name from the yoke's `button_aliases`, any case), optionally with `_released` or `_hold`
added. A value is an X-Plane command name, or a list of them (`"a/b"` and `["a/b"]` mean the same; a list may name several, which run
in the order written: for a press or release each is run once, for a hold each is begun in order and ended in the same order);
`null`, `""`, `[]` or a missing entry means no command, which is the default for every control, and nothing is written to the file
unless you write it.

Several keys that name the **same control** (`trigger`, `hid_button_1` and `can_button_1` are one on the NG) are merged: all of their
commands run, in the alphabetical order of the key names, and one note at resolution says which keys collided.

Errors, none thrown, each logged once and the rest still applies:
- `button_commands` not an object: ignored.
- A value that is not a string, a list of strings or `null`: that entry ignored. A list item that is not a string: that item ignored.
- A name that is not a control of the connected yoke: ignored, but kept in the file as written (a save never erases it).
- A command containing spaces or control characters: that command ignored (an empty string in a list is skipped silently).
- An entry whose control the active read method cannot see (a control with no CAN slot under `CanBus`; one with no HID input under
  `HID`): kept, with a note, since it fires only with the other read method until the yoke's `button_aliases` link it.
- A command X-Plane does not know: said once on its first use, nothing runs.
- A save writes each entry back in the form it was read: a string stays a string, a list stays a list.

## What each yoke has

| Yoke | Hat switch | Joystick (HID) interface | Where its buttons are on the bridge |
|---|---|---|---|
| Original CLS-E | **none** | none | node 2, `0x60fd`, slots 1 to 14 (slot 6 not fitted): two grips, three panel buttons |
| CLS-E NG | yes (8 positions) | yes: 6 buttons and the hat | node 2, **`0x2880`**: entry 3 the buttons, entry 4 the hat (its `0x60fd` has slots 1 to 5 only) |
| CLS-E MK2 | reported to have one (not verified) | not known | not known; **no preset yet** |

The CLS-E MK2 is something the owner has been told about, not something Mesmer has run on: it needs its own yoke preset (its USB ids,
and its `button_aliases`) once someone has one to test, and the same probes below say where its buttons and hat are.

**The NG's own button record, `0x2880` on node 2** (found 2026-11-16 by watching the vendor's Windows tool, then confirmed with
`src/hid/mac/register_scan_probe_mac.cpp`, read-only; the same object answers on node 1 and reads the same at rest, but node 1 has
not been watched under a press). It is a record with seven entries; entry 0 reads 7, the entry count:

| Entry | Size | Meaning |
|---|---|---|
| `0x2880:3` | 1 byte | the buttons: bit n-1 is button n, 1 to 6 (trigger, top left, top right, rocker down, rocker up, hat push): the numbering of `hid_button_1` to `hid_button_6` |
| `0x2880:4` | 1 byte | the hat: 0 centred, 1 to 8 clockwise from up, the coding of the joystick reports |
| `0x2880:1` | 2 bytes | `0x8792` at rest and never changed; unknown |
| `0x2880:2`, `:5`, `:6`, `:7` | 2, 2, 2, 4 bytes | 0 at rest and never changed; unknown |

Compared with `0x60fd` on the same presses (a sweep of eight reads took 16 ms): every press and release appeared in both within one
sweep, the shortest trigger tap (62 ms) was seen 8 times out of 8 in each, and the quick rocker alternations matched edge for edge, so
the two answer equally fast. `0x60fd` has the NG's five buttons and neither the hat push nor the hat; `0x2880` has all of them, so the
NG's `button_registers` is `0x2880` and the `CanBus` read gives it every control. Not yet run through X-Plane on hardware (the plugin's
own reader, buttons and hat, was run on the NG with the probes and the unit tests, not in a flight).

## Other yoke models and operating systems

A model that differs gives its own `button_aliases` in its preset (no code), and, for the HID reports, adds a profile in its own folder
under `src/buttons/input/` returned from `hid_profile_for`. A new operating system implements `hid::open_joystick` (the HID read) and
`make_virtual_joystick()` (the VJoy action); the decoder, hub, edges, holds, commands and service do not change.
