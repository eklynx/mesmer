# Glossary

**Control** — One physical thing on the yoke: a button, one side of a rocker, a hat position. A control can be seen two ways, one per read method, and has several names; the yoke's `button_aliases` says which names are the same control.

**Canonical input** — A name a read method can see: `hid_button_1` to `hid_button_6` (the CLS-E NG's joystick buttons: 1 trigger, 2 top left, 3 top right, 4 rocker down, 5 rocker up, 6 hat push), the hat positions `hat_n` to `hat_nw`, `can_button_1` to `can_button_16` (the CAN slots) and `can_hat_n` to `can_hat_nw` (the hat as the bridge reports it).

**Hat position** — One of the eight directions the yoke's hat switch can be pushed, numbered 1 to 8 clockwise from up (N, NE, E, SE, S, SW, W, NW). Centred is not a position.

**CAN slot** — One of the buttons a yoke's controller (a CANopen device on the bridge) reports in a register: on the original CLS-E 16 slots in bits 16 to 31 of node 2's `0x60fd`, on the NG six in its own record. Which register a model uses is its `button_registers`. Read with the `CanBus` read method.

**Alias** — A name for a control that the yoke's config gives, or a link between names: `"trigger": ["hid_button_1", "can_button_1"]` makes the three names one control. Friendly names are the custom ones.

**Read method** — Where the buttons are read from: `CanBus` (over the yoke's bridge) or `HID` (the NG's own joystick reports). A yoke model's preset gives it (`button_read_method`).

**Action method** — What the plugin does with the buttons: `VJoy` (press a virtual joystick's buttons) or `Direct` (run X-Plane commands from `button_commands`). A yoke model's preset gives it (`button_action_method`).

**Rocker** — The two-sided switch on the yoke, which can never have both sides pressed at once. On the NG its sides are `hid_button_4` (down) and `hid_button_5` (up).

**Glitched report** — A joystick report that cannot come from a real yoke: both sides of the rocker set, or any padding bit set. It is ignored whole.

**Press edge** — The moment a control goes from released to pressed. A command runs once per press edge; holding does nothing. The release edge is the opposite moment and can run its own command (`<name>_released`).

**Hold** — A command begun on the press edge and ended on the release edge (`<name>_hold`), so it acts for as long as the control is held, as opposed to a command run once.

**Command** — An X-Plane command name, such as `sim/flight_controls/flaps_down`, that a control runs. An entry can name one command or a list, which run in the order written.

**HID profile** — What a yoke model's joystick reports may contain, which reports are genuine. All models share one default profile today.

**Virtual joystick** — A joystick that exists only in software and that X-Plane binds like a real one; the `VJoy` action presses its buttons. Windows has Brunner's vJoy driver; other systems have none yet.
