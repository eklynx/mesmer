// macOS stand-in for hid/win/vjoy_win.hpp: vJoy is a Windows driver, so there is no virtual
// joystick to press the yoke's buttons on. The plugin's ButtonPresser (brunner/buttons.hpp)
// treats a failed load() as "buttons off" and says why once in Log.txt; the yoke's pitch,
// roll and force feel are not affected. Contract: VirtualJoystick in brunner/buttons.hpp.
#pragma once
#include "brunner/buttons.hpp"

namespace brunner {
class VJoy final : public VirtualJoystick {
public:
    std::string load() override {return "vJoy is a Windows driver; the yoke's buttons are not sent on this platform";}
    void unload() {}
    bool enabled() override {return false;}
    Status status() override {return Status::missing;}
    bool acquire() override {return false;}
    void relinquish() override {}
    int buttons() override {return 0;}
    bool set(unsigned,bool) override {return false;}
    bool reset() override {return false;}
};
}
