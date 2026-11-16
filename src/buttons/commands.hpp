// SOURCE OF TRUTH for what the owner's button_commands mean for the connected yoke. Specification:
// docs/dev/button-commands.md. Portable; read this header, not commands.cpp.
//
// button_commands (BrunnerDirect.state.json) maps names to X-Plane commands: `<name>` runs once when the control is pressed,
// `<name>_released` once when it is let go, `<name>_hold` is begun on the press and ended on the release. A name is any name of
// a control (buttons/controls.hpp): button_N, hat_<dir> or a friendly name from the yoke's button_aliases.
// Each entry's value is a list of command names (the state file's single string is a list of one; null and "" are an empty list).
//
// Happy path: ButtonCommands::resolve(entries, map, notes) turns every entry into commands of a control and a variant.
// commands_for(control, variant) is what to run, in order. Several entries that name the SAME control (trigger and button_1
// are one control on the NG) are MERGED: all their commands run, in the alphabetical order of the entry names, and
// one note says which entries collided.
//
// An aircraft can override: resolve(base, aircraft, label, map, notes) resolves the state file's entries, then the aircraft's, which
// REPLACE, they do not add: for each control and variant (press, `_released`, `_hold`) an aircraft entry names, the state file's commands
// for that control and variant are dropped and the aircraft's stand in their place (an entry that is null, "" or [] leaves it with no
// command on this aircraft). Controls and variants the aircraft does not name keep the state file's: the three variants are independent,
// so an aircraft's press entry never unsets or replaces the state file's `_released` or `_hold` for that control, nor the other way round. A name that is two names of one
// control (trigger and button_1) is the same control and variant in both files. The aircraft's own entries merge among themselves like
// any others. `label` names the aircraft in notes ("aircraft C172").
//
// Error cases, none thrown, each as a line in `notes` for Log.txt, the rest still applies: a name that is not a control of this
// yoke is ignored; a command with spaces or control characters is ignored. A control the yoke does not have (a button its
// controller has no slot for) is a control of the map like any other: its commands are kept and never fire.
#pragma once
#include "buttons/controls.hpp"
#include "buttons/methods.hpp"
#include <map>
#include <string>
#include <vector>

namespace buttons {
enum class Variant { press, release, hold };

class ButtonCommands {
public:
    // name as written in the state file -> its commands.
    using Entries = std::map<std::string, std::vector<std::string>>;
    static ButtonCommands resolve(const Entries& entries, const ControlMap& map, std::vector<std::string>& notes);
    static ButtonCommands resolve(const Entries& base, const Entries& aircraft, const std::string& label, const ControlMap& map, std::vector<std::string>& notes);
    // Empty when there are none.
    const std::vector<std::string>& commands_for(int control, Variant variant) const;
    // How many (control, variant) pairs have at least one command.
    std::size_t size() const { return commands_.size(); }
private:
    std::map<std::pair<int, Variant>, std::vector<std::string>> commands_;
};
}
