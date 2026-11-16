// SOURCE OF TRUTH for reading the yoke's buttons and hat: objects of its controller, read over the bridge in every sampling cycle.
// Specification: docs/dev/button-commands.md. Portable (the bridge is the same on every operating system).
//
// Which objects and what pressed looks like is the yoke model's (buttons/input/can_layout.hpp, YokePreset::button_registers), handed
// over with set_layout() at the start of each USB session. The reader (brunner/reader.cpp) owns one CanSource and calls
// sample_buttons() inside each sampling cycle, on its own thread.
//
// Happy path: with the source enabled (something uses the buttons) and a sink attached, every cycle reads each distinct object the
// layout names once, with the sampling timeout (one attempt, 20 ms), evaluates every control's mask or value against what was read,
// keeps the buttons in Snapshot::buttons and publishes them with the hat position to the sink as a ButtonState (bit n-1 = button_n).
// Disabled, nothing is read: no bus traffic at all. link_lost() releases everything at the sink (the reader calls it when a USB
// session ends).
//
// Error cases: a read that the controller refuses, or answers with nonsense, leaves the buttons unread, with why, in
// Snapshot::button_refusal; the sink is told to release everything, and the read is tried again from 5 s later: the buttons never
// stop pitch, roll or force feel. A lost USB link, a missed reply (brunner::ReplyMissed, which the reader tolerates for a while)
// or a stop request are thrown on, as for any read, and nothing of that cycle is published. If two hat positions are pressed at once
// the lower one is the hat. Nothing else is thrown.
#pragma once
#include "brunner/core.hpp"
#include "brunner/reference.hpp"
#include "buttons/input/can_layout.hpp"
#include "buttons/state.hpp"
#include <atomic>
#include <chrono>
#include <vector>

namespace buttons {
constexpr std::chrono::seconds can_retry_after{5};

class CanSource {
public:
    CanSource() { set_layout(cls_e_can_layout()); }   // the original CLS-E's until a session names the yoke's
    // Where readings go. Set from X-Plane's thread, used by the reader's: nullptr (the default) reads and publishes nothing.
    void attach(ButtonSink* sink) { sink_ = sink; }
    void enable(bool on) { enabled_ = on; }
    bool enabled() const { return enabled_; }
    // Which objects: the yoke model's. Called by the reader, on its own thread, when a USB session starts (the CLS-E's until then).
    void set_layout(const CanLayout& layout) {
        layout_ = layout;
        reads_.clear();
        signal_read_.clear();
        for (const auto& entry : layout_) {
            const auto& signal = entry.second;
            std::size_t found = 0;
            while (found < reads_.size() && !(reads_[found].node == signal.node && reads_[found].index == signal.index && reads_[found].sub == signal.sub)) ++found;
            if (found == reads_.size()) reads_.push_back({signal.node, signal.index, signal.sub, signal.bytes()});
            signal_read_.push_back(found);
        }
    }
    const CanLayout& layout() const { return layout_; }
    void link_lost() { if (auto* sink = sink_.load()) sink->release_all(); }
    // Link is the reader's bridge (brunner/bridge.hpp), or a test's.
    template<class Link> void sample(Link& link, brunner::Snapshot& s, const std::atomic<bool>& stop, Clock::time_point now) {
        if (!enabled_ || !sink_) return;
        if (!s.button_refusal.empty() && now < s.button_retry) return;
        try {
            std::vector<std::uint32_t> words(reads_.size());
            for (std::size_t i = 0; i < reads_.size(); ++i)
                words[i] = link.ref_read(reads_[i].node, brunner::Register{reads_[i].index, reads_[i].sub, reads_[i].bytes}, stop, brunner::sample_reply_timeout_ms);
            std::uint32_t buttons = 0;
            int hat = 0;
            std::size_t n = 0;
            for (const auto& entry : layout_) {
                if (entry.second.pressed(words[signal_read_[n++]])) {
                    if (entry.first.kind == InputKind::button) buttons |= 1u << (entry.first.index - 1);
                    else if (hat == 0) hat = entry.first.index;
                }
            }
            s.buttons = static_cast<std::uint16_t>(buttons);
            s.button_refusal.clear();
            sink_.load()->publish(ButtonState{buttons, hat}, now);
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
    struct Read { std::uint8_t node; std::uint16_t index; std::uint8_t sub; std::uint8_t bytes; };
    CanLayout layout_;                      // the reader's thread only, with what follows
    std::vector<Read> reads_;               // each distinct object the layout names, once
    std::vector<std::size_t> signal_read_;  // for each entry of layout_ in order, which of reads_ gives its word
};

// The reader's call inside a sampling cycle: the Link carries the CanSource the reader gave it, or none.
template<class Link> void sample_buttons(Link& link, brunner::Snapshot& s, const std::atomic<bool>& stop, Clock::time_point now) {
    if (auto* can = link.can_source()) can->sample(link, s, stop, now);
}
}
