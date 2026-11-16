// SOURCE OF TRUTH for the commands a `<name>_hold` entry begins and ends. Specification: docs/dev/button-commands.md.
// Portable; read this header, not hold.cpp.
//
// A batch of Edges (everything that arrived since the last flight-loop callback) goes to plan(), which returns the
// XPLMCommandBegin / XPLMCommandEnd calls to make, in order. Holds are kept per COMMAND, not per control: after a batch the
// commands that should be begun are those of every hold entry whose control is pressed, and only the difference from what is
// begun now is done. A command that two pressed controls share stays begun until both let go, and a command the next hat
// position also lists is not ended and begun again. Every end comes before any begin. A control pressed and released
// within one batch (a tap) still begins and ends its commands, after the other ends, unless they are held anyway. Nothing
// is ever left begun without its end. end_all() ends everything (the plugin is stopping) and forgets it. held() says whether
// a command is begun now. Not thread-safe: X-Plane's thread only. No errors are thrown.
#pragma once
#include "buttons/commands.hpp"
#include "buttons/edges.hpp"
#include <string>
#include <utility>
#include <vector>

namespace buttons {
struct HoldAction {
    std::string command;   // the X-Plane command name
    int control = 0;       // the control that wants it, for messages
    bool begin = false;    // false: end
};
class HoldTracker {
public:
    std::vector<HoldAction> plan(const std::vector<Edge>& edges, const ButtonCommands& commands);
    std::vector<HoldAction> end_all();
    bool held(const std::string& command) const;
private:
    std::vector<int> active_;                                  // controls with a hold entry that are pressed, in press order
    std::vector<std::pair<std::string, int>> begun_;           // commands begun, in begin order, and who wanted them
};
}
