// SOURCE OF TRUTH for the thread that watches the yoke's joystick interface and turns it into presses.
// Specification: docs/dev/button-commands.md. Portable; the operating system sits behind hid/joystick_device.hpp.
//
// Happy path: start() launches one thread that opens the joystick interface (from the opener it was given), parses its report descriptor, then decodes every report, drops the
// glitched ones (YokeProfile::invalid_reason) and pushes each genuine press and release onto presses(). When the session ends (the yoke removed, a failure, stop()) it pushes
// the release of everything still pressed, so a held command is always ended. X-Plane's thread
// drains presses() and fires the commands; this thread makes no X-Plane SDK calls and does not log. stop()
// joins the thread. A yoke with no joystick interface, or one that is unplugged, is simply looked for again
// every two seconds.
//
// Error cases (none thrown): everything that goes wrong becomes a status() line (what the owner reads in
// Log.txt; empty until something worth saying happens, and only changes when the situation does). A device that is
// removed or fails ends that session and the next one starts after the retry delay. take_drops() reports the
// glitched reports dropped since the last call, or empty when none, so the caller can log them at its own pace.
#pragma once
#include "brunner/button_input.hpp"
#include "hid/joystick_device.hpp"
#include <atomic>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

namespace brunner {
class ButtonRunner {
public:
    using Opener = std::function<std::unique_ptr<hid::JoystickDevice>(std::string& why)>;
    // The opener says where the device comes from: the plugin passes hid::open_joystick, tests a fake.
    explicit ButtonRunner(Opener opener, int retry_ms = 2000);
    ~ButtonRunner() { stop(); }
    ButtonRunner(const ButtonRunner&) = delete;
    ButtonRunner& operator=(const ButtonRunner&) = delete;
    void start();
    void stop();
    PressQueue& presses() { return presses_; }
    std::string status();
    // "dropped N glitched joystick reports (rule: count, ...)" for the reports dropped since the last call; "" for none.
    std::string take_drops();
private:
    void run();
    void set_status(std::string text);
    void wait_retry();
    Opener opener_;
    int retry_ms_;
    PressQueue presses_;
    std::atomic<bool> stop_{false};
    std::thread thread_;
    std::mutex mutex_;
    std::string status_;
    std::map<std::string, std::uint64_t> drops_;
};
}
