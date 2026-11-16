// SOURCE OF TRUTH for the two ways of choosing what the yoke's buttons do: where they are READ from, and what the plugin
// DOES with them. Specification: docs/dev/button-commands.md. Portable. Both are settings of a yoke model, fields of its preset
// (brunner/yoke_preset.hpp: button_read_method, button_action_method).
//
//   ReadMethod  CanBus  the buttons are registers of the yoke's controller, read over the bridge (a CANopen request, in every
//                       sampling cycle) as can_button_1..16 and the can_hat positions; works on the original CLS-E and the NG
//               HID     the NG's own joystick HID reports, observed read-only, as hid_button_1..6 and the hat positions
//   ActionMethod VJoy   the yoke's controls press the buttons of a virtual joystick that X-Plane binds (Windows: Brunner's
//                       vJoy driver; the seam is per operating system so others can follow)
//               Direct  the plugin runs the X-Plane commands the owner gave in button_commands
//
// Happy path: parse_read_method("canbus") is CanBus (any case), parse_action_method("DIRECT") is Direct. A model that gives none
// is CanBus + Direct (default_read_method, default_action_method). VJoy on an operating system with no virtual joystick becomes
// Direct (buttons/service.hpp). Error case: parse_* return nullopt for any other text, and the preset parser then refuses the file
// naming the field.
#pragma once
#include <algorithm>
#include <cctype>
#include <optional>
#include <string>
#include <string_view>

namespace buttons {
enum class ReadMethod { can_bus, hid };
enum class ActionMethod { vjoy, direct };

inline std::string lower_case(std::string_view text) {
    std::string out(text);
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}
inline std::optional<ReadMethod> parse_read_method(std::string_view text) {
    const auto word = lower_case(text);
    if (word == "canbus") return ReadMethod::can_bus;
    if (word == "hid") return ReadMethod::hid;
    return std::nullopt;
}
inline std::optional<ActionMethod> parse_action_method(std::string_view text) {
    const auto word = lower_case(text);
    if (word == "vjoy") return ActionMethod::vjoy;
    if (word == "direct") return ActionMethod::direct;
    return std::nullopt;
}
inline const char* read_method_name(ReadMethod method) { return method == ReadMethod::can_bus ? "CanBus" : "HID"; }
inline const char* action_method_name(ActionMethod method) { return method == ActionMethod::vjoy ? "VJoy" : "Direct"; }

inline constexpr ReadMethod default_read_method = ReadMethod::can_bus;
inline constexpr ActionMethod default_action_method = ActionMethod::direct;
}
