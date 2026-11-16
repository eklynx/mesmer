// SOURCE OF TRUTH for what the owner's button_commands mean for the connected yoke. Specification:
// docs/dev/button-commands.md. Portable; read this header, not commands.cpp.
//
// button_commands (BrunnerDirect.state.json) maps names to X-Plane commands: `<name>` runs once when the control is pressed,
// `<name>_released` once when it is let go, `<name>_hold` is begun on the press and ended on the release. A name is any name of
// a control (buttons/controls.hpp): hid_button_N, can_button_N, hat_<dir> or a friendly name from the yoke's button_aliases.
// Each entry's value is a list of command names (the state file's single string is a list of one; null and "" are an empty list).
//
// Happy path: ButtonCommands::resolve(entries, map, read, notes) turns every entry into commands of a control and a variant.
// commands_for(control, variant) is what to run, in order. Several entries that name the SAME control (trigger and hid_button_1
// and can_button_1 are one control) are MERGED: all their commands run, in the alphabetical order of the entry names, and
// one note says which entries collided.
//
// Error cases, none thrown, each as a line in `notes` for Log.txt, the rest still applies: a name that is not a control of this
// yoke is ignored; a command with spaces or control characters is ignored; an entry whose control the active read method cannot
// see (a control with no CAN slot under CanBus, none with no HID input under HID) is kept but noted, since it will not fire
// until the yoke's button_aliases link it or the read method changes.
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
    static ButtonCommands resolve(const Entries& entries, const ControlMap& map, ReadMethod read, std::vector<std::string>& notes);
    // Empty when there are none.
    const std::vector<std::string>& commands_for(int control, Variant variant) const;
    // How many (control, variant) pairs have at least one command.
    std::size_t size() const { return commands_.size(); }
private:
    std::map<std::pair<int, Variant>, std::vector<std::string>> commands_;
};
}
