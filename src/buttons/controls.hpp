// SOURCE OF TRUTH for how the yoke's physical controls are named. Specification: docs/dev/button-commands.md.
// Read this header, not controls.cpp, to use it. Portable: no operating-system code.
//
// A *control* is one physical thing on the yoke (a button, a rocker side, a hat position). The yoke's controller reports each as one
// of the *canonical inputs*: `button_1`..`button_16` (the controller's button slots) and the hat positions `hat_n`, `hat_ne`,
// `hat_e`, `hat_se`, `hat_s`, `hat_sw`, `hat_w`, `hat_nw` (1 up, then clockwise). The yoke's configuration
// (YokePreset::button_aliases) gives controls friendly names, keyed by the same names as YokePreset::button_registers:
//     "button_1": ["trigger"], "hat_n": ["up", "north"]
// Each entry says its key and the names in its list are one control. A name is a way to say a control, and commands written under
// any of a control's names run for it.
//
// Happy path: ControlMap(aliases) builds the controls, one per canonical input; resolve(name) turns any canonical or friendly
// name (any case) into a control number; control_of(input) does the same for what the controller reported; vjoy_button(control) is
// the virtual joystick button that stands for it: n for button_n, and 17 up for the hat positions, in order.
//
// Error cases: the ControlMap constructor throws ControlError (a message worded for the preset file) when a key is not a canonical
// name or is given twice (in any case); when an alias is not [a-z0-9_]+ of 1 to 32 characters, ends in _released or _hold, or is a
// canonical name; or when an alias is given twice, for one control or two. resolve() and parse_input() return nullopt for what they
// do not know.
#pragma once
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace buttons {
inline constexpr int button_slots = 16, hat_positions = 8, first_hat_vjoy_button = 17, vjoy_buttons = 32;

enum class InputKind { button, hat };
struct Input {
    InputKind kind = InputKind::button;
    int index = 0;   // button 1..16, hat 1..8 (1 up, clockwise)
    bool operator==(const Input& o) const { return kind == o.kind && index == o.index; }
    bool operator<(const Input& o) const { return kind != o.kind ? kind < o.kind : index < o.index; }
};
// "button_11", "hat_nw".
std::string input_name(const Input& input);
// The canonical input a name stands for (any case), or nullopt.
std::optional<Input> parse_input(std::string_view name);
// Every canonical input, in the order controls are numbered: buttons, then hat positions.
std::vector<Input> all_inputs();
// Whether a friendly name is allowed (rules above); a canonical name is not "friendly".
bool valid_friendly_name(std::string_view name);

struct ControlError : std::runtime_error { using std::runtime_error::runtime_error; };

class ControlMap {
public:
    using Aliases = std::map<std::string, std::vector<std::string>>;   // canonical name -> its friendly names
    explicit ControlMap(const Aliases& aliases = {});
    std::size_t size() const { return controls_.size(); }
    // Any canonical or friendly name, in any case.
    std::optional<int> resolve(std::string_view name) const;
    // The control a canonical input belongs to (every canonical input belongs to one).
    int control_of(const Input& input) const;
    Input input_of(int control) const { return controls_.at(static_cast<std::size_t>(control)).input; }
    // The control's names: its canonical input, then its friendly names alphabetically.
    std::vector<std::string> names(int control) const { return controls_.at(static_cast<std::size_t>(control)).names; }
    // 1..32: the virtual joystick button for the control.
    int vjoy_button(int control) const { return controls_.at(static_cast<std::size_t>(control)).vjoy; }
private:
    struct Control {
        Input input;
        std::vector<std::string> names;
        int vjoy = 0;
    };
    std::vector<Control> controls_;
    std::map<std::string, int> by_name_;
    std::map<Input, int> by_input_;
};
}
