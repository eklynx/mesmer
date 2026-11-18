// SOURCE OF TRUTH for the choice of what the plugin DOES with the yoke's buttons. Specification: docs/dev/button-commands.md.
// Portable. It is a setting of a yoke model, a field of its preset (config/yoke/yoke_preset.hpp: button_action_method).
//
//   ActionMethod VJoy   the yoke's controls press the buttons of a virtual joystick that X-Plane binds (Windows: Brunner's
//                       vJoy driver; the seam is per operating system so others can follow)
//               Direct  the plugin runs the X-Plane commands the owner gave in button_commands
//               Disabled  the plugin reads no buttons, presses nothing and runs no commands: for a yoke that already appears as a
//                       joystick in X-Plane, whose own interface the pilot uses instead
//
// Happy path: parse_action_method("DIRECT") is Direct (any case); "disabled" is Disabled. A model that gives none is Direct
// (default_action_method). VJoy on an operating system with no virtual joystick becomes Direct (buttons/service.hpp); Disabled is never
// changed. The owner's BrunnerDirect.state.json may name a method for every yoke: effective_action_method(preset's, owner's) is the
// owner's when there is one, else the preset's. Error case: parse_action_method returns nullopt for any other text (including the
// never-released "None"), and the preset parser then refuses the field (naming it), the state file ignores the key (and says so).
#pragma once
#include <algorithm>
#include <cctype>
#include <optional>
#include <string>
#include <string_view>

namespace buttons {
enum class ActionMethod { vjoy, direct, disabled };

inline std::string lower_case(std::string_view text) {
    std::string out(text);
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}
inline std::optional<ActionMethod> parse_action_method(std::string_view text) {
    const auto word = lower_case(text);
    if (word == "vjoy") return ActionMethod::vjoy;
    if (word == "direct") return ActionMethod::direct;
    if (word == "disabled") return ActionMethod::disabled;
    return std::nullopt;
}
inline const char* action_method_name(ActionMethod method) {
    switch (method) {
        case ActionMethod::vjoy: return "VJoy";
        case ActionMethod::disabled: return "Disabled";
        default: return "Direct";
    }
}

inline constexpr ActionMethod default_action_method = ActionMethod::direct;
// The owner's choice (state file) beats the yoke model's (preset); with none, the model's stands.
inline ActionMethod effective_action_method(ActionMethod preset, const std::optional<ActionMethod>& owner) { return owner ? *owner : preset; }
}
