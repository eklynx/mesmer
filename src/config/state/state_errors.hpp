// SOURCE OF TRUTH for the lines the owner's saved state (config/state/owner_state.hpp) puts in Log.txt when something in
// BrunnerDirect.state.json cannot be used. None of them stops the plugin: the entry, or the whole file, is ignored and the
// documented default is used. Each line starts with the file's name, passed in by the reader (owner_state.hpp's state_file), so the
// owner sees which file to edit. Kept here, apart from the reader, so the wording can be read, reviewed and tested in one place.
#pragma once
#include <string>

namespace brunner::state_errors {
// Why the whole file is ignored (the second half of ignored_whole's line).
inline constexpr const char* not_a_json_object = "it is not a JSON object";
inline constexpr const char* needs_version = "it needs \"version\":1";
inline constexpr const char* not_valid_json = "it is not valid JSON";
inline std::string ignored_whole(const char* file, const std::string& why) { return std::string(file) + " ignored: " + why; }
// One key or entry that is ignored.
inline std::string unknown_key(const char* file, const std::string& key) { return std::string(file) + ": unknown key \"" + key + "\" ignored"; }
inline std::string reply_tolerance_range(const char* file, int most, int used_instead) {
    return std::string(file) + ": reply_tolerance_ms must be a whole number from 0 to " + std::to_string(most) + "; ignored (" + std::to_string(used_instead) + " is used)";
}
inline std::string button_commands_not_an_object(const char* file) { return std::string(file) + ": button_commands must be an object; ignored"; }
inline std::string list_item_not_a_command(const char* file, const std::string& entry) {
    return std::string(file) + ": button_commands." + entry + " has a list item that is not a command name; that item ignored";
}
inline std::string entry_wrong_kind(const char* file, const std::string& entry) {
    return std::string(file) + ": button_commands." + entry + " must be a command name, a list of them, or null; ignored";
}
inline std::string not_a_boolean(const char* file, const std::string& key) { return std::string(file) + ": " + key + " must be true or false; ignored"; }
}
