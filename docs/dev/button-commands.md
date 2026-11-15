# The yoke's buttons and hat run X-Plane commands: specification

How Mesmer turns the CLS-E NG's joystick buttons and hat into X-Plane commands. The code it describes, in
reading order: `src/hid/joystick_device.hpp` (the seam to the OS), `src/hid/report_decoder.hpp` (descriptor and
reports), `src/brunner/yoke_profile.hpp` (which reports are genuine), `src/brunner/button_input.hpp` (presses
and the command map), `src/brunner/button_runner.hpp` (the thread), `src/brunner/owner_state.hpp` (the
`button_commands` key), and `src/plugin.cpp` (`run_button_presses`). `src/hid/joystick_tests.cpp` checks every
statement in the sections below. Terms are in [GLOSSARY.md](../../GLOSSARY.md). Setting it up is in
[advanced configuration](advanced-configuration.md#button-commands).

## The interface Mesmer reads

The NG's USB presents two HID interfaces ([hid-bridge.md](hid-bridge.md) covers the first):

| Interface | Usage | Mesmer |
|---|---|---|
| Bridge | vendor page `0xFF00` / `0xFF02`, 64-byte reports | Seized; the controller protocol |
| Joystick | Generic Desktop `0x01` / Joystick `0x04`, 6-byte reports, no report ID | Observed: shared (`kIOHIDOptionsTypeNone` on macOS, `GENERIC_READ` shared both ways on Windows), read-only, never written. X-Plane keeps reading the same reports |

The joystick interface is found by Brunner's vendor id (`0x25BB`) **and** its top-level usage, in the OS's own
match, so the bridge is never touched by the search; the search's shared open is released again before the joystick
device itself is opened. It is opened only when `button_commands` has at least one command: with none, Mesmer
opens nothing but the bridge, and `Log.txt` says so once at start. (That also means glitch drops are only logged
once there is something bound.) More than one
match is refused ("found several"); none, or no permission, is not an error: Mesmer looks again every two seconds
and `Log.txt` says why when the OS says.

On Windows this is the button path only when `BrunnerDirect.ini` says `windows_button_style=HID`
([advanced configuration](advanced-configuration.md#windows-button-style)). With `VJoy`, the default, Windows feeds the vJoy virtual joystick instead
([buttons.md](../buttons.md)) and the joystick interface is never opened, whatever `button_commands` holds. macOS
has no choice to make. Either way the reader still reads node 2's button word over the bridge each cycle.

On Windows the same code is used for everything except opening the interface and reading from it
(`src/hid/win/joystick_win.cpp`). The interface is found through the same HID walk the bridge uses
(`src/hid/win/hid_common_win.hpp`) and opened `GENERIC_READ`, shared both ways: Windows gives every program that
opens a joystick its own copy of each report, so X-Plane still gets them. Windows gives no program the raw report
descriptor, so the layout comes from Windows' own parse of it instead. Each input usage is written all ones into an
empty report (`HidP_SetUsages`, `HidP_SetUsageValue`), and the bits that change are that usage's.
`layout_from_fields` (`src/hid/report_decoder.hpp`) turns those bits into the same `ReportLayout` that
`parse_report_layout` gets from the descriptor. On the NG they match bit for bit (checked with
`brunner_joystick_probe`, built with the Windows tools). Windows starts every report with a report-ID byte, 0
when the device uses none, and the payload is what follows it.

## Report layout (from the descriptor, not fixed offsets)

`parse_report_layout` reads the interface's report descriptor. On the NG (captured with
`src/hid/mac/joystick_probe_mac.cpp`, 97 bytes) it finds:

| Bits of the 6-byte report | Meaning |
|---|---|
| byte 0, bits 0 to 5 | Buttons 1 to 6 (bit n-1 is button n) |
| byte 0, bits 6 and 7 | Padding, always 0 in a genuine report |
| byte 1, low nibble | Hat: 0 centred, 1 to 8 clockwise from up (1 N, 2 NE, 3 E, 4 SE, 5 S, 6 SW, 7 W, 8 NW) |
| byte 1, high nibble | Padding |
| bytes 2 to 3, 4 to 5 | X and Y (little endian); not read |

A hat value outside the descriptor's range decodes as centred. A report shorter than the layout is skipped.

## Glitched reports

On macOS the NG sometimes sends reports that no real yoke state produces. Measured over 2,470 reports: 15%
were `7d 00 xx xx 00 00`, with buttons 1, 3, 4, 5 and 6 set, a padding bit set and X/Y collapsed. A report is
dropped whole (the previous genuine state stays, so nothing is pressed or released by it) when the yoke
profile says so. The default profile, used for every model, drops a report with:

- buttons 4 and 5 both set (the rocker's two sides, which cannot be pressed together; buttons 5 and 6, rocker up with the hat pushed in, can), or
- any padding bit set.

Every dropped report is counted by rule, and at most every 10 seconds `Log.txt` says
`Joystick buttons: dropped N glitched reports (rule: count, ...)`, **whether or not `debug_log` is on** (the line is
also in the recording when `debug_log=1`). Nothing is logged in a period with no drops. Other operating systems have
no joystick reader yet; the decoder, profile and filter are the same code, so one only needs a `JoystickDevice`. (macOS and Windows have one.)

Because Mesmer cannot stop X-Plane seeing the same reports, **leave X-Plane's own bindings for these
buttons and the hat unset** and bind commands in `button_commands` instead.

## Presses

`ButtonInput` runs on the reader thread. The first genuine report only sets the baseline, so a button held
when the plugin starts runs nothing. After that a command runs **once per press edge**: a button going from
released to pressed, or the hat moving onto a position. Holding does not repeat. The matching *release* edge (a button going from
pressed to released, or the hat moving off a position, to centre or to another position) runs the command named
`<name>_released`, if there is one. A hat moving straight from one position to another releases the old one,
then presses the new one. In one report the releases run first (buttons in order, then the hat), then the
presses (buttons, then the hat). A glitched report causes neither edge; a button already held when the plugin
starts runs no press, but its release is seen.

### Once, or held

Three entries per input, any combination, all optional:

| Entry | Runs | X-Plane call |
|---|---|---|
| `<name>` | on the press edge | `XPLMCommandOnce` |
| `<name>_released` | on the release edge | `XPLMCommandOnce` |
| `<name>_hold` | begun on the press edge, ended on the release edge | `XPLMCommandBegin`, then `XPLMCommandEnd` |

Use `_hold` for commands that act while they are held (trim, brake, push to talk, panning the view) and the
plain entry for ones that act once (flaps, toggles). `_released_hold` and `_hold_released` are not names.
Holds are kept **per command**, not per control (`HoldTracker`). After each flight-loop callback the commands that
should be held are those of every `_hold` entry whose control is pressed, and only the difference from what is
begun now is done. So the hat moving from `hat_n_hold: ["rot_up"]` to `hat_ne_hold: ["rot_up", "rot_right"]` begins
`rot_right` and leaves `rot_up` running; moving back ends only `rot_right`. A command that two pressed controls share
stays begun until both let go. Within a callback every end comes before any begin, and the once-commands run first,
in the order they happened. A control pressed and released within one callback (a tap) still begins and ends its
commands, after the other ends, except those held anyway. The one case that cannot be smoothed over is a hat move
whose release and press land in different callbacks (0.1 s apart); the yoke reports both in one report, so this is
rare, and then the shared command ends and begins again.

**Nothing stays held:** when the yoke goes away (unplugged, the interface fails, the reader stops) the reader
releases everything it had pressed, and when the plugin is disabled it ends every held command itself.
A release is delivered at the next flight-loop callback (0.1 s), so a
tap shorter than that begins and ends in the same callback.

Presses cross to X-Plane's thread through `PressQueue`; `update()` drains it each flight-loop callback
(0.1 s) and calls `XPLMCommandOnce`. The reader thread makes no X-Plane SDK calls and does not log.
Commands run whatever the force-feel, read-only or verified-yoke state is: they are input only.

## `button_commands`

An optional object in `BrunnerDirect.state.json`, read once at plugin load (restart X-Plane after editing). Names are `button_1` to `button_6` and `hat_n`, `hat_ne`, `hat_e`,
`hat_se`, `hat_s`, `hat_sw`, `hat_w`, `hat_nw`; each also has a release name, the same with `_released` added
(`button_1_released`, `hat_n_released`), and a held name with `_hold` added (`button_1_hold`, `hat_n_hold`). A value is an X-Plane command name, or a list of them (`"a/b"` and `["a/b"]` mean the same; a list may name
several, which run in the order written: for a press or release each is run once, for a hold each is begun in
order and ended in the same order); `null`, `""`, `[]` or a missing entry means no command, which is the default for every button and hat position, and nothing is written to the
file unless you write it.

Errors, none thrown, each logged once at load and the rest still applies:
- `button_commands` not an object: ignored.
- A value that is not a string, a list of strings or `null`: that entry ignored.
- A list item that is not a string: that item ignored, the others stay.
- A name that is not a button or hat position (optionally with `_released` or `_hold`) of the yoke profile: ignored, but kept in the file as written
  (a save never erases it).
- A command containing spaces or control characters: that command ignored (an empty string in a list is skipped
  silently, as for `null`).
- A save writes each entry back in the form it was read: a string stays a string, a list stays a list.
- A command X-Plane does not know: found on its first press (other plugins and aircraft add commands after
  start); `Log.txt` says so once and nothing runs.

## Other yoke models

A model that differs adds a `YokeProfile` in its own folder under `src/brunner/` and returns it from
`yoke_profile_for`; the decoder, filter, map and thread do not change.
