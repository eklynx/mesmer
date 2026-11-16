// Implementation of buttons/edges.hpp (read that header for the contract).
#include "buttons/edges.hpp"
#include <algorithm>

namespace buttons {
std::vector<int> pressed_controls(const ControlMap& map, ReadMethod read, const ButtonState& state) {
    std::vector<int> pressed;
    const bool hid = read == ReadMethod::hid;
    const int buttons = hid ? hid_button_count : can_slot_count;
    for (int n = 1; n <= buttons; ++n)
        if (state.buttons & (1u << (n - 1))) pressed.push_back(map.control_of({hid ? InputKind::hid_button : InputKind::can_button, n}));
    if (state.hat >= 1 && state.hat <= hat_positions) pressed.push_back(map.control_of({hid ? InputKind::hat : InputKind::can_hat, state.hat}));
    return pressed;
}

std::vector<Edge> EdgeDetector::feed(const ButtonState& state) {
    std::vector<Edge> edges;
    const auto now = pressed_controls(*map_, read_, state);
    const auto has = [](const std::vector<int>& list, int control) { return std::find(list.begin(), list.end(), control) != list.end(); };
    if (last_) {
        for (const int control : *last_) if (!has(now, control)) edges.push_back({control, true});
        for (const int control : now) if (!has(*last_, control)) edges.push_back({control, false});
    }
    last_ = now;
    return edges;
}

std::vector<Edge> EdgeDetector::release_all() {
    std::vector<Edge> edges;
    if (last_) for (const int control : *last_) edges.push_back({control, true});
    last_.reset();
    return edges;
}
}
