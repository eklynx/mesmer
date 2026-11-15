# Glossary

**Button** — One of the yoke's six joystick buttons, numbered from 1 as the HID report numbers them: 1 trigger, 2 top left, 3 top right, 4 rocker down, 5 rocker up, 6 hat button.

**Hat position** — One of the eight directions the yoke's hat switch can be pushed, numbered 1 to 8 clockwise from up (N, NE, E, SE, S, SW, W, NW). Centred is not a position.

**Rocker** — The two-sided switch on the yoke, which can never have both sides pressed at once. By the owner's labels its sides are buttons 4 (down) and 5 (up).

**Glitched report** — A joystick report that cannot come from a real yoke: buttons 4 and 5 (both sides of the rocker) both set, or any padding bit set. It is ignored whole.

**Press edge** — The moment a button or hat position goes from released to pressed. A command runs once per press edge; holding does nothing. The release edge is the opposite moment and can run its own command (`<name>_released`).

**Hold** — A command begun on the press edge and ended on the release edge (`<name>_hold`), so it acts for as long as the button or hat position is held, as opposed to a command run once.

**Command** — An X-Plane command name, such as `sim/flight_controls/flaps_down`, that a button or hat position runs. An entry can name one command or a list, which run in the order written.

**Yoke profile** — What a yoke model's joystick reports may contain: how many buttons it has and which reports are genuine. All models share one default profile today.
