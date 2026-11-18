# The yoke's buttons: specification

How Mesmer reads the yoke's buttons and hat and what it does with them. The code it describes lives in `src/buttons/`
(read the interface headers, not the implementations; each says what happens on the happy path and on every error):

| File | What it holds |
|---|---|
| `buttons/methods.hpp` | the action setting (`VJoy` or `Direct`), its parsing and default |
| `buttons/controls.hpp` | the canonical input names, friendly names and the `ControlMap` built from a yoke's `button_aliases` |
| `buttons/state.hpp`, `buttons/hub.hpp` | what the controller hands over (`ButtonState`, `ButtonSink`) and the one place it is delivered to and both actions take from (`ButtonHub`) |
| `buttons/edges.hpp`, `buttons/hold.hpp`, `buttons/commands.hpp` | press and release edges, the per-command hold tracking, `button_commands` resolved through the controls |
| `buttons/input/` | `can_layout.hpp` (where each of a model's buttons and hat positions is on the bridge) and `can_source.hpp` (reading them in the reader's sampling cycle) |
| `buttons/actions/` | the actions: `direct_action.hpp` (X-Plane commands), `vjoy_action.hpp` with `vjoy_presser.hpp` (a virtual joystick), `win/vjoy_win.hpp` (Brunner's vJoy driver) |
| `buttons/service.hpp` | `ButtonService`, the one object the plugin talks to |

Around it: `src/config/state/owner_state.hpp` (the `button_commands` key), `src/config/yoke/yoke_preset.hpp` (`button_aliases`,
`button_registers`, `button_action_method`), `src/brunner/reader.cpp` (the sampling cycle that reads the registers) and
`src/plugin.cpp` (the glue). Tests: `buttons/buttons_tests.cpp`, `buttons/vjoy_tests.cpp`, `brunner/readonly_tests.cpp` (the reader's
read of the registers) and `config/yoke/preset_tests.cpp`. Terms are in [GLOSSARY.md](../../GLOSSARY.md). Setting it up is in
[advanced configuration](advanced-configuration.md#button-commands).

## What the plugin does with the buttons

A yoke model's preset ([yoke presets](yoke-presets.md)) gives its `button_action_method`, in any case; a model that gives none is
`Direct`. The owner can name one for every yoke in `BrunnerDirect.state.json` (`button_action_method`, same words, any case), which
beats the model's (`effective_action_method` in `src/buttons/methods.hpp`; the state file is read once, when X-Plane starts).

| Preset field | Values | Meaning |
|---|---|---|
| `button_action_method` | `VJoy`, `Direct`, `Disabled` | what the plugin **does** with the buttons |

- **VJoy** presses the buttons of a virtual joystick that X-Plane binds.
- **Direct** runs the X-Plane commands in `button_commands`.
- **Disabled** does nothing with the buttons: no action exists, no register is read, nothing is pressed, run or held, and every
  `button_commands` entry (the owner's and an aircraft's) is ignored. It is for a yoke that already appears as a joystick in X-Plane.
  Switching to it ends what was held (the Direct commands' `_hold` entries are ended, a virtual joystick is let go and unloaded). It
  is never changed by a fallback. `Log.txt` says `Yoke buttons: <model>: action Disabled` and then either `the buttons are disabled;
  button_commands are ignored` or `the yoke's buttons are not read`, and once per aircraft file with commands `the buttons are
  disabled; its button_commands are ignored`. The word `None` was never accepted. A state-file word that is not one of the three is
  ignored and named (`button_action_method must be VJoy, Direct or Disabled; ignored`); in a preset the field is skipped and the model
  keeps its own.

The shipped models: the original CLS-E is `VJoy`, the CLS-E NG is `Direct`. It is a setting of the connected yoke:
`ButtonService::set_yoke` applies it when the model becomes known (and again if another model connects), switching the action.

**The virtual joystick is per operating system** (`make_virtual_joystick()`): only Windows has one today. A model asking for `VJoy`
elsewhere gets `Direct` and `Log.txt` says so once per model; MacOS and Linux gain it by implementing that one factory (what was found
looking for a MacOS one is in [macos-virtual-joystick.md](macos-virtual-joystick.md)).

**When the buttons are read at all.** They are only read when something uses them: the `VJoy` action, or `Direct` with at least one
command in `button_commands`; never with `Disabled`. With neither, no register is read (no bus traffic) and `Log.txt` says so once per model. Reading costs one or two
requests, about 2 ms each of the 20 ms sampling cycle, only while it is wanted.

## Controls and names

A **control** is one physical thing on the yoke. The controller reports each as one of the **canonical inputs**:

| Name | Meaning |
|---|---|
| `button_1` to `button_16` | the controller's button slots: on the original CLS-E bits 16 to 31 of `0x60fd` (CLS2Sim's mapping); on the NG bits 0 to 5 of its own record `0x2880:3`: 1 trigger, 2 top left, 3 top right, 4 rocker down, 5 rocker up, 6 hat push |
| `hat_n`, `hat_ne`, `hat_e`, `hat_se`, `hat_s`, `hat_sw`, `hat_w`, `hat_nw` | the hat's positions (1 up, clockwise), `0x2880:4` on the NG; the original CLS-E has no hat |

Which registers a model's buttons come from is the model's `button_registers` ([yoke presets](yoke-presets.md)); see
[What each yoke has](#what-each-yoke-has). The yoke's `button_aliases` gives controls friendly names: a map of lists keyed like
`button_registers`, the key being the control (`button_1` to `button_16`, `hat_n` to `hat_nw`) and the list its friendly names,
`"button_1": ["trigger"]`. A control may have several names; a name belongs to one control. Friendly names are `a-z`, `0-9`, `_`, at
most 32, not ending `_released` or `_hold` and not a button or hat name. The shipped files name the NG's six buttons and the CLS-E's
slots.

**A press is matched on every name of its control**, so a command written under any of them runs for it. `ButtonCommands::resolve`
runs when the connected yoke's model becomes known (and again if it changes), since the map is the model's. A name for a button the
yoke does not have (a slot its controller has no bit for) is a control like any other and never fires.

**vJoy numbering:** a control's virtual-joystick button is its button number (1 to 16), and the hat's eight positions are 17 to 24,
up first, then clockwise.

## Where the buttons are read

`can_layout.hpp` and `can_source.hpp`: each model's `button_registers` is a map from a control (`button_1`.., `hat_n`..) to the controller
object that says it is pressed ([yoke presets](yoke-presets.md)): a `node`, an `object` and `sub`, and either a `mask` (pressed when all
its bits are set in the value read) or a `value` (pressed when the value read is exactly it). In each of the reader's sampling cycles,
on its thread, every distinct object the map names is read once with the sampling timeout (one attempt, 20 ms), each control is
tested against what was read, and the result is published together as a `ButtonState`; the buttons are also kept in
`Snapshot::buttons`. The original CLS-E has one object, node 2's `0x60fd`, with a mask per slot (bits 16 to 31). The NG has two, its
own `0x2880:3` (a mask per button, bits 0 to 5) and `0x2880:4` (the hat, a value per position, 1 to 8; any other value reads as
centred), about 2 ms of the 20 ms cycle each. Only objects on `brunner::button_register`'s read-only allowlist can be asked for
(`0x60fd:0`, `0x2880:3`, `0x2880:4`); a preset naming another is refused. A control with no entry is one the yoke does not have. If
two hat positions test true at once the lower one is the hat. If any reply is missed the whole cycle is dropped, so a half-read state
is never published. A read the controller refuses, or answers with nonsense, leaves the buttons unread with the reason in
`Snapshot::button_refusal`, releases everything at the sink, and is retried from 5 s later; the buttons never stop pitch, roll or
force feel. A missed reply is the reader's to tolerate like any sampling read ([hid-bridge.md](hid-bridge.md)). When the USB session
ends the reader releases everything at the sink. A press shorter than one cycle can fall between two reads: the objects are polled,
not event-driven.

## Presses, releases and holds

The hub turns the states into **edges** of controls (`edges.hpp`). The first state only sets the baseline, so a button held when the
plugin starts runs no press (its release is seen). After that a command runs **once per press edge**: a control going from released
to pressed, or the hat moving onto a position. Holding does not repeat. The matching *release* edge (a control let go, or the hat
moving off a position, to centre or to another position) runs the entry named `<name>_released`. A hat moving straight from one
position to another releases the old one, then presses the new one. In one state the releases come first, then the presses.

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
The one case that cannot be smoothed over is a hat move whose release and press land in different callbacks (0.1 s apart); then the
shared command ends and begins again.

**Nothing stays held:** when the reader goes away it releases everything it had pressed, and when the plugin is disabled the Direct
action ends every held command itself. A release is delivered at the next flight-loop callback (0.1 s), so a tap shorter than that
begins and ends in the same callback.

## Actions

**Direct** (`direct_action.hpp`): each flight-loop callback, on X-Plane's thread, takes the hub's edges and runs the commands through
a `CommandRunner` the plugin implements with the SDK (`XPLMFindCommand` the first time a name is needed, since other plugins' and
aircraft's commands may not exist at start; a name X-Plane does not know is said once and skipped). Commands run whatever the
force-feel, read-only or verified-yoke state is: they are input only. The reader makes no X-Plane SDK calls and does not log.

**VJoy** (`vjoy_action.hpp`): each callback takes the hub's level (`vjoy_word`: bit n-1 for virtual-joystick button n) and gives it to
the feeder: it takes the device when it can, lets every button up, then presses and lets up buttons as the word changes (only
changes are sent). Every change and every refusal is logged. Everything is up whenever nothing is known: before the first state,
after the reader is lost, and when the last state is older than 250 ms (the registers are read every cycle, so silence means the
reading stopped). No driver, the driver switched off, no buttons on device 1 or another program holding it is said once
(`Yoke buttons off: <why>; trying again every 5 seconds`) and retried every 5 seconds; a button the driver refuses lets all up and
gives the device back. The yoke flies exactly as before in every case. The setup of Brunner's vJoy is in [buttons.md](../buttons.md).

## `button_commands`

An optional object in `BrunnerDirect.state.json`, read once at plugin load (restart X-Plane after editing). A key is a name of a
control (a canonical name or a friendly name from the yoke's `button_aliases`, any case), optionally with `_released` or `_hold`
added. A value is an X-Plane command name, or a list of them (`"a/b"` and `["a/b"]` mean the same; a list may name several, which run
in the order written: for a press or release each is run once, for a hold each is begun in order and ended in the same order);
`null`, `""`, `[]` or a missing entry means no command, which is the default for every control, and nothing is written to the file
unless you write it.

Several keys that name the **same control** (`trigger` and `button_1` are one on the NG) are merged: all of their commands run, in
the alphabetical order of the key names, and one note at resolution says which keys collided.

Errors, none thrown, each logged once and the rest still applies:
- `button_commands` not an object: ignored.
- A value that is not a string, a list of strings or `null`: that entry ignored. A list item that is not a string: that item ignored.
- A name that is not a control of the connected yoke: ignored, but kept in the file as written (a save never erases it).
- A command containing spaces or control characters: that command ignored (an empty string in a list is skipped silently).
- A command X-Plane does not know: said once on its first use, nothing runs.
- A save writes each entry back in the form it was read: a string stays a string, a list stays a list.

## Commands for one aircraft

An aircraft can override the owner's `button_commands` with a file of its own, `Output/BrunnerDirect/aircraft/<id>.json` (the `.acf` file's
name, else the ICAO type code; format and rules in [advanced configuration](advanced-configuration.md#button-commands-for-one-aircraft)).
`config/state/aircraft_state.hpp` reads it, `ButtonCommands::resolve(base, aircraft, label, map, notes)` lays it over the state file's
entries and `ButtonService::set_aircraft` applies the result when X-Plane loads or changes the aircraft. The rule is **replace, not add**: for
each control and variant (press, `_released`, `_hold`) an aircraft entry names, the state file's commands for that control and variant are
dropped and the aircraft's stand in their place; an empty entry removes the command on that aircraft; everything not named stays. The
variants are independent: an aircraft's press entry never unsets or replaces the state file's `_released` or `_hold` commands for that
control (they keep running), nor a hold or release entry its press; the same holds for switching off. (`buttons_tests.cpp`,
`an_aircraft_overrides_the_state_file`, pins each case.) Holds are resolved after the override, per command as always
([Presses, releases and holds](#presses-releases-and-holds)). The yoke's
controls (`hub`, `edges`) are not touched by a change of aircraft: only the commands are resolved again, and what the old commands held is ended.

## What each yoke has

| Yoke | Hat switch | Where its buttons are on the bridge |
|---|---|---|
| Original CLS-E | **none** | node 2, `0x60fd`, slots 1 to 5 and 7 to 11: two grips. Slots 12 to 14 (the three front panel buttons) are on the bridge but deliberately not given a control |
| CLS-E NG | yes (8 positions) | node 2, **`0x2880`**: entry 3 the six buttons, entry 4 the hat (its `0x60fd` has slots 1 to 5 only) |
| CLS-E MK2 | reported to have one (not verified) | not known; **no preset yet** |

### The original CLS-E's disabled panel buttons

The original CLS-E has three more controls, the front panel buttons, in slots 12 to 14 of `0x60fd` (found by pressing each control
in turn, 2026-11-10). They are **deliberately disabled**: they also work the yoke's own menu, and on the tested yoke one press switched
the motors on while the plugin was driving it, and the plugin stopped with a fault ([The yoke's buttons](../buttons.md#the-cls-es-front-panel-buttons)).
They have no entry in the model's `button_registers` or `button_aliases`, so they have no name and are never read. What they were,
kept here so they can be turned on again:

| Slot | Control | Name | Friendly name | `button_registers` entry |
|---|---|---|---|---|
| 12 | Panel, middle | `button_12` | `panel_middle` | `{"node": 2, "object": "0x60FD", "sub": 0, "mask": "0x08000000"}` |
| 13 | Panel, right | `button_13` | `panel_right` | `{"node": 2, "object": "0x60FD", "sub": 0, "mask": "0x10000000"}` |
| 14 | Panel, left | `button_14` | `panel_left` | `{"node": 2, "object": "0x60FD", "sub": 0, "mask": "0x20000000"}` |

Each mask is `1 << (15 + slot)`, the same rule as slots 1 to 11 (bits 16 to 26, with no slot 6). To enable them:

1. In `config/yokes/cls-e.json`, add the three names to `button_aliases` (`"button_12": ["panel_middle"]`, and so on) and the three
   entries above to `button_registers`.
2. For the built-in model, which is used when no file is found, raise the loop in `cls_e_can_layout()` (`src/buttons/input/can_layout.hpp`)
   from `n <= 11` to `n <= 14` and add the three names to `cls_e_button_aliases()` (`src/config/yoke/yoke_preset.hpp`).
3. Update the tests in `src/config/yoke/preset_tests.cpp` (`button_aliases`: ten controls and `panel_*` not controls today).
4. Undo the "disabled" wording in [The yoke's buttons](../buttons.md#the-cls-es-front-panel-buttons) and
   [advanced configuration](advanced-configuration.md#names).

In a file you write yourself, `button_aliases` and `button_registers` each replace the model's whole map, so a `BrunnerDirect.yokes.json`
override must list all thirteen controls, not just the three. Before turning them on, find out what the yoke's own menu does with a press
while the plugin is driving it: that is what stopped the plugin.

The CLS-E MK2 is something the owner has been told about, not something Mesmer has run on: it needs its own yoke preset (its USB ids,
its `button_registers` and its `button_aliases`) once someone has one to test, and the probes in `src/hid/mac/` say where its buttons
and hat are.

**The NG's own button record, `0x2880` on node 2** (found 2026-11-16 by watching the vendor's Windows tool, then confirmed with
`src/hid/mac/register_scan_probe_mac.cpp`, read-only; the same object answers on node 1 and reads the same at rest, but node 1 has
not been watched under a press). It is a record with seven entries; entry 0 reads 7, the entry count:

| Entry | Size | Meaning |
|---|---|---|
| `0x2880:3` | 1 byte | the buttons: bit n-1 is button n, 1 to 6 (trigger, top left, top right, rocker down, rocker up, hat push) |
| `0x2880:4` | 1 byte | the hat: 0 centred, 1 to 8 clockwise from up (1 N, 2 NE, 3 E, 4 SE, 5 S, 6 SW, 7 W, 8 NW) |
| `0x2880:1` | 2 bytes | `0x8792` at rest and never changed; unknown |
| `0x2880:2`, `:5`, `:6`, `:7` | 2, 2, 2, 4 bytes | 0 at rest and never changed; unknown |

Compared with `0x60fd` on the same presses (a sweep of eight reads took 16 ms): every press and release appeared in both within one
sweep, the shortest trigger tap (62 ms) was seen 8 times out of 8 in each, and the quick rocker alternations matched edge for edge, so
the two answer equally fast. `0x60fd` has the NG's five buttons and neither the hat push nor the hat; `0x2880` has all of them, so the
NG's `button_registers` name `0x2880`.

## Other yoke models and operating systems

A model that differs gives its own `button_aliases`, `button_registers` (its controls and the objects, masks and values that say they are pressed) and `button_action_method` in its preset (no code). A new
operating system implements `make_virtual_joystick()` (the VJoy action); the hub, edges, holds, commands and service do not change.
