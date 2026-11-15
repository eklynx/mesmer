// SOURCE OF TRUTH for turning decoded joystick reports into button and hat presses and mapping them to X-Plane
// commands. Specification: docs/dev/button-commands.md. Portable; read this header, not button_input.cpp.
//
// Happy path: ButtonInput::feed(state) takes each decoded report (hid/report_decoder.hpp). A report the
// YokeProfile calls invalid is dropped (the previous good state stays) and feed() says why; a genuine one yields
// the edges: inputs that went from released to pressed (released=false) and from pressed to released
// (released=true), once each, releases before presses. The first genuine report only sets the baseline.
// A hat position is pressed when the hat moves onto it and released when it moves off (to centre or elsewhere). ButtonCommands holds the
// owner's map (OwnerState::button_commands) and answers commands_for(input): the X-Plane command names to run, in
// the order written (one, or several when the owner wrote a list), or none for "do nothing", which is the default
// for every button and hat position.
//
// Error cases (none thrown): ButtonCommands::from_state notes (and ignores) a name the profile does not have and a
// value that is not a usable command; empty and null values are not errors, they mean no command. ButtonInput is
// for one thread (the joystick reader); PressQueue is the thread-safe hand-over to X-Plane's thread.
#pragma once
#include "brunner/owner_state.hpp"
#include "brunner/yoke_profile.hpp"
#include <map>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace brunner {
enum class InputKind { button, hat };
struct InputId {
    InputKind kind = InputKind::button;
    int index = 0;   // button 1..N; hat 1..8 (1 up, clockwise)
    bool released = false;   // the release edge of this input (named "<name>_released"), else the press edge
    bool hold = false;       // a configuration entry only, never an edge: the command held while pressed ("<name>_hold")
    bool operator==(const InputId& o) const { return kind == o.kind && index == o.index && released == o.released && hold == o.hold; }
    bool operator<(const InputId& o) const {
        return kind != o.kind ? kind < o.kind : index != o.index ? index < o.index : released != o.released ? released < o.released : hold < o.hold;
    }
};
// "button_3", "hat_n", "hat_ne", ... "hat_nw": the names used in BrunnerDirect.state.json; the release edge
// adds "_released" ("button_3_released", "hat_n_released") and the held command adds "_hold" ("button_3_hold").
// An entry named "<name>" runs once on the press edge, "<name>_released" once on the release edge, and
// "<name>_hold" is begun on the press edge and ended on the release edge (XPLMCommandBegin / XPLMCommandEnd);
// any combination may be set. "_released_hold" is not a name.
std::string input_name(const InputId& id);
// The input a name stands for, or nullopt when the profile has no such button or hat position.
std::optional<InputId> parse_input_name(std::string_view name, const YokeProfile& profile);
// Every input the profile has, buttons first then the eight hat positions (the press edges; set `released` for the others).
std::vector<InputId> all_inputs(const YokeProfile& profile);

class ButtonCommands {
public:
    // notes gets one line per ignored entry, worded for the owner (for Log.txt).
    static ButtonCommands from_state(const std::map<std::string, ButtonEntry>& entries,
                                     const YokeProfile& profile, std::vector<std::string>& notes);
    // The commands to run for this entry, in order; empty when there are none (unset, null, [] or empty strings).
    const std::vector<std::string>& commands_for(const InputId& id) const;
    // How many entries have at least one command.
    std::size_t size() const { return commands_.size(); }
private:
    std::map<InputId, std::vector<std::string>> commands_;
};

class ButtonInput {
public:
    explicit ButtonInput(const YokeProfile& profile) : profile_(&profile) {}
    struct Result {
        std::vector<InputId> edges;   // releases first, then presses
        std::string_view dropped;   // empty when the report was used, else why it was dropped
    };
    Result feed(const hid::JoystickState& state);
    // The release edges for everything the last genuine report had pressed (the yoke went away: nothing may
    // stay held); empty before any genuine report. The next feed() is a new baseline.
    std::vector<InputId> release_all();
private:
    const YokeProfile* profile_;
    std::optional<hid::JoystickState> last_;
};

// What to do with "_hold" entries for one batch of edges (everything the queue held since the last flight-loop
// callback), as the XPLMCommandBegin / XPLMCommandEnd calls to make, in order. Holds are kept per COMMAND, not per
// control: after a batch the commands that should be held are those of every "_hold" entry whose control is pressed,
// and only the difference from what is begun now is done. So when the hat moves from an entry listing ["rot_up"] to
// one listing ["rot_up", "rot_left"], rot_up is not ended and begun again; only rot_left begins (and the other way
// round only rot_left ends). A command two pressed controls share stays begun until both let go. Every end comes
// before any begin. A control pressed and released within one batch (a tap) still begins and ends its commands, after
// the rest, unless they are held anyway. Nothing is ever left begun without its end. held() says whether a command is
// begun now; end_all() ends everything (the plugin is stopping) and forgets it. X-Plane's thread only.
struct HoldAction {
    std::string command;   // the X-Plane command name
    InputId source;        // the "_hold" entry (hold = true) that wants it, for messages
    bool begin = false;    // false: end
};
class HoldTracker {
public:
    std::vector<HoldAction> plan(const std::vector<InputId>& edges, const ButtonCommands& commands);
    std::vector<HoldAction> end_all();
    bool held(const std::string& command) const;
private:
    std::vector<InputId> active_;                                  // "_hold" entries whose control is pressed, in press order
    std::vector<std::pair<std::string, InputId>> begun_;           // commands begun, in begin order, and who wanted them
};

// Presses from the reader thread to X-Plane's thread.
class PressQueue {
public:
    void push(const InputId& id);
    std::vector<InputId> drain();
private:
    std::mutex mutex_;
    std::vector<InputId> items_;
};
}
