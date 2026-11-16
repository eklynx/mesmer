// Implementation of buttons/actions/vjoy_action.hpp (read that header for the contract).
#include "buttons/actions/vjoy_action.hpp"

namespace buttons {
void VJoyAction::update(ButtonHub& hub, Clock::time_point now, const Log& log) {
    hub.take_edges();   // this action works from the level, not the edges
    const auto word = hub.vjoy_word(now);
    for (const auto& line : presser_.update(*joystick_, word, now)) log(line);
    if (const auto told = changes_.update(word, now); !told.empty()) log(told);
}
void VJoyAction::close() {
    presser_.close(*joystick_);
    changes_ = ButtonLog{};
}
void VJoyAction::unload() { joystick_->unload(); }
}
