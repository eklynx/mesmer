#pragma once
#include "core.hpp"
#include "loading.hpp"
#include "buffet.hpp"
#include "hid_device.hpp"
#include "yoke_preset.hpp"
#include <atomic>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

namespace brunner {
class ReadOnlyBridge;
class Reader {
public:
    ~Reader() { stop(); }
    void start();
    void stop();
    // The yoke models (presets), and so the USB ids to open; before the reader starts.
    void set_presets(std::vector<YokePreset> presets) {presets_=std::move(presets);usb_ids_=preset_usb_ids(presets_);}
    // The owner's acknowledgement (Developer menu, owner_state.hpp): drive a model whose preset is not verified.
    void set_allow_unverified(bool allow) {allow_unverified_=allow;}
    // For tests: where the bridge comes from (default: open_bridge(ids), the real device).
    void set_device_factory(std::function<std::unique_ptr<HidDevice>()> factory) {device_factory_=std::move(factory);}
    // Read-only mode (readonly.hpp): every request that could write to the yoke is
    // refused here as well as ignored by the session. Only the survey is served.
    bool request_survey() {
        if (!snapshot().read_only) return false;
        survey_requested_=true;return true;
    }
    bool request_reference_test() {
        const auto s=snapshot();
        if (!s.connected || s.read_only || s.reference_active || s.loading_active) return false;
        bool expected=false;
        if (!busy_.compare_exchange_strong(expected,true)) return false;
        reference_requested_=true;return true;
    }
    void request_loading_inspection() { if (!snapshot().read_only) inspection_requested_=true; }
    void request_loading_recovery() { if (!snapshot().read_only) recovery_requested_=true; }
    bool request_communication_gap() {
        if (!snapshot().loading_active) return false;
        gap_requested_=true;return true;
    }
    bool request_trim_step(std::int32_t counts) {
        if (counts < -600 || counts > 600 || !snapshot().loading_active) return false;
        return request_tuning(TuningField::pitch_trim,counts);
    }
    bool request_tuning(TuningField field,int value) {
        std::lock_guard<std::mutex> lock(mutex_);
        return set_tuning(tuning_,field,value);
    }
    Tuning requested_tuning() const {std::lock_guard<std::mutex> lock(mutex_);return tuning_;}
    // What the next force-feel session writes on pitch with the motors off
    // (curve, drag gains, constant force kept on); a running session keeps its own.
    bool set_pitch_setup(const PitchSetup& p) {
        if (!valid_pitch_setup(p)) return false;
        std::lock_guard<std::mutex> lock(mutex_);pitch_setup_=p;return true;
    }
    // Buffet's knob and what the flight asks of each axis (buffet.hpp); a
    // force-feel session reads it every update.
    void set_buffet(const BuffetTarget& t) {std::lock_guard<std::mutex> lock(mutex_);buffet_target_=t;}
    bool request_loading_test() {
        const auto s=snapshot();
        if (!s.connected || s.read_only || s.reference_active || s.loading_active) return false;
        bool expected=false;
        if (!busy_.compare_exchange_strong(expected,true)) return false;
        loading_requested_=true;return true;
    }
    bool request_flight() {
        const auto s=snapshot();
        if (!s.connected || s.read_only || s.reference_active || s.loading_active) return false;
        bool expected=false;
        if (!busy_.compare_exchange_strong(expected,true)) return false;
        flight_requested_=true;return true;
    }
    // End a loading session (force feel or the light spring test) cleanly:
    // motors off, baseline restored, reference kept, reader connected.
    bool request_loading_end() {
        if (!snapshot().loading_active) return false;
        end_requested_=true;return true;
    }
    bool running() const {return !stop_;}
    // flight_target carries roll; pitch is solved from pitch_model in the
    // session itself, with the yoke's position read in each update.
    void simulator_update(bool running,bool paused,const Tuning& flight_target,const std::optional<PitchModel>& pitch_model={},
        const std::optional<RollModel>& roll_model={}) {
        std::lock_guard<std::mutex> lock(mutex_);
        simulator_running_=running;simulator_paused_=paused;
        heartbeat_=Clock::now();
        // Stop/pause carries no new device demand. If a poll is finishing
        // concurrently, it must never see a default 40% target on source loss.
        // Both models or neither: a session never flies one axis on its own.
        if (running && !paused && valid_tuning(flight_target) && pitch_model && valid_pitch_model(*pitch_model)
            && roll_model && valid_roll_model(*roll_model))
            flight_target_={flight_target,pitch_model,roll_model};
    }
    Snapshot snapshot() const { std::lock_guard<std::mutex> lock(mutex_); return state_; }
private:
    void run();
    void session(Snapshot& s);
    void read_only_session(ReadOnlyBridge& bridge,Snapshot& s,const std::string& why);
    std::vector<YokePreset> presets_=builtin_yoke_presets();
    bool allow_unverified_=false;
    std::vector<UsbId> usb_ids_=default_usb_ids();
    std::function<std::unique_ptr<HidDevice>()> device_factory_=[this] {return open_bridge(usb_ids_);};
    std::atomic<bool> survey_requested_{false};
    void publish(Snapshot state) { std::lock_guard<std::mutex> lock(mutex_); state_=std::move(state); }
    std::atomic<bool> stop_{true};
    std::atomic<bool> reference_requested_{false};
    std::atomic<bool> inspection_requested_{false};
    std::atomic<bool> loading_requested_{false};
    std::atomic<bool> recovery_requested_{false};
    std::atomic<bool> gap_requested_{false};
    std::atomic<bool> busy_{false},flight_requested_{false},end_requested_{false};
    Tuning tuning_{}; // guarded by mutex_; writes never enable a motor
    SessionTarget flight_target_{};
    PitchSetup pitch_setup_{};
    BuffetTarget buffet_target_{}; // guarded by mutex_
    BuffetDrive buffet_; // reader thread only
    std::thread thread_;
    mutable std::mutex mutex_;
    Snapshot state_;
    Clock::time_point heartbeat_{};
    bool simulator_running_=false,simulator_paused_=false;
    bool simulator_paused() const {std::lock_guard<std::mutex> lock(mutex_);return simulator_paused_;}
    SessionTarget flight_target() const {std::lock_guard<std::mutex> lock(mutex_);return flight_target_;}
    PitchSetup pitch_setup() const {std::lock_guard<std::mutex> lock(mutex_);return pitch_setup_;}
    BuffetTarget buffet_target() const {std::lock_guard<std::mutex> lock(mutex_);return buffet_target_;}
    // Any lapse releases the motors; none of them is a controller fault. The
    // session is suspended with its reference kept and resumes when steady.
    std::string simulator_failure() const {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto now=Clock::now();
        if (!simulator_running_) return "Simulator controls are not ready";
        const auto age=std::chrono::duration_cast<std::chrono::milliseconds>(now-heartbeat_).count();
        if (now<heartbeat_ || now-heartbeat_>std::chrono::milliseconds(500)) return "Simulator heartbeat expired ("+std::to_string(age)+" ms)";
        return {};
    }
};
}
