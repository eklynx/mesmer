# xplane/ - the X-Plane datarefs

`sim.hpp` is every dataref the plugin reads or writes in X-Plane (axis and trim overrides,
pause, the flight model's air, the aircraft). `published.hpp` is the plugin's own
`brunner_direct/...` datarefs. The headers are the interface; `.cpp` files are the X-Plane
implementation. Nothing here knows about the yoke, the menu or the feel page; `../plugin.cpp`
wires them together.
