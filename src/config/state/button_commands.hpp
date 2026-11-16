// SOURCE OF TRUTH for the "button_commands" section of a state file: what the owner wants each control of the yoke to run. It is the
// same section in BrunnerDirect.state.json (config/state/owner_state.hpp) and in a per-aircraft file (config/state/aircraft_state.hpp),
// so both are read by read_button_commands. Specification: docs/dev/advanced-configuration.md ("Button commands").
//
// Happy path: the value of "button_commands" is an object; each key is a control's name ("button_1", "hat_n_released", "button_4_hold",
// or a friendly name; buttons/commands.hpp judges it) and each value is null, one command string, or a list of command strings (several
// commands run in the order written). The form is kept as read, so a save writes back what was read: "a" stays "a", ["a"] stays ["a"],
// null stays null. Names are kept as read too, so a save never erases an entry this version does not understand.
//
// Errors, none thrown, each one a line in `notes` worded for the owner with the file's name in front (`file`), and the rest still
// applies: the section not being an object ignores it; a value that is not null, a string or a list ignores that entry; inside a list an
// item that is not a string is dropped and the others stay.
#pragma once
#include "config/json.hpp"
#include "config/state/state_errors.hpp"
#include <map>
#include <string>
#include <vector>

namespace brunner {
// One "button_commands" value as the owner wrote it: null, one command string, or a list of command strings. The form
// is kept so that a save writes back what was read ("a" stays "a", ["a"] stays ["a"], null stays null).
struct ButtonEntry {
    enum class Form { none, single, list } form = Form::none;
    std::vector<std::string> commands;   // empty for none, one for single, any number for list (items kept as written)
    bool operator==(const ButtonEntry& o) const { return form == o.form && commands == o.commands; }
};
// Reads the value of a "button_commands" key into `into` (entries it finds are added or replace the same name); problems go to `notes`.
inline void read_button_commands(const Json& section,const char* file,std::map<std::string,ButtonEntry>& into,std::vector<std::string>& notes) {
    if (section.kind!=Json::Kind::object) {notes.push_back(state_errors::button_commands_not_an_object(file));return;}
    for (const auto& entry:section.members) {
        ButtonEntry value;
        if (entry.second.kind==Json::Kind::null) {}
        else if (entry.second.kind==Json::Kind::string) {value.form=ButtonEntry::Form::single;value.commands.push_back(entry.second.text);}
        else if (entry.second.kind==Json::Kind::array) {
            value.form=ButtonEntry::Form::list;
            for (const auto& item:entry.second.items) {
                if (item.kind==Json::Kind::string) value.commands.push_back(item.text);
                else notes.push_back(state_errors::list_item_not_a_command(file,entry.first));
            }
        } else {notes.push_back(state_errors::entry_wrong_kind(file,entry.first));continue;}
        into[entry.first]=std::move(value);
    }
}
}
