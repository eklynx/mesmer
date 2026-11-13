# force/ - forces from the simulation

What the simulation asks the yoke to feel: the elevator and aileron models and their
solvers (`elevator.hpp`, `aileron.hpp`), the drive limits and blends they stay within
(`tuning.hpp`), the air and panel readings that feed them (`flight.hpp`, `slew.hpp`),
the aircraft profiles (`profiles.hpp`) and the stall buffet (`buffet.hpp`).

The models are written twice (here and in `../elevator.html`, `../aileron.html`) and kept
equal by `../*_vectors.json`. Writing a solution to the controllers is `../brunner/loading.hpp`.
`../brunner/core.hpp` includes `aileron.hpp` (the solution types in `Snapshot`), the one place
`brunner/` and `force/` meet.
