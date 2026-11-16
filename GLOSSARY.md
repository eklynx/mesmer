# Glossary

**Control** — One physical thing on the yoke: a button, one side of a rocker, a hat position. It has a canonical input and may have friendly names; the yoke's `button_aliases` gives them.

**Canonical input** — The name the yoke's controller reports a control under: `button_1` to `button_16` (the controller's button slots) and the hat positions `hat_n` to `hat_nw`.

**Hat position** — One of the eight directions the yoke's hat switch can be pushed, numbered 1 to 8 clockwise from up (N, NE, E, SE, S, SW, W, NW). Centred is not a position.

**Button slot** — One of the buttons a yoke's controller (a CANopen device on the bridge) reports in a register: on the original CLS-E 16 slots in bits 16 to 31 of node 2's `0x60fd`, on the NG six in its own record. Which object and bit a model uses for each is its `button_registers`.

**Alias** — A friendly name for a control that the yoke's config gives: `"button_1": ["trigger"]` makes `trigger` another name for `button_1`.

**Action method** — What the plugin does with the buttons: `VJoy` (press a virtual joystick's buttons) or `Direct` (run X-Plane commands from `button_commands`). A yoke model's preset gives it (`button_action_method`).

**Rocker** — The two-sided switch on the yoke, which can never have both sides pressed at once. On the NG its sides are `button_4` (down) and `button_5` (up).

**Press edge** — The moment a control goes from released to pressed. A command runs once per press edge; holding does nothing. The release edge is the opposite moment and can run its own command (`<name>_released`).

**Hold** — A command begun on the press edge and ended on the release edge (`<name>_hold`), so it acts for as long as the control is held, as opposed to a command run once.

**Command** — An X-Plane command name, such as `sim/flight_controls/flaps_down`, that a control runs. An entry can name one command or a list, which run in the order written.

**Virtual joystick** — A joystick that exists only in software and that X-Plane binds like a real one; the `VJoy` action presses its buttons. Windows has Brunner's vJoy driver; other systems have none yet.
