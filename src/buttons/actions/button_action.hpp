// SOURCE OF TRUTH for what the plugin does with the yoke's buttons once the reader has delivered them. Specification:
// docs/dev/button-commands.md. Portable; implementations: direct_action.hpp (X-Plane commands), vjoy_action.hpp (a virtual joystick).
//
// An action works from the ButtonHub (buttons/hub.hpp): update() is called once per flight-loop frame on X-Plane's thread and
// takes from the hub what it needs (the edges since last time, or the level now). close() leaves nothing pressed or held (the
// plugin is disabled or stopping); unload() frees anything that may not be freed from a destructor (XPluginStop). Neither throws.
// Whatever an action wants said goes to `log`, a line at a time, for Log.txt.
#pragma once
#include "buttons/hub.hpp"
#include <functional>
#include <string>

namespace buttons {
using Log = std::function<void(const std::string&)>;
class ButtonAction {
public:
    virtual ~ButtonAction() = default;
    virtual void update(ButtonHub& hub, Clock::time_point now, const Log& log) = 0;
    virtual void close() = 0;
    virtual void unload() {}
};
}
