// Implementation of brunner/button_runner.hpp (read that header for the contract).
#include "brunner/button_runner.hpp"
#include <chrono>

namespace brunner {
ButtonRunner::ButtonRunner(Opener opener, int retry_ms) : opener_(std::move(opener)), retry_ms_(retry_ms) {}

void ButtonRunner::start() {
    if (thread_.joinable()) return;
    stop_ = false;
    thread_ = std::thread([this] { run(); });
}
void ButtonRunner::stop() {
    stop_ = true;
    if (thread_.joinable()) thread_.join();
}
std::string ButtonRunner::status() {
    const std::lock_guard<std::mutex> lock(mutex_);
    return status_;
}
void ButtonRunner::set_status(std::string text) {
    const std::lock_guard<std::mutex> lock(mutex_);
    status_ = std::move(text);
}
std::string ButtonRunner::take_drops() {
    std::map<std::string, std::uint64_t> drops;
    {
        const std::lock_guard<std::mutex> lock(mutex_);
        drops.swap(drops_);
    }
    if (drops.empty()) return {};
    std::uint64_t total = 0;
    std::string rules;
    for (const auto& entry : drops) {
        total += entry.second;
        rules += (rules.empty() ? "" : ", ") + entry.first + ": " + std::to_string(entry.second);
    }
    return "Joystick buttons: dropped " + std::to_string(total) + " glitched report" + (total == 1 ? "" : "s") + " (" + rules + ")";
}
void ButtonRunner::wait_retry() {
    for (int waited = 0; waited < retry_ms_ && !stop_; waited += 25) std::this_thread::sleep_for(std::chrono::milliseconds(25));
}

void ButtonRunner::run() {
    while (!stop_) {
        std::unique_ptr<hid::JoystickDevice> device;
        std::string why;
        try {
            device = opener_(why);
        } catch (const std::exception& e) {
            set_status(std::string("Joystick buttons: ") + e.what());
            wait_retry();
            continue;
        }
        if (!device) {
            set_status(why.empty() || why == "no joystick interface" ? std::string() : "Joystick buttons: " + why);
            wait_retry();
            continue;
        }
        const auto layout = hid::parse_report_layout(device->descriptor());
        if (!layout) {
            set_status("Joystick buttons: the joystick interface has no buttons or hat; not used");
            wait_retry();
            continue;
        }
        set_status("Joystick buttons: reading the yoke's joystick interface");
        ButtonInput input(yoke_profile_for({device->vendor(), device->product()}));
        std::vector<std::uint8_t> payload;
        try {
            while (!stop_) {
                if (!device->read(payload, 100)) continue;
                const auto state = hid::decode_report(*layout, payload.data(), payload.size());
                if (!state) continue;
                const auto result = input.feed(*state);
                if (!result.dropped.empty()) {
                    const std::lock_guard<std::mutex> lock(mutex_);
                    ++drops_[std::string(result.dropped)];
                }
                for (const auto& edge : result.edges) presses_.push(edge);
            }
        } catch (const std::exception& e) {
            set_status(std::string("Joystick buttons: ") + e.what() + "; looking again");
        }
        for (const auto& edge : input.release_all()) presses_.push(edge);   // nothing stays held when the yoke goes
        device.reset();
        wait_retry();
    }
}
}
