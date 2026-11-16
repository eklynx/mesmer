// SOURCE OF TRUTH for how the yoke's physical controls are named. Specification: docs/dev/button-commands.md.
// Read this header, not controls.cpp, to use it. Portable: no operating-system code.
//
// A *control* is one physical thing on the yoke (a button, a rocker side, a hat position). It can be seen two ways, one
// per read method: through the NG's joystick HID reports (`hid_button_1`..`hid_button_6` and the hat positions `hat_n`..`hat_nw`)
// or through the controller's CANopen registers (`can_button_1`..`can_button_16`, the slots of the original CLS-E, and the NG's hat
// positions `can_hat_n`..`can_hat_nw`; which registers a model uses is its YokePreset::button_registers, buttons/input/can_layout.hpp).
// Those are the *canonical inputs*. The yoke's configuration (YokePreset::button_aliases) says which of them are the same
// control, and gives controls friendly names:
//     "trigger": ["hid_button_1", "can_button_1"], "hat_n": ["can_button_15"], "left_index": ["trigger"]
// Each entry says its key and its members are one control; chains are transitive. A name is therefore a way to say a
// control, whichever read method is in use, and commands written under any of a control's names run for it.
//
// Happy path: ControlMap(aliases) builds the controls; resolve(name) turns any canonical or friendly name (any case) into a
// control number; control_of(input) does the same for what a read method saw; vjoy_button(control) is the virtual joystick
// button that stands for it: the CAN slot number when it has a can_button (so the numbering is the same under both read methods),
// else the next free number from 17 up, HID buttons first and then the hat positions (a hat linked to its can_hat is still
// there: it has no slot number); a control that finds all 32 taken has none (0) and is never pressed on the virtual joystick.
//
// Error cases: the ControlMap constructor throws ControlError (a message worded for the preset file) when a friendly name
// is not [a-z0-9_]+ of 1 to 32 characters, starts with hid_ or can_, or ends in _released or _hold; when a member is neither a
// canonical name nor another entry's key; when a group is not linked to any canonical input; or when a group joins two HID
// inputs or two CAN inputs into one control. resolve() and parse_input() return nullopt for what they do not know.
#pragma once
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace buttons {
inline constexpr int hid_button_count = 6, hat_positions = 8, can_slot_count = 16, first_unlinked_vjoy_button = 17, vjoy_buttons = 32;

enum class InputKind { hid_button, hat, can_button, can_hat };
struct Input {
    InputKind kind = InputKind::hid_button;
    int index = 0;   // hid_button 1..6, hat 1..8 (1 up, clockwise), can_button 1..16, can_hat 1..8 (the same way round)
    bool operator==(const Input& o) const { return kind == o.kind && index == o.index; }
    bool operator<(const Input& o) const { return kind != o.kind ? kind < o.kind : index < o.index; }
};
// "hid_button_3", "hat_nw", "can_button_11", "can_hat_ne".
std::string input_name(const Input& input);
// The canonical input a name stands for (any case), or nullopt.
std::optional<Input> parse_input(std::string_view name);
// Every canonical input, in the order controls are numbered: hid buttons, hat positions, can slots, can hat positions.
std::vector<Input> all_inputs();
// Whether a friendly name is allowed (rules above); a canonical name is not "friendly".
bool valid_friendly_name(std::string_view name);

struct ControlError : std::runtime_error { using std::runtime_error::runtime_error; };

class ControlMap {
public:
    using Aliases = std::map<std::string, std::vector<std::string>>;
    explicit ControlMap(const Aliases& aliases = {});
    std::size_t size() const { return controls_.size(); }
    // Any canonical or friendly name, in any case.
    std::optional<int> resolve(std::string_view name) const;
    // The control a canonical input belongs to (every canonical input belongs to one).
    int control_of(const Input& input) const;
    std::optional<Input> hid_source(int control) const { return controls_.at(static_cast<std::size_t>(control)).hid; }
    std::optional<Input> can_source(int control) const { return controls_.at(static_cast<std::size_t>(control)).can; }
    // The control's names: its canonical inputs, then its friendly names alphabetically.
    std::vector<std::string> names(int control) const { return controls_.at(static_cast<std::size_t>(control)).names; }
    // 1..32: the virtual joystick button for the control, or 0 when there is none (the 32 are used up).
    int vjoy_button(int control) const { return controls_.at(static_cast<std::size_t>(control)).vjoy; }
private:
    struct Control {
        std::optional<Input> hid, can;
        std::vector<std::string> names;
        int vjoy = 0;
    };
    std::vector<Control> controls_;
    std::map<std::string, int> by_name_;
    std::map<Input, int> by_input_;
};
}
