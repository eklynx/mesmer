// SOURCE OF TRUTH for what the yoke's controller hands over: its buttons and hat at one moment, and who receives them.
// Specification: docs/dev/button-commands.md. Portable.
//
// A ButtonState is bit n-1 of `buttons` for button_n (the slots 1..16) and `hat`, 0 (centred) or 1..8 (1 up, clockwise).
// buttons/controls.hpp says which of those are the same control.
//
// A ButtonSink takes states from the thread that produces them (the reader's sampling cycle). publish() gives the latest state;
// release_all() says the source went away (link lost, stopping) so nothing may stay pressed or held. Neither throws.
// buttons/hub.hpp is the one implementation.
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
