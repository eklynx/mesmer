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
#include "config/state/button_commands.hpp"
#include "config/state/state_errors.hpp"
#include "brunner/core.hpp"
#include "config/json.hpp"
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace brunner {
inline constexpr char state_file[]="BrunnerDirect.state.json";
inline constexpr int owner_state_version=1;
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
    const auto whole=[&](const std::string& why) {out=OwnerStateRead{};out.notes.push_back(state_errors::ignored_whole(state_file,why));return out;};
    try {
        const auto root=parse_json(text);
        if (root.kind!=Json::Kind::object) return whole(state_errors::not_a_json_object);
        const auto* version=root.get("version");
        if (!version || version->kind!=Json::Kind::integer || version->integer!=owner_state_version) return whole(state_errors::needs_version);
        for (const auto& m:root.members) {
            const auto& key=m.first;
            if (key=="version") continue;
            const bool known=key=="allow_unverified_yoke" || key=="auto_start" || key=="force_feel" || key=="button_commands" || key=="reply_tolerance_ms";
            if (!known) {out.notes.push_back(state_errors::unknown_key(state_file,key));continue;}
            if (key=="reply_tolerance_ms") {
                if (m.second.kind!=Json::Kind::integer || m.second.integer<0 || m.second.integer>max_reply_tolerance_ms) {
                    out.notes.push_back(state_errors::reply_tolerance_range(state_file,max_reply_tolerance_ms,default_reply_tolerance_ms));
                    continue;
                }
                out.state.reply_tolerance_ms=static_cast<int>(m.second.integer);
                continue;
            }
            if (key=="button_commands") {read_button_commands(m.second,state_file,out.state.button_commands,out.notes);continue;}
            if (m.second.kind!=Json::Kind::boolean) {out.notes.push_back(state_errors::not_a_boolean(state_file,key));continue;}
            if (key=="allow_unverified_yoke") out.state.allow_unverified_yoke=m.second.boolean;
            else if (key=="auto_start") out.state.auto_start=m.second.boolean;
            else out.state.force_feel=m.second.boolean;
        }
    } catch (const std::exception&) {return whole(state_errors::not_valid_json);}
    return out;
}
// Just the state (the notes dropped).
inline OwnerState parse_owner_state(std::string_view text) {return read_owner_state(text).state;}
// An override beats the ini's value (0 or 1); no override leaves it.
inline int applied(const std::optional<bool>& override_,int ini_value) {return override_?(*override_?1:0):ini_value;}
}
