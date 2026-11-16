// Implementation of buttons/controls.hpp (read that header for the contract).
#include "buttons/controls.hpp"
#include <algorithm>
#include <cctype>
#include <numeric>

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
// The order controls are numbered in: a canonical input's position in all_inputs().
int rank(const Input& input) {
    switch (input.kind) {
    case InputKind::hid_button: return input.index - 1;
    case InputKind::hat: return hid_button_count + input.index - 1;
    case InputKind::can_button: return hid_button_count + hat_positions + input.index - 1;
    case InputKind::can_hat: return hid_button_count + hat_positions + can_slot_count + input.index - 1;
    }
    return 0;
}
struct DisjointSets {
    std::vector<int> parent;
    explicit DisjointSets(int n) : parent(static_cast<std::size_t>(n)) { std::iota(parent.begin(), parent.end(), 0); }
    int find(int x) { return parent[static_cast<std::size_t>(x)] == x ? x : parent[static_cast<std::size_t>(x)] = find(parent[static_cast<std::size_t>(x)]); }
    void join(int a, int b) { parent[static_cast<std::size_t>(find(a))] = find(b); }
};
}

std::string input_name(const Input& input) {
    switch (input.kind) {
    case InputKind::hid_button: return "hid_button_" + std::to_string(input.index);
    case InputKind::hat: return std::string("hat_") + hat_names[(input.index - 1) & 7];
    case InputKind::can_button: return "can_button_" + std::to_string(input.index);
    case InputKind::can_hat: return std::string("can_hat_") + hat_names[(input.index - 1) & 7];
    }
    return {};
}

std::optional<Input> parse_input(std::string_view raw) {
    const auto name = lower(raw);
    constexpr std::string_view hid = "hid_button_", can = "can_button_", hat = "hat_", can_hat = "can_hat_";
    if (name.compare(0, hid.size(), hid) == 0)
        if (const auto n = small_number(std::string_view(name).substr(hid.size()), 1, hid_button_count)) return Input{InputKind::hid_button, *n};
    if (name.compare(0, can.size(), can) == 0)
        if (const auto n = small_number(std::string_view(name).substr(can.size()), 1, can_slot_count)) return Input{InputKind::can_button, *n};
    if (name.compare(0, can_hat.size(), can_hat) == 0)
        for (int k = 0; k < hat_positions; ++k)
            if (name.substr(can_hat.size()) == hat_names[k]) return Input{InputKind::can_hat, k + 1};
    if (name.compare(0, hat.size(), hat) == 0)
        for (int k = 0; k < hat_positions; ++k)
            if (name.substr(hat.size()) == hat_names[k]) return Input{InputKind::hat, k + 1};
    return std::nullopt;
}

std::vector<Input> all_inputs() {
    std::vector<Input> inputs;
    for (int n = 1; n <= hid_button_count; ++n) inputs.push_back({InputKind::hid_button, n});
    for (int k = 1; k <= hat_positions; ++k) inputs.push_back({InputKind::hat, k});
    for (int n = 1; n <= can_slot_count; ++n) inputs.push_back({InputKind::can_button, n});
    for (int k = 1; k <= hat_positions; ++k) inputs.push_back({InputKind::can_hat, k});
    return inputs;
}

