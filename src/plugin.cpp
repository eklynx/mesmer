#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "brunner/reader.hpp"
#include "brunner/owner_state.hpp"
#include "brunner/readonly.hpp"
#include "config.hpp"
#include "lab.hpp"
#include "brunner/yoke.hpp"
#include "ui/view/panel_win.hpp"
#include "ui/menu/menu.hpp"
#include "xplane/published.hpp"
#include "xplane/sim.hpp"
#include "ui/view/startup_notice.hpp"
#include "brunner/loading.hpp"
#include "force/slew.hpp"
#include "ui/workbench/workbench_server.hpp"
#include "XPLMPlugin.h"
#include "XPLMProcessing.h"
#include "XPLMUtilities.h"
#include "XPLMMenus.h"
#include "XPLMDataAccess.h"
#include "XPLMPlanes.h"
#include "XPLMDisplay.h"
#include "XPLMGraphics.h"
#include <cstdio>
#include <ctime>
#include <sstream>
#include <system_error>
#include <cstring>
#include <limits>
#include <filesystem>
#include <fstream>
#include <memory>

namespace {
namespace wb=brunner::workbench;
brunner::Reader reader;
brunner::monitor::Window force_monitor,force_hud{true};
brunner::recording::Recorder recorder;
XPLMCommandRef hud_command=nullptr,pin_command=nullptr,move_command=nullptr;
struct HudDataref {const char* name;int* value;int low,high;};
HudDataref hud_datarefs[]{
    {"brunner_direct/hud/visible",&force_hud.settings.visible,0,1},
    {"brunner_direct/hud/opacity_percent",&force_hud.settings.opacity,35,95},
    {"brunner_direct/hud/hold_ms",&force_hud.settings.hold_ms,1000,30000},
    {"brunner_direct/hud/fade_ms",&force_hud.settings.fade_ms,0,3000},
    {"brunner_direct/hud/trim_threshold_milli",&force_hud.settings.trim_milli,1,100},
    {"brunner_direct/hud/mismatch_ms",&force_hud.settings.mismatch_ms,100,10000},
    {"brunner_direct/hud/spring_error_percent",&force_hud.settings.spring_error_percent,0,20},
    {"brunner_direct/hud/neutral_error_counts",&force_hud.settings.neutral_error_counts,0,1000},
    {"brunner_direct/hud/right",&force_hud.settings.right,0,10000},
    {"brunner_direct/hud/top",&force_hud.settings.top,0,10000},
};
brunner::xplane::PublishedDatarefs published; // the plugin's own datarefs (xplane/published.hpp)
int read_hud(void* ref) {return *static_cast<HudDataref*>(ref)->value;}
void write_hud(void* ref,int value) {
    const auto& r=*static_cast<HudDataref*>(ref);
    if (value<r.low || value>r.high) {recorder.event(std::string("HUD rejected: ")+r.name);return;}
    *r.value=value;force_hud.refresh();recorder.event(std::string("HUD configured: ")+r.name+"="+std::to_string(value));
}
int read_recorder(void* ref) {
    const auto field=reinterpret_cast<std::intptr_t>(ref);
    return field==0?static_cast<int>(recorder.state()):static_cast<int>(std::min<std::uint64_t>(field==1?recorder.dropped():recorder.written(),2147483647));
}

XPLMCommandRef monitor_command=nullptr;
brunner::ui::YokeMenu yoke_menu; // the menu (ui/menu/menu.hpp); what a click does is menu_action below
bool enabled=false;
std::string last_status;
XPLMCommandRef reference_command=nullptr,stop_command=nullptr,stop_motors_command=nullptr;
XPLMCommandRef inspect_command=nullptr;
XPLMCommandRef loading_command=nullptr;
XPLMCommandRef recovery_command=nullptr;
XPLMCommandRef gap_command=nullptr;
XPLMCommandRef reconnect_command=nullptr;
XPLMCommandRef flight_command=nullptr;
XPLMCommandRef recenter_command=nullptr,force_feel_toggle=nullptr,force_feel_on=nullptr,force_feel_off=nullptr,status_line_command=nullptr;
brunner::FlightSettings flight_settings;
// The feel workbench (docs/brunner/workbench): its state is shared with the
// server thread, which never calls the SDK; everything here runs on X-Plane's.
std::unique_ptr<wb::Workbench> bench;
wb::Server workbench_server;
int workbench_port=0;
std::filesystem::path plugin_folder;
std::string feel_menu_text="Feel tuning: off";
// A port X-Plane's last listener left in TIME_WAIT frees up on its own: while
// enabled and not listening, the bind is tried again every 5 s.
brunner::Clock::time_point workbench_retry_at{};
bool workbench_failure_logged=false;
XPLMCommandRef mark_feel_command=nullptr,compare_feel_command=nullptr;
wb::FrameInputs bench_inputs;
wb::Applied flown; // what the workbench said to fly this frame: settings, pitch's knobs, the profile's figures
// The aircraft profiles (decision 5), BrunnerDirect.profiles.json beside the
// plugin; re-read within 2 s of a change. Pitch has no target until one parses.
std::optional<brunner::ProfileSet> profile_set;
std::filesystem::file_time_type profiles_time{};std::uintmax_t profiles_size=0;bool profiles_present=false;
brunner::Clock::time_point profiles_checked{};
// The drag gains the running force-feel session started with (they apply only at a start).
std::optional<std::pair<int,int>> session_drag;
brunner::Clock::time_point last_publish{};
brunner::YokeSupervisor yoke;
brunner::YokeDecision decision;
std::string corner_reason; // the corner window's short reason while Retry is offered
bool force_feel=false; // the pilot's switch; startup value from configuration
bool show_status_line=true;
brunner::StartupReadiness startup_readiness;
XPLMFlightLoopID readiness_loop=nullptr;
brunner::ui::StartupNotice startup_notice; // the corner window (ui/view/startup_notice.hpp)
// The workbench's newest notice while it is fresh: the corner line shows it.
std::optional<wb::Notice> corner_notice;
constexpr std::chrono::seconds corner_notice_time{6};
wb::SessionNotices session_notices; // force feel on and off, said in the workbench's notices
bool simulator_ready_for_start=false;
bool reset_pending=false; // Developer > Reset yoke: asked once force feel and any test have ended
brunner::FlightSample flight_sample;
// X-Plane's stalled shares as the models take them (force/slew.hpp); flight_sample keeps the raw ones.
brunner::StallSlews stall_slews;
// The wing roots' two sections as buffet reads them for pitch (force/buffet.hpp):
// pressure, angle of attack and stalled share, on the profile's own aircraft only.
std::optional<std::array<brunner::AileronSide,2>> root_stall;
const void* buffet_seeded_for=nullptr; // the page whose buffet knob the feel settings seeded
brunner::FlightTarget flight_output;
brunner::TailAir flight_tail; // the model's tail pressure this frame, and where it came from
brunner::xplane::Sim sim; // X-Plane's own datarefs (xplane/sim.hpp)
brunner::DiagnosticTrim diagnostic_trim;
std::array<XPLMCommandRef,3> trim_commands{};
std::array<std::int32_t,3> trim_steps{{600,-600,0}};
std::string last_inspection;
std::string last_loading;
// Stored controller soft travel, with directions confirmed by the supervised
// November 6 push/pull/right/left recording. End-to-end simulator check follows.
// The CLS-E-flow input calibration: the active drive's travel either way, in its directions
// (roll right positive, pitch forward positive). The CLS-E's is -13000..13000 reversed / -12000..12000.
std::array<brunner::Calibration,2> calibration() {
    const auto& d=brunner::active_drive();
    const auto roll=static_cast<std::int32_t>(std::lround(d.roll_travel_counts)),pitch=static_cast<std::int32_t>(std::lround(d.pitch_travel_counts));
    return {{{-roll,roll,d.roll_input_reverse,true},{-pitch,pitch,d.pitch_input_reverse,true}}};
}
std::string last_input_status;
// Read-only mode (brunner/readonly.hpp): the learned travel, where it is kept, and the surveys written.
brunner::Travel read_only_travel;
brunner::UsbId travel_id{0,0}; // the yoke model the learned range belongs to
bool travel_dirty=false;
brunner::Clock::time_point travel_saved{};
unsigned surveys_written=0;
std::filesystem::path output_folder; // Output/BrunnerDirect, set when the plugin is enabled
// The yoke models: the built-ins, plus anything BrunnerDirect.yokes.json adds or changes.
std::vector<brunner::YokePreset> yoke_presets=brunner::builtin_yoke_presets();
std::string last_yoke_note,last_yoke_model;
// The owner's choice to drive a yoke model that is not verified (brunner/owner_state.hpp): off unless saved on.
bool allow_unverified_yoke=false;
enum class LiveField { roll_target,pitch_target,trim_target,roll_applied,pitch_applied,trim_applied,
    connected,reference_active,referenced,loading_active,tuning_valid,remaining,sample_age,
    roll_encoder,pitch_encoder,roll_status,pitch_status,last_write_accepted,rejected_writes,
    reference_airspeed,roll_units_per_nm,units_per_lb,gain_divisor,move_back,auto_start,flight_active,flight_suspended,loading_fault,
    bias_limited,airspeed_knots,flight_trim_target,owns_trim,lab_trim,lab_lease,lab_active,startup_notice,notice_draws,
    force_feel,status_line,retry_offered,bridge_present,stock_aircraft };
struct LiveDataref {const char* name;LiveField field;bool writable=false;};
LiveDataref live_datarefs[]{
    {"brunner_direct/config/roll_spring_percent",LiveField::roll_target,true},
    {"brunner_direct/config/pitch_spring_percent",LiveField::pitch_target,true},
    {"brunner_direct/config/pitch_trim_counts",LiveField::trim_target,true},
    {"brunner_direct/state/roll_spring_percent",LiveField::roll_applied},
    {"brunner_direct/state/pitch_spring_percent",LiveField::pitch_applied},
    {"brunner_direct/state/pitch_trim_counts",LiveField::trim_applied},
    {"brunner_direct/state/connected",LiveField::connected},
    {"brunner_direct/state/reference_active",LiveField::reference_active},
    {"brunner_direct/state/referenced",LiveField::referenced},
    {"brunner_direct/state/loading_active",LiveField::loading_active},
    {"brunner_direct/state/tuning_valid",LiveField::tuning_valid},
    {"brunner_direct/state/seconds_remaining",LiveField::remaining},
    {"brunner_direct/state/sample_age_ms",LiveField::sample_age},
    {"brunner_direct/state/roll_encoder",LiveField::roll_encoder},
    {"brunner_direct/state/pitch_encoder",LiveField::pitch_encoder},
    {"brunner_direct/state/roll_status",LiveField::roll_status},
    {"brunner_direct/state/pitch_status",LiveField::pitch_status},
    {"brunner_direct/config/last_write_accepted",LiveField::last_write_accepted},
    {"brunner_direct/config/rejected_writes",LiveField::rejected_writes},
    {"brunner_direct/config/reference_airspeed_knots",LiveField::reference_airspeed},
    // Pitch's firmness spring and trim range are gone (decision 15), and
    // roll's airflow and plain springs (decision 6); these are the drives'
    // calibration and pitch's drag.
    {"brunner_direct/config/roll_torque_units_per_nm",LiveField::roll_units_per_nm,true},
    {"brunner_direct/config/pitch_force_units_per_lb",LiveField::units_per_lb,true},
    {"brunner_direct/config/pitch_force_gain_divisor",LiveField::gain_divisor,true},
    {"brunner_direct/config/pitch_move_back_gain",LiveField::move_back,true},
    {"brunner_direct/config/auto_start",LiveField::auto_start},
    {"brunner_direct/state/flight_active",LiveField::flight_active},
    {"brunner_direct/state/flight_suspended",LiveField::flight_suspended},
    {"brunner_direct/state/loading_fault",LiveField::loading_fault},
    // 1 while the pitch solver holds the constant force at its bound.
    {"brunner_direct/state/bias_limited",LiveField::bias_limited},
    {"brunner_direct/state/airspeed_knots",LiveField::airspeed_knots},
    {"brunner_direct/state/flight_trim_target",LiveField::flight_trim_target},
    {"brunner_direct/state/owns_pitch_trim",LiveField::owns_trim},
    {"brunner_direct/lab/trim_milli",LiveField::lab_trim,true},
    {"brunner_direct/lab/lease_ms",LiveField::lab_lease,true},
    {"brunner_direct/lab/active",LiveField::lab_active},
    {"brunner_direct/state/startup_notice",LiveField::startup_notice},
    {"brunner_direct/state/notice_draws",LiveField::notice_draws},
    {"brunner_direct/config/force_feel",LiveField::force_feel,true},
    {"brunner_direct/config/show_status_line",LiveField::status_line,true},
    {"brunner_direct/state/retry_offered",LiveField::retry_offered},
    {"brunner_direct/state/bridge_present",LiveField::bridge_present},
    {"brunner_direct/state/stock_aircraft",LiveField::stock_aircraft}
};
int last_write_accepted=1,rejected_writes=0;
void log(const std::string& s);
brunner::TuningField tuning_field(LiveField field) {
    switch (field) {
    case LiveField::roll_target:return brunner::TuningField::roll_percent;
    case LiveField::pitch_target:return brunner::TuningField::pitch_percent;
    case LiveField::trim_target:return brunner::TuningField::pitch_trim;
    default:throw std::invalid_argument("Read-only calibration field");
    }
}
// Compatibility view of the status line for the headless runner: 0 hidden,
// 1 waiting, 2 centring (hands off), 3 needs the pilot.
brunner::StartupNotice notice_code() {
    if (!decision.show_notice) return brunner::StartupNotice::hidden;
    if (decision.retry_offered) return brunner::StartupNotice::unavailable;
    if (decision.status==brunner::centring_text) return brunner::StartupNotice::centering;
    return brunner::StartupNotice::waiting;
}
int read_live(void* refcon) {
    const auto field=static_cast<LiveDataref*>(refcon)->field;
    switch (field) {
    case LiveField::roll_target:case LiveField::pitch_target:case LiveField::trim_target:
        return brunner::tuning_value(reader.requested_tuning(),tuning_field(field));
    case LiveField::last_write_accepted:return last_write_accepted;
    case LiveField::rejected_writes:return rejected_writes;
    case LiveField::reference_airspeed:return flight_settings.reference_airspeed;
    case LiveField::roll_units_per_nm:return flight_settings.roll_torque_units_per_nm;
    // The version shown; units ramp toward it in flight.
    case LiveField::units_per_lb:return flight_settings.pitch_force_units_per_lb;
    case LiveField::gain_divisor:return flight_settings.pitch_force_gain_divisor;
    case LiveField::move_back:return flight_settings.pitch_move_back_gain;
    case LiveField::auto_start:return flight_settings.auto_start;
    case LiveField::startup_notice:return static_cast<int>(notice_code());
    case LiveField::notice_draws:return startup_notice.draws();
    case LiveField::airspeed_knots:return std::isfinite(flight_sample.airspeed)?static_cast<int>(std::lround(flight_sample.airspeed)):-1;
    case LiveField::owns_trim:return sim.owns_trim();
    case LiveField::lab_trim:return diagnostic_trim.target();
    case LiveField::lab_lease:case LiveField::lab_active:
        return diagnostic_trim.active(enabled && !reader.snapshot().connected,brunner::Clock::now());
    case LiveField::force_feel:return force_feel;
    case LiveField::status_line:return show_status_line;
    case LiveField::retry_offered:return decision.retry_offered;
    case LiveField::stock_aircraft:return sim.default_cessna();
    default:break;
    }
    const auto s=reader.snapshot();
    const auto age=s.connected?std::chrono::duration_cast<std::chrono::milliseconds>(brunner::Clock::now()-s.sampled).count():-1;
    const bool valid=s.connected && s.tuning_valid && age>=0 && age<=250;
    switch (field) {
    case LiveField::roll_applied:return valid?s.applied_tuning.roll_percent:-1;
    case LiveField::pitch_applied:return valid?s.applied_tuning.pitch_percent:-1;
    case LiveField::trim_applied:return valid?s.applied_tuning.pitch_trim:0;
    case LiveField::connected:return s.connected;
    case LiveField::bridge_present:return s.bridge_present;
    case LiveField::reference_active:return s.reference_active;
    case LiveField::referenced:return s.session_reference;
    case LiveField::loading_active:return s.loading_active;
    case LiveField::tuning_valid:return valid;
    case LiveField::remaining:return s.loading_seconds_remaining;
    case LiveField::sample_age:return static_cast<int>(std::clamp<std::int64_t>(age,-1,std::numeric_limits<int>::max()));
    case LiveField::roll_encoder:return s.axes[0].encoder;
    case LiveField::pitch_encoder:return s.axes[1].encoder;
    case LiveField::roll_status:return s.axes[0].status;
    case LiveField::pitch_status:return s.axes[1].status;
    case LiveField::flight_active:return s.flight_session && s.loading_active && valid;
    // The pitch solver's last update in a force-feel session.
    case LiveField::bias_limited:return s.pitch_solved && s.pitch_solution.bias_limited;
    case LiveField::flight_trim_target:return s.pitch_solved?s.pitch_solution.centre:0;
    case LiveField::flight_suspended:return s.flight_suspended;
    case LiveField::loading_fault:return s.loading_fault;
    default:return 0;
    }
}
void set_force_feel(bool on,const char* source) {
    if (on && reader.snapshot().read_only) {log(std::string("Force feel refused (")+source+"): this firmware is read-only");return;}
    if (force_feel==on) return;
    force_feel=on;log(std::string("Force feel switched ")+(on?"on":"off")+" from "+source);
    if (on) yoke.force_feel_switched_on();
}
void sync_feel(brunner::Clock::time_point now);
// A feel dataref is an edit of the workbench's Trying, in its page units; the
// workbench checks its bounds.
bool edit_feel(LiveField field,int value,const char* name) {
    if (!bench) return false;
    const auto edit=[&](wb::Settings& s) {
        switch (field) {
        case LiveField::roll_units_per_nm:s.roll.units_per_nm=value;return true;
        case LiveField::units_per_lb:s.pitch.units_per_lb=value;return true;
        case LiveField::gain_divisor:s.pitch.drag_divisor=value;return true;
        case LiveField::move_back:s.pitch.move_back=value;return true;
        default:return false;
        }
    };
    const auto now=brunner::Clock::now();
    if (!bench->edit_live(now,edit,std::string(name)+"="+std::to_string(value))) return false;
    sync_feel(now);
    return true;
}
void write_live(void* refcon,int value) {
    const auto& entry=*static_cast<LiveDataref*>(refcon);
    last_write_accepted=0;
    if (entry.writable) {
        switch (entry.field) {
        case LiveField::lab_trim:
            last_write_accepted=diagnostic_trim.set_target(value);break;
        case LiveField::lab_lease:
            last_write_accepted=diagnostic_trim.renew(value,enabled && !reader.snapshot().connected,brunner::Clock::now());
            // A developer lease holds the yoke until the pilot's one action, as before.
            if (last_write_accepted && value) yoke.stop();
            break;
        case LiveField::force_feel:
            if (value==0 || value==1) {set_force_feel(value!=0,"dataref");last_write_accepted=1;}
            break;
        case LiveField::status_line:
            if (value==0 || value==1) {show_status_line=value!=0;last_write_accepted=1;}
            break;
        case LiveField::roll_units_per_nm:
        case LiveField::units_per_lb:case LiveField::gain_divisor:case LiveField::move_back:
            last_write_accepted=edit_feel(entry.field,value,entry.name);
            break;
        // The spring percents are the developer light spring test's
        // commissioning channel only (20-100 %): a force-feel session's
        // strengths are its solvers', up to the drive's own ceiling (decision 16).
        default:
            if (entry.field!=LiveField::trim_target || !reader.snapshot().flight_session)
                last_write_accepted=reader.request_tuning(tuning_field(entry.field),value);
            break;
        }
    }
    if (!last_write_accepted && rejected_writes<std::numeric_limits<int>::max()) ++rejected_writes;
    if (entry.field!=LiveField::lab_lease || !last_write_accepted)
        log(std::string(last_write_accepted?"Calibration requested: ":"Calibration rejected: ")+entry.name+"="+std::to_string(value));
}
// Why force feel cannot run on this aircraft, or empty.
constexpr char no_profile_text[]="Ready - force feel needs the aircraft profiles; see X-Plane's Log.txt";
constexpr char trim_taken_text[]="Ready - force feel off: another plugin holds the pitch trim";
void log(const std::string& s) {
    recorder.event(s); XPLMDebugString(("[BrunnerDirect] "+s+"\n").c_str()); }
void act(brunner::YokeAction action) {
    switch (action) {
    case brunner::YokeAction::none:break;
    case brunner::YokeAction::start_reader:
        sim.release_axes();reader.start();log("Looking for the yoke again");break;
    case brunner::YokeAction::reference:
        if (reader.request_reference_test()) log("Centring the yoke; motors stop after centring");break;
    case brunner::YokeAction::recover:
        reader.request_loading_recovery();
        if (reader.request_reference_test()) log("Recovering from a controller fault: resting settings with motors off, then centring");
        break;
    case brunner::YokeAction::start_forces:
        if (reader.request_flight()) log("Starting force feel: pitch follows the elevator model and roll the aileron model ("
            +(bench?bench->profile_name().value_or("no profile"):std::string("no profile"))
            +" profile), roll's air "+(flown.matched && flown.aileron_sections?"from the aileron panels":"from airspeed"));
        break;
    case brunner::YokeAction::stop_forces:
        if (reader.request_loading_end()) log("Stopping force feel; the yoke keeps supplying pitch and roll");break;
    }
}
void refresh_menu(const brunner::Snapshot& s) {
    brunner::ui::MenuModel model;
    model.status=decision.status;model.retry_offered=decision.retry_offered;
    model.force_feel=force_feel;model.status_line=show_status_line;model.pinned=force_hud.pinned();model.unverified=allow_unverified_yoke;
    std::string* lines[2]={&model.roll_line,&model.pitch_line};
    for (unsigned i=0;i<2;++i) {
        char text[160];
        if (s.connected) std::snprintf(text,sizeof(text),"%s: encoder %ld, control %ld, status %04X",
            i==0?"Roll":"Pitch",static_cast<long>(s.axes[i].encoder),static_cast<long>(s.axes[i].position),s.axes[i].status);
        else std::snprintf(text,sizeof(text),"%s: unavailable",i==0?"Roll":"Pitch");
        *lines[i]=text;
    }
    yoke_menu.refresh(model);
}
int stop_motors_handler(XPLMCommandRef,XPLMCommandPhase,void*);
int force_feel_handler(XPLMCommandRef,XPLMCommandPhase,void*);
void retry_workbench(brunner::Clock::time_point now);
int recenter_handler(XPLMCommandRef,XPLMCommandPhase,void*);
// What the workbench says to fly: Trying or Kept, pitch's knobs as they ramp,
// and the profile's figures with any overrides.
void sync_feel(brunner::Clock::time_point now) {
    if (!bench) return;
    flown=bench->frame(now,bench_inputs);
    wb::apply(flown.settings,flight_settings);
}
// The aircraft's profile to the workbench: on a new flight, or a new file.
void apply_aircraft_profile(brunner::Clock::time_point now,bool new_flight) {
    if (!bench || !profile_set) return;
    try {bench->set_profile(now,brunner::profile_for(*profile_set,sim.aircraft_path()),new_flight,sim.aircraft_name().empty()?sim.aircraft_file():sim.aircraft_name());}
    catch (const std::exception& e) {bench->profile_refused(e.what());}
}
// BrunnerDirect.profiles.json, re-read within 2 s of a change (decision 5)
// and on every new flight, which also matches the new aircraft. A file that
// does not parse is refused whole: the figures already flown stay.
void poll_profiles(brunner::Clock::time_point now,bool new_flight) {
    if (!bench || (!new_flight && profiles_checked!=brunner::Clock::time_point{} && now-profiles_checked<std::chrono::seconds(2))) return;
    profiles_checked=now;
    const auto path=plugin_folder/brunner::profiles_file;
    std::error_code ec;
    std::string problem;
    if (!std::filesystem::exists(path,ec)) {profiles_present=false;problem=std::string(brunner::profiles_file)+" is missing";}
    else {
        const auto time=std::filesystem::last_write_time(path,ec);
        const auto size=ec?0:std::filesystem::file_size(path,ec);
        if (ec) problem="cannot read "+std::string(brunner::profiles_file);
        else if (new_flight || !profiles_present || time!=profiles_time || size!=profiles_size) {
            profiles_present=true;profiles_time=time;profiles_size=size;
            try {
                profile_set=brunner::parse_profiles(wb::read_bounded(path,256*1024));
                bench->profile_refused("");
                apply_aircraft_profile(now,new_flight);
                return;
            } catch (const std::exception& e) {problem=e.what();}
        } else return;
    }
    bench->profile_refused(problem);
    // The aircraft changed: the set read before still matches it.
    if (new_flight) apply_aircraft_profile(now,true);
}
// The page's buttons run the same handlers as X-Plane's menu, on this thread.
void run_feel_commands() {
    if (!bench) return;
    for (const auto command:bench->take_commands()) {
        log(std::string("Feel tuning page: ")+(command==wb::Command::stop_motors?"Stop yoke motors":command==wb::Command::force_feel_on?"Turn force feel on":"Retry"));
        switch (command) {
        case wb::Command::stop_motors:stop_motors_handler(nullptr,xplm_CommandBegin,nullptr);break;
        case wb::Command::force_feel_on:force_feel_handler(force_feel_on,xplm_CommandBegin,nullptr);break;
        case wb::Command::retry:recenter_handler(nullptr,xplm_CommandBegin,nullptr);break;
        }
    }
}
void log_feel_events() {
    if (bench) for (const auto& e:bench->take_events()) log(e);
}
// The pitch solver's output the page and the HUD both show: the session's
// own while force feel runs, else solved here for where the yoke is, where the
// flow sample is fresh and the aircraft has a profile. Display only.
std::optional<brunner::PitchSolution> shown_pitch(const brunner::Snapshot& s,const brunner::FlightInput& input) {
    if (s.loading_active && s.flight_session && s.pitch_solved) return s.pitch_solution;
    if (!bench_inputs.valid || !flown.has_profile) return std::nullopt;
    return brunner::solve_pitch(flown.figures,bench_inputs.inputs,
        input.ready?brunner::drive_counts_from_encoder(s.axes[1].encoder):std::numeric_limits<double>::quiet_NaN(),
        flown.knobs.units_per_lb);
}
// The roll solver's output the page shows, the same way: the session's own
// while force feel runs, else solved here on the flight's target for where the
// wheel is. Display only.
std::optional<brunner::RollSolution> shown_roll(const brunner::Snapshot& s,const brunner::FlightInput& input) {
    if (s.loading_active && s.flight_session && s.roll_solved) return s.roll_solution;
    if (!flight_output.roll) return std::nullopt;
    return brunner::solve_roll(*flight_output.roll,
        input.ready?brunner::roll_counts_from_encoder(s.axes[0].encoder):std::numeric_limits<double>::quiet_NaN());
}
// The HUD's pitch card: the tail drawn from the model the page draws.
std::optional<brunner::tail::View> pitch_card(const brunner::Snapshot& s,const brunner::FlightInput& input,
    const std::optional<brunner::PitchSolution>& solution) {
    if (!solution || !bench_inputs.valid || !flown.has_profile) return std::nullopt;
    brunner::tail::View v;
    v.figures=flown.figures;v.inputs=bench_inputs.inputs;v.solution=*solution;
    if (input.ready) v.yoke_counts=brunner::drive_counts_from_encoder(s.axes[1].encoder);
    v.force_feel=force_feel;v.session=s.loading_active && s.flight_session;v.blending=v.session && !s.pitch_engaged;
    return v;
}
// Ten times a second, what the page shows: the model's inputs where the
// flow sample is fresh, the solver's output (shown_pitch), and the stock
// C172's surface details on that aircraft only; null otherwise.
void publish_feel(const brunner::Snapshot& s,const brunner::FlightInput& input,const std::optional<brunner::PitchSolution>& solution,
    bool lab_active,bool fresh,brunner::Clock::time_point now) {
    if (!bench || now-last_publish<std::chrono::milliseconds(100)) return;
    last_publish=now;
    const bool flow_valid=bench_inputs.valid;
    wb::Live live;
    live.status=decision.status;
    wb::ViewInputs view;
    view.enabled=enabled;view.connected=s.connected;view.retry_offered=decision.retry_offered;view.lab_lease=lab_active;
    view.flight_loading=s.loading_active && s.flight_session;view.force_feel=force_feel;view.status=decision.status;
    live.view=wb::view_for(view);
    live.motors=wb::motors_for(decision.retry_offered,s.loading_active || s.reference_active);
    if (decision.retry_offered) {live.reason=s.stop_reason;live.retry_helps=s.retry_helps;}
    live.aircraft=sim.aircraft_name();live.aircraft_file=sim.aircraft_file();live.buffet_units=s.buffet_units;live.buffet_uncleared=s.buffet_uncleared;live.buffet_time_unrestored=s.buffet_time_unrestored;live.aileron_panels=flown.matched && flown.aileron_sections.has_value();
    const auto page=sim.read_page();
    live.ias_kt=page.ias_kt;live.bank_deg=page.bank_deg;live.vertical_fpm=page.vertical_fpm;
    if (!std::isnan(page.flaps_deg)) live.flaps_deg=page.flaps_deg;
    const auto age=now-s.sampled;
    if (s.connected && s.tuning_valid && age>=brunner::Clock::duration::zero() && age<=std::chrono::milliseconds(250)) {
        live.pitch.now_percent=s.applied_tuning.pitch_percent;live.roll.now_percent=s.applied_tuning.roll_percent;
    }
    if (input.ready) {live.pitch.yoke_percent=100.0*input.pitch;live.roll.yoke_percent=100.0*input.roll;}
    const bool session=s.loading_active && s.flight_session;
    if (flow_valid) {
        live.pitch.inputs=bench_inputs.inputs;live.pitch.tail_source=flight_tail.source;
        // X-Plane's share as read, beside the slewed one the model takes.
        if (flight_tail.source==brunner::TailSource::surface && flight_sample.tail) live.pitch.tail_stalled_raw=flight_sample.tail->stalled;
    }
    live.pitch.solution=solution;
    live.pitch.phase=!session?"off":!s.pitch_engaged?"blending":"following";
    if (session && s.update_hz>0) live.pitch.update_hz=s.update_hz;
    live.pitch.drag_waiting=session && session_drag
        && *session_drag!=std::pair<int,int>{flight_settings.pitch_force_gain_divisor,flight_settings.pitch_move_back_gain};
    // The aileron model's inputs and its solution at the wheel.
    if (flight_output.roll) {
        live.roll.inputs=flight_output.roll->inputs;live.roll.source=flight_output.aileron_source;
        if (flight_output.aileron_source==brunner::AileronSource::surface && flight_sample.ailerons)
            live.roll.stalled_raw={flight_sample.ailerons->left.stalled,flight_sample.ailerons->right.stalled};
    }
    live.roll.solution=shown_roll(s,input);
    live.roll.phase=!session?"off":!s.pitch_engaged?"blending":"following";
    if (sim.default_cessna() && fresh) {
        live.roll.airflow_pa=flight_sample.flow.roll;
        const auto c172=sim.read_c172();
        live.pitch.aoa_deg=c172.pitch_aoa_deg;live.roll.aoa_deg=c172.roll_aoa_deg;
        live.pitch.stalled=c172.pitch_stalled;live.roll.stalled=c172.roll_stalled;
        live.pitch.deflection_deg=c172.pitch_deflection_deg;live.roll.deflection_deg=c172.roll_deflection_deg;
    }
    const auto shape=brunner::pitch_setup_for(true,flight_settings.pitch_force_gain_divisor,flight_settings.pitch_move_back_gain).curve;
    for (std::size_t i=0;i<live.pitch_curve.size();++i) live.pitch_curve[i]=shape[i];
    bench->publish(now,live);
}
// The saved choice, read once at start. A missing file is normal (off); a file that is not
// exactly what is written is ignored and means off, and the log says so.
void load_owner_state() {
    allow_unverified_yoke=false;
    std::ifstream in(plugin_folder/brunner::state_file,std::ios::binary);
    if (in) {
        std::stringstream text;text<<in.rdbuf();
        const auto state=brunner::parse_owner_state(text.str());
        allow_unverified_yoke=state.allow_unverified_yoke;
        if (!allow_unverified_yoke && text.str()!=brunner::serialize_owner_state({}))
            log(std::string("Ignored ")+brunner::state_file+": not valid, so unverified yoke models stay off");
    }
    log(std::string("Allow unverified yoke models: ")+(allow_unverified_yoke?"ON":"off")+" (Developer menu)");
}
// Written atomically (temp file, then rename) into the plugin folder. A failed save is logged and the
// choice still holds for this run.
void save_owner_state() {
    const auto path=plugin_folder/brunner::state_file;
    const std::filesystem::path temp=path.string()+".tmp";
    {
        std::ofstream out(temp,std::ios::binary|std::ios::trunc);
        out<<brunner::serialize_owner_state({allow_unverified_yoke});
        if (!out) {log("Could not save "+path.string()+": the choice holds for this run only");return;}
    }
    std::error_code ec;
    std::filesystem::rename(temp,path,ec);
    if (ec) log("Could not save "+path.string()+": "+ec.message()+"; the choice holds for this run only");
}
// The Developer menu's toggle. It takes effect at once, through the safe path: force feel is switched
// off, the axes are released and the reader is stopped the existing way (motors disabled, rest settings)
// before it starts again with the new choice.
void unverified_toggle() {
    allow_unverified_yoke=!allow_unverified_yoke;
    log(allow_unverified_yoke
        ?"Allow unverified yoke models: ON. A model that is not verified will now be driven: its first writes are a no-change test write, then the normal flow (motors stopped, rest settings written, the yoke centred and moved)"
        :"Allow unverified yoke models: off. Force feel is switched off and a model that is not verified returns to read-only");
    save_owner_state();
    if (!enabled) return;
    set_force_feel(false,"the unverified-yoke toggle");
    sim.release_axes();
    reader.stop();
    reader.set_allow_unverified(allow_unverified_yoke);
    reader.start();
}
// Read once at start: the shipped yokes/*.json (one model each), then the user's BrunnerDirect.yokes.json
// patching them. A missing overrides file is normal; anything that does not validate is refused whole and
// logged (the built-in models stand in for a refused folder). Parsing is brunner::load_yoke_models.
std::string read_text_file(const std::filesystem::path& path) {
    std::ifstream in(path,std::ios::binary);
    std::stringstream text;text<<in.rdbuf();
    return text.str();
}
void load_yoke_presets() {
    std::vector<std::filesystem::path> files;
    std::error_code ec;
    for (std::filesystem::directory_iterator it(plugin_folder/"yokes",ec),end;!ec && it!=end;it.increment(ec))
        if (it->is_regular_file(ec) && it->path().extension()==".json") files.push_back(it->path());
    std::sort(files.begin(),files.end());
    std::vector<std::string> shipped;
    for (const auto& f:files) shipped.push_back(read_text_file(f));
    std::optional<std::string> overrides;
    const auto overrides_path=plugin_folder/brunner::yokes_file;
    if (std::filesystem::exists(overrides_path,ec)) overrides=read_text_file(overrides_path);
    const auto loaded=brunner::load_yoke_models(shipped,overrides);
    yoke_presets=loaded.presets;
    for (const auto& note:loaded.notes) log(note);
    log(loaded.from_files?"Yoke models loaded from "+(plugin_folder/"yokes").string():std::string("Yoke models: built-in fallback in force"));
    if (loaded.overrides_applied) log("Yoke overrides applied from "+overrides_path.string());
    std::string names;
    for (const auto& m:yoke_presets) names+=(names.empty()?"":", ")+m.name+(m.verified?"":" (unverified)");
    log("Yoke models: "+names);
    reader.set_presets(yoke_presets);
    load_owner_state();
    reader.set_allow_unverified(allow_unverified_yoke);
}
std::filesystem::path travel_file() {return output_folder/"read-only-travel.json";}
// Written atomically (temp file, then rename), at most every 5 s and only when the range widened.
void save_travel(brunner::Clock::time_point now,bool force) {
    if (!travel_dirty || output_folder.empty()) return;
    if (!force && now-travel_saved<std::chrono::seconds(5)) return;
    const auto path=travel_file();
    const std::filesystem::path temp=path.string()+".tmp";
    {
        std::ofstream out(temp,std::ios::binary|std::ios::trunc);
        out<<brunner::serialize_travel(travel_id,read_only_travel);
        if (!out) {log("Could not save the read-only travel range");travel_saved=now;return;}
    }
    std::error_code ec;
    std::filesystem::rename(temp,path,ec);
    if (ec) {log("Could not save the read-only travel range: "+ec.message());travel_saved=now;return;}
    travel_dirty=false;travel_saved=now;
}
void load_travel(const brunner::UsbId& id) {
    travel_id=id;read_only_travel={};
    if (output_folder.empty()) return;
    std::ifstream in(travel_file(),std::ios::binary);
    if (!in) return;
    std::stringstream text;text<<in.rdbuf();
    if (const auto saved=brunner::parse_travel(text.str(),id)) {read_only_travel=*saved;log("Read-only travel range restored from "+travel_file().string());}
    else log("Ignored read-only-travel.json: not for this yoke model (USB id), or not valid");
}
// Position input: the active drive's own once centred, else, in read-only
// mode, the learned travel. The learners watch every sample; the file follows them.
brunner::FlightInput current_input(const brunner::Snapshot& s,brunner::Clock::time_point now) {
    if (!s.read_only) return brunner::stopped_flight_input(s,calibration(),now);
    const brunner::UsbId id{s.usb_vendor,s.usb_product};
    if (id.vendor!=travel_id.vendor || id.product!=travel_id.product) {save_travel(now,true);load_travel(id);}
    const auto result=brunner::read_only_flight_input(s,read_only_travel,
        std::array<bool,2>{{flight_settings.read_only_roll_reverse!=0,flight_settings.read_only_pitch_reverse!=0}},now);
    if (result.widened) travel_dirty=true;
    save_travel(now,false);
    return result.input;
}
// A survey (read-only SDO uploads) arrives in the snapshot; keep it in a file.
void write_survey(const brunner::Snapshot& s) {
    if (s.surveys<surveys_written) surveys_written=s.surveys;
    if (s.surveys==surveys_written || s.survey.empty() || output_folder.empty()) return;
    surveys_written=s.surveys;
    char stamp[32]{};
    const std::time_t t=std::time(nullptr);
    std::strftime(stamp,sizeof(stamp),"%Y%m%d-%H%M%S",std::localtime(&t));
    const auto path=output_folder/(std::string("survey-")+stamp+".json");
    std::ofstream out(path,std::ios::binary|std::ios::trunc);
    out<<s.survey<<'\n';
    log(out?"Survey written: "+path.string():"Could not write the survey to "+path.string());
}
float update(float,float,int,void*) {
    sim.bind_controls();
    run_feel_commands();
    const auto now=brunner::Clock::now();
    retry_workbench(now);
    poll_profiles(now,false);
    const auto s=reader.snapshot();
    if (!s.loading_result.empty() && s.loading_result!=last_loading) {log(s.loading_result);last_loading=s.loading_result;}
    if (!s.loading_inspection.empty() && s.loading_inspection!=last_inspection) {log("Loading inspection: "+s.loading_inspection);last_inspection=s.loading_inspection;}
    const auto input=current_input(s,now);
    write_survey(s);
    if (s.model!=last_yoke_model) {last_yoke_model=s.model;if (!s.model.empty()) log("Yoke model: "+s.model_title+" (USB "+brunner::status_hex(s.usb_vendor)+":"+brunner::status_hex(s.usb_product)+")");}
    if (s.yoke_note!=last_yoke_note) {last_yoke_note=s.yoke_note;if (!s.yoke_note.empty()) log("Yoke firmware note: "+s.yoke_note);}
    last_input_status=sim.apply_input(input);
    const bool is_paused=sim.paused();
    const bool input_running=sim.pause_known() && !is_paused && sim.controls_available();
    // The model's inputs: trim, the tail's pressure (X-Plane's over the
    // profile's own sections, else airspeed's), the load factor, and the
    // tail's angle and stall with its sections' pressure.
    const bool fresh=brunner::valid_frame_age(sim.flow_stamp(),now,is_paused);
    // X-Plane's stalled shares, slewed once a frame before either model takes
    // them; the recorder and the page's raw readings keep flight_sample's.
    const auto modelled=stall_slews.slewed(flight_sample);
    flight_tail=brunner::tail_air(modelled.flow.tail,modelled.airspeed);
    bench_inputs.valid=fresh && flight_tail.pa>=0;bench_inputs.force_feel=force_feel;bench_inputs.connected=s.connected;
    bench_inputs.inputs=brunner::elevator_inputs(modelled,flight_tail);
    sync_feel(now);
    // What the next force-feel session writes on pitch, motors off: the drag
    // gains, the straight-line curve and the constant force kept on.
    reader.set_pitch_setup(brunner::pitch_setup_for(true,flight_settings.pitch_force_gain_divisor,flight_settings.pitch_move_back_gain));
    // Buffet (decision 19): the page's knob, which the feel settings seed (off
    // unless the owner turned it on there), and each surface's stalled share.
    const wb::BuffetView configured{flight_settings.buffet==1,flight_settings.buffet_percent,flight_settings.buffet_period};
    if (bench && bench.get()!=buffet_seeded_for) {bench->set_buffet(configured);buffet_seeded_for=bench.get();}
    const auto view=bench?bench->buffet():configured;
    brunner::BuffetTarget buffet{{view.on,view.percent,view.period},0,0};
    if (fresh && flight_sample.ailerons) buffet.roll=brunner::buffet_units(buffet.knob,brunner::buffet_share(flight_sample.ailerons->left,flight_sample.ailerons->right));
    if (fresh && root_stall) buffet.pitch=brunner::buffet_units(buffet.knob,brunner::buffet_share((*root_stall)[0],(*root_stall)[1]));
    reader.set_buffet(buffet);
    if (s.loading_active && s.flight_session) {
        if (!session_drag) session_drag=std::pair<int,int>{flight_settings.pitch_force_gain_divisor,flight_settings.pitch_move_back_gain};
    } else session_drag.reset();
    // Force feel coming on or going off is not the flight: say what did it
    // (owner, 2026-11-08). The end of a session clears the constant force;
    // the notice says cleared only when its read-back confirmed it.
    const bool in_session=s.loading_active && s.flight_session;
    // "see X-Plane's Log.txt" must lead somewhere: log a refusal's reason once per change.
    static std::string logged_refusal;
    if (s.loading_fault && !s.message.empty() && s.message!=logged_refusal) {logged_refusal=s.message;log("Yoke refused: "+s.message);}
    else if (s.connected) logged_refusal.clear();
    const auto said=session_notices.update(now,in_session,s.loading_result.empty()?s.message:s.loading_result,s.pitch_force_cleared);
    if (bench && said) bench->notify(now,"force_feel",*said);
    corner_notice=bench?bench->latest_notice(now,corner_notice_time):std::nullopt;
    // Every aircraft: pitch follows the elevator model and roll the aileron
    // model with its profile's figures (decisions 6 and 15). No profile, no
    // target: force feel waits.
    const auto target=fresh && flown.has_profile
        ? brunner::flight_target(modelled,flight_settings,reader.requested_tuning(),flown.figures,flown.knobs,flown.ailerons,flown.roll_knobs)
        : std::nullopt;
    flight_output=target.value_or(brunner::FlightTarget{});
    const char* blocked=!flown.has_profile?no_profile_text:!sim.trim_available()?trim_taken_text:nullptr;
    const bool running=input_running && target.has_value() && !blocked;
    reader.simulator_update(running,is_paused,flight_output.tuning,flight_output.pitch,flight_output.roll);
    simulator_ready_for_start=input_running;
    // Where force feel cannot run, the yoke still centres and flies as with
    // force feel off, and the line says why.
    const bool settled=(blocked?input_running:running) && startup_readiness.ready(now);
    const bool lab_active=diagnostic_trim.active(enabled && !s.connected,now);
    brunner::YokeInputs inputs;
    inputs.snapshot=s;inputs.enabled=enabled;inputs.reader_running=reader.running();inputs.sim_steady=settled;
    inputs.paused=is_paused;inputs.input_ready=sim.owns_axes() && input.ready;inputs.force_feel=force_feel && !blocked;inputs.lab_lease=lab_active;inputs.now=now;
    decision=yoke.step(inputs);
    // Read-only mode: until both axes have moved far enough, the line says how far they are.
    if (s.read_only && !input.ready) {decision.status=input.reason;decision.show_notice=true;}
    if (force_feel && blocked && decision.status=="Ready - force feel off") {decision.status=blocked;decision.show_notice=true;}
    corner_reason=decision.retry_offered?s.stop_reason_short:std::string();
    act(decision.action);
    // The physical encoder includes the trim-induced displacement. Suppress
    // only X-Plane's synthetic pitch trim, leaving its trim command and every
    // other control binding intact. Own/release this flag with the live axes,
    // on every aircraft: each one's rest carries its trim.
    if (running && sim.owns_axes() && input.ready && s.flight_session && s.loading_active && s.tuning_valid) sim.claim_trim();
    else sim.release_trim();
    // The pilot may hide routine status; a movement warning or a Retry offer is always shown.
    const bool must_show=decision.status==brunner::centring_text || decision.retry_offered;
    // A feel notice shows for a few seconds when the yoke's own status line
    // has nothing to say: it never takes the place of a link lost or a fault.
    startup_notice.set_visible((decision.show_notice && (show_status_line || must_show)) || corner_notice);
    if (decision.status!=last_status) {log(decision.status);last_status=decision.status;}
    refresh_menu(s);
    if (reset_pending && !s.loading_active && !s.reference_active) {
        reset_pending=false;
        if (!reader.request_reset()) log("Reset refused: the yoke is read-only or busy (a model that is not verified needs Developer > Allow unverified yoke models)");
    }
    const auto solution=shown_pitch(s,input);
    publish_feel(s,input,solution,lab_active,fresh,now);
    force_hud.observe_pitch(pitch_card(s,input,solution));
    log_feel_events();
    force_monitor.set_status(s.yoke_note.empty()?decision.status:decision.status+"  |  "+s.yoke_note);force_hud.set_status(decision.status,corner_reason);
    // Log live evidence at one-second intervals without a companion recorder.
    static auto last_log=brunner::Clock::time_point{};
    if (s.connected && now-last_log>=std::chrono::seconds(1)) {
        char text[220];
        std::snprintf(text,sizeof(text),"cycle=%llu roll=%ld/%ld/%04X pitch=%ld/%ld/%04X input=%s %.4f/%.4f",
            static_cast<unsigned long long>(s.cycles),static_cast<long>(s.axes[0].encoder),static_cast<long>(s.axes[0].position),s.axes[0].status,
            static_cast<long>(s.axes[1].encoder),static_cast<long>(s.axes[1].position),s.axes[1].status,
            sim.owns_axes()?"active":"unavailable",input.roll,input.pitch);
        log(text); last_log=now;
    }
    // Apply after physical joystick processing, on the simulator thread. A
    // stopped/disconnected device and renewable short lease are mandatory.
    if (lab_active) sim.set_sim_trim(static_cast<float>(diagnostic_trim.target())/1000.0f);
    auto frame=brunner::monitor::make_frame(s,reader.requested_tuning(),flight_output,flight_settings,
        flight_sample,target.has_value(),calibration(),now);
    frame.owns_axes=sim.owns_axes();frame.owns_trim=sim.owns_trim();
    recorder.observe(frame);
    const auto recording_state=recorder.state();const auto recording_dropped=recorder.dropped();
    force_monitor.observe(frame,recording_state,recording_dropped);
    force_hud.observe(frame,recording_state,recording_dropped);
    static bool recorder_error_reported=false;
    if (recorder.state()==brunner::recording::State::failed) {
        if (!recorder_error_reported) {log(std::string("Session recorder failed: ")+recorder.error());recorder_error_reported=true;}
    } else recorder_error_reported=false;
    return -1.0f; // Update the input before every flight-model cycle.
}
// This post-flight-model callback only reads simulation results and records
// readiness. Resource creation and motor requests remain outside this phase.
float observe_completed_frame(float,float,int,void*) {
    const auto now=brunner::Clock::now();
    brunner::xplane::FrameSections sections;
    sections.default_cessna=sim.default_cessna();sections.matched=flown.matched;
    sections.tail=flown.tail_sections;sections.aileron=flown.aileron_sections;sections.wing_root=flown.wing_root_sections;
    if (const auto frame=sim.observe_frame(enabled,now,sections)) {
        flight_sample=frame->sample;root_stall=frame->root_stall;
    }
    const auto ready=sim.readiness();
    if (!enabled || !ready) startup_readiness.reset();
    else startup_readiness.observe(simulator_ready_for_start,ready->scenery_busy,ready->flight_time,brunner::Clock::now());
    return -1.0f;
}
int recenter_handler(XPLMCommandRef,XPLMCommandPhase,void*);
// What the corner window shows: the yoke's status and a hint, or the feel page's
// newest notice when the status line has nothing to say (ui/view/startup_notice.hpp).
brunner::ui::NoticeFrame notice_frame(int inner_width,const brunner::ui::TextWidth& measure) {
    const bool must_show=decision.show_notice && (decision.status==brunner::centring_text || decision.retry_offered);
    const bool status_shown=decision.show_notice && (show_status_line || must_show);
    brunner::ui::NoticeFrame frame;
    frame.visible=status_shown || corner_notice.has_value();
    if (!frame.visible) return frame;
    if (!status_shown) {
        // The workbench's notice, as the page shows it, in the window's two
        // lines, fitted by the font's own widths.
        frame.lines=wb::corner_lines(corner_notice->text,inner_width,measure);
        return frame;
    }
    // A stop's short reason replaces the bare retry hint: what failed, where
    // the pilot is looking. The page has the whole sentence.
    const std::string detail=decision.retry_offered?(corner_reason.empty()?"Click here to retry":corner_reason):
        decision.status==brunner::centring_text?"Keep your hands off the yoke until it settles":"Pitch and roll return automatically";
    frame.lines={decision.status,detail};
    return frame;
}
void create_startup_notice() {
    startup_notice.create({notice_frame,
        [] {if (decision.retry_offered) recenter_handler(nullptr,xplm_CommandBegin,nullptr);},
        [](const std::string& line) {log(line);}});
}
// Developer tests: the supervisor stands back while they run and resumes
// automatic operation when they end on their own.
int reference_handler(XPLMCommandRef,XPLMCommandPhase phase,void*) {
    if (phase==xplm_CommandBegin && enabled) {
        sim.release_axes();
        const bool accepted=reader.request_reference_test();
        if (accepted) yoke.developer_test();
        log(accepted?"Supervised reference test requested":"Reference request refused: reader unavailable or test active");
    }
    return 1;
}
// Developer stop: motors off, reader disconnected, nothing automatic until the
// pilot's Retry. The headless runner relies on this hard stop.
int stop_handler(XPLMCommandRef,XPLMCommandPhase phase,void*) {
    if (phase==xplm_CommandBegin) {yoke.stop();sim.release_axes();reader.stop();log(reader.snapshot().message);}
    return 1;
}
// Pilot's stop: force feel off and any motor session ended cleanly, so pitch
// and roll keep working with the motors disabled. Only a centring in progress
// has no clean end; that one is cut by the hard stop above.
int stop_motors_handler(XPLMCommandRef,XPLMCommandPhase phase,void*) {
    if (phase!=xplm_CommandBegin) return 1;
    set_force_feel(false,"Stop yoke motors");
    const auto s=reader.snapshot();
    if (s.reference_active) {stop_handler(nullptr,xplm_CommandBegin,nullptr);return 1;}
    if (s.loading_active) {
        if (reader.request_loading_end()) log("Stopping yoke motors; the yoke keeps supplying pitch and roll");
        else stop_handler(nullptr,xplm_CommandBegin,nullptr);
    } else log("Yoke motors already stopped");
    return 1;
}
int inspect_handler(XPLMCommandRef,XPLMCommandPhase phase,void*) {
    if (phase==xplm_CommandBegin && enabled) reader.request_loading_inspection();
    return 1;
}
int loading_handler(XPLMCommandRef,XPLMCommandPhase phase,void*) {
    if (phase==xplm_CommandBegin && enabled) {
        const bool accepted=reader.request_loading_test();
        if (accepted) yoke.developer_test();
        log(accepted?"Supervised light spring test requested":"Loading request refused: reader unavailable or test active");
    }
    return 1;
}
int recovery_handler(XPLMCommandRef,XPLMCommandPhase phase,void*) {
    if (phase==xplm_CommandBegin && enabled) reader.request_loading_recovery();
    return 1;
}
int gap_handler(XPLMCommandRef,XPLMCommandPhase phase,void*) {
    if (phase==xplm_CommandBegin && enabled)
        log(reader.request_communication_gap()?"Ten-second USB request gap queued":"Communication-gap request refused: no light spring test active");
    return 1;
}
// Reconnect restarts the reader and leaves any hold in place: after a Stop the
// yoke stays read-only, so a headless or bench session can verify controller
// state without motion; otherwise automatic operation resumes.
int reconnect_handler(XPLMCommandRef,XPLMCommandPhase phase,void*) {
    if (phase==xplm_CommandBegin && enabled) {
        const auto s=reader.snapshot();
        if (s.reference_active || s.loading_active) log("Reconnect refused during active test; use Stop first");
        else {sim.release_axes();reader.start();log("Reader reconnect requested; automatic operation resumes unless the yoke is stopped");}
    }
    return 1;
}
int trim_handler(XPLMCommandRef,XPLMCommandPhase phase,void* refcon) {
    if (phase==xplm_CommandBegin && enabled) {
        const auto counts=*static_cast<const std::int32_t*>(refcon);
        log(reader.request_trim_step(counts)?"Pitch trim test requested: "+std::to_string(counts):"Trim request refused: no light spring test active");
    }
    return 1;
}
// The pilot's one action: clear any hold, restart the reader if needed, centre again.
int recenter_handler(XPLMCommandRef,XPLMCommandPhase phase,void*) {
    if (phase==xplm_CommandBegin && enabled) {
        const auto s=reader.snapshot();
        if (s.read_only) {log("Re-centre refused: this firmware is read-only; nothing is ever written to the yoke");return 1;}
        if (s.reference_active || (s.loading_active && !s.flight_session)) {log("Re-centre refused: a yoke session is already active");return 1;}
        // Pitch and roll stay live: force feel is ended cleanly first, then the
        // yoke centres once the supervisor sees it still.
        yoke.retry();
        if (!reader.running()) reader.start();
        log("Re-centre requested; the yoke centres once X-Plane is steady and the yoke is still");
    }
    return 1;
}
// Legacy Start flight: force feel on and automatic operation resumed.
int flight_handler(XPLMCommandRef,XPLMCommandPhase phase,void*) {
    if (phase==xplm_CommandBegin && enabled) {
        set_force_feel(true,"Start flight");
        if (yoke.hold()!=brunner::YokeSupervisor::Hold::none) recenter_handler(nullptr,xplm_CommandBegin,nullptr);
    }
    return 1;
}
int force_feel_handler(XPLMCommandRef command,XPLMCommandPhase phase,void*) {
    if (phase==xplm_CommandBegin && enabled)
        set_force_feel(command==force_feel_on?true:command==force_feel_off?false:!force_feel,"menu or command");
    return 1;
}
int status_line_handler(XPLMCommandRef,XPLMCommandPhase phase,void*) {
    if (phase==xplm_CommandBegin && enabled) {show_status_line=!show_status_line;log(show_status_line?"Yoke status line shown":"Yoke status line hidden");}
    return 1;
}
int monitor_handler(XPLMCommandRef,XPLMCommandPhase phase,void*) {
    if (phase==xplm_CommandBegin && enabled) {
        try {force_monitor.toggle();} catch (const std::exception& e) {log(e.what());}
    }
    return 1;
}
int hud_handler(XPLMCommandRef command,XPLMCommandPhase phase,void*) {
    if (phase==xplm_CommandBegin && enabled) {
        // Keeping the pitch card on screen shows the HUD if it was hidden.
        if (command==pin_command) {force_hud.pin();if (force_hud.pinned()) force_hud.show_hud();}
        else if (command==move_command) force_hud.move();
        else force_hud.toggle();
        recorder.event(command==pin_command?(force_hud.pinned()?"Pitch feel kept on screen":"Pitch feel shown only when useful"):command==move_command?"HUD move toggled":"HUD visibility toggled");
    }
    return 1;
}
// brunner_direct/mark_feel and compare_feel, for a yoke button.
int feel_handler(XPLMCommandRef command,XPLMCommandPhase phase,void*) {
    if (phase==xplm_CommandBegin && enabled && bench) {
        const auto now=brunner::Clock::now();
        if (command==mark_feel_command) bench->mark(now);
        else {bench->compare(now);sync_feel(now);}
        log_feel_events();
    }
    return 1;
}
void start_workbench() {
    feel_menu_text="Feel tuning: off";
    if (bench && workbench_port) {
        wb::ServerOptions options;options.port=workbench_port;options.page=[]{return wb::embedded_page();};
        std::string error;
        if (workbench_server.start(*bench,options,error)) {
            const auto& addresses=workbench_server.addresses();
            const auto where=(addresses.empty()?std::string("localhost"):addresses.front())+":"+std::to_string(workbench_server.port());
            feel_menu_text="Feel tuning: "+where;
            log("Feel tuning page listening at http://"+where+"/");
            workbench_failure_logged=false;
        } else {
            feel_menu_text=error.find("in use")!=std::string::npos?"Feel tuning unavailable: port "+std::to_string(workbench_port)+" in use":"Feel tuning unavailable";
            if (!workbench_failure_logged) log("Feel tuning unavailable: "+error+"; trying again every 5 s");
            workbench_failure_logged=true;
            workbench_retry_at=brunner::Clock::now()+std::chrono::seconds(5);
        }
    }
    yoke_menu.set_feel_text(feel_menu_text);
}
void retry_workbench(brunner::Clock::time_point now) {
    if (bench && workbench_port && !workbench_server.running() && workbench_retry_at!=brunner::Clock::time_point{} && now>=workbench_retry_at) start_workbench();
}
void stop_workbench() {
    workbench_retry_at={};workbench_failure_logged=false;
    if (workbench_server.running()) {workbench_server.stop();log("Feel tuning page stopped");}
    // Nothing asked before disable runs after the next enable.
    if (bench) (void)bench->take_commands();
    log_feel_events();
    feel_menu_text="Feel tuning: off";
    yoke_menu.set_feel_text(feel_menu_text);
}
// What a click asks for (ui/menu/menu.hpp); the menu catches anything thrown and logs it.
void menu_action(brunner::ui::MenuItem item) {
    using brunner::ui::MenuItem;
        switch (item) {
        case MenuItem::hud:hud_handler(hud_command,xplm_CommandBegin,nullptr);break;
        case MenuItem::pin:hud_handler(pin_command,xplm_CommandBegin,nullptr);break;
        case MenuItem::move:hud_handler(move_command,xplm_CommandBegin,nullptr);break;
        case MenuItem::monitor:monitor_handler(nullptr,xplm_CommandBegin,nullptr);break;
        case MenuItem::reconnect:reconnect_handler(nullptr,xplm_CommandBegin,nullptr);break;
        case MenuItem::reference:reference_handler(nullptr,xplm_CommandBegin,nullptr);break;
        case MenuItem::stop:stop_handler(nullptr,xplm_CommandBegin,nullptr);break;
        case MenuItem::stop_motors:stop_motors_handler(nullptr,xplm_CommandBegin,nullptr);break;
        case MenuItem::loading:loading_handler(nullptr,xplm_CommandBegin,nullptr);break;
        case MenuItem::flight:flight_handler(nullptr,xplm_CommandBegin,nullptr);break;
        case MenuItem::recenter:recenter_handler(nullptr,xplm_CommandBegin,nullptr);break;
        case MenuItem::force_feel:force_feel_handler(force_feel_toggle,xplm_CommandBegin,nullptr);break;
        case MenuItem::status_line:status_line_handler(nullptr,xplm_CommandBegin,nullptr);break;
        case MenuItem::unverified:unverified_toggle();break;
        case MenuItem::reset_yoke:
            // Forces off and the supervisor held (Re-centre resumes), then the reader resets once nothing is running.
            set_force_feel(false,"Reset yoke");yoke.stop();
            if (reader.snapshot().loading_active) reader.request_loading_end();
            reset_pending=true;log("Resetting the yoke: both motors off and the model's resting settings written; Re-centre afterwards");
            break;
        case MenuItem::survey:log(reader.request_survey()?"Survey requested: reading registers only; the file appears in Output/BrunnerDirect":"Survey refused: it is only for a yoke that is read-only");break;
        default:break;
        }
}
}
PLUGIN_API int XPluginStart(char* name,char* signature,char* description) {
    std::snprintf(name,256,"Yoke (Brunner)");
    std::snprintf(signature,256,"one.abaci.brunner-direct");
    std::snprintf(description,256,"Brunner CLS-E yoke: pitch and roll for every aircraft, optional force feel");
#if APL
    // Without this, X-Plane on macOS hands out classic paths ("Volume:Folder:"), which the
    // filesystem calls below would take for a relative name. Must precede every path call.
    XPLMEnableFeature("XPLM_USE_NATIVE_PATHS",1);
#endif
    try {
        char plugin_path[1024]{};
        XPLMGetPluginInfo(XPLMGetMyID(),nullptr,plugin_path,nullptr,nullptr);
        const auto defaults=std::filesystem::path(plugin_path).parent_path().parent_path()/"BrunnerDirect.ini";
        if (std::filesystem::exists(defaults)) {
            std::ifstream file(defaults,std::ios::binary);
            if (!file) throw std::runtime_error("Cannot read BrunnerDirect.ini");
            const auto config=brunner::parse_configuration(file);
            reader.request_tuning(brunner::TuningField::roll_percent,config.tuning.roll_percent);
            reader.request_tuning(brunner::TuningField::pitch_percent,config.tuning.pitch_percent);
            reader.request_tuning(brunner::TuningField::pitch_trim,config.tuning.pitch_trim);
            flight_settings=config.flight;workbench_port=config.workbench_port;
            log("Loaded managed yoke defaults from BrunnerDirect.ini");
            for (const auto& key:config.retired) log("BrunnerDirect.ini: "+key+" is retired and ignored"+(key=="allow_unverified_yoke"?" (use Developer > Allow unverified yoke models; it is off until you turn it on)":""));
        } else {flight_settings.auto_start=0;log("No startup defaults file; the yoke waits for Re-centre in the plugin menu");}
        // Kept overlays the managed values; an invalid feel file is logged and ignored.
        plugin_folder=defaults.parent_path();
        for (const auto& line:wb::managed_adjustments(flight_settings)) log(line);
        bench=std::make_unique<wb::Workbench>(wb::from_managed(flight_settings));
        for (const auto& line:bench->load(plugin_folder)) log(line);
        load_yoke_presets();
        sync_feel(brunner::Clock::now());
    } catch (const std::exception& e) {log(std::string("Configuration invalid; plugin not started: ")+e.what());return 0;}
    yoke_menu.create({menu_action,[](const std::string& line) {log(line);}},feel_menu_text);
    hud_command=XPLMCreateCommand("brunner_direct/toggle_hud","Show or hide contextual HUD; recording continues");
    pin_command=XPLMCreateCommand("brunner_direct/pin_hud","Keep the yoke feel card on screen, or show it only when useful");
    move_command=XPLMCreateCommand("brunner_direct/move_hud","Enable or finish dragging the HUD by its status strip");
    for (auto command:{hud_command,pin_command,move_command}) XPLMRegisterCommandHandler(command,hud_handler,1,nullptr);
    for (auto& entry:hud_datarefs)
        published.publish({entry.name,true,[&entry] {return read_hud(&entry);},[&entry](int value) {write_hud(&entry,value);}});
    const char* names[]{"brunner_direct/recording/state","brunner_direct/recording/dropped","brunner_direct/recording/written"};
    for (std::size_t i=0;i<3;++i)
        published.publish({names[i],false,[i] {return read_recorder(reinterpret_cast<void*>(i));},nullptr});
    monitor_command=XPLMCreateCommand("brunner_direct/toggle_monitor","Show or hide read-only force command history and provenance");
    XPLMRegisterCommandHandler(monitor_command,monitor_handler,1,nullptr);
    reference_command=XPLMCreateCommand("brunner_direct/reference_test","Supervised reference and centering test: moves yoke, then disables motors");
    stop_command=XPLMCreateCommand("brunner_direct/stop","Developer stop: motors off and disconnect; nothing automatic until Re-centre");
    stop_motors_command=XPLMCreateCommand("brunner_direct/stop_motors","Stop yoke motors: force feel off, pitch and roll keep working");
    XPLMRegisterCommandHandler(reference_command,reference_handler,1,nullptr);
    XPLMRegisterCommandHandler(stop_command,stop_handler,1,nullptr);
    XPLMRegisterCommandHandler(stop_motors_command,stop_motors_handler,1,nullptr);
    inspect_command=XPLMCreateCommand("brunner_direct/inspect_loading","Read the current controller spring profile into the simulator log");
    XPLMRegisterCommandHandler(inspect_command,inspect_handler,1,nullptr);
    loading_command=XPLMCreateCommand("brunner_direct/loading_test","Reference, apply a light bounded spring for ten minutes, then stop and restore settings");
    XPLMRegisterCommandHandler(loading_command,loading_handler,1,nullptr);
    recovery_command=XPLMCreateCommand("brunner_direct/restore_loading","Put the yoke at the plugin's resting spring settings and clear controller faults, motors disabled only");
    XPLMRegisterCommandHandler(recovery_command,recovery_handler,1,nullptr);
    gap_command=XPLMCreateCommand("brunner_direct/communication_gap","During a light spring test, stop all USB requests for ten seconds and inspect the retained state");
    XPLMRegisterCommandHandler(gap_command,gap_handler,1,nullptr);
    reconnect_command=XPLMCreateCommand("brunner_direct/reconnect","Reconnect the native USB reader; a stopped yoke stays read-only, otherwise automatic operation resumes; refuses an active test");
    XPLMRegisterCommandHandler(reconnect_command,reconnect_handler,1,nullptr);
    flight_command=XPLMCreateCommand("brunner_direct/start_flight","Switch force feel on and resume automatic yoke operation");
    XPLMRegisterCommandHandler(flight_command,flight_handler,1,nullptr);
    recenter_command=XPLMCreateCommand("brunner_direct/recenter","Re-centre the yoke (hands off); also the Retry after a fault or Stop");
    XPLMRegisterCommandHandler(recenter_command,recenter_handler,1,nullptr);
    force_feel_toggle=XPLMCreateCommand("brunner_direct/toggle_force_feel","Switch yoke force feel on or off; pitch and roll keep working");
    force_feel_on=XPLMCreateCommand("brunner_direct/force_feel_on","Switch yoke force feel on");
    force_feel_off=XPLMCreateCommand("brunner_direct/force_feel_off","Switch yoke force feel off; motors stop, yoke keeps supplying pitch and roll");
    for (auto command:{force_feel_toggle,force_feel_on,force_feel_off}) XPLMRegisterCommandHandler(command,force_feel_handler,1,nullptr);
    status_line_command=XPLMCreateCommand("brunner_direct/toggle_status_line","Show or hide the on-screen yoke status line");
    XPLMRegisterCommandHandler(status_line_command,status_line_handler,1,nullptr);
    mark_feel_command=XPLMCreateCommand("brunner_direct/mark_feel","Feel tuning: mark this moment with the settings being flown");
    compare_feel_command=XPLMCreateCommand("brunner_direct/compare_feel","Feel tuning: switch between Kept and Trying");
    for (auto command:{mark_feel_command,compare_feel_command}) XPLMRegisterCommandHandler(command,feel_handler,1,nullptr);
    for (auto& entry:live_datarefs)
        published.publish({entry.name,entry.writable,[&entry] {return read_live(&entry);},
            entry.writable?std::function<void(int)>([&entry](int value) {write_live(&entry,value);}):std::function<void(int)>{}});
    // The one status line. Everything that shows status reads decision.status.
    published.publish_text("brunner_direct/state/status",[] {return decision.status;});
    const std::array<const char*,3> trim_names{{"brunner_direct/trim_positive","brunner_direct/trim_negative","brunner_direct/trim_center"}};
    for (unsigned i=0;i<3;++i) {
        trim_commands[i]=XPLMCreateCommand(trim_names[i],"During the light spring test, slowly shift pitch neutral by up to 600 counts; direction is under test");
        XPLMRegisterCommandHandler(trim_commands[i],trim_handler,1,&trim_steps[i]);
    }
    log("Yoke plugin loaded; automatic operation="+std::to_string(flight_settings.auto_start)+"; force feel default="+std::to_string(flight_settings.force_feel)
        +"; pitch follows the elevator model from "+brunner::profiles_file+", "+std::to_string(flight_settings.pitch_force_units_per_lb)
        +" drive units per lb (derived, not measured), firmness up to 100%, tested to 40%"
        +"; pitch drag gain 1/"+std::to_string(flight_settings.pitch_force_gain_divisor)+", move-back "+std::to_string(flight_settings.pitch_move_back_gain)
        +"; roll follows the aileron model, "+std::to_string(flight_settings.roll_torque_units_per_nm)
        +" drive units per N·m (derived, not measured), force-exact below the drive's softest spring");
    return 1;
}
PLUGIN_API int XPluginEnable() {
    try {
        char system_path[1024]{};XPLMGetSystemPath(system_path);
        output_folder=std::filesystem::path(system_path)/"Output"/"BrunnerDirect";
        recorder.start(output_folder);
        recorder.event("Plugin enabled; force settings preserved; HUD telemetry schema 1");
        force_hud.show_hud();create_startup_notice();sim.identify_aircraft();sim.bind_surfaces();
        session_drag.reset();poll_profiles(brunner::Clock::now(),true);log_feel_events();
        XPLMCreateFlightLoop_t loop{sizeof(XPLMCreateFlightLoop_t),xplm_FlightLoop_Phase_AfterFlightModel,observe_completed_frame,nullptr};
        readiness_loop=XPLMCreateFlightLoop(&loop);
        if (!readiness_loop) throw std::runtime_error("Could not observe simulator readiness");
        yoke=brunner::YokeSupervisor{};decision=brunner::YokeDecision{};
        force_feel=flight_settings.force_feel!=0;
        // auto_start=0 keeps the old fail-closed behaviour: nothing moves until the pilot's Re-centre.
        if (!flight_settings.auto_start) yoke.stop();
        reader.start();startup_readiness.reset();
        simulator_ready_for_start=false;enabled=true;
        XPLMRegisterFlightLoopCallback(update,0.1f,nullptr);XPLMScheduleFlightLoop(readiness_loop,-1.0f,1);
        start_workbench();
        return 1;
    } catch (const std::exception& e) {
        log(e.what());reader.stop();force_hud.close();recorder.stop();
        if (readiness_loop) {XPLMDestroyFlightLoop(readiness_loop);readiness_loop=nullptr;}
        startup_notice.destroy();
        return 0;
    }
}
PLUGIN_API void XPluginDisable() {
    save_travel(brunner::Clock::now(),true); // the learned read-only travel, if it moved since the last save
    stop_workbench();
    session_notices.reset();corner_notice.reset(); // a re-enable starts with no session to have ended
    force_monitor.close();force_hud.close();
    diagnostic_trim.renew(0,false,brunner::Clock::now());
    yoke.stop();startup_readiness.reset();
    sim.release_axes();
    if (enabled) XPLMUnregisterFlightLoopCallback(update,nullptr);
    enabled=false;simulator_ready_for_start=false;reader.stop();last_status.clear();
    recorder.event("Plugin disabled; reader stopped");recorder.stop();
    decision=brunner::YokeDecision{};decision.status="Yoke plugin is disabled";
    if (readiness_loop) {XPLMDestroyFlightLoop(readiness_loop);readiness_loop=nullptr;}
    startup_notice.destroy();
    yoke_menu.set_status(decision.status);
}
PLUGIN_API void XPluginStop() {
    XPluginDisable();
    for (auto command:{hud_command,pin_command,move_command}) if (command) XPLMUnregisterCommandHandler(command,hud_handler,1,nullptr);
    published.unpublish_all();
    if (monitor_command) XPLMUnregisterCommandHandler(monitor_command,monitor_handler,1,nullptr);
    if (reference_command) XPLMUnregisterCommandHandler(reference_command,reference_handler,1,nullptr);
    if (stop_command) XPLMUnregisterCommandHandler(stop_command,stop_handler,1,nullptr);
    if (stop_motors_command) XPLMUnregisterCommandHandler(stop_motors_command,stop_motors_handler,1,nullptr);
    if (inspect_command) XPLMUnregisterCommandHandler(inspect_command,inspect_handler,1,nullptr);
    if (loading_command) XPLMUnregisterCommandHandler(loading_command,loading_handler,1,nullptr);
    if (recovery_command) XPLMUnregisterCommandHandler(recovery_command,recovery_handler,1,nullptr);
    if (gap_command) XPLMUnregisterCommandHandler(gap_command,gap_handler,1,nullptr);
    if (reconnect_command) XPLMUnregisterCommandHandler(reconnect_command,reconnect_handler,1,nullptr);
    if (flight_command) XPLMUnregisterCommandHandler(flight_command,flight_handler,1,nullptr);
    if (recenter_command) XPLMUnregisterCommandHandler(recenter_command,recenter_handler,1,nullptr);
    for (auto command:{force_feel_toggle,force_feel_on,force_feel_off}) if (command) XPLMUnregisterCommandHandler(command,force_feel_handler,1,nullptr);
    if (status_line_command) XPLMUnregisterCommandHandler(status_line_command,status_line_handler,1,nullptr);
    for (auto command:{mark_feel_command,compare_feel_command}) if (command) XPLMUnregisterCommandHandler(command,feel_handler,1,nullptr);
    for (unsigned i=0;i<3;++i) if (trim_commands[i]) XPLMUnregisterCommandHandler(trim_commands[i],trim_handler,1,&trim_steps[i]);
    yoke_menu.destroy();
}
// An aircraft change releases the axes and waits for steady physics again; the
// supervisor keeps running, so pitch and roll return on the new aircraft.
PLUGIN_API void XPluginReceiveMessage(XPLMPluginID,int message,void* parameter) {
    if (message==XPLM_MSG_PLANE_LOADED && reinterpret_cast<std::intptr_t>(parameter)==0) {
        diagnostic_trim.renew(0,false,brunner::Clock::now());
        sim.release_axes();startup_readiness.reset();simulator_ready_for_start=false;sim.identify_aircraft();sim.bind_surfaces();
        poll_profiles(brunner::Clock::now(),true);log_feel_events();
        reader.simulator_update(false,false,{});
    }
}
