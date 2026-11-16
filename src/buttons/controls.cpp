// Implementation of buttons/controls.hpp (read that header for the contract).
#include "buttons/controls.hpp"
#include <algorithm>
#include <cctype>
#include <set>

namespace buttons {
namespace {
constexpr const char* hat_names[hat_positions] = {"n", "ne", "e", "se", "s", "sw", "w", "nw"};
std::string lower(std::string_view text) {
    std::string out(text);
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}
bool ends_with(std::string_view text, std::string_view tail) { return text.size() >= tail.size() && text.substr(text.size() - tail.size()) == tail; }
std::optional<int> small_number(std::string_view digits, int low, int high) {
    if (digits.empty() || digits.size() > 2 || !std::all_of(digits.begin(), digits.end(), [](char c) { return c >= '0' && c <= '9'; })) return std::nullopt;
    const int n = std::stoi(std::string(digits));
    return n >= low && n <= high ? std::optional<int>(n) : std::nullopt;
}
}

std::string input_name(const Input& input) {
    if (input.kind == InputKind::button) return "button_" + std::to_string(input.index);
    return std::string("hat_") + hat_names[(input.index - 1) & 7];
}

std::optional<Input> parse_input(std::string_view raw) {
    const auto name = lower(raw);
    constexpr std::string_view button = "button_", hat = "hat_";
    if (name.compare(0, button.size(), button) == 0)
        if (const auto n = small_number(std::string_view(name).substr(button.size()), 1, button_slots)) return Input{InputKind::button, *n};
    if (name.compare(0, hat.size(), hat) == 0)
        for (int k = 0; k < hat_positions; ++k)
            if (name.substr(hat.size()) == hat_names[k]) return Input{InputKind::hat, k + 1};
    return std::nullopt;
}

std::vector<Input> all_inputs() {
    std::vector<Input> inputs;
    for (int n = 1; n <= button_slots; ++n) inputs.push_back({InputKind::button, n});
    for (int k = 1; k <= hat_positions; ++k) inputs.push_back({InputKind::hat, k});
    return inputs;
}

bool valid_friendly_name(std::string_view raw) {
    const auto name = lower(raw);
    if (name.empty() || name.size() > 32) return false;
    if (!std::all_of(name.begin(), name.end(), [](char c) { return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_'; })) return false;
    if (ends_with(name, "_released") || ends_with(name, "_hold")) return false;
    return !parse_input(name);
}

ControlMap::ControlMap(const Aliases& aliases) {
    std::map<Input, std::vector<std::string>> friendly_of;
    std::set<std::string> taken;   // every alias given, lower-case
    for (const auto& entry : aliases) {
        const auto input = parse_input(entry.first);
        if (!input) throw ControlError("button alias \"" + entry.first + "\" is not a button or hat name (button_1 to button_16, hat_n to hat_nw)");
        if (friendly_of.count(*input)) throw ControlError("button alias \"" + entry.first + "\" is given twice");
        auto& names = friendly_of[*input];
        for (const auto& alias : entry.second) {
            if (!valid_friendly_name(alias))
                throw ControlError("button alias \"" + alias + "\" for " + input_name(*input) + " is not a usable name (1 to 32 of a-z, 0-9 and _; not ending in _released or _hold; not a button name)");
            const auto name = lower(alias);
            if (!taken.insert(name).second) throw ControlError("button alias \"" + alias + "\" is given twice");
            names.push_back(name);
        }
    }
    for (const auto& input : all_inputs()) {
        Control control;
        control.input = input;
        control.names.push_back(input_name(input));
        auto friendly = friendly_of[input];
        std::sort(friendly.begin(), friendly.end());
        control.names.insert(control.names.end(), friendly.begin(), friendly.end());
        control.vjoy = input.kind == InputKind::button ? input.index : first_hat_vjoy_button + input.index - 1;
        const int number = static_cast<int>(controls_.size());
        for (const auto& name : control.names) by_name_[name] = number;
        by_input_[input] = number;
        controls_.push_back(std::move(control));
    }
}

std::optional<int> ControlMap::resolve(std::string_view name) const {
    const auto found = by_name_.find(lower(name));
    return found == by_name_.end() ? std::nullopt : std::optional<int>(found->second);
}
int ControlMap::control_of(const Input& input) const { return by_input_.at(input); }
}
