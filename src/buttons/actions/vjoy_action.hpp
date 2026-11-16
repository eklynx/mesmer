// SOURCE OF TRUTH for the VJoy action: the yoke's controls press the buttons of a virtual joystick that X-Plane binds.
// Specification: docs/dev/button-commands.md. Portable; the feeder is VirtualJoystick (vjoy_presser.hpp), made per operating system
// by make_virtual_joystick(): Windows has Brunner's signed vJoy driver (win/vjoy_win.hpp); everywhere else there is none yet
// and the factory returns nullptr, which the service answers by falling back to the Direct action. The seam is the place a
// macOS or Linux virtual joystick goes later.
//
// Happy path: update() once per frame takes the ButtonHub's level (vjoy_word: bit n-1 for virtual joystick button n, a control's
// number being its CAN slot, else 17 up) and gives it to the feeder: it takes the device when it can, lets every button up,
// then presses and lets up the buttons as the word changes (only changes are sent). Every change and every refusal reaches `log`.
// All buttons are up whenever nothing is known (no link, a read method's source gone, or CanBus readings older than 250 ms).
//
// Error cases (none thrown): no driver, the driver switched off, no buttons on device 1, or another program holding it: said
// once ("Yoke buttons off: <why>; trying again every 5 seconds") and tried again every 5 seconds. A button the driver refuses
// lets all up and gives the device back. The yoke flies exactly as before in every case.
#pragma once
#include "buttons/actions/button_action.hpp"
#include "buttons/actions/vjoy_presser.hpp"
#include <memory>

namespace buttons {
// The virtual joystick this operating system has, or nullptr.
std::unique_ptr<VirtualJoystick> make_virtual_joystick();

class VJoyAction final : public ButtonAction {
public:
    explicit VJoyAction(std::unique_ptr<VirtualJoystick> joystick) : joystick_(std::move(joystick)) {}
    void update(ButtonHub& hub, Clock::time_point now, const Log& log) override;
    void close() override;
    void unload() override;
private:
    std::unique_ptr<VirtualJoystick> joystick_;
    ButtonPresser presser_;
    ButtonLog changes_;
};
}
