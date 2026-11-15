// Implementation of brunner/button_input.hpp (read that header for the contract).
#include "brunner/button_input.hpp"
#include <algorithm>
#include <cctype>

namespace brunner {
namespace {
constexpr const char* hat_names[8] = {"n", "ne", "e", "se", "s", "sw", "w", "nw"};
}

std::string input_name(const InputId& id) {
    const std::string base = id.kind == InputKind::button ? "button_" + std::to_string(id.index)
                                                          : std::string("hat_") + hat_names[(id.index - 1) & 7];
    return id.hold ? base + "_hold" : id.released ? base + "_released" : base;
}

std::optional<InputId> parse_input_name(std::string_view name, const YokeProfile& profile) {
    constexpr std::string_view hold_suffix = "_hold";
    if (name.size() > hold_suffix.size() && name.substr(name.size() - hold_suffix.size()) == hold_suffix) {
        auto id = parse_input_name(name.substr(0, name.size() - hold_suffix.size()), profile);
        if (!id || id->released) return std::nullopt;
        id->hold = true;
        return id;
    }
    constexpr std::string_view suffix = "_released";
    if (name.size() > suffix.size() && name.substr(name.size() - suffix.size()) == suffix) {
        auto id = parse_input_name(name.substr(0, name.size() - suffix.size()), profile);
        if (!id || id->hold) return std::nullopt;
        id->released = true;
        return id;
    }
    constexpr std::string_view button = "button_", hat = "hat_";
    if (name.substr(0, button.size()) == button) {
        const auto digits = name.substr(button.size());
        if (digits.empty() || digits.size() > 2 || !std::all_of(digits.begin(), digits.end(), [](char c) { return std::isdigit(static_cast<unsigned char>(c)); }) || digits[0] == '0')
            return std::nullopt;
        const int n = std::stoi(std::string(digits));
        if (n > profile.button_count()) return std::nullopt;
        return InputId{InputKind::button, n};
    }
    if (name.substr(0, hat.size()) == hat) {
        const auto direction = name.substr(hat.size());
        for (int k = 0; k < 8; ++k)
            if (direction == hat_names[k]) return InputId{InputKind::hat, k + 1};
    }
    return std::nullopt;
}

std::vector<InputId> all_inputs(const YokeProfile& profile) {
    std::vector<InputId> ids;
    for (int n = 1; n <= profile.button_count(); ++n) ids.push_back({InputKind::button, n});
    for (int k = 1; k <= 8; ++k) ids.push_back({InputKind::hat, k});
    return ids;
}

ButtonCommands ButtonCommands::from_state(const std::map<std::string, ButtonEntry>& entries,
                                          const YokeProfile& profile, std::vector<std::string>& notes) {
    ButtonCommands out;
    for (const auto& entry : entries) {
        const auto id = parse_input_name(entry.first, profile);
        if (!id) {
            notes.push_back("button_commands: \"" + entry.first + "\" is not a button or hat position of this yoke; ignored");
            continue;
        }
        for (const auto& text : entry.second.commands) {
            if (text.empty()) continue;   // no command, as for null
            if (std::any_of(text.begin(), text.end(), [](unsigned char c) { return std::isspace(c) || std::iscntrl(c); })) {
                notes.push_back("button_commands: " + entry.first + " has \"" + text + "\", which is not a command name (it has spaces or control characters); ignored");
                continue;
            }
            out.commands_[*id].push_back(text);
        }
    }
    return out;
}

const std::vector<std::string>& ButtonCommands::commands_for(const InputId& id) const {
    static const std::vector<std::string> none;
    const auto it = commands_.find(id);
    return it == commands_.end() ? none : it->second;
}

ButtonInput::Result ButtonInput::feed(const hid::JoystickState& state) {
    Result result;
    result.dropped = profile_->invalid_reason(state);
    if (!result.dropped.empty()) return result;
    if (last_) {
        const std::uint32_t newly = state.buttons & ~last_->buttons, gone = last_->buttons & ~state.buttons;
        for (int n = 1; n <= 32; ++n)
            if (gone & (1u << (n - 1))) result.edges.push_back({InputKind::button, n, true});
        if (last_->hat != 0 && state.hat != last_->hat) result.edges.push_back({InputKind::hat, last_->hat, true});
        for (int n = 1; n <= 32; ++n)
            if (newly & (1u << (n - 1))) result.edges.push_back({InputKind::button, n});
        if (state.hat != 0 && state.hat != last_->hat) result.edges.push_back({InputKind::hat, state.hat});
    }
    last_ = state;
    return result;
}

std::vector<InputId> ButtonInput::release_all() {
    std::vector<InputId> edges;
    if (!last_) return edges;
    for (int n = 1; n <= 32; ++n)
        if (last_->buttons & (1u << (n - 1))) edges.push_back({InputKind::button, n, true});
    if (last_->hat != 0) edges.push_back({InputKind::hat, last_->hat, true});
    last_.reset();
    return edges;
}

bool HoldTracker::held(const std::string& command) const {
    return std::any_of(begun_.begin(), begun_.end(), [&](const auto& b) { return b.first == command; });
}

std::vector<HoldAction> HoldTracker::plan(const std::vector<InputId>& edges, const ButtonCommands& commands) {
    // 1. Which "_hold" controls are pressed once the whole batch is applied, and which were pressed and let go within it.
    auto active = active_;
    std::vector<InputId> tapped;
    std::vector<InputId> pressed_now;   // pressed within this batch and still pressed (not yet a tap)
    for (const auto& edge : edges) {
        auto key = edge;
        key.released = false;
        key.hold = true;
        if (commands.commands_for(key).empty()) continue;
        const auto at = std::find(active.begin(), active.end(), key);
        if (!edge.released) {
            if (at == active.end()) {
                active.push_back(key);
                pressed_now.push_back(key);
            }
        } else if (at != active.end()) {
            active.erase(at);
            const auto fresh = std::find(pressed_now.begin(), pressed_now.end(), key);
            if (fresh != pressed_now.end()) {
                pressed_now.erase(fresh);
                tapped.push_back(key);
            }
        }
    }
    // 2. The commands that should be held now: every pressed control's, each once, in press order.
    std::vector<std::pair<std::string, InputId>> wanted;
    const auto wants = [&](const std::string& command) {
        return std::any_of(wanted.begin(), wanted.end(), [&](const auto& w) { return w.first == command; });
    };
    for (const auto& key : active)
        for (const auto& command : commands.commands_for(key))
            if (!wants(command)) wanted.push_back({command, key});
    // 3. The difference: ends first, then begins, then the taps.
    std::vector<HoldAction> actions;
    for (const auto& b : begun_)
        if (!wants(b.first)) actions.push_back({b.first, b.second, false});
    for (const auto& w : wanted)
        if (!held(w.first)) actions.push_back({w.first, w.second, true});
    std::vector<std::string> tapped_commands;
    for (const auto& key : tapped)
        for (const auto& command : commands.commands_for(key)) {
            if (wants(command) || held(command) || std::find(tapped_commands.begin(), tapped_commands.end(), command) != tapped_commands.end()) continue;
            tapped_commands.push_back(command);
            actions.push_back({command, key, true});
            actions.push_back({command, key, false});
        }
    active_ = std::move(active);
    begun_ = std::move(wanted);
    return actions;
}

std::vector<HoldAction> HoldTracker::end_all() {
    std::vector<HoldAction> actions;
    for (const auto& b : begun_) actions.push_back({b.first, b.second, false});
    begun_.clear();
    active_.clear();
    return actions;
}

void PressQueue::push(const InputId& id) {
    const std::lock_guard<std::mutex> lock(mutex_);
    items_.push_back(id);
}
std::vector<InputId> PressQueue::drain() {
    const std::lock_guard<std::mutex> lock(mutex_);
    std::vector<InputId> out;
    out.swap(items_);
    return out;
}
}
