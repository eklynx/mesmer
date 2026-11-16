// SOURCE OF TRUTH for the CanBus read method: the yoke's buttons and hat as registers of its controller, read over the bridge in
// every sampling cycle. Specification: docs/dev/button-commands.md. Portable (the bridge is the same on every operating system).
//
// Which registers is the yoke model's (buttons/input/can_layout.hpp, YokePreset::button_registers), handed over with set_layout()
// at the start of each USB session: the original CLS-E's buttons are node 2's register 0x60fd (bits 16 to 31 are the slots
// can_button_1..16), the CLS-E NG's are its own record 0x2880 (entry 3: buttons 1 to 6 in bits 0 to 5, entry 4: the hat, 0 to 8).
// The reader (brunner/reader.cpp) owns one CanSource and calls sample_buttons() inside each sampling cycle, on its own thread.
//
// Happy path: with the source enabled (the read method is CanBus) and a sink attached, every cycle reads the buttons register, and the
// hat register when the layout has one, each with the sampling timeout (one attempt, 20 ms), keeps the slots in Snapshot::buttons and
// publishes both to the sink as a ButtonState in CAN numbering (bit n-1 = can_button_n, hat = can_hat position). Disabled (the read
// method is HID) nothing is read: no bus traffic at all. link_lost() releases everything at the sink (the reader calls it when a USB
// session ends).
//
// Error cases: a read that the controller refuses, or answers with nonsense, leaves the buttons unread, with why, in
// Snapshot::button_refusal; the sink is told to release everything, and the read is tried again from 5 s later: the buttons never
// stop pitch, roll or force feel. A lost USB link, a missed reply (brunner::ReplyMissed, which the reader tolerates for a while)
// or a stop request are thrown on, as for any read. A hat value above 8 reads as centred. Nothing else is thrown.
#pragma once
#include "brunner/core.hpp"
#include "brunner/reference.hpp"
#include "buttons/input/can_layout.hpp"
#include "buttons/state.hpp"
#include <atomic>
#include <chrono>

namespace buttons {
constexpr std::chrono::seconds can_retry_after{5};

class CanSource {
public:
    // Where readings go. Set from X-Plane's thread, used by the reader's: nullptr (the default) reads and publishes nothing.
    void attach(ButtonSink* sink) { sink_ = sink; }
    void enable(bool on) { enabled_ = on; }
    bool enabled() const { return enabled_; }
    // Which registers: the yoke model's. Called by the reader, on its own thread, when a USB session starts (the CLS-E's until then).
    void set_layout(const CanLayout& layout) { layout_ = layout; }
    const CanLayout& layout() const { return layout_; }
    void link_lost() { if (auto* sink = sink_.load()) sink->release_all(); }
    // Link is the reader's bridge (brunner/bridge.hpp), or a test's.
    template<class Link> void sample(Link& link, brunner::Snapshot& s, const std::atomic<bool>& stop, Clock::time_point now) {
        if (!enabled_ || !sink_) return;
        if (!s.button_refusal.empty() && now < s.button_retry) return;
        try {
            const auto& reg = layout_.buttons;
            const auto word = link.ref_read(2, brunner::Register{reg.index, reg.sub, reg.bytes}, stop, brunner::sample_reply_timeout_ms);
            const std::uint32_t mask = layout_.bits >= 16 ? 0xffffu : (1u << layout_.bits) - 1u;
            const auto slots = static_cast<std::uint16_t>((word >> layout_.shift) & mask);
            int hat = 0;
            if (layout_.hat) {
                const auto value = link.ref_read(2, brunner::Register{layout_.hat->index, layout_.hat->sub, layout_.hat->bytes}, stop, brunner::sample_reply_timeout_ms);
                hat = value >= 1 && value <= 8 ? static_cast<int>(value) : 0;
            }
            s.buttons = slots;
            s.button_refusal.clear();
            sink_.load()->publish(ButtonState{slots, hat}, now);
        } catch (const brunner::TransportError&) { throw;
        } catch (const brunner::OperationCancelled&) { throw;
        } catch (const std::exception& e) {
            s.buttons.reset();
            s.button_refusal = e.what();
            s.button_retry = now + can_retry_after;
            sink_.load()->release_all();
        }
    }
private:
    std::atomic<ButtonSink*> sink_{nullptr};
    std::atomic<bool> enabled_{false};
    CanLayout layout_;   // the reader's thread only
};

// The reader's call inside a sampling cycle: the Link carries the CanSource the reader gave it, or none.
template<class Link> void sample_buttons(Link& link, brunner::Snapshot& s, const std::atomic<bool>& stop, Clock::time_point now) {
    if (auto* can = link.can_source()) can->sample(link, s, stop, now);
}
}
