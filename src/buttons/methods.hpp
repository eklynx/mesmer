// SOURCE OF TRUTH for the choice of what the plugin DOES with the yoke's buttons. Specification: docs/dev/button-commands.md.
// Portable. It is a setting of a yoke model, a field of its preset (config/yoke/yoke_preset.hpp: button_action_method).
//
//   ActionMethod VJoy   the yoke's controls press the buttons of a virtual joystick that X-Plane binds (Windows: Brunner's
//                       vJoy driver; the seam is per operating system so others can follow)
//               Direct  the plugin runs the X-Plane commands the owner gave in button_commands
//
// Happy path: parse_action_method("DIRECT") is Direct (any case). A model that gives none is Direct (default_action_method). VJoy on an
// operating system with no virtual joystick becomes Direct (buttons/service.hpp). Error case: parse_action_method returns nullopt for
// any other text, and the preset parser then refuses the file naming the field.
#pragma once
#include <algorithm>
#include <cctype>
#include <optional>
#include <string>
#include <string_view>

namespace buttons {
enum class ActionMethod { vjoy, direct };

inline std::string lower_case(std::string_view text) {
    std::string out(text);
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}
inline std::optional<ActionMethod> parse_action_method(std::string_view text) {
    const auto word = lower_case(text);
    if (word == "vjoy") return ActionMethod::vjoy;
    if (word == "direct") return ActionMethod::direct;
    return std::nullopt;
}
inline const char* action_method_name(ActionMethod method) { return method == ActionMethod::vjoy ? "VJoy" : "Direct"; }

inline constexpr ActionMethod default_action_method = ActionMethod::direct;
}
