// SOURCE OF TRUTH for the per-aircraft state files: what the owner wants different for one aircraft. Specification:
// docs/dev/advanced-configuration.md ("Button commands for one aircraft"). Today a file holds only a "button_commands" section.
//
// Where: Output/BrunnerDirect/aircraft/<id>.json in X-Plane's folder (beside BrunnerDirect.state.json, so updating the plugin never
// replaces it), one file per aircraft, named by the id X-Plane gives it. aircraft_state_file_names() lists the names to try, in order:
// the name of the .acf file X-Plane loaded ("Cessna_172SP.json"), then the ICAO type code (`sim/aircraft/view/acf_ICAO`, "C172.json");
// the first that exists is the aircraft's file, so a file for one add-on beats a file for its whole type. The plugin reads it when X-Plane loads or changes the aircraft, so edit it and reload
// the aircraft to see the change.
//
// Happy path: {"version":1,"button_commands":{"button_1":"sim/view/default_view","hat_n_hold":null}}. "version" is required and is 1.
// The "button_commands" section is the one in BrunnerDirect.state.json (config/state/button_commands.hpp) and its entries OVERRIDE the
// state file's for this aircraft, they do not add to them (buttons/commands.hpp): an entry for a control and variant (press,
// `_released`, `_hold`) replaces whatever the state file gave that control and variant, and null, "" or [] means no command for it on
// this aircraft. Controls the file does not mention keep the state file's commands.
//
// Errors, none thrown, each a line in `notes` for Log.txt: the whole file is ignored (nothing overridden) when it is not JSON, not an
// object, or not "version":1; any key but "version" and "button_commands" is ignored and named; "button_commands" is judged as in
// config/state/button_commands.hpp. A file that cannot be read is the plugin's to say.
#pragma once
#include "config/json.hpp"
#include "config/state/button_commands.hpp"
#include "config/state/state_errors.hpp"
#include <cctype>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace brunner {
inline constexpr char aircraft_state_folder[]="aircraft";
inline constexpr int aircraft_state_version=1;
struct AircraftState {
    std::map<std::string,ButtonEntry> button_commands;
    bool operator==(const AircraftState& o) const {return button_commands==o.button_commands;}
};
struct AircraftStateRead {
    AircraftState state;
    std::vector<std::string> notes;
};
// `file` is how the file is named in notes ("aircraft/C172.json").
inline AircraftStateRead read_aircraft_state(std::string_view text,const char* file) {
    AircraftStateRead out;
    const auto whole=[&](const std::string& why) {out=AircraftStateRead{};out.notes.push_back(state_errors::ignored_whole(file,why));return out;};
    try {
        const auto root=parse_json(text);
        if (root.kind!=Json::Kind::object) return whole(state_errors::not_a_json_object);
        const auto* version=root.get("version");
        if (!version || version->kind!=Json::Kind::integer || version->integer!=aircraft_state_version) return whole(state_errors::needs_version);
        for (const auto& m:root.members) {
            if (m.first=="version") continue;
            if (m.first=="button_commands") read_button_commands(m.second,file,out.state.button_commands,out.notes);
            else out.notes.push_back(state_errors::unknown_key(file,m.first));
        }
    } catch (const std::exception&) {return whole(state_errors::not_valid_json);}
    return out;
}
// An id as a file name: letters, digits, '_', '-' and '.' stay, anything else (a space, a slash) becomes '_', so an id can never name a
// path; empty when there is nothing usable. (X-Plane's ICAO is up to 40 characters of free text.)
inline std::string aircraft_state_id(const std::string& id) {
    std::string out;
    for (const unsigned char c:id) {
        if (std::isalnum(c) || c=='_' || c=='-' || c=='.') out+=static_cast<char>(c);
        else if (!out.empty() || !std::isspace(c)) out+='_';
    }
    while (!out.empty() && (out.back()=='_' || out.back()=='.')) out.pop_back();
    while (!out.empty() && out.front()=='.') out.erase(out.begin());
    return out;
}
// The file names to try for an aircraft, in order, without the folder: "<acf file name>.json" (the .acf extension dropped; a path in
// `acf_file` is cut to its last part), then "<ICAO>.json" when it has an ICAO. Nothing repeats.
inline std::vector<std::string> aircraft_state_file_names(const std::string& icao,const std::string& acf_file) {
    std::vector<std::string> names;
    const auto add=[&](const std::string& id) {
        const auto clean=aircraft_state_id(id);
        if (clean.empty()) return;
        const auto name=clean+".json";
        for (const auto& n:names) if (n==name) return;
        names.push_back(name);
    };
    std::string stem=acf_file.substr(acf_file.find_last_of("/\\")==std::string::npos?0:acf_file.find_last_of("/\\")+1);
    if (stem.size()>4) {
        std::string tail=stem.substr(stem.size()-4);
        for (auto& c:tail) c=static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (tail==".acf") stem.resize(stem.size()-4);
    }
    add(stem);
    add(icao);
    return names;
}
}
