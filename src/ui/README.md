# ui/

- `menu/` - the Yoke (Brunner) menu (`menu.hpp` is the interface).
- `view/` - what is drawn: the corner notice (`startup_notice.hpp`), the feel card (`hud.hpp`),
  the force monitor (`panel*.hpp`), the tail drawing (`tail.hpp`), the frames they show (`telemetry.hpp`).
- `workbench/` - the feel page: a pure core (`workbench.hpp`) and its small HTTP server. The page
  itself (`../workbench.html`) and the model pages stay in `src/` because they refer to each other.

The windows and the menu show what `../plugin.cpp` gives them and decide nothing.
