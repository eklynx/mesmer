// SOURCE OF TRUTH for the text of every error BrunnerDirect.profiles.json can be refused with (config/aircraft/profiles.hpp), as the
// `what()` of a ProfileError. The plugin logs it and keeps the figures already flown, so each message says which profile
// (`where`: "profile c172"), and which figure where it can, so the file can be fixed. Kept here, apart from the parser, so the wording
// can be read, reviewed and tested in one place.
#pragma once
#include <string>

namespace brunner::aircraft_errors {
// The file as a whole.
inline constexpr const char* not_an_object = "not an object";
inline constexpr const char* unsupported_schema = "unsupported schema";
inline constexpr const char* no_default_profile = "no default profile";
inline std::string not_json(const std::string& why) { return "not JSON: " + why; }
inline std::string unknown_key(const std::string& key) { return "unknown key " + key; }
inline std::string needs(const std::string& where, const std::string& key) { return where + " needs " + key; }
inline std::string no_default_profile_named(const std::string& name) { return "no default profile " + name; }
// One profile.
inline std::string profile_not_an_object(const std::string& where) { return where + " is not an object"; }
inline std::string profile_unknown_key(const std::string& where, const std::string& key) { return where + ": unknown key " + key; }
inline std::string aircraft_not_a_path(const std::string& where) { return where + ": an aircraft is not a path"; }
inline std::string sections_need_two_wings(const std::string& where, const std::string& key) { return where + ": " + key + " needs two wings"; }
inline std::string section_not_a_wing(const std::string& where, const std::string& key) { return where + ": a section of " + key + " is not a wing 0-9"; }
inline std::string sections_same_wing(const std::string& where, const std::string& key) { return where + ": " + key + " names one wing twice"; }
inline std::string figures_out_of_bounds(const std::string& where) { return where + ": figures out of bounds"; }
inline std::string aileron_figures_out_of_bounds(const std::string& where) { return where + ": aileron figures out of bounds"; }
// One figure (`surface`: "elevator" or "aileron"; `fwhere`: "profile c172 elevator area").
inline std::string unknown_figure(const std::string& where, const std::string& surface, const std::string& figure) { return where + ": unknown " + surface + " figure " + figure; }
inline std::string needs_figure(const std::string& where, const std::string& surface, const std::string& figure) { return where + " needs " + surface + " " + figure; }
inline std::string figure_needs_value_tag_source(const std::string& fwhere) { return fwhere + " needs value, tag and source only"; }
inline std::string figure_needs_a_number(const std::string& fwhere) { return fwhere + " needs a number"; }
inline std::string figure_range(const std::string& fwhere, const std::string& low, const std::string& high) { return fwhere + " must be " + low + " to " + high; }
inline std::string figure_tag(const std::string& fwhere) { return fwhere + ": tag must be published, derived or guessed"; }
inline std::string figure_needs_a_source(const std::string& fwhere) { return fwhere + " needs a source"; }
}
