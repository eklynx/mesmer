#pragma once
#include "brunner/core.hpp"
#include "brunner/loading.hpp"
#include "force/buffet.hpp"
#include "hid/hid_device.hpp"
#include "brunner/yoke_preset.hpp"
#include <atomic>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

namespace brunner {
class ReadOnlyBridge;
// What the reader knows of the simulator, from the plugin's per-frame updates (docs/how-it-works.md,
// "Pause"). failure() is empty while a force-feel session may carry on. Not thread-safe: the
// Reader guards it with its mutex.
//  - A pause is not a lapse: the verdict of the last unpaused update stands through it, so forces
//    hold and resume is instant. The pause itself is paused(), for sessions that do end on one.
//  - Just after a pause the first frames have no fresh physics, so "controls not ready" is not
//    believed for resume_grace; a real loss outlasts it.
//  - A heartbeat older than 500 ms is a lapse.
class SimulatorWatch {
    bool running_=false,paused_=false;
    Clock::time_point heartbeat_{},last_paused_{};
public:
    static constexpr std::chrono::milliseconds resume_grace{500},heartbeat_limit{500};
    void update(bool running,bool paused,Clock::time_point now) {
        if (!paused) running_=running; else last_paused_=now;
        paused_=paused;heartbeat_=now;
    }
    bool paused() const {return paused_;}
    std::string failure(Clock::time_point now) const {
        const bool just_resumed=last_paused_!=Clock::time_point{} && now>=last_paused_ && now-last_paused_<=resume_grace;
        if (!running_ && !just_resumed) return "Simulator controls are not ready";
        const auto age=std::chrono::duration_cast<std::chrono::milliseconds>(now-heartbeat_).count();
        if (now<heartbeat_ || now-heartbeat_>heartbeat_limit) return "Simulator heartbeat expired ("+std::to_string(age)+" ms)";
        return {};
    }
};
class Reader {
public:
    ~Reader() { stop(); }
    void start();
    void stop();
    // The yoke models (presets), and so the USB ids to open; before the reader starts.
    void set_presets(std::vector<YokePreset> presets) {presets_=std::move(presets);usb_ids_=preset_usb_ids(presets_);}
    // The owner's acknowledgement (Developer menu, brunner/owner_state.hpp): drive a model whose preset is not verified.
    void set_allow_unverified(bool allow) {allow_unverified_=allow;}
    // For tests: where the bridge comes from (default: open_bridge(ids), the real device).
    void set_device_factory(std::function<std::unique_ptr<HidDevice>()> factory) {device_factory_=std::move(factory);}
    // Read-only mode (brunner/readonly.hpp): every request that could write to the yoke is
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
    // "Reset yoke": both motors off, the model's resting settings written and read back, faults cleared.
    // False (and nothing happens) when the yoke is read-only: it never writes. The yoke is left stopped;
    // Re-centre references it again as usual.
    // How far a centring may drift and still count as settled, in thousandths of an axis's travel (2 = 0.2 %,
    // the default; 0 to 50). The plugin sets it from BrunnerDirect.ini (centre_drift_permille).
    void set_centre_drift_permille(int permille) {centre_drift_permille_=std::clamp(permille,0,50);}
    bool request_reset() {
        if (snapshot().read_only || busy_) return false;
        reset_requested_=true;return true;
    }
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
    // Buffet's knob and what the flight asks of each axis (force/buffet.hpp); a
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
        watch_.update(running,paused,Clock::now());
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
    std::function<std::unique_ptr<HidDevice>()> device_factory_=[this] {return open_bridge(usb_ids_,{brunner_vendor});};
    std::atomic<bool> survey_requested_{false};
    void publish(Snapshot state) { std::lock_guard<std::mutex> lock(mutex_); state_=std::move(state); }
    std::atomic<bool> stop_{true};
    std::atomic<bool> reference_requested_{false};
    std::atomic<bool> inspection_requested_{false};
    std::atomic<bool> loading_requested_{false};
    std::atomic<bool> recovery_requested_{false},reset_requested_{false};
    std::atomic<int> centre_drift_permille_{2};
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
    SimulatorWatch watch_;
    bool simulator_paused() const {std::lock_guard<std::mutex> lock(mutex_);return watch_.paused();}
    SessionTarget flight_target() const {std::lock_guard<std::mutex> lock(mutex_);return flight_target_;}
    PitchSetup pitch_setup() const {std::lock_guard<std::mutex> lock(mutex_);return pitch_setup_;}
    BuffetTarget buffet_target() const {std::lock_guard<std::mutex> lock(mutex_);return buffet_target_;}
    // Any lapse releases the motors; none of them is a controller fault. (An engaged force-feel
    // session does not treat a pause as a lapse: see check_engaged in Reader::session.) The
    // session is suspended with its reference kept and resumes when steady.
    std::string simulator_failure() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return watch_.failure(Clock::now());
    }
};
}
