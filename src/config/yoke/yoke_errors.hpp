// SOURCE OF TRUTH for the text of every error and skipped-mapping line the yoke model files can give (config/yoke/yoke_preset.hpp,
// yoke_overrides.hpp): the shipped yokes/*.json and the owner's BrunnerDirect.yokes.json. Kept here, apart from the parser, so the
// wording can be read, reviewed and tested in one place.
//
// Three kinds, by how they reach the owner:
//  - a PresetError, which refuses the whole file (the plugin logs it and keeps the built-in models): the validation of a preset's
//    numbers, the file's shape, and the fields that drive the yoke's forces;
//  - a line in LoadedYokes::skipped, for a button mapping that was skipped while the rest of the file applied (logged as
//    "Yoke config: ..." and shown on screen until clicked): these end with "; skipped" and say what the model keeps;
//  - a note in LoadedYokes::notes, why a whole file was not used.
// `what` is the place in the file ("yoke \"cls-e\": roll_travel_counts"), `at` the yoke ("yoke \"cls-e\": "), `rw` and `aw` one button
// mapping entry ("yoke \"cls-e\": button_registers.button_1"). Each function says its own text; the wording does not depend on the
// caller.
#pragma once
#include <string>

namespace brunner::yoke_errors {
// ---- A preset that does not validate: preset_invalid puts `yoke preset "<name>": ` in front of the reason below ----
inline std::string preset_invalid(const std::string& name, const std::string& why) { return "yoke preset \"" + name + "\": " + why; }
inline constexpr const char* name_length = "name must be 1 to 32 characters";
inline constexpr const char* name_characters = "name may use only a-z, 0-9 and '-'";
inline constexpr const char* title_length = "title is longer than 64 characters";
inline constexpr const char* usb_id_count = "needs 1 to 8 USB ids";
inline constexpr const char* usb_id_zero = "a USB id is zero";
inline constexpr const char* firmware_count = "more than 16 verified firmware versions";
inline constexpr const char* firmware_zero = "a verified firmware version is zero";
inline constexpr const char* travel_counts = "travel counts must be 1000 to 30000";
inline std::string input_ends(const char* axis) { return std::string(axis) + " input low must be -30000 to -1000 and high 1000 to 30000, or both left out"; }
inline constexpr const char* wheel_deg = "wheel_deg must be 10 to 180";
inline constexpr const char* pitch_travel_mm = "pitch_travel_mm must be 0 to 1000";
inline constexpr const char* encoder_signs = "encoder signs must be 1 or -1";
inline constexpr const char* spring_signs = "spring signs must be 1 or -1";
inline constexpr const char* pitch_units_per_lb = "pitch_units_per_lb must be 50 to 1000";
inline constexpr const char* roll_units_per_nm = "roll_units_per_nm must be 100 to 10000";
inline constexpr const char* force_ceilings = "force ceilings must be 100 to 20000 units";
inline constexpr const char* margin_fraction = "margin_fraction must be 0.02 to 0.5";
inline constexpr const char* target_torque = "target_torque must be 1 to 1000";
inline constexpr const char* centre_drift_percent = "centre_drift_percent must be 0 to 5";
inline constexpr const char* status_words = "status words must be 1 to 65535";
inline constexpr const char* rest_curve_points = "rest curve points must be 5 to 1000";
inline constexpr const char* rest_spring_percents = "rest spring percents must be 1 to 1000";
inline constexpr const char* rest_pitch_gain_denominator = "rest pitch_gain_denominator must be 1 to 6";
inline constexpr const char* rest_pitch_move_back = "rest pitch_move_back must be 180 to 720";
inline constexpr const char* rest_pitch_cutoff = "rest pitch_cutoff must be 0 (not written) or up to 101";
inline constexpr const char* rest_mode = "rest mode must be 0 (not written) or the spring_mode with only the flow's bits 13, 11, 6 and 5 added";
inline constexpr const char* power_on_spring_mode = "power_on_spring_mode must be 0 (the same as spring_mode) or 1 to 65535 with bits 13, 11, 6 and 5 clear";
inline constexpr const char* spring_mode = "spring_mode must be 1 to 65535 with bits 13, 11, 6 and 5 clear (they are the flow's, not the resting mode's)";
inline constexpr const char* too_many_aliases = "more than 64 button aliases";
inline std::string alias_lists_too_many(const std::string& key) { return "button alias \"" + key + "\" lists more than 16 names"; }
inline std::string button_register(const std::string& control, const std::string& problem) { return "button_registers." + control + " " + problem; }

// ---- A button signal that cannot be used (the end of a register entry's line; button_signal_problem) ----
inline constexpr const char* signal_node = "needs node 1 or 2";
inline constexpr const char* signal_object = "is not an object the yoke's buttons may be read from (0x60fd:0, 0x2880:3 or 0x2880:4)";
inline std::string signal_too_wide(bool is_mask, int bytes) { return std::string(is_mask ? "mask" : "value") + " is wider than the object (" + std::to_string(bytes) + " bytes)"; }
inline constexpr const char* signal_mask_zero = "mask must not be 0";

// ---- No two models may clash ----
inline std::string duplicate_name(const std::string& name) { return "duplicate yoke preset name \"" + name + "\""; }
inline std::string same_usb_id(const std::string& first, const std::string& second) { return "yoke presets \"" + first + "\" and \"" + second + "\" claim the same USB id"; }

// ---- A value of the wrong kind (`what` is the field's place) ----
inline std::string must_be_a_number(const std::string& what) { return what + " must be a number"; }
inline std::string must_be_a_whole_number(const std::string& what) { return what + " must be a whole number"; }
inline std::string must_be_a_hex_id(const std::string& what) { return what + " must be a hex string \"0x0001\" to \"0xFFFF\""; }
inline std::string must_be_a_boolean(const std::string& what) { return what + " must be true or false"; }
inline std::string must_be_text(const std::string& what) { return what + " must be text"; }
inline std::string must_be_a_list(const std::string& what) { return what + " must be a list"; }
inline std::string must_be_an_object(const std::string& what) { return what + " must be an object"; }
inline std::string usb_entries_need_ids(const std::string& what) { return what + " entries need vendorId and productId"; }
inline std::string entries_range_1_65535(const std::string& what) { return what + " entries must be 1 to 65535"; }
inline std::string curve_needs_nine(const std::string& where) { return where + " must be a list of 9 numbers"; }
inline std::string curve_entries_range(const std::string& where) { return where + " entries must be 0 to 65535"; }

// ---- The file's shape ----
inline std::string not_json(const std::string& why) { return "not JSON: " + why; }
inline constexpr const char* file_not_an_object = "the presets file must be a JSON object";
inline constexpr const char* unsupported_schema = "unsupported presets schema (expected 1)";
inline std::string unknown_top_level_field(const std::string& key) { return "unknown top-level field \"" + key + "\""; }
inline constexpr const char* yokes_not_a_list = "\"yokes\" must be a list";
inline constexpr const char* too_many_presets = "more than 32 yoke presets";
inline constexpr const char* yoke_not_an_object = "each yoke must be an object";
inline constexpr const char* yoke_has_no_name = "a yoke has no name";
inline std::string new_model_must_give(const std::string& at, const std::string& key) { return at + "a new model must give \"" + key + "\""; }
inline std::string unknown_field(const std::string& at, const std::string& key) { return at + "unknown field \"" + key + "\""; }
inline std::string unknown_field_in(const std::string& at, const std::string& key, const std::string& inner) { return at + "unknown field \"" + key + "." + inner + "\""; }
inline constexpr const char* yokes_folder_empty = "the yokes folder has no files";

// ---- Why a whole file was not used (LoadedYokes::notes) ----
inline std::string files_refused(const std::string& why) { return "Yoke model files refused, built-in models in force: " + why; }
inline std::string overrides_refused(const std::string& why) { return "Yoke overrides file refused, ignored: " + why; }

// ---- A button mapping that is skipped (LoadedYokes::skipped): the rest of the file applies ----
inline std::string skipped(const std::string& why) { return why + "; skipped"; }
inline std::string action_method_skipped(const std::string& what, const std::string& kept) { return what + " must be VJoy or Direct; skipped, the model keeps " + kept; }
inline std::string registers_not_an_object(const std::string& what) { return what + " must be an object of button and hat names; skipped, the model keeps its own"; }
inline std::string aliases_not_an_object(const std::string& what) { return what + " must be an object of lists of names; skipped, the model keeps its own"; }
// One button_registers entry (thrown inside the loop, which turns it into a skipped line).
inline std::string not_a_control_name(const std::string& where) { return where + " is not a button or hat name (button_1 to button_16, hat_n to hat_nw)"; }
inline std::string given_twice(const std::string& where) { return where + " is given twice"; }
inline std::string register_not_an_object(const std::string& rw) { return rw + " must be an object with node, object, sub and mask or value"; }
inline std::string register_needs_object(const std::string& rw) { return rw + " needs \"node\", \"object\" and \"sub\""; }
inline std::string register_needs_mask_or_value(const std::string& rw) { return rw + " needs exactly one of \"mask\" and \"value\""; }
inline std::string register_unknown_field(const std::string& rw, const std::string& field) { return rw + " has an unknown field \"" + field + "\""; }
inline std::string register_node(const std::string& rw) { return rw + ".node must be 1 or 2"; }
inline std::string register_sub(const std::string& rw) { return rw + ".sub must be 0 to 255"; }
inline std::string register_mask_hex(const std::string& rw) { return rw + ".mask must be a hex string with the 0x prefix (1 to 8 digits)"; }
inline std::string register_value_negative(const std::string& rw) { return rw + ".value must not be negative"; }
inline std::string register_problem(const std::string& rw, const std::string& problem) { return rw + " " + problem; }
// One button_aliases entry or name.
inline std::string alias_not_a_list(const std::string& aw) { return aw + " must be a list of names; skipped"; }
inline std::string alias_entry_not_a_name(const std::string& aw) { return aw + " has an entry that is not a name in quotes; skipped"; }
inline std::string alias_not_usable(const std::string& aw, const std::string& name) {
    return aw + ": \"" + name + "\" is not a usable name (1 to 32 of a-z, 0-9 and _; not ending in _released or _hold; not a button name); skipped";
}
inline std::string alias_given_twice(const std::string& aw, const std::string& name) { return aw + ": \"" + name + "\" is given twice; skipped"; }
inline std::string alias_past_the_limit(const std::string& aw, const std::string& name) { return aw + ": \"" + name + "\" is past the 16 names a control may have; skipped"; }

// ---- The overrides editor (config/yoke/yoke_overrides.hpp): why a change from the feel page was refused ----
inline constexpr const char* direction_out_of_range = "direction value out of range";
inline std::string presets_file_not_json(const std::string& why) { return "the presets file is not valid JSON: " + why; }
inline constexpr const char* presets_file_has_no_yokes_list = "the presets file has no \"yokes\" list";
inline constexpr const char* presets_file_refused = "the presets file was refused";
inline std::string no_model_named(const std::string& model) { return "no yoke model named " + model; }
}
