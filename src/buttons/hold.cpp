// Implementation of buttons/hold.hpp (read that header for the contract).
#include "buttons/hold.hpp"
#include <algorithm>

namespace buttons {
bool HoldTracker::held(const std::string& command) const {
    return std::any_of(begun_.begin(), begun_.end(), [&](const auto& b) { return b.first == command; });
}

std::vector<HoldAction> HoldTracker::plan(const std::vector<Edge>& edges, const ButtonCommands& commands) {
    // 1. Which controls with a hold entry are pressed once the whole batch is applied, and which were pressed and let go within it.
    auto active = active_;
    std::vector<int> tapped, pressed_now;
    for (const auto& edge : edges) {
        if (commands.commands_for(edge.control, Variant::hold).empty()) continue;
        const auto at = std::find(active.begin(), active.end(), edge.control);
        if (!edge.released) {
            if (at == active.end()) { active.push_back(edge.control); pressed_now.push_back(edge.control); }
        } else if (at != active.end()) {
            active.erase(at);
            const auto fresh = std::find(pressed_now.begin(), pressed_now.end(), edge.control);
            if (fresh != pressed_now.end()) { pressed_now.erase(fresh); tapped.push_back(edge.control); }
        }
    }
    // 2. The commands that should be held now: every pressed control's, each once, in press order.
    std::vector<std::pair<std::string, int>> wanted;
    const auto wants = [&](const std::string& command) {
        return std::any_of(wanted.begin(), wanted.end(), [&](const auto& w) { return w.first == command; });
    };
    for (const int control : active)
        for (const auto& command : commands.commands_for(control, Variant::hold))
            if (!wants(command)) wanted.push_back({command, control});
    // 3. The difference: ends first, then begins, then the taps.
    std::vector<HoldAction> actions;
    for (const auto& b : begun_) if (!wants(b.first)) actions.push_back({b.first, b.second, false});
    for (const auto& w : wanted) if (!held(w.first)) actions.push_back({w.first, w.second, true});
    std::vector<std::string> tapped_commands;
    for (const int control : tapped)
        for (const auto& command : commands.commands_for(control, Variant::hold)) {
            if (wants(command) || held(command) || std::find(tapped_commands.begin(), tapped_commands.end(), command) != tapped_commands.end()) continue;
            tapped_commands.push_back(command);
            actions.push_back({command, control, true});
            actions.push_back({command, control, false});
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
}
