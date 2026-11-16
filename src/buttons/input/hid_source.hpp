// SOURCE OF TRUTH for the HID read method: the thread that watches the yoke's joystick interface and hands its buttons and
// hat to a ButtonSink. Specification: docs/dev/button-commands.md. Portable; the operating system sits behind
// hid/joystick_device.hpp.
//
// Happy path: start(sink) launches one thread that opens the joystick interface (from the opener it was given), takes where
// its buttons, hat and padding sit (JoystickDevice::layout()), then decodes every report, drops the glitched ones
// (HidProfile::invalid_reason) and publishes each genuine one to the sink as a ButtonState in HID numbering (bit n-1 =
// hid_button_n, hat 0..8). When the session ends (the device removed, a failure, stop()) it calls sink.release_all(), so
// nothing stays pressed or held. A yoke with no joystick interface, or one that is unplugged, is simply looked for again every
// two seconds. This thread makes no X-Plane SDK calls and does not log; status() and take_drops() are for the caller to log.
//
// Error cases (none thrown): everything that goes wrong becomes a status() line (what the owner reads in Log.txt; empty until
// something worth saying happens, and only changes when the situation does). take_drops() reports the glitched reports dropped
// since the last call, or empty when none, so the caller can log them at its own pace.
#pragma once
#include "buttons/input/hid_profile.hpp"
#include "buttons/state.hpp"
#include "hid/joystick_device.hpp"
#include <atomic>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

namespace buttons {
class HidSource {
public:
    using Opener = std::function<std::unique_ptr<hid::JoystickDevice>(std::string& why)>;
    // The opener says where the device comes from: the plugin passes hid::open_joystick, tests a fake.
    explicit HidSource(Opener opener, int retry_ms = 2000);
    ~HidSource() { stop(); }
    HidSource(const HidSource&) = delete;
    HidSource& operator=(const HidSource&) = delete;
    void start(ButtonSink& sink);
    void stop();
    bool running() const { return thread_.joinable(); }
    std::string status();
    // "dropped N glitched joystick reports (rule: count, ...)" for the reports dropped since the last call; "" for none.
    std::string take_drops();
private:
    void run();
    void set_status(std::string text);
    void wait_retry();
    Opener opener_;
    int retry_ms_;
    ButtonSink* sink_ = nullptr;
    std::atomic<bool> stop_{false};
    std::thread thread_;
    std::mutex mutex_;
    std::string status_;
    std::map<std::string, std::uint64_t> drops_;
};
}