bool valid_friendly_name(std::string_view raw) {
    const auto name = lower(raw);
    if (name.empty() || name.size() > 32) return false;
    if (!std::all_of(name.begin(), name.end(), [](char c) { return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_'; })) return false;
    if (name.compare(0, 4, "hid_") == 0 || name.compare(0, 4, "can_") == 0) return false;
    if (ends_with(name, "_released") || ends_with(name, "_hold")) return false;
    return !parse_input(name);
}

ControlMap::ControlMap(const Aliases& aliases) {
    const auto inputs = all_inputs();
    const int canonical = static_cast<int>(inputs.size());
    // Nodes: the canonical inputs, then each entry's key that is not itself canonical.
    std::map<std::string, int> node_of;   // lower-case name -> node
    for (int i = 0; i < canonical; ++i) node_of[input_name(inputs[static_cast<std::size_t>(i)])] = i;
    int nodes = canonical;
    std::vector<std::string> node_name(inputs.size());
    for (int i = 0; i < canonical; ++i) node_name[static_cast<std::size_t>(i)] = input_name(inputs[static_cast<std::size_t>(i)]);
    for (const auto& entry : aliases) {
        const auto key = lower(entry.first);
        if (parse_input(key)) continue;   // a canonical name as a key (hat_n): it is its own node already
        if (!valid_friendly_name(key))
            throw ControlError("button alias \"" + entry.first + "\" is not a usable name (1 to 32 of a-z, 0-9 and _; not starting with hid_ or can_; not ending in _released or _hold; not a button name)");
        if (node_of.count(key)) throw ControlError("button alias \"" + entry.first + "\" is given twice");
        node_of[key] = nodes++;
        node_name.push_back(key);
    }
    DisjointSets sets(nodes);
    for (const auto& entry : aliases) {
        const int key = node_of.at(lower(entry.first));
        if (entry.second.empty() && !parse_input(entry.first)) throw ControlError("button alias \"" + entry.first + "\" lists nothing");
        for (const auto& member : entry.second) {
            const auto found = node_of.find(lower(member));
            if (found == node_of.end()) throw ControlError("button alias \"" + entry.first + "\" lists \"" + member + "\", which is neither a button name nor another alias");
            sets.join(key, found->second);
        }
    }
    // Controls: the components that hold a canonical input, numbered by their lowest-ranked input.
    std::map<int, std::vector<int>> members;   // root -> nodes
    for (int n = 0; n < nodes; ++n) members[sets.find(n)].push_back(n);
    std::vector<std::pair<int, int>> order;     // (lowest canonical rank, root)
    for (const auto& component : members) {
        int lowest = canonical;
        for (const int n : component.second) if (n < canonical) lowest = std::min(lowest, rank(inputs[static_cast<std::size_t>(n)]));
        if (lowest == canonical) {
            const auto& first = node_name[static_cast<std::size_t>(component.second.front())];
            throw ControlError("button alias \"" + first + "\" is not linked to any button");
        }
        order.push_back({lowest, component.first});
    }
    std::sort(order.begin(), order.end());
    int next_vjoy = first_unlinked_vjoy_button;
    for (const auto& item : order) {
        Control control;
        std::vector<std::string> friendly;
        for (const int n : members.at(item.second)) {
            if (n < canonical) {
                const Input input = inputs[static_cast<std::size_t>(n)];
                const bool from_can = input.kind == InputKind::can_button || input.kind == InputKind::can_hat;
                auto& slot = from_can ? control.can : control.hid;
                if (slot) throw ControlError(std::string("a button alias joins ") + input_name(*slot) + " and " + input_name(input) + " into one control, but a control has one "
                                             + (from_can ? "CAN input" : "HID input"));
                slot = input;
            } else {
                friendly.push_back(node_name[static_cast<std::size_t>(n)]);
            }
        }
        if (control.hid) control.names.push_back(input_name(*control.hid));
        if (control.can) control.names.push_back(input_name(*control.can));
        std::sort(friendly.begin(), friendly.end());
        control.names.insert(control.names.end(), friendly.begin(), friendly.end());
        // A CAN slot number, else the next free one from 17 up; when the 32 are used up (only a map with many unlinked inputs gets there) 0: no virtual button.
        if (control.can && control.can->kind == InputKind::can_button) control.vjoy = control.can->index;
        else control.vjoy = next_vjoy <= vjoy_buttons ? next_vjoy++ : 0;
        const int number = static_cast<int>(controls_.size());
        for (const auto& name : control.names) by_name_[name] = number;
        if (control.hid) by_input_[*control.hid] = number;
        if (control.can) by_input_[*control.can] = number;
        controls_.push_back(std::move(control));
    }
}

std::optional<int> ControlMap::resolve(std::string_view name) const {
    const auto found = by_name_.find(lower(name));
    return found == by_name_.end() ? std::nullopt : std::optional<int>(found->second);
}
int ControlMap::control_of(const Input& input) const { return by_input_.at(input); }
}
