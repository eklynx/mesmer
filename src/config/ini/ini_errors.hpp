// SOURCE OF TRUTH for the text of every error BrunnerDirect.ini can be refused with (config/ini/config.hpp), as the `what()` of a
// std::invalid_argument. The plugin logs it and keeps the built-in defaults, so each message names the key at fault where there is
// one. Kept here, apart from the parser, so the wording can be read, reviewed and tested in one place.
#pragma once
#include <string>

namespace brunner::ini_errors {
inline constexpr const char* malformed_line = "Malformed Brunner configuration line";
inline constexpr const char* incomplete = "Incomplete Brunner configuration";   // unreadable, no schema_version, or a required key missing
inline constexpr const char* unsupported_schema = "Unsupported Brunner configuration schema";
inline constexpr const char* workbench_port_range = "Brunner workbench_port must be 0 or 1024-65535";
inline constexpr const char* flight_defaults_out_of_bounds = "Flight defaults outside validated bounds";
inline std::string duplicate_key(const std::string& key) { return "Duplicate Brunner configuration key: " + key; }
inline std::string invalid_integer(const std::string& key) { return "Invalid integer for " + key; }
inline std::string unknown_key(const std::string& key) { return "Unknown Brunner configuration key: " + key; }
inline std::string outside_commissioning_bounds(const std::string& key) { return "Brunner configuration outside commissioning bounds: " + key; }
}
