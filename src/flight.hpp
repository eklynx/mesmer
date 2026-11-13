#pragma once
#include "core.hpp"
#include <cmath>
#include <initializer_list>
#include <limits>
#include <optional>

namespace brunner {
// Startup defaults (BrunnerDirect.ini) and the workbench's settings. Pitch is
// the elevator model and roll the aileron model on every aircraft (decisions
// 6 and 15), their figures the aircraft's profile (profiles.hpp); the knobs
// here are the drive's. Roll's airflow spring and plain spring are retired.
struct FlightSettings {
    int auto_start=0; // fail closed without explicit operator-managed startup defaults
    int reference_airspeed=316; // saved CLS2Sim speed-curve x maximum, knots
    // Roll's spring floor: the drive's softest, no longer a setting (the
    // aileron model is force-exact below it). Kept for the panel's line.
    int roll_minimum_percent=softest_spring_percent;
    int force_feel=0; // startup default for the pilot's force-feel switch; off until Step 1 flips it
    // Roll drive torque units per N·m at the wheel. DERIVED from CLS2Sim's
    // display factor for this yoke's roll, 0.001 N·m per unit
    // (docs/brunner/calibration-control.md): 1000. Unmeasured, as pitch's is;
    // it sets how hard, never where the wheel rests.
    int roll_torque_units_per_nm=1000;
    // Drive force units per pound at the grip. DERIVED from CLS2Sim's display
    // factor for this yoke's pitch, 0.02 N per unit (docs/brunner/calibration-control.md):
    // 222. Nothing has measured delivered force (decision 11); it stays until
    // the owner's luggage scale arrives. It sets how hard, never where the yoke rests.
    int pitch_force_units_per_lb=222;
    // The drive's force-following gain, 1/divisor (0x2100:01/02), and its
    // move-back gain (0x2102): the drag a hands-off yoke moves against. The
    // defaults are the drive's own switch-on values; a lower divisor or a
    // higher move-back gain is untested on the yoke.
    int pitch_force_gain_divisor=6,pitch_move_back_gain=180;
    // Buffet through the drive's vibration generator (decision 19, buffet.hpp):
    // off unless the owner turns it on; its strength in percent of the plugin's
    // ceiling and the drive's time value. Untested with the motors on.
    int buffet=0,buffet_percent=10,buffet_period=80;
    // Read-only mode (readonly.hpp): flip an axis whose learned direction is backwards.
    int read_only_roll_reverse=0,read_only_pitch_reverse=0;
    // Drive a yoke model whose preset is not verified (yoke_preset.hpp): the owner's acknowledgement.
    int allow_unverified_yoke=0;
};
inline bool valid_flight_settings(const FlightSettings& c) {
    return (c.auto_start==0 || c.auto_start==1) && (c.force_feel==0 || c.force_feel==1)
        && c.roll_torque_units_per_nm>=100 && c.roll_torque_units_per_nm<=10000
        && c.pitch_force_units_per_lb>=50 && c.pitch_force_units_per_lb<=1000
        && c.pitch_force_gain_divisor>=1 && c.pitch_force_gain_divisor<=6 && c.pitch_move_back_gain>=180 && c.pitch_move_back_gain<=720
        && c.reference_airspeed>=40 && c.reference_airspeed<=400
        && c.roll_minimum_percent==softest_spring_percent
        && (c.allow_unverified_yoke==0 || c.allow_unverified_yoke==1) && (c.read_only_roll_reverse==0 || c.read_only_roll_reverse==1) && (c.read_only_pitch_reverse==0 || c.read_only_pitch_reverse==1)
        && (c.buffet==0 || c.buffet==1) && c.buffet_percent>=0 && c.buffet_percent<=100 && c.buffet_period>=10 && c.buffet_period<=140;
}
// Per-element dynamic pressure over two X-Plane wings, area-weighted so
// unequal sides combine; the tip element is excluded. The indices come from
// the profile (tail_sections, aileron_sections); c172_flow's are the stock
// C172 SP's, for the diagnostic page only.
struct PanelLoad { double area=0, pressure_area=0;
    double pa() const {return area>0 ? pressure_area/area : -1;}
};
inline PanelLoad operator+(PanelLoad a, PanelLoad b) {return {a.area+b.area,a.pressure_area+b.pressure_area};}
struct SurfaceFlow { double tail=-1,left=-1,right=-1,roll=-1; };
inline SurfaceFlow c172_flow(double rho,const float* speed,const float* area,int length) {
    if (length<99 || !speed || !area || !std::isfinite(rho) || rho<=0) return {};
    auto section=[&](int wing) {
        PanelLoad load;
        for (int i=wing*10;i<wing*10+9;++i) {
            const double v=speed[i],a=area[i];
            if (!std::isfinite(v) || v<0 || !std::isfinite(a) || a<=0) return PanelLoad{};
            load.area+=a;load.pressure_area+=a*.5*rho*v*v;
        }
        return load;
    };
    const auto l=section(2),r=section(3),el=section(8),er=section(9);
    return {el.area && er.area?(el+er).pa():-1,l.pa(),r.pa(),l.area && r.area?(l+r).pa():-1};
}
// The two wings' area-weighted dynamic pressure, -1 when X-Plane does not supply it.
inline double sections_pa(double rho,const float* speed,const float* area,int length,int first_wing,int second_wing) {
    if (length<99 || !speed || !area || !std::isfinite(rho) || rho<=0) return -1;
    PanelLoad load;
    for (const int wing:{first_wing,second_wing}) for (int i=wing*10;i<wing*10+9;++i) {
        const double v=speed[i],a=area[i];
        if (!std::isfinite(v) || v<0 || !std::isfinite(a) || a<=0) return -1;
        load.area+=a;load.pressure_area+=a*.5*rho*v*v;
    }
    return load.pa();
}
// Area-weighted mean of a per-element value over the same C172 sections
// c172_flow uses (2/3 ailerons, 8/9 elevator, tip element excluded). NaN when
// X-Plane does not supply it.
inline double c172_section_mean(const float* value,const float* area,int length,int first_wing,int second_wing) {
    if (length<99 || !value || !area) return std::numeric_limits<double>::quiet_NaN();
    double weight=0,sum=0;
    for (const int wing:{first_wing,second_wing}) for (int i=wing*10;i<wing*10+9;++i) {
        if (!std::isfinite(value[i]) || !std::isfinite(area[i]) || area[i]<=0) return std::numeric_limits<double>::quiet_NaN();
        weight+=area[i];sum+=area[i]*value[i];
    }
    return weight>0?sum/weight:std::numeric_limits<double>::quiet_NaN();
}
// One aileron's air for the model from its X-Plane wing's elements 0-8 (the
// tip excluded), section 3 of docs/brunner/aileron-and-aoa-plan.md: dynamic
// pressure area-weighted over all of them; the angle of attack weighted by
// each panel's q*area, and the stalled share of q*area, both over the panels
// with at least aileron_minimum_panel_pa, so a panel with no air says nothing
// (its angle is noise and its stall flag flickers). Empty when X-Plane does
// not supply it. Angles wrap at ±180°: while every counted panel lies within
// ±90° the angle is their plain weighted mean; otherwise each is unwrapped
// about the weighted circular mean (atan2 of the weighted sines and cosines)
// before the mean is taken, and the result wrapped to (-180°, 180°], so
// panels astride ±180° (air from behind) read near 180°, not near 0°. The two
// agree where they meet, so the angle stays continuous.
// The tail's the same way over both of its wings (the profile's
// tail_sections, decision 17): elevator.html's TailAir.panels, the gate the
// elevator model's (tail_minimum_pa, equal to the ailerons').
inline std::optional<AileronSide> panels_side(double rho,const float* speed,const float* area,const float* aoa,const float* stalled,int length,
    std::initializer_list<int> wings) {
    if (!speed || !area || !aoa || !stalled || !std::isfinite(rho) || rho<=0) return {};
    for (const int wing:wings) if (wing<0 || length<wing*10+9) return {};
    double total_area=0,pressure_area=0,weight=0,angle=0,stalled_weight=0,sines=0,cosines=0;
    bool ahead=true;
    for (const int wing:wings) for (int i=wing*10;i<wing*10+9;++i) {
        const double v=speed[i],a=area[i];
        if (!std::isfinite(v) || v<0 || !std::isfinite(a) || a<=0) return {};
        const double q=.5*rho*v*v;
        total_area+=a;pressure_area+=a*q;
        if (q<aileron_minimum_panel_pa) continue;
        if (!std::isfinite(aoa[i]) || !std::isfinite(stalled[i])) return {};
        weight+=q*a;angle+=q*a*aoa[i];stalled_weight+=stalled[i]>=.5f?q*a:0.0;
        sines+=q*a*std::sin(aoa[i]*radians_per_degree);cosines+=q*a*std::cos(aoa[i]*radians_per_degree);
        ahead=ahead && std::abs(aoa[i])<90;
    }
    if (!(total_area>0)) return {};
    const auto wrapped=[](double deg) {return deg-360*std::ceil((deg-180)/360);}; // to (-180, 180]
    if (!ahead && weight>0) {
        const double centre=std::atan2(sines,cosines)/radians_per_degree;
        angle=0;
        for (const int wing:wings) for (int i=wing*10;i<wing*10+9;++i) {
            const double q=.5*rho*double(speed[i])*double(speed[i]);
            if (q<aileron_minimum_panel_pa) continue;
            angle+=q*area[i]*(centre+wrapped(aoa[i]-centre));
        }
        angle=wrapped(angle/weight)*weight;
    }
    AileronSide out;
    out.pa=pressure_area/total_area;
    if (weight>0) {out.alpha_deg=angle/weight;out.stalled=stalled_weight/weight;}
    return out;
}
inline std::optional<AileronSide> aileron_side(double rho,const float* speed,const float* area,const float* aoa,const float* stalled,int length,int wing) {
    return panels_side(rho,speed,area,aoa,stalled,length,{wing});
}
inline std::optional<AileronSide> tail_side(double rho,const float* speed,const float* area,const float* aoa,const float* stalled,int length,
    int first_wing,int second_wing) {
    return panels_side(rho,speed,area,aoa,stalled,length,{first_wing,second_wing});
}
static_assert(tail_minimum_pa==aileron_minimum_panel_pa,"the tail's gate is the ailerons'");
// The ailerons' air for the model: X-Plane's over the profile's aileron
// sections where the aircraft is the profile's own and X-Plane supplies them
// (propwash, roll rate and sideslip included); else indicated airspeed's on
// both sides, level and unstalled.
enum class AileronSource {surface,airspeed};
struct AileronAir {AileronInputs inputs;AileronSource source=AileronSource::airspeed;bool valid=false;};
inline AileronAir aileron_air(const std::optional<AileronInputs>& panels,double ias_kt) {
    if (panels && std::isfinite(panels->left.pa) && panels->left.pa>=0 && std::isfinite(panels->right.pa) && panels->right.pa>=0)
        return {*panels,AileronSource::surface,true};
    if (!std::isfinite(ias_kt)) return {};
    const double v=std::max(0.0,ias_kt)*metres_per_second_per_knot,q=.5*sea_level_density*v*v;
    return {{{q,0,0},{q,0,0}},AileronSource::airspeed,true};
}
// Any element of the two sections stalled; -1 when X-Plane does not supply it.
inline int c172_section_stalled(const float* stalled,int length,int first_wing,int second_wing) {
    if (length<99 || !stalled) return -1;
    bool any=false;
    for (const int wing:{first_wing,second_wing}) for (int i=wing*10;i<wing*10+9;++i) {
        if (!std::isfinite(stalled[i])) return -1;
        any=any || stalled[i]>=.5f;
    }
    return any;
}
// The recorder's name for how force feel maps the flight to the drive.
inline constexpr char stock_loading_profile[]="elevator-model-v1";
struct FlightSample {
    double airspeed=0,trim=0; // IAS is diagnostic only
    SurfaceFlow flow{};
    double pitch=0,roll=0; // simulator absolute input; only used to explain resident restoring direction
    double native_pitch_lb=std::numeric_limits<double>::quiet_NaN(),native_roll_lb=std::numeric_limits<double>::quiet_NaN();
    double physics_time=-1;
    double load_factor=1; // X-Plane's normal load factor, g
    // Each aileron's air from the profile's aileron sections, on its own aircraft.
    std::optional<AileronInputs> ailerons{};
    // The tail's angle and stalled share from the profile's tail sections, on
    // its own aircraft, as X-Plane reports them (decision 17).
    std::optional<AileronSide> tail{};
};
inline bool valid_frame_age(Clock::time_point stamp,Clock::time_point now,bool paused) {
    return !paused && stamp!=Clock::time_point{} && now>=stamp && now-stamp<=std::chrono::milliseconds(250);
}
// What the flight loop sends the session: the commissioning springs (only
// where the session would start without the models: the light spring bench
// test), each axis's model, and where each axis rests relative to X-Plane's
// input. A model session's strengths are its solvers', up to the drive's own
// ceiling (decision 16).
struct FlightTarget {
    Tuning tuning;
    std::optional<PitchModel> pitch;
    std::optional<RollModel> roll;
    AileronSource aileron_source=AileronSource::airspeed;
    // Signed displacements FROM the resting point in sim coordinates.
    // The drive supplies restoring effort in the opposite direction; these aren't measured loads.
    double pitch_error=0,roll_error=0;
};
// The tail's dynamic pressure for the model: X-Plane's over the profile's
// tail sections where the aircraft is the profile's own and X-Plane supplies
// them (propwash included); else indicated airspeed's, which is the same
// pressure at sea level and has no propwash.
enum class TailSource {surface,airspeed};
struct TailAir {double pa=-1;TailSource source=TailSource::airspeed;};
inline TailAir tail_air(double surface_pa,double ias_kt) {
    if (std::isfinite(surface_pa) && surface_pa>=0) return {surface_pa,TailSource::surface};
    if (!std::isfinite(ias_kt)) return {};
    const double v=std::max(0.0,ias_kt)*0.514444;
    return {0.5*1.225*v*v,TailSource::airspeed};
}
// The elevator model's inputs: trim, the tail's pressure and the load factor;
// the tail's angle and stall with its panels' pressure, none on airspeed's.
inline ElevatorInputs elevator_inputs(const FlightSample& s,const TailAir& tail) {
    const bool angled=tail.source==TailSource::surface && s.tail.has_value();
    return {s.trim,tail.pa,s.load_factor,angled?s.tail->alpha_deg:0.0,angled?s.tail->stalled:0.0};
}
// The drive's knobs as the workbench flies them, and its count of the feel's
// edits (FeelBlend blends each one in).
struct PitchKnobs {int units_per_lb=222;std::uint64_t edit=0;};
struct RollKnobs {int units_per_nm=1000;std::uint64_t edit=0;};
// Every aircraft. Pitch follows the elevator model and roll the aileron
// model with the profile's figures, each solved in the session for the
// yoke's position (elevator.hpp, aileron.hpp). No fallback: without the
// profile's figures there is no target.
inline std::optional<FlightTarget> flight_target(const FlightSample& s,const FlightSettings& c,const Tuning& caps,
    const ElevatorFigures& figures,const PitchKnobs& k,const AileronFigures& ailerons,const RollKnobs& r) {
    if (!valid_flight_settings(c) || !valid_tuning(caps) || !std::isfinite(s.trim) || std::abs(s.trim)>1
        || !std::isfinite(s.pitch) || std::abs(s.pitch)>1 || !std::isfinite(s.roll) || std::abs(s.roll)>1) return {};
    const auto tail=tail_air(s.flow.tail,s.airspeed);
    if (tail.pa<0) return {};
    const auto air=aileron_air(s.ailerons,s.airspeed);
    if (!air.valid) return {};
    const PitchModel m{figures,elevator_inputs(s,tail),k.units_per_lb,k.edit};
    const RollModel roll{ailerons,air.inputs,r.units_per_nm,r.edit};
    if (!valid_pitch_model(m) || !valid_roll_model(roll)) return {};
    // The rests reached from where the yoke and wheel are (X-Plane's ratios:
    // pitch aft positive, the drive's counts forward positive; roll right
    // positive in both), as the session's solve takes them.
    const double rest=model_rest_counts(figures,m.inputs,-s.pitch*full_travel_counts());
    const double roll_rest=wheel_exact_counts(ailerons,wheel_rest_rad(ailerons,air.inputs,wheel_rad(ailerons,s.roll*roll_travel_counts())));
    return FlightTarget{{caps.roll_percent,caps.pitch_percent,0,0},m,roll,air.source,s.pitch+rest/full_travel_counts(),s.roll-roll_rest/roll_travel_counts()};
}
// A loaded aircraft alone is insufficient: the first frame can still stall.
// Observe completed physics frames after scenery work, then start from the
// next pre-physics callback. No wall-clock countdown or weakened watchdog.
class StartupReadiness {
    std::optional<Clock::time_point> previous_;
    double sim_time_=0;
    unsigned advancing_=0;
public:
    void reset() {previous_.reset();advancing_=0;sim_time_=0;}
    bool ready(Clock::time_point now) const {
        return advancing_>=3 && previous_ && now>=*previous_
            && now-*previous_<=std::chrono::milliseconds(250);
    }
    void observe(bool running,bool scenery_busy,double sim_time,Clock::time_point now) {
        if (!running || scenery_busy || !std::isfinite(sim_time) || sim_time<0) {reset();return;}
        if (!previous_ || now<*previous_ || now-*previous_>std::chrono::milliseconds(250) || sim_time<=sim_time_)
            advancing_=0;
        else advancing_=(std::min)(advancing_+1,3u);
        previous_=now;sim_time_=sim_time;
    }
};
enum class StartupNotice {hidden,waiting,centering,unavailable};
inline StartupNotice startup_notice(bool supported,bool input_ready,bool pending,const Snapshot& s) {
    if (!supported) return StartupNotice::hidden;
    if (s.loading_fault) return StartupNotice::unavailable;
    if (s.reference_active) return StartupNotice::centering;
    if (input_ready) return StartupNotice::hidden;
    return pending?StartupNotice::waiting:StartupNotice::unavailable;
}
// Only a fresh launch or an explicit Start command arms a new flight. A clean
// pause can resume; transport/controller/heartbeat faults cannot re-arm it.
class FlightLifecycle {
    bool armed_=false,requested_=false,blocked_=false;
public:
    void reset(bool automatic) {armed_=automatic;requested_=false;blocked_=false;}
    void hold() {armed_=false;requested_=false;blocked_=true;}
    void arm() {armed_=true;requested_=false;blocked_=false;}
    bool pending() const {return !blocked_ && (armed_ || requested_);}
    bool should_start(bool ready,const Snapshot& s) const {
        return ready && !blocked_ && !requested_ && s.connected && !s.loading_fault
            && !s.reference_active && !s.loading_active && (armed_ || s.flight_suspended);
    }
    void requested() {requested_=true;armed_=false;}
    void observe(const Snapshot& s) {
        if (s.reference_active || s.loading_active || s.flight_suspended || s.loading_fault) requested_=false;
        if (s.loading_fault || (!s.connected && requested_)) hold();
    }
};
}
