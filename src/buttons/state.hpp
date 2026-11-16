// SOURCE OF TRUTH for what a read method hands over: the yoke's buttons and hat at one moment, and who receives them.
// Specification: docs/dev/button-commands.md. Portable.
//
// A ButtonState is in the numbering of the read method that produced it: with HID, bit n-1 of `buttons` is hid_button_n
// (1..6) and `hat` is 0 (centred) or 1..8 (1 up, clockwise); with CanBus, bit n-1 is can_button_n (the slots 1..16) and `hat`
// is 0. buttons/controls.hpp says which of those are the same control.
//
// A ButtonSink takes states from whichever thread produces them (the reader's sampling cycle for CanBus, the joystick
// thread for HID). publish() gives the latest state; release_all() says the source went away (link lost, device removed,
// stopping) so nothing may stay pressed or held. Neither throws. buttons/hub.hpp is the one implementation.
#pragma once
#include <chrono>
#include <cstdint>

namespace buttons {
using Clock = std::chrono::steady_clock;
struct ButtonState {
    std::uint32_t buttons = 0;
    int hat = 0;
    bool operator==(const ButtonState& o) const { return buttons == o.buttons && hat == o.hat; }
};
class ButtonSink {
public:
    virtual ~ButtonSink() = default;
    virtual void publish(const ButtonState& state, Clock::time_point now) = 0;
    virtual void release_all() = 0;
};
}
