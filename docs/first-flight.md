# Your first flight

About fifteen minutes in the stock Cessna 172 SP. You'll centre the yoke, fly
with it as a plain control, then try force feel on the ground before you
take it into the air. Read [SAFETY.md](../SAFETY.md) first.

## Before you start

- Bind a key or a spare joystick button to **`brunner_direct/stop_motors`**
  in X-Plane's *Settings > Keyboard* (or *Joysticks*). Search for "Stop
  yoke motors". This is your quick stop.
- If you have a tablet or a second screen, open the feel page. Its address is
  in the menu as *Feel tuning: (your PC's address):8087*.
  Type `http://` and that address into a browser on the same network.

## 1. Centre the yoke

1. Switch the yoke on. Start X-Plane and load the Cessna 172 SP on a runway
   or parking spot.
2. In the top left corner a line reads **Yoke stopped - Retry to resume**.
   The plugin is waiting for you before it moves the yoke the first time.
3. Take your hands off the yoke. Click the line, or choose
   *Plugins > Yoke (Brunner) > Retry*.
4. The line reads **Yoke centring - hands off**. Once the yoke has been still
   for a second it drives itself to the middle of its travel, then lets go.
5. The line disappears. The menu's top line reads **Ready - force feel off**.

From here on you don't need to do this again in this X-Plane session. If
you switch the yoke off and on, or pull and replug the USB cable, the
plugin centres it again by itself.

## 2. Fly with the yoke as a plain control

With force feel off the motors are off. The yoke moves freely and gives
X-Plane pitch and roll in any aircraft. Check:

- Pull back: the elevator goes up. Turn right: the right aileron goes up.
- Pause X-Plane and resume: the controls come back by themselves.
- Change aircraft: the controls work in the new one.

## 3. Force feel, parked

1. Parked, engine off, brakes on. Rest a light hand on the yoke.
2. Choose *Plugins > Yoke (Brunner) > Force feel*. The menu item gets a
   check mark, and the line reads *Ready - force feel starting*, then
   *Ready - force feel on*. The change blends in over half a second.
3. **What you should feel:** the yoke eases forward. That is the Cessna's
   elevator, a little heavier at its trailing edge, sagging with no air on
   it. The wheel stays where you leave it: no air over the ailerons, so
   nothing centres it.
4. Turn on *Plugins > Yoke (Brunner) > Keep yoke feel on screen* to keep
   the feel card up in the corner.

## 4. A run-up

1. Engine on. Brakes on. Bring the power up slowly to the run-up.
2. **What you should feel:** pitch firms up and the yoke comes back towards
   the middle as the propwash builds over the tail. Roll firms up a little.
3. Bring the power back. The yoke softens and eases forward again.

## 5. Into the air

1. Take off and climb out as usual. The yoke gets firmer with speed.
2. **Trim.** Trim nose up and down with your usual trim control. The yoke's
   resting point moves with the trim, so a well trimmed aircraft needs no
   hand on the yoke. The plugin reads X-Plane's trim and never changes it.
3. **Slow flight against cruise.** At about 60 kt the yoke is noticeably
   lighter than at 100 kt.
4. **A bank.** Roll into a 30° bank each way. The wheel's resting point leans
   a little into the turn.

In cruise the model wants more stiffness than the yoke's motors can give, so
the feel card and the feel page say *Saturated* or *at the drive's limit*.
The yoke then feels lighter than the real aircraft but still rests in the
right place.

## Stop if

- the yoke knocks, buzzes, hunts back and forth, or runs to a stop by itself;
- it pushes the wrong way (forward when it should come back, or the wheel
  pulled to the opposite side from where you put it);
- anything feels wrong.

Use *Stop yoke motors*. Pitch and roll keep working. Then please
[tell us](https://github.com/antialias/mesmer/issues/new/choose), with
the airspeed, power and what the feel page showed.

## After the flight

Force feel is off again the next time X-Plane starts. To start every flight
with it on, set `force_feel=1` in `BrunnerDirect.ini`; to skip the first
*Retry*, set `auto_start=1`. Both can also go in `BrunnerDirect.state.json`, which an
update does not replace. See [configuration](configuration.md).
