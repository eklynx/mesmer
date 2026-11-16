// SOURCE OF TRUTH for the owner's saved choices that are not in BrunnerDirect.ini:
// "allow unverified yoke" (Developer menu), which the plugin writes, and the optional startup overrides
// "auto_start" and "force_feel", which the owner writes by hand and the plugin only reads and keeps, and the
// optional "reply_tolerance_ms" and "button_commands" map (a control of the yoke -> X-Plane command, buttons/commands.hpp).
// Specification: docs/configuration.md (the overrides), docs/dev/advanced-configuration.md (the format and
// every rule below) and docs/dev/yoke-presets.md (the gate). Saved as BrunnerDirect.state.json in X-Plane's
// Output/BrunnerDirect (so updating the plugin never replaces it); an older install's copy in the plugin
// folder is read once and moved.
//
// Happy path: {"version":1,"allow_unverified_yoke":false,"auto_start":true,"force_feel":true}. Every key but
// "version" is optional. A value of an override beats the same key in BrunnerDirect.ini (see `applied`).
//
// Errors, none thrown: the file as a whole is ignored (everything off, no overrides, one note) when it is
// not JSON, not an object, or not "version":1. Otherwise each key is judged alone: a key that is not a
// literal true or false, or one this version does not know, is ignored and named in a note, and the other keys
// still apply. The default is OFF for the unverified choice, and only a literal `true` turns it on; an override
// is absent unless it is a literal true or false. "reply_tolerance_ms" is a whole number from 0 to 5000 (anything
// else is ignored and named): how long the yoke may go without completing a sample before the link counts as lost;
// absent means 1000, 0 means none (brunner/core.hpp). "button_commands" must be an object; inside it a value is null, one
// command string, or a list of command strings (several commands run in the order written). Any other value is ignored
// and named; inside a list an item that is not a string is dropped and named and the others stay. Entry names are kept
// as read (buttons/commands.hpp judges them, so a save never erases an entry this version does not understand, and
// it writes back the form that was read: a string stays a string, a list stays a list).
#pragma once
#include "brunner/core.hpp"
#include "json.hpp"
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace brunner {
inline constexpr char state_file[]="BrunnerDirect.state.json";
inline constexpr int owner_state_version=1;
// One "button_commands" value as the owner wrote it: null, one command string, or a list of command strings. The form
// is kept so that a save writes back what was read ("a" stays "a", ["a"] stays ["a"], null stays null).
struct ButtonEntry {
    enum class Form { none, single, list } form = Form::none;
    std::vector<std::string> commands;   // empty for none, one for single, any number for list (items kept as written)
    bool operator==(const ButtonEntry& o) const { return form == o.form && commands == o.commands; }
};
struct OwnerState {
    bool allow_unverified_yoke=false;
    std::optional<bool> auto_start;     // startup override of BrunnerDirect.ini's auto_start (hand-edited)
    std::optional<bool> force_feel;     // startup override of BrunnerDirect.ini's force_feel (hand-edited)
    std::optional<int> reply_tolerance_ms;   // 0..5000, default 1000 (brunner/core.hpp); hand-edited
    // Hand-edited: input name ("button_1", "hat_n_released", "button_4_hold") -> X-Plane command or commands; null, "" and
    // [] mean no command. Empty by default and never written unless the owner wrote it.
    std::map<std::string,ButtonEntry> button_commands;
    bool operator==(const OwnerState& o) const {
        return allow_unverified_yoke==o.allow_unverified_yoke && auto_start==o.auto_start && force_feel==o.force_feel
            && reply_tolerance_ms==o.reply_tolerance_ms && button_commands==o.button_commands;
    }
};
// What a read found: the state, and one line per thing that was ignored (for Log.txt), worded for the owner.
struct OwnerStateRead {
    OwnerState state;
    std::vector<std::string> notes;
};
// The file as written: the overrides appear only when set, so a state without them is the original form.
inline std::string serialize_owner_state(const OwnerState& s) {
    std::string text=std::string("{\"version\":")+std::to_string(owner_state_version)+",\"allow_unverified_yoke\":"+(s.allow_unverified_yoke?"true":"false");
    if (s.auto_start) text+=std::string(",\"auto_start\":")+(*s.auto_start?"true":"false");
    if (s.force_feel) text+=std::string(",\"force_feel\":")+(*s.force_feel?"true":"false");
    if (s.reply_tolerance_ms) text+=",\"reply_tolerance_ms\":"+std::to_string(*s.reply_tolerance_ms);
    if (!s.button_commands.empty()) {
        text+=",\"button_commands\":{";
        bool first=true;
        for (const auto& entry:s.button_commands) {
            if (!first) text+=",";
            first=false;
            text+=json_string(entry.first)+":";
            const auto& value=entry.second;
            if (value.form==ButtonEntry::Form::none) text+="null";
            else if (value.form==ButtonEntry::Form::single) text+=json_string(value.commands.empty()?std::string():value.commands.front());
            else {
                text+="[";
                for (std::size_t i=0;i<value.commands.size();++i) text+=(i?",":"")+json_string(value.commands[i]);
                text+="]";
            }
        }
        text+="}";
    }
    return text+"}\n";
}
inline OwnerStateRead read_owner_state(std::string_view text) {
    OwnerStateRead out;
    const auto whole=[&](const std::string& why) {out=OwnerStateRead{};out.notes.push_back(std::string(state_file)+" ignored: "+why);return out;};
    try {
        const auto root=parse_json(text);
        if (root.kind!=Json::Kind::object) return whole("it is not a JSON object");
        const auto* version=root.get("version");
        if (!version || version->kind!=Json::Kind::integer || version->integer!=owner_state_version) return whole("it needs \"version\":1");
        for (const auto& m:root.members) {
            const auto& key=m.first;
            if (key=="version") continue;
            const bool known=key=="allow_unverified_yoke" || key=="auto_start" || key=="force_feel" || key=="button_commands" || key=="reply_tolerance_ms";
            if (!known) {out.notes.push_back(std::string(state_file)+": unknown key \""+key+"\" ignored");continue;}
            if (key=="reply_tolerance_ms") {
                if (m.second.kind!=Json::Kind::integer || m.second.integer<0 || m.second.integer>max_reply_tolerance_ms) {
                    out.notes.push_back(std::string(state_file)+": reply_tolerance_ms must be a whole number from 0 to "+std::to_string(max_reply_tolerance_ms)+"; ignored ("+std::to_string(default_reply_tolerance_ms)+" is used)");
                    continue;
                }
                out.state.reply_tolerance_ms=static_cast<int>(m.second.integer);
                continue;
            }
            if (key=="button_commands") {
                if (m.second.kind!=Json::Kind::object) {out.notes.push_back(std::string(state_file)+": button_commands must be an object; ignored");continue;}
                for (const auto& entry:m.second.members) {
                    ButtonEntry value;
                    if (entry.second.kind==Json::Kind::null) {}
                    else if (entry.second.kind==Json::Kind::string) {value.form=ButtonEntry::Form::single;value.commands.push_back(entry.second.text);}
                    else if (entry.second.kind==Json::Kind::array) {
                        value.form=ButtonEntry::Form::list;
                        for (const auto& item:entry.second.items) {
                            if (item.kind==Json::Kind::string) value.commands.push_back(item.text);
                            else out.notes.push_back(std::string(state_file)+": button_commands."+entry.first+" has a list item that is not a command name; that item ignored");
                        }
                    } else {out.notes.push_back(std::string(state_file)+": button_commands."+entry.first+" must be a command name, a list of them, or null; ignored");continue;}
                    out.state.button_commands[entry.first]=std::move(value);
                }
                continue;
            }
            if (m.second.kind!=Json::Kind::boolean) {out.notes.push_back(std::string(state_file)+": "+key+" must be true or false; ignored");continue;}
            if (key=="allow_unverified_yoke") out.state.allow_unverified_yoke=m.second.boolean;
            else if (key=="auto_start") out.state.auto_start=m.second.boolean;
            else out.state.force_feel=m.second.boolean;
        }
    } catch (const std::exception&) {return whole("it is not valid JSON");}
    return out;
}
// Just the state (the notes dropped).
inline OwnerState parse_owner_state(std::string_view text) {return read_owner_state(text).state;}
// An override beats the ini's value (0 or 1); no override leaves it.
inline int applied(const std::optional<bool>& override_,int ini_value) {return override_?(*override_?1:0):ini_value;}
}
