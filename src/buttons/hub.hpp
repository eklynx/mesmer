// SOURCE OF TRUTH for the one place the yoke's buttons are delivered, and every action takes them from.
// Specification: docs/dev/button-commands.md. Portable; read this header, not hub.cpp.
//
// Happy path: configure(map) says which controls exist (it resets what was seen). The reader's sampling cycle calls publish(state, now)
// with each reading, and release_all() when the link goes away; the hub turns them into edges (buttons/edges.hpp). The X-Plane thread
// calls take_edges() for what happened since last time (what the Direct action runs) and vjoy_word(now) for the level (what the VJoy
// action presses): bit n-1 set while vJoy button n of a control is down.
//
// Error cases (none thrown): vjoy_word is nullopt, meaning "nothing is known, every button up", before the first state, after
// release_all(), and when the last state is older than 250 ms (the registers are read every sampling cycle, so silence means the
// reading stopped). Thread-safe: every call takes the one lock; the reader and the X-Plane thread may call at once.
#pragma once
#include "buttons/edges.hpp"
#include <memory>
#include <mutex>
#include <optional>
#include <vector>

namespace buttons {
class ButtonHub final : public ButtonSink {
public:
    static constexpr std::chrono::milliseconds freshness{250};
    ButtonHub();
    // The controls in use. Edges not yet taken are dropped and the baseline restarts; the caller ends
    // what the old configuration had held (take_edges first, or release_all) when that matters.
    void configure(std::shared_ptr<const ControlMap> map);
    void publish(const ButtonState& state, Clock::time_point now) override;
    void release_all() override;
    std::vector<Edge> take_edges();
    std::optional<std::uint32_t> vjoy_word(Clock::time_point now) const;
    std::shared_ptr<const ControlMap> controls() const;
private:
    mutable std::mutex mutex_;
    std::shared_ptr<const ControlMap> map_;
    EdgeDetector detector_;
    std::vector<Edge> edges_;
    std::optional<ButtonState> state_;
    Clock::time_point stamp_{};
};
}
