// SOURCE OF TRUTH for the owner's saved choices that are not in BrunnerDirect.ini:
// "allow unverified yoke" (Developer menu), which the plugin writes, and the optional startup overrides
// "auto_start" and "force_feel", which the owner writes by hand and the plugin only reads and keeps.
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
// is absent unless it is a literal true or false.
#pragma once
#include "json.hpp"
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
    bool operator==(const OwnerState& o) const {
        return allow_unverified_yoke==o.allow_unverified_yoke && auto_start==o.auto_start && force_feel==o.force_feel;
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
            const bool known=key=="allow_unverified_yoke" || key=="auto_start" || key=="force_feel";
            if (!known) {out.notes.push_back(std::string(state_file)+": unknown key \""+key+"\" ignored");continue;}
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
