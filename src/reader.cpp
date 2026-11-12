// The portable yoke reader: sampling, the supervised reference/loading tests,
// session and reconnect logic, and Reader's start/stop/run. It reaches the
// yoke only through Bridge (bridge.hpp) over a HidDevice (hid_device.hpp), so it
// is identical on every platform. Moved unchanged from reader_win.cpp; the
// operating system's HID access is in hid_win.cpp / hid_mac.cpp.
#include "reader.hpp"
#include "bridge.hpp"
#include "reference.hpp"
#include "loading.hpp"
#include <functional>
#include <cstdlib>
#include <sstream>
#include <memory>
#include <vector>

namespace brunner {
namespace {
// A motor stop that could not be verified. Reported, never retried automatically.
struct StopUnconfirmed : std::runtime_error {explicit StopUnconfirmed(const std::string& what):std::runtime_error(what) {}};
void sample(Bridge& bridge,Snapshot& s,const std::atomic<bool>& stop) {
    s.sampled=Clock::now();
    for (unsigned i=0;i<2;++i) {
        auto& a=s.axes[i];
        a.status=static_cast<std::uint16_t>(bridge.read(i+1,Object::status,stop));
        a.encoder=signed32(bridge.read(i+1,Object::encoder,stop));
        a.position=signed32(bridge.read(i+1,Object::position,stop));
    }
    s.connected=true;++s.cycles;
}
// Supervised one-shot test. Always stops both motors; never hands an enabled
// drive back to the normal read-only loop. No custom loading/torque commands.
struct FlightPaused:std::runtime_error {explicit FlightPaused(const std::string& why="Simulation paused"):std::runtime_error(why) {}};
// The USB link failed while the motors may be enabled (yoke switched off, cable
// pulled). Nothing can be confirmed over a dead link, so this is reported as
// the link lost, not as an unconfirmed stop: the snapshot goes disconnected at
// once, pitch's constant force is cleared and the stop commands sent in case
// the link is flickering, and the next session disables and recovers any
// motors it finds enabled.
void link_lost(Bridge& bridge,Snapshot& s,const std::array<std::uint16_t,2>& words,const std::array<std::uint16_t,2>& modes,
    const std::function<void(const Snapshot&)>& publish) {
    Snapshot lost;lost.bridge_present=true;lost.message="Yoke link lost during motor operation";publish(lost);
    s=Snapshot{};
    bool cleared=false;
    try {stop_session(bridge,words,modes,&cleared);} catch (const std::exception&) {}
    lost.pitch_force_cleared=cleared;publish(lost);
}
void reference_test(Bridge& bridge,Snapshot& s,const std::atomic<bool>& stop,const std::function<void(const Snapshot&)>& publish,
    const std::function<void()>& check_alive) {
    sample(bridge,s,stop);
    for (const auto& a:s.axes) if (a.status&0x000c) throw std::runtime_error("Reference test requires disabled, fault-free drives");
    // Every centring starts from the plugin's own resting settings, whatever a
    // switch-on, CLS2Sim or an interrupted session left (motors off throughout).
    rest_controllers(bridge,stop);
    std::array<std::uint16_t,2> words{},modes{};
    for (unsigned i=0;i<2;++i) {
        words[i]=static_cast<std::uint16_t>(bridge.ref_read(i+1,controlword,stop));
        modes[i]=static_cast<std::uint16_t>(bridge.ref_read(i+1,force_mode,stop));
    }
    s.reference_active=true;s.session_reference=false;s.reference_result.clear();publish(s);
    std::string error;
    bool paused=false;
    try {
        for (unsigned i=0;i<2;++i) {
            check_alive();
            const auto sequence=reference_start(words[i],modes[i]);
            words[i]=static_cast<std::uint16_t>(sequence[4].value);
            modes[i]=static_cast<std::uint16_t>(sequence[5].value);
            for (const auto& op:sequence) {check_alive();bridge.ref_write(i+1,op,stop);}
            const auto deadline=Clock::now()+std::chrono::seconds(40);
            bool acquired=false,center_commanded=false,settled=false;
            CenterEvidence center;
            while (!stop && Clock::now()<deadline) {
                check_alive();
                sample(bridge,s,stop);publish(s);
                const auto& axis=s.axes[i];
                if (axis.status&0x2108) throw std::runtime_error("Controller reported an error during reference test");
                if ((axis.status&0x1004)==0x1004) acquired=true;
                if (acquired && (axis.status&0x1004)!=0x1004) throw std::runtime_error("Reference was lost during centering");
                if (acquired && !center_commanded) {
                    // Target first, then select the vendor's position-centering mode.
                    bridge.ref_write(i+1,{center_position,0,0},stop);
                    bridge.ref_write(i+1,{center_mode,1,0},stop);
                    center_commanded=true;
                }
                if (center_commanded && center.observe(axis.encoder,Clock::now())) {settled=true;break;}
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
            }
            if (!settled) throw std::runtime_error("Reference or centering did not complete within 40 seconds");
        }
    } catch (const FlightPaused& e) {error=e.what();paused=true;}
    catch (const TransportError&) {link_lost(bridge,s,words,modes,publish);throw;}
    catch (const std::exception& e) {error=e.what();}
    // Cancellation of the reader must not cancel the cleanup commands.
    const std::atomic<bool> cleanup{false};
    if (!stop_reference(bridge,words,modes)) throw StopUnconfirmed("STOP NOT CONFIRMED: switch off yoke power");
    sample(bridge,s,cleanup);s.reference_active=false;
    s.session_reference=error.empty();
    s.flight_suspended=s.flight_session && paused;
    s.loading_fault=!error.empty() && !paused && !stop;
    s.reference_result=error.empty()?"Reference and centering passed; motors disabled":error+"; motors disabled";
    publish(s);
}
void loading_test(Bridge& bridge,Snapshot& s,const std::atomic<bool>& stop,
    const std::function<void(const Snapshot&)>& publish,const std::function<void()>& check_alive,std::atomic<bool>& gap,
    const std::function<SessionTarget()>& requested,bool flight,std::atomic<bool>& end,const PitchSetup& pitch={},
    BuffetDrive* buffet=nullptr,const std::function<BuffetTarget()>& buffet_target={}) {
    s.pitch_solved=false;s.pitch_engaged=false;s.roll_solved=false;s.update_hz=0;
    s.tuning_valid=false;s.loading_seconds_remaining=0;s.flight_session=flight;s.flight_suspended=false;s.loading_fault=false;
    s.force_unavailable.clear();s.power_cycle_needed=false;s.pitch_force_cleared.reset();
    s.stop_reason.clear();s.stop_reason_short.clear();s.retry_helps=true;
    if (!flight || !s.session_reference) reference_test(bridge,s,stop,publish,check_alive);
    if (!s.session_reference || stop) return;
    std::array<std::uint16_t,2> words{},modes{};
    for (unsigned i=0;i<2;++i) {
        words[i]=static_cast<std::uint16_t>(bridge.ref_read(i+1,controlword,stop));
        modes[i]=static_cast<std::uint16_t>(bridge.ref_read(i+1,force_mode,stop));
    }
    auto applied=requested().tuning;applied.pitch_trim=0;applied.pitch_bias=0;applied.roll_trim=0;
    // A force-feel session starts where the drive is: no force on either
    // axis, each centred where the yoke is at the softest spring, then blends
    // to the solutions (FeelBlend). The light spring test starts as asked.
    std::int32_t start_centre=0,roll_start_centre=0;
    if (pitch.constant_force) {
        sample(bridge,s,stop);
        const auto load=session_load(applied,drive_counts_from_encoder(s.axes[1].encoder),roll_counts_from_encoder(s.axes[0].encoder));
        applied=load.written;start_centre=load.centre;roll_start_centre=load.roll_centre;
    }
    s.reference_active=true;s.loading_result="Writing the plugin's spring settings; protection limits untouched";publish(s);
    std::string error,refused;FailureReason reason;
    bool paused=false,motor_enabled=false;
    std::optional<std::uint32_t> offset_off; // pitch's constant-force cut-off before a model session
    try {
        prepare_loading(bridge,stop,applied,pitch,&offset_off);
        if (start_centre!=0) {
            checked_loading_write(bridge,2,{trim_position,static_cast<std::uint32_t>(start_centre),0},stop);
            applied.pitch_trim=start_centre;
        }
        if (roll_start_centre!=0) {
            checked_loading_write(bridge,1,{trim_position,static_cast<std::uint32_t>(roll_start_centre),0},stop);
            applied.roll_trim=roll_start_centre;
        }
        for (unsigned node=1;node<=2;++node) verify_loading(bridge,node,stop,applied,pitch);
        s.applied_tuning=applied;s.tuning_valid=true;s.force_bias={0,0};s.force_bias_valid=true;
        for (unsigned i=0;i<2;++i) {
            check_alive();
            const auto sequence=reference_start(words[i],modes[i]);
            words[i]=static_cast<std::uint16_t>(sequence[4].value);modes[i]=static_cast<std::uint16_t>(sequence[5].value);
            motor_enabled=true; // from here a failure is a controller fault, not a refusal
            for (const auto& op:sequence) {check_alive();bridge.ref_write(i+1,op,stop);}
            const auto deadline=Clock::now()+std::chrono::seconds(2);
            bool enabled=false;
            while (!stop && Clock::now()<deadline) {
                check_alive();
                sample(bridge,s,stop);publish(s);
                const auto status=s.axes[i].status;
                if (status&0x2108) throw std::runtime_error("Controller fault enabling light spring");
                if ((status&0x1017)==0x1017) {enabled=true;break;}
            }
            if (!enabled) throw std::runtime_error("Warm reference not acquired for light spring");
            verify_loading(bridge,i+1,stop,applied,pitch);
        }
        s.reference_active=false;s.loading_active=true;
        s.loading_result="Spring active: roll "+std::to_string(applied.roll_percent)+"%, pitch "+std::to_string(applied.pitch_percent)+"%; live calibration available";publish(s);
        const auto deadline=flight?Clock::time_point::max():Clock::now()+std::chrono::minutes(10);
        auto last_tuning=Clock::now();
        // A force-feel session follows the elevator and aileron models: every
        // update writes the solvers' output, blended in only as force feel
        // comes on and at an edit of the feel (FeelBlend). The light spring
        // test keeps its ramp.
        std::optional<FeelBlend> engage;std::optional<TuningRamp> ramp;
        if (pitch.constant_force) {const auto t=requested();engage.emplace(applied,t.pitch?t.pitch->edit:t.roll?t.roll->edit:0);}
        else ramp.emplace(applied);
        while (!stop && Clock::now()<deadline) {
            check_alive();
            if (end.exchange(false)) break; // force feel switched off or motors stopped: clean stop, reference kept
            if (gap.exchange(false)) {
                if (Clock::now()+std::chrono::seconds(11)>deadline) throw std::runtime_error("Not enough test time remains for a ten-second gap");
                s.loading_result="Communication-gap test: no USB requests for ten seconds";publish(s);
                const auto gap_end=Clock::now()+std::chrono::seconds(10);
                while (!stop && Clock::now()<gap_end) {
                    check_alive();
                    std::this_thread::sleep_for(std::chrono::milliseconds(20));
                }
                if (stop) break;
                for (unsigned node=1;node<=2;++node) verify_loading(bridge,node,stop,applied,pitch);
                s.loading_result="Ten-second USB request gap ended; curve and protection limits retained";
            }
            const auto tuning_time=Clock::now();
            const auto target=requested();
            auto want=target.tuning;
            if (engage) {
                // Solved for the encoder read at the end of the last update (0x2106
                // freezes while stopped; the encoder never does).
                if (target.pitch) {
                    s.pitch_solution=solve_pitch(*target.pitch,drive_counts_from_encoder(s.axes[1].encoder));s.pitch_solved=true;
                    want=solved_tuning(want,s.pitch_solution);
                } else {want.pitch_percent=applied.pitch_percent;want.pitch_trim=applied.pitch_trim;want.pitch_bias=applied.pitch_bias;}
                if (target.roll) {
                    s.roll_solution=solve_roll(*target.roll,roll_counts_from_encoder(s.axes[0].encoder));s.roll_solved=true;
                    want=solved_tuning(want,s.roll_solution);
                } else {want.roll_percent=applied.roll_percent;want.roll_trim=applied.roll_trim;}
            }
            const double seconds=std::chrono::duration<double>(tuning_time-last_tuning).count();
            const auto next=engage?engage->advance(want,target.pitch?target.pitch->edit:target.roll?target.roll->edit:0,seconds):ramp->advance(want,seconds);
            s.pitch_engaged=engage && !engage->blending();
            if (seconds>0) s.update_hz=s.update_hz>0?0.9*s.update_hz+0.1/seconds:1/seconds;
            last_tuning=tuning_time;
            // Buffet's knob, read once for the update: its reserve comes out of
            // the spring's budget before its shake goes on (buffet.hpp).
            const auto shake=flight && buffet && buffet_target?buffet_target():BuffetTarget{};
            const auto reserve=flight && buffet?buffet->reserve(shake.knob):DriveReserve{};
            if (update_live_tuning(bridge,applied,next,stop,reserve)) {
                s.applied_tuning=applied;s.force_bias={0,applied.pitch_bias};
                if (!flight && same_tuning(applied,want)) s.loading_result="Live calibration reached: roll "+std::to_string(applied.roll_percent)+"%, pitch "+std::to_string(applied.pitch_percent)+"%, pitch trim "+std::to_string(applied.pitch_trim)+" counts";
            }
            // Buffet after both axes' writes; with its knob off it writes nothing.
            if (flight && buffet && buffet_target) {buffet->update(bridge,shake,applied,modes,stop);s.buffet_units=buffet->units();}
            s.loading_seconds_remaining=flight?-1:static_cast<int>(std::max<std::int64_t>(0,std::chrono::duration_cast<std::chrono::seconds>(deadline-Clock::now()).count()));
            sample(bridge,s,stop);
            for (const auto& axis:s.axes) if ((axis.status&0x1017)!=0x1017 || (axis.status&0x2108)) throw std::runtime_error("Controller state changed during loading");
            publish(s);
            std::this_thread::sleep_until(s.sampled+std::chrono::milliseconds(20));
        }
    } catch (const FlightPaused& e) {error=e.what();paused=true;}
    catch (const TransportError&) {link_lost(bridge,s,words,modes,publish);throw;}
    catch (const SettingsRefused& e) {(motor_enabled?error:refused)=e.what();}
    catch (const std::exception& e) {error=e.what();reason=failure_reason(e);}
    const std::atomic<bool> cleanup{false};
    if (!refused.empty()) {
        // Refused before any write, with both motors confirmed disabled: nothing
        // is sent, and the reference and the pilot's pitch and roll are as they
        // were. Only force feel is unavailable.
        sample(bridge,s,cleanup);record_force_refusal(s,flight,refused);publish(s);
        return;
    }
    // The spring stays configured if transport is lost. The controllers'
    // protection thresholds are never written.
    bool cleared=false;
    const bool stopped=stop_session(bridge,words,modes,&cleared);
    s.pitch_force_cleared=cleared;
    // Whatever buffet set is cleared before the resting settings, which
    // refuse a spring mode with the vibration bit still on.
    if (buffet && buffet->dirty()) s.buffet_uncleared=!buffet->clear(bridge);
    if (buffet) {s.buffet_units=buffet->units();s.buffet_time_unrestored=buffet->time_unrestored();}
    if (!stopped) throw StopUnconfirmed("STOP NOT CONFIRMED: switch off yoke power; "+error);
    std::string rest_detail;
    if (!rest_after_stop(bridge,&rest_detail,offset_off)) throw std::runtime_error("Motors disabled; resting settings incomplete: "+rest_detail+"; original cause: "+error);
    sample(bridge,s,cleanup);s.reference_active=false;s.loading_active=false;
    const auto outcome=classify_session_end(!error.empty(),paused,stop,motor_enabled);
    s.session_reference=outcome.keep_reference;s.flight_suspended=flight && paused;s.loading_fault=outcome.fault;
    if (flight && outcome.force_unavailable) s.force_unavailable="Force feel unavailable - Retry";
    if (outcome.force_unavailable || outcome.fault) {
        if (reason.text.empty()) reason={"Force feel stopped: "+error+".","Details in X-Plane's Log.txt",true};
        s.stop_reason=reason.text;s.stop_reason_short=reason.short_text;s.retry_helps=reason.retry_helps;
    }
    s.applied_tuning={resting_scale,resting_scale,0};s.tuning_valid=true;s.loading_seconds_remaining=0;
    s.force_bias={0,0};s.force_bias_valid=true;s.pitch_solved=false;s.pitch_engaged=false;s.roll_solved=false;s.update_hz=0;
    s.loading_result=(error.empty()?(flight?"Flight loading stopped":"Light spring test ended"):error)+std::string("; motors disabled, resting settings in place");publish(s);
}
}
void Reader::start() {
    stop(); stop_=false;reference_requested_=false;inspection_requested_=false;loading_requested_=false;recovery_requested_=false;gap_requested_=false;
    busy_=false;flight_requested_=false;end_requested_=false;
    Snapshot initial; initial.message="Opening yoke"; publish(initial);
    thread_=std::thread(&Reader::run,this);
}
void Reader::stop() {
    stop_=true;
    if (thread_.joinable()) thread_.join();
    const auto final=snapshot();
    Snapshot disconnected;
    // An unconfirmed motor stop stays on record for the pilot and the lab.
    disconnected.stop_unconfirmed=final.stop_unconfirmed;disconnected.loading_fault=final.stop_unconfirmed;
    if (!final.connected) disconnected.message=final.message;
    else if (!final.loading_result.empty()) disconnected.message=final.loading_result;
    else if (!final.reference_result.empty()) disconnected.message=final.reference_result;
    publish(disconnected);
}
namespace {
// A pause, an aircraft load or a stalled simulator frame releases the motors
// but is not a controller fault: the session suspends with its reference kept.
void wait_before_retry(const std::atomic<bool>& stop,std::chrono::milliseconds delay) {
    const auto until=Clock::now()+delay;
    while (!stop && Clock::now()<until) std::this_thread::sleep_for(std::chrono::milliseconds(50));
}
}
// One USB session: open the bridge, confirm firmware, adopt a kept reference,
// then serve requests and samples until the link fails or the reader stops.
// Controller-level errors from a request are reported in the snapshot and the
// session continues; only transport errors end it. The outer loop in run()
// keeps looking for the yoke for as long as the reader is running.
void Reader::session(Snapshot& s) {
    Bridge bridge;
    s.bridge_present=true;s.message="Waiting for the yoke's controllers";publish(s);
    const auto check_alive=[this] {
        if (simulator_paused()) throw FlightPaused{};
        const auto failure=simulator_failure();
        if (!failure.empty()) throw FlightPaused(failure);
    };
    const auto publish_state=[this](const Snapshot& current) {publish(current);};
    for (unsigned i=0;i<2;++i) {
        s.axes[i].firmware=static_cast<std::uint16_t>(bridge.read(i+1,Object::firmware,stop_));
        if (s.axes[i].firmware!=367) throw std::runtime_error("Unsupported controller firmware; expected v367");
    }
    // A link lost while buffet was on: what it set is cleared once both
    // motors are confirmed stopped (the stop comes first, never delayed), and
    // before recovery, which refuses a spring mode with the vibration bit on.
    const auto clear_buffet=[&] {
        if (!buffet_.dirty()) return;
        s.buffet_uncleared=!buffet_.clear(bridge);s.buffet_units=buffet_.units();s.buffet_time_unrestored=buffet_.time_unrestored();
    };
    sample(bridge,s,stop_);
    if (((s.axes[0].status|s.axes[1].status)&0x0004)!=0) {
        // Motors found enabled, e.g. the USB link dropped during force feel.
        // Disable them first, then restore the surveyed baseline with motors off.
        std::array<std::uint16_t,2> words{},modes{};
        for (unsigned i=0;i<2;++i) {
            words[i]=static_cast<std::uint16_t>(bridge.ref_read(i+1,controlword,stop_));
            modes[i]=static_cast<std::uint16_t>(bridge.ref_read(i+1,force_mode,stop_));
        }
        if (!stop_reference(bridge,words,modes)) {
            s.loading_fault=true;s.stop_unconfirmed=true;s.loading_result="STOP NOT CONFIRMED at reconnect: switch off yoke power";publish(s);
        } else {
            s.loading_result="Motors were enabled when the yoke reconnected; disabled";
            clear_buffet();
            try {recover_controllers(bridge,stop_);}
            catch (const TransportError&) {throw;}
            catch (const OperationCancelled&) {throw;}
            catch (const std::exception& e) {
                s.loading_fault=true;s.power_cycle_needed=dynamic_cast<const SettingsRefused*>(&e)!=nullptr;++s.failures;
                s.loading_result=std::string("Recovery after reconnect failed: ")+e.what();
            }
        }
        sample(bridge,s,stop_);
    }
    // Motors found off (or now confirmed off): nothing delays a stop here.
    if (((s.axes[0].status|s.axes[1].status)&0x0004)==0) clear_buffet();
    // A controller that still reports referenced-and-stopped (bit 10 set, as
    // observed when X-Plane starts after an earlier centring in the same power
    // session) keeps its reference: no movement. Power-on (0x0031) does not.
    // Validation of this adoption on hardware is still owed (see README).
    s.session_reference=referenced_stopped_status(s.axes[0].status) && referenced_stopped_status(s.axes[1].status);
    s.reference_active=false;s.loading_active=false;s.flight_suspended=false;
    s.reference_result=s.session_reference?"Yoke kept its centre; no movement needed":"Yoke needs centring";
    s.message="Receiving both axes";publish(s);
    const auto serve=[&](const char* what,const std::function<void()>& request) {
        try {request();}
        catch (const StopUnconfirmed& e) {
            const std::atomic<bool> cleanup{false};
            try {sample(bridge,s,cleanup);} catch (const std::exception&) {}
            s.reference_active=false;s.loading_active=false;s.session_reference=false;++s.failures;
            s.loading_fault=true;s.stop_unconfirmed=true;s.loading_result=e.what();publish(s);
        }
        catch (const TransportError&) {throw;}
        catch (const OperationCancelled&) {throw;}
        catch (const std::exception& e) {
            record_request_failure(s,dynamic_cast<const SettingsRefused*>(&e)!=nullptr,std::string(what)+" failed: "+e.what());publish(s);
        }
        busy_=false;
    };
    while (!stop_) {
        if (recovery_requested_.exchange(false)) serve("Recovery",[&]{
            recover_controllers(bridge,stop_);
            s.applied_tuning={resting_scale,resting_scale,0};s.tuning_valid=true;s.power_cycle_needed=false;
            s.loading_result="Resting settings in place and faults cleared with both motors disabled";
        });
        if (inspection_requested_.exchange(false)) serve("Inspection",[&]{s.loading_inspection=inspect_loading(bridge,stop_);});
        if (reference_requested_.exchange(false)) serve("Centring",[&]{
            s.flight_session=false;s.flight_suspended=false;s.loading_fault=false;s.stop_unconfirmed=false;s.power_cycle_needed=false;
            reference_test(bridge,s,stop_,publish_state,check_alive);
        });
        if (loading_requested_.exchange(false)) serve("Light spring test",[&]{
            end_requested_=false;
            loading_test(bridge,s,stop_,publish_state,check_alive,gap_requested_,[this]{return SessionTarget{requested_tuning(),{},{}};},false,end_requested_);
        });
        if (flight_requested_.exchange(false)) serve("Force feel",[&]{
            end_requested_=false;
            loading_test(bridge,s,stop_,publish_state,check_alive,gap_requested_,[this]{return flight_target();},true,end_requested_,pitch_setup(),
                &buffet_,[this]{return buffet_target();});
        });
        if (stop_) break;
        sample(bridge,s,stop_);
        if (!referenced_stopped_status(s.axes[0].status) || !referenced_stopped_status(s.axes[1].status)) {
            if (s.session_reference) s.reference_result="Controller state changed; repeat reference";
            s.session_reference=false;
        }
        s.message="Receiving both axes"; publish(s);
        std::this_thread::sleep_until(s.sampled+std::chrono::milliseconds(20));
    }
}
void Reader::run() {
    Snapshot s;
    while (!stop_) {
        try {
            session(s);
            break;
        } catch (const OperationCancelled&) {
            // Expected cancellation between read transactions. Reference/loading
            // cleanup catches cancellation internally and still reports real faults.
            break;
        } catch (const BridgeAbsent& e) {
            Snapshot absent;absent.message=e.what();publish(absent);
        } catch (const BridgeBusy& e) {
            Snapshot busy;busy.bridge_present=true;busy.bridge_busy=true;busy.message=e.what();publish(busy);
        } catch (const TransportError& e) {
            // Bridge present, controllers silent or the link dropped mid-session:
            // close it and look again. The reference is re-evaluated on reconnect.
            // What the lost session's end read back stays on record.
            Snapshot lost;lost.bridge_present=true;lost.message=e.what();lost.pitch_force_cleared=snapshot().pitch_force_cleared;publish(lost);
        } catch (const std::exception& e) {
            // Firmware mismatch or another refusal to talk to this hardware.
            Snapshot refused;refused.bridge_present=true;refused.message=e.what();refused.loading_fault=true;publish(refused);
        }
        // A request made while the link was failing must not run, unannounced,
        // against the next session: the supervisor asks again when it is safe.
        busy_=false;reference_requested_=false;inspection_requested_=false;loading_requested_=false;
        recovery_requested_=false;gap_requested_=false;flight_requested_=false;end_requested_=false;
        s=Snapshot{};
        wait_before_retry(stop_,std::chrono::milliseconds(1000));
    }
    busy_=false;
}
}
