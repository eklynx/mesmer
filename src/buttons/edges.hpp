// SOURCE OF TRUTH for turning a stream of ButtonStates into presses and releases of CONTROLS. Specification:
// docs/dev/button-commands.md. Portable; read this header, not edges.cpp.
//
// Happy path: an EdgeDetector knows a ControlMap and which read method produced the states. feed(state) returns the edges
// since the previous state: a control that went from released to pressed (released=false) or back (released=true), once
// each, releases before presses. The first state only sets the baseline, so a button held at startup is not a press (its release
// is seen). A hat position is pressed when the hat moves onto it and released when it moves off it (to centre or elsewhere).
// release_all() returns the release of everything last seen pressed and forgets it: the next feed() is a new baseline.
//
// Error cases (none thrown): state bits beyond what the read method has (HID: 6 buttons, CanBus: 16 slots) are ignored; the hat is
// the hat position under HID and the can_hat position under CanBus (a yoke whose registers have no hat publishes none). pressed_controls() is the same reading of a state, as a list of control numbers in input order
// (buttons ascending, then the hat), for anyone who needs the level instead of the edges.
#pragma once
#include "buttons/controls.hpp"
#include "buttons/methods.hpp"
#include "buttons/state.hpp"
#include <memory>
#include <optional>
#include <vector>

namespace buttons {
struct Edge {
    int control = 0;
    bool released = false;
    bool operator==(const Edge& o) const { return control == o.control && released == o.released; }
};
std::vector<int> pressed_controls(const ControlMap& map, ReadMethod read, const ButtonState& state);

class EdgeDetector {
public:
    EdgeDetector(std::shared_ptr<const ControlMap> map, ReadMethod read) : map_(std::move(map)), read_(read) {}
    std::vector<Edge> feed(const ButtonState& state);
    std::vector<Edge> release_all();
private:
    std::shared_ptr<const ControlMap> map_;
    ReadMethod read_;
    std::optional<std::vector<int>> last_;   // the controls pressed in the last state
};
}
