# Read-only mode: specification

What Mesmer does with a yoke whose controller firmware it does not drive.
The code it describes: `src/readonly.hpp` (the rules; read it first),
`src/bridge.hpp` (`ReadOnlyBridge`), `src/reader.cpp` (`read_only_session`),
`src/reference.hpp` (`survey_controllers`), `src/yoke.hpp` and `src/plugin.cpp`.
`src/readonly_tests.cpp` checks every statement below.

## When

Mesmer drives the original CLS-E, whose controllers report firmware **367**.
If either controller on a listed USB bridge ([hid-bridge.md](hid-bridge.md))
reports anything else, for example **642** on a CLS-E NG, the yoke is
**read-only**: automatically, with no setting. Unlisted USB ids are never
opened.

## The guarantee: nothing is written

- The read-only session is given a `ReadOnlyBridge`, a type with no write
  members. It cannot write to the yoke, whatever it is asked to do.
- Every report it sends is an SDO *upload* (a read request).
- Re-centre, force feel, recovery, loading and the supervised tests are
  refused when asked for (`Reader::request_*` return false) and are ignored if
  they get as far as the session. The supervisor, seeing a read-only
  snapshot, never asks for any of them. The plugin menu, the datarefs and the
  feel page's buttons say so in `Log.txt`.
- The motors are not stopped, recovered or touched when the session starts.
  (A CLS-E on firmware 367 still does that, as before.)

## What it does

1. **Shows the state.** The status line reads *Yoke firmware 642 not
   supported: read-only, no force feel*, or, until the travel is learned, how
   far each axis has come.
2. **Feeds X-Plane's pitch and roll** from the live encoder, mapped by a
   travel it learns (below). Experimental.
3. **Surveys the controllers** on request (Developer menu, *Survey yoke
   registers*): live firmware, status, encoder and position, and every
   register the allowlist can read, with SDO uploads only. A register the
   controller lacks is recorded as `"unsupported 0x..."` and does not stop the
   survey. The result is `Output/BrunnerDirect/survey-<time>.json`.

## Learned travel

The CLS-E's ranges (roll ±13000, pitch ±12000) and its motor-driven centring
cannot be assumed for another controller, so read-only input learns each
axis's travel by watching the encoder's extremes:

- Move the yoke to all four stops. An axis is *learned* once it has moved at
  least **4000 counts** (about a third of the CLS-E's travel); until both are,
  no input reaches X-Plane and the line shows *roll N%, pitch N%*.
- The centre is the **midpoint** of the observed range, whatever the
  encoder's own zero is; the ends are -1 and +1; beyond what was seen is
  clamped. The range only ever widens.
- Status bits never gate this input: their meaning on this firmware is
  unknown, and nothing is written whatever they say. Data older than 250 ms
  is not used.
- **Direction** is not detectable. If an axis is backwards, set
  `read_only_roll_reverse=1` or `read_only_pitch_reverse=1` in
  `BrunnerDirect.ini` (see [configuration.md](configuration.md)).

### Remembered range

The learned range is kept in `Output/BrunnerDirect/read-only-travel.json`,
written atomically at most every five seconds and only when it widened, and
when the plugin is disabled:

```json
{"firmware":[642,642],"roll":{"low":-9000,"high":9100},"pitch":{"low":-7000,"high":7200}}
```

At start it is used as the starting range, only if it is well-formed, for the
**same firmware**, and sane (low <= high, within one billion counts); anything
else is ignored. Delete the file to start over. It is keyed by firmware only,
not by yoke.

## Known limits

- Experimental: the NG's encoder scale, direction and end-stop behaviour are
  unverified. The NG also presents a standard USB joystick that X-Plane can use
  without Mesmer.
- A fault in a controller is shown but not acted on: nothing is written, and
  nothing is moved by Mesmer.
- No force feel on this firmware. Supporting it needs the controller's
  register map and force limits, which the survey is meant to help collect.
