#pragma once
#include "brunner/reference.hpp"
#include <algorithm>
#include <array>
#include <vector>
#include "brunner/vibration.hpp"
#include <cmath>
#include <cstdlib>
#include <iomanip>
#include <optional>

namespace brunner {
inline constexpr Register force_scale{0x2114,0,2},force_limit{0x211b,0,2},torque_limit{0x6072,0,2};
// CLS2Sim 5.9 MicroDriveDevice: SDOTrimPosition is 0x2116, while
// SDOTrimOffset is the separate 0x2115 coordinate/compensation path.
inline constexpr Register trim_position{0x2116,0,4},trim_offset{0x2115,0,4};
// The constant force, 0x211a, signed 16-bit: the elevator model's weight
// (decision 15), written only by a force-feel session following the model;
// zero otherwise, which also clears a stale CLS2Sim value. 0x211f is the
// strength (0x2114 units) at which the firmware drops it (CLS2Sim's yoke-pull
// cut-off, docs/brunner/cls2sim-device-setup-20261107.md); a model session
// raises it to bias_offset_off (force/tuning.hpp), above any strength the plugin
// sends with a constant force, so the weight stays on.
inline constexpr Register manual_force_bias{0x211a,0,2},manual_offset_off{0x211f,0,2};
inline std::uint32_t encode_bias(std::int32_t bias) {return static_cast<std::uint16_t>(static_cast<std::int16_t>(bias));}
inline std::int32_t decode_bias(std::uint32_t raw) {return static_cast<std::int16_t>(static_cast<std::uint16_t>(raw));}
// The pitch drive's force-following gain, numerator over denominator, and its
// move-back gain: CLS2Sim's names, never written by it. Switch-on values are
// 1/6 and 180 on pitch (docs/brunner/switch-on-inspection-20261107.jsonl and the
// 2026-11-08 friction survey); the hands-off yoke's drag is inferred to come from them.
inline constexpr Register force_gain_numerator{0x2100,1,2},force_gain_denominator{0x2100,2,2},move_back_gain{0x2102,0,4};
inline constexpr std::uint32_t default_gain_denominator=6,default_move_back=180;
// The drive's target-torque setpoint. CLS2Sim writes 50 on both axes and every
// force-feel session felt so far ran at 50; a switch-on leaves roll at 20. The
// owner chose (2026-11-07) that the plugin writes 50, this one fixed value
// only, motors off; it is never driven from the simulator.
inline constexpr Register target_torque{0x6071,0,2};
inline std::uint32_t fixed_target_torque() {return active_drive().target_torque;}
// The plugin owns the spring settings. It writes them itself, motors off, and
// reads each one back; it never depends on what CLS2Sim or a switch-on left
// in the controllers (docs/brunner/switch-on-inspection-20261107.jsonl).
// The curve is the plugin's own; the resting strength is what the yoke is left
// at between sessions, and centring was observed to work from it.
inline constexpr std::array<std::uint16_t,9> bench_curve{{100,350,400,450,500,600,750,900,1000}};
inline constexpr std::uint16_t bench_scale=40,resting_scale=100;
// The elevator model's spring is linear: a straight line from the centre to
// 1000 at full deflection, its first point the lowest the plugin writes. The
// nine points are taken to span the centre to full travel evenly, as the feel
// page draws them; the drive's own point positions were never surveyed.
inline constexpr std::array<std::uint16_t,9> model_curve{{5,125,250,375,500,625,750,875,1000}};
// What a force-feel session writes, motors off, besides the strengths: on
// pitch the curve, the drive's drag gains and whether the constant force
// stays on; on roll the curve. The defaults are the resting settings.
struct PitchSetup {
    std::array<std::uint16_t,9> curve=bench_curve;
    std::array<std::uint16_t,9> roll_curve=bench_curve; // the aileron model's straight line in a model session
    std::uint32_t gain_denominator=default_gain_denominator,move_back=default_move_back;
    bool constant_force=false;
};
// The next session's setup: the drag gains asked for, and following the
// models the straight-line curve on both axes with pitch's constant force kept on.
inline PitchSetup pitch_setup_for(bool model,int gain_divisor,int move_back) {
    PitchSetup p;
    p.gain_denominator=static_cast<std::uint32_t>(gain_divisor);p.move_back=static_cast<std::uint32_t>(move_back);
    if (model) {p.curve=model_curve;p.roll_curve=model_curve;p.constant_force=true;}
    return p;
}
inline bool valid_pitch_setup(const PitchSetup& p) {
    return p.gain_denominator>=1 && p.gain_denominator<=default_gain_denominator && p.move_back>=default_move_back && p.move_back<=720
        && std::all_of(p.curve.begin(),p.curve.end(),[](std::uint16_t v) {return v>=5 && v<=1000;})
        && std::all_of(p.roll_curve.begin(),p.roll_curve.end(),[](std::uint16_t v) {return v>=5 && v<=1000;});
}
// The drives' own protection thresholds, found at switch-on and after CLS2Sim
// alike. The plugin checks them and never writes them.
inline std::uint32_t expected_protection_limit(unsigned node) {return node==1?active_drive().roll_limit_register:active_drive().pitch_limit_register;}
// Refused before anything was written: the controllers are as they were, so
// the reference and the pilot's pitch and roll are unaffected. Switching the
// yoke off and on returns it to its known switch-on state.
struct SettingsRefused:std::runtime_error {using std::runtime_error::runtime_error;};
inline bool same_register(Register a,Register b) {return a.index==b.index && a.sub==b.sub && a.bytes==b.bytes;}
inline std::int32_t advance_trim(std::int32_t current,std::int32_t target,double seconds) {
    if (current < -600 || current > 600 || target < -600 || target > 600 || !std::isfinite(seconds) || seconds<0)
        throw std::invalid_argument("Invalid commissioning trim step");
    // At most 600 encoder counts/second, and at most 30 counts after a stall.
    const auto step=static_cast<std::int32_t>(std::floor((std::min)(seconds,0.05)*600.0));
    return current+std::clamp(target-current,-step,step);
}
inline bool stopped_for_recovery(std::uint32_t status) {
    // CiA-402 uses different masks for ready-to-switch-on and fault/disabled;
    // quick-stop bit 5 is not significant in the latter two states.
    return !(status&4) && ((status&0x006f)==0x21 || (status&0x004f)==0x08 || (status&0x004f)==0x40);
}
// Only the plugin's own spring settings, within bounds. Protection limits and
// a trim offset are never written; the constant force only on pitch, within
// its bound. Pitch's cut-off is raised above any strength (101) for a model
// session and put back afterwards to what that session found, a strength.
inline bool loading_write_allowed(unsigned node,RegisterWrite op) {
    if (vibration_register(op.reg)) return vibration_write_allowed(node,op); // buffet's own bounds (brunner/vibration.hpp)
    if (same_register(op.reg,trim_offset)) return op.value==0;
    if (same_register(op.reg,manual_force_bias)) return op.value<=0xffff && (node==2?std::abs(decode_bias(op.value))<=max_pitch_bias():op.value==0);
    if (same_register(op.reg,manual_offset_off)) return node==2 && op.value<=bias_offset_off;
    if (same_register(op.reg,force_gain_numerator)) return op.value==1;
    if (same_register(op.reg,force_gain_denominator)) return op.value>=1 && op.value<=default_gain_denominator;
    if (same_register(op.reg,move_back_gain)) return op.value>=default_move_back && op.value<=720;
    if (same_register(op.reg,trim_position)) {
        const auto bound=node==1?roll_travel_bound():pitch_travel_bound();
        return signed32(op.value)>=-bound && signed32(op.value)<=bound;
    }
    if (op.reg.bytes!=2) return false;
    // The drive refused 0 (force/tuning.hpp's softest_spring_percent); above
    // strongest_spring_percent() no centre keeps the margin (decision 16).
    // Whether a strength is within the margin with the axis's centre and
    // constant force is apply_live_tuning's check, write by write.
    if (same_register(op.reg,force_scale))
        return op.value>=static_cast<std::uint32_t>(softest_spring_percent) && op.value<=static_cast<std::uint32_t>(strongest_spring_percent());
    if (same_register(op.reg,target_torque)) return op.value==fixed_target_torque();
    return op.reg.index==0x2113 && op.reg.sub>=1 && op.reg.sub<=9 && op.value>=5 && op.value<=1000;
}
inline Report loading_write_request(unsigned node,RegisterWrite op,std::uint16_t counter) {
    if (!loading_write_allowed(node,op)) throw std::invalid_argument("Loading value outside commissioning bounds");
    auto r=reference_read_request(node,op.reg,counter);
    r[6]=op.reg.bytes==2?0x2b:0x23;
    for (unsigned i=0;i<4;++i) r[10+i]=static_cast<std::uint8_t>(op.value>>(i*8));
    return r;
}
// A setting write that did not take: held back by the plugin's own bounds,
// refused by the drive (its SDO abort code), or kept at another value. what()
// is the log's technical line; failure_reason() says it in the pilot's words.
struct DriveWriteFailed : std::runtime_error {
    unsigned node;RegisterWrite op;std::optional<std::uint32_t> abort,kept;
    DriveWriteFailed(const std::string& what,unsigned n,RegisterWrite w,std::optional<std::uint32_t> a,std::optional<std::uint32_t> k)
        :std::runtime_error(what),node(n),op(w),abort(a),kept(k) {}
};
template<class Transport> void checked_loading_write(Transport& bridge,unsigned node,RegisterWrite op,const std::atomic<bool>& stop) {
    const auto where=[&] {
        std::ostringstream m;m<<"node="<<node<<" register="<<std::hex<<op.reg.index<<":"<<unsigned(op.reg.sub)<<std::dec;return m.str();
    };
    if (!loading_write_allowed(node,op))
        throw DriveWriteFailed("Write held back, outside the plugin's bounds: "+where()+" value="+std::to_string(op.value),node,op,{},{});
    try {bridge.load_write(node,op,stop);}
    catch (const SdoAbort& e) {
        std::ostringstream message;
        message<<"Write rejected "<<where()<<" value="<<op.value<<" abort=0x"<<std::hex<<e.code;
        throw DriveWriteFailed(message.str(),node,op,e.code,{});
    }
    const auto actual=bridge.ref_read(node,op.reg,stop);
    if (actual!=op.value)
        throw DriveWriteFailed("Readback mismatch "+where()+" expected="+std::to_string(op.value)+" actual="+std::to_string(actual),node,op,{},actual);
}
// A drive setting and its value as the pilot would read them.
inline std::string setting_words(Register reg,std::uint32_t value) {
    const auto is=[&](Register r) {return same_register(reg,r);};
    if (is(force_scale)) return "spring strength "+std::to_string(value)+"%";
    if (is(trim_position)) return "resting point "+std::to_string(signed32(value))+" counts";
    if (is(manual_force_bias)) return "constant force "+std::to_string(decode_bias(value));
    if (is(trim_offset)) return "trim offset "+std::to_string(signed32(value));
    if (is(target_torque)) return "target torque "+std::to_string(value);
    if (is(manual_offset_off)) return "constant-force cut-off "+std::to_string(value);
    if (is(move_back_gain)) return "move-back gain "+std::to_string(value);
    if (is(force_gain_numerator) || is(force_gain_denominator)) return "force-following gain "+std::to_string(value);
    if (reg.index==0x2113) return "spring curve point "+std::to_string(reg.sub)+" = "+std::to_string(value);
    std::ostringstream m;m<<"register "<<std::hex<<reg.index<<":"<<unsigned(reg.sub)<<std::dec<<" = "<<value;return m.str();
}
// Why a session stopped: the page's sentence, the corner window's short line
// (about 45 characters), and whether Retry can help.
struct FailureReason {std::string text,short_text;bool retry_helps=true;};
inline FailureReason failure_reason(const std::exception& e) {
    if (const auto* w=dynamic_cast<const DriveWriteFailed*>(&e)) {
        const std::string drive=w->node==1?"roll":"pitch",Drive=w->node==1?"Roll":"Pitch",setting=setting_words(w->op.reg,w->op.value);
        if (w->abort) {
            std::ostringstream code;code<<std::hex<<std::setw(8)<<std::setfill('0')<<*w->abort;
            return {"The "+drive+" drive refused "+setting+" (code "+code.str()+"). The plugin asked for a value the drive does not take, "
                "so Retry will fail the same way until the plugin is fixed. The yoke still flies the aircraft without force feel.",
                Drive+" drive refused "+setting,false};
        }
        if (w->kept)
            return {"The "+drive+" drive kept "+setting_words(w->op.reg,*w->kept)+" when the plugin wrote "+setting+". Retry may help; "
                "if it happens again, it is a plugin fault.",Drive+" drive did not keep "+setting,true};
        return {"The plugin held back "+setting+" for the "+drive+" drive: it is outside the plugin's own bounds. Retry will fail the "
            "same way until the plugin is fixed. The yoke still flies the aircraft without force feel.","Plugin held back "+drive+" "+setting,false};
    }
    const std::string what=e.what();
    if (dynamic_cast<const std::invalid_argument*>(&e))
        return {"The plugin's own check stopped force feel: "+what+". That is a plugin fault, so Retry will fail the same way until "
            "the plugin is fixed. The yoke still flies the aircraft without force feel.","Plugin fault; Retry won't help",false};
    if (what=="Controller state changed during loading")
        return {"A drive left its ready state while force feel was on (a fault, or the yoke's power switch). Retry centres the yoke and starts again.",
            "A drive left its ready state",true};
    return {"Force feel stopped: "+what+". Retry starts it again; X-Plane's Log.txt has the details.","Details in X-Plane's Log.txt",true};
}
// Read before any write. Both motors disabled (and, for force feel,
// referenced) first: a running motor is a fault, never a refusal. Then, with
// both confirmed off, protection thresholds intact and a spring mode the
// plugin knows; failing those is a refusal. Nothing else about the
// controllers' prior settings matters.
template<class Transport> void check_controllers(Transport& bridge,const std::atomic<bool>& stop,bool referenced) {
    for (unsigned node=1;node<=2;++node) {
        const auto status=static_cast<std::uint16_t>(bridge.read(node,Object::status,stop));
        if (referenced?!referenced_stopped_status(status):!stopped_for_recovery(status))
            throw std::runtime_error("Spring setup requires both motors disabled (node "+std::to_string(node)+" status "+status_hex(status)+")");
    }
    for (unsigned node=1;node<=2;++node) {
        for (const auto reg:{force_limit,torque_limit})
            if (bridge.ref_read(node,reg,stop)!=expected_protection_limit(node))
                throw SettingsRefused("Controller protection limits differ from their switch-on values");
        if (base_spring_mode(static_cast<std::uint16_t>(bridge.ref_read(node,force_mode,stop)))!=spring_mode())
            throw SettingsRefused("Unknown controller spring mode");
    }
}
// A pitch setting written only when it differs, so the resting settings and a
// session at the defaults only read the drag gains.
template<class Transport> void ensure_pitch(Transport& bridge,Register reg,std::uint32_t value,const std::atomic<bool>& stop) {
    if (bridge.ref_read(2,reg,stop)!=value) checked_loading_write(bridge,2,{reg,value,0},stop);
}
// One axis's spring, written with its motor off: position control off (as
// CLS2Sim does on every connect), no constant force yet, no trim offset, zero
// trim, the fixed target torque, the curve, on pitch its drag gains (and,
// following the model, the constant force's cut-off raised), then the
// strength. Left alone on purpose
// (docs/brunner/cls2sim-device-setup-20261107.md): the soft-limit mode
// 2100:0c, whose two values give the same limits on this yoke's symmetric soft
// limits; 211f outside a model session, which only acts with a non-zero
// constant force; 2093, a reading.
template<class Transport> void write_spring(Transport& bridge,unsigned node,std::uint16_t scale,const std::atomic<bool>& stop,const PitchSetup& pitch={}) {
    if (!valid_pitch_setup(pitch)) throw std::invalid_argument("Pitch setup outside its bounds");
    bridge.ref_write(node,{center_mode,0,0},stop);
    if (bridge.ref_read(node,center_mode,stop)!=0) throw std::runtime_error("Position control did not switch off");
    checked_loading_write(bridge,node,{manual_force_bias,0,0},stop);
    checked_loading_write(bridge,node,{trim_offset,0,0},stop);
    checked_loading_write(bridge,node,{trim_position,0,0},stop);
    checked_loading_write(bridge,node,{target_torque,fixed_target_torque(),0},stop);
    const auto& curve=node==2?pitch.curve:pitch.roll_curve;
    for (unsigned sub=1;sub<=9;++sub)
        checked_loading_write(bridge,node,{{0x2113,static_cast<std::uint8_t>(sub),2},curve[sub-1],0},stop);
    if (node==2) {
        ensure_pitch(bridge,force_gain_numerator,1,stop);
        ensure_pitch(bridge,force_gain_denominator,pitch.gain_denominator,stop);
        ensure_pitch(bridge,move_back_gain,pitch.move_back,stop);
        if (pitch.constant_force) ensure_pitch(bridge,manual_offset_off,bias_offset_off,stop);
    }
    checked_loading_write(bridge,node,{force_scale,scale,0},stop);
}
// The model's resting settings (brunner/yoke_preset.hpp RestSettings): what a yoke is left at between
// sessions, so it is left as it was found. The CLS-E's are the plugin's historical constants.
inline PitchSetup rest_setup() {
    const auto& r=active_drive().rest;
    PitchSetup s;
    s.curve=r.pitch_curve;s.roll_curve=r.roll_curve;
    s.gain_denominator=static_cast<std::uint32_t>(r.pitch_gain_denominator);s.move_back=static_cast<std::uint32_t>(r.pitch_move_back);
    return s;
}
inline std::uint16_t rest_scale(unsigned node) {
    const auto& r=active_drive().rest;
    return static_cast<std::uint16_t>(node==1?r.roll_spring_percent:r.pitch_spring_percent);
}
// One axis's resting spring, motors off.
template<class Transport> void write_rest(Transport& bridge,unsigned node,const std::atomic<bool>& stop) {
    write_spring(bridge,node,rest_scale(node),stop,rest_setup());
}
// The force mode a model rests in, when it has one (the NG's carries the reference-at-power-on bit), written
// with the motors off and read back.
template<class Transport> void write_rest_mode(Transport& bridge,const std::atomic<bool>& stop) {
    const auto mode=active_drive().rest.mode;
    if (mode==0) return;
    for (unsigned node=1;node<=2;++node) {
        bridge.ref_write(node,{force_mode,static_cast<std::uint32_t>(mode),0},stop);
        if (bridge.ref_read(node,force_mode,stop)!=static_cast<std::uint32_t>(mode))
            throw std::runtime_error("The resting force mode did not take (node "+std::to_string(node)+")");
    }
}
// What a rest would write, read from the controllers as they are (reads only), for a preset that has no
// resting settings of its own. An unreadable register throws before anything is written.
template<class Transport> RestSettings capture_rest(Transport& bridge,const std::atomic<bool>& stop) {
    RestSettings r;
    for (unsigned node=1;node<=2;++node) {
        auto& curve=node==1?r.roll_curve:r.pitch_curve;
        for (unsigned sub=1;sub<=9;++sub)
            curve[sub-1]=static_cast<std::uint16_t>(bridge.ref_read(node,Register{0x2113,static_cast<std::uint8_t>(sub),2},stop));
        (node==1?r.roll_spring_percent:r.pitch_spring_percent)=static_cast<int>(bridge.ref_read(node,force_scale,stop));
    }
    r.pitch_gain_denominator=static_cast<int>(bridge.ref_read(2,force_gain_denominator,stop));
    r.pitch_move_back=static_cast<int>(bridge.ref_read(2,move_back_gain,stop));
    r.pitch_cutoff=static_cast<int>(bridge.ref_read(2,manual_offset_off,stop));
    const auto mode=bridge.ref_read(1,force_mode,stop);
    if (bridge.ref_read(2,force_mode,stop)!=mode) throw SettingsRefused("The controllers rest in different force modes");
    r.mode=static_cast<int>(mode);
    r.captured=false;
    return r;
}
// The plugin's resting settings, motors off: before a centring, after force
// feel stops and in recovery. After a model session, pitch's constant-force
// cut-off goes back to what that session found (else the model's own, if it has one).
template<class Transport> void rest_controllers(Transport& bridge,const std::atomic<bool>& stop,std::optional<std::uint32_t> offset_off={}) {
    check_controllers(bridge,stop,false);
    for (unsigned node=1;node<=2;++node) write_rest(bridge,node,stop);
    if (offset_off) ensure_pitch(bridge,manual_offset_off,*offset_off,stop);
    else if (active_drive().rest.pitch_cutoff>0) ensure_pitch(bridge,manual_offset_off,static_cast<std::uint32_t>(active_drive().rest.pitch_cutoff),stop);
    write_rest_mode(bridge,stop);
}
// Pitch's constant force cleared, best effort, never throwing: the first thing
// at every end of a session, whatever the link, the stop or roll do next.
template<class Transport> bool clear_pitch_force(Transport& bridge) noexcept {
    const std::atomic<bool> cleanup{false};
    try {
        bridge.load_write(2,{manual_force_bias,0,0},cleanup);
        return bridge.ref_read(2,manual_force_bias,cleanup)==0;
    } catch (...) {return false;}
}
// Ending a session with the motors possibly on: the constant force first, then
// both motors stopped. True only when both stops are confirmed; *cleared says
// whether the constant force read back as zero.
template<class Transport> bool stop_session(Transport& bridge,const std::array<std::uint16_t,2>& words,const std::array<std::uint16_t,2>& modes,
    bool* cleared=nullptr) {
    const bool zero=clear_pitch_force(bridge);
    if (cleared) *cleared=zero;
    return stop_reference(bridge,words,modes);
}
// Motors off, the session's spring written. For a model session, *found gets
// the constant-force cut-off pitch had (40 at switch-on, 30 after CLS2Sim)
// before anything is written, so resting can put it back even if a later write
// fails; a value no strength could be is refused before any write.
template<class Transport> void prepare_loading(Transport& bridge,const std::atomic<bool>& stop,const Tuning& initial={},const PitchSetup& pitch={},
    std::optional<std::uint32_t>* found=nullptr) {
    if (!valid_tuning(initial))
        throw std::invalid_argument("Loading asked for "+tuning_words(initial)+": outside the plugin's bounds (springs "
            +std::to_string(softest_spring_percent)+" to "+std::to_string(strongest_spring_percent())+"%, within the drive's margin)");
    if (initial.pitch_trim!=0 || initial.pitch_bias!=0 || initial.roll_trim!=0)
        throw std::invalid_argument("Loading must start at zero trim and no constant force, not "+tuning_words(initial));
    if (!valid_pitch_setup(pitch)) throw std::invalid_argument("Pitch setup outside its bounds");
    check_controllers(bridge,stop,true);
    if (pitch.constant_force) {
        const auto had=bridge.ref_read(2,manual_offset_off,stop);
        if (had>bias_offset_off) throw SettingsRefused("Unknown constant-force cut-off on pitch");
        if (found) *found=had;
    }
    for (unsigned node=1;node<=2;++node)
        write_spring(bridge,node,static_cast<std::uint16_t>(node==1?initial.roll_percent:initial.pitch_percent),stop,pitch);
}
template<class Transport> void verify_loading(Transport& bridge,unsigned node,const std::atomic<bool>& stop,const Tuning& expected={},const PitchSetup& pitch={}) {
    if (!valid_tuning(expected) || (node!=1 && node!=2)) throw std::invalid_argument("Invalid loading readback request");
    for (const auto reg:{force_limit,torque_limit})
        if (bridge.ref_read(node,reg,stop)!=expected_protection_limit(node)) throw std::runtime_error("Original controller protection limit changed");
    if (decode_bias(bridge.ref_read(node,manual_force_bias,stop))!=(node==2?expected.pitch_bias:0)) throw std::runtime_error("Unexpected constant force (force bias)");
    if (bridge.ref_read(node,target_torque,stop)!=fixed_target_torque()) throw std::runtime_error("Target torque changed");
    if (bridge.ref_read(node,force_scale,stop)!=static_cast<std::uint32_t>(node==1?expected.roll_percent:expected.pitch_percent)) throw std::runtime_error("Spring scale changed");
    if (signed32(bridge.ref_read(node,trim_position,stop))!=(node==2?expected.pitch_trim:expected.roll_trim)
        || bridge.ref_read(node,trim_offset,stop)!=0 || bridge.ref_read(node,center_mode,stop)!=0)
        throw std::runtime_error("Unexpected trim or position mode during spring test");
    const auto& curve=node==2?pitch.curve:pitch.roll_curve;
    for (unsigned sub=1;sub<=9;++sub)
        if (bridge.ref_read(node,{0x2113,static_cast<std::uint8_t>(sub),2},stop)!=curve[sub-1])
            throw std::runtime_error("Controller spring curve changed after enabling");
    if (node==2 && (bridge.ref_read(2,force_gain_numerator,stop)!=1 || bridge.ref_read(2,force_gain_denominator,stop)!=pitch.gain_denominator
        || bridge.ref_read(2,move_back_gain,stop)!=pitch.move_back))
        throw std::runtime_error("Pitch drag gains changed after enabling");
    if (node==2 && pitch.constant_force && bridge.ref_read(2,manual_offset_off,stop)!=bias_offset_off)
        throw std::runtime_error("Constant-force cut-off changed after enabling");
}
// One axis's registers, and the order a live update writes them in: the
// usual order (strength, centre, constant force) when every step on the way
// stays within the drive's margin (decision 16), else the first order of the
// three that does; failing all, through the softest spring, which is within
// the margin with any centre and constant force. No write can leave the
// drive asking for more than the margin anywhere in the travel.
enum class AxisRegister {strength,centre,bias};
struct AxisState {int strength=0;std::int32_t centre=0,bias=0;};
struct AxisWrite {AxisRegister reg;std::int32_t value;};
// `reserve` is what else the drive is asked for at once (force/tuning.hpp's DriveReserve).
inline std::vector<AxisWrite> axis_writes(const AxisState& from,const AxisState& to,std::int32_t travel,int reserve=0) {
    const auto safe=[travel,reserve](const AxisState& a) {return within_margin(a.strength,a.centre,a.bias,travel,reserve) && keeps_constant_force(a.strength,a.bias);};
    const auto value=[](const AxisState& a,AxisRegister r) {return r==AxisRegister::strength?a.strength:r==AxisRegister::centre?a.centre:a.bias;};
    const auto set=[](AxisState& a,AxisRegister r,std::int32_t v) {(r==AxisRegister::strength?a.strength:r==AxisRegister::centre?a.centre:a.bias)=v;};
    std::array<AxisRegister,3> order{AxisRegister::strength,AxisRegister::centre,AxisRegister::bias};
    do {
        AxisState at=from;std::vector<AxisWrite> steps;bool ok=true;
        for (const auto r:order) {
            if (value(at,r)==value(to,r)) continue;
            set(at,r,value(to,r));steps.push_back({r,value(to,r)});
            if (!safe(at)) {ok=false;break;}
        }
        if (ok) return steps;
    } while (std::next_permutation(order.begin(),order.end(),[](AxisRegister a,AxisRegister b) {return int(a)<int(b);}));
    std::vector<AxisWrite> steps;
    if (from.strength!=softest_spring_percent) steps.push_back({AxisRegister::strength,softest_spring_percent});
    if (from.centre!=to.centre) steps.push_back({AxisRegister::centre,to.centre});
    if (from.bias!=to.bias) steps.push_back({AxisRegister::bias,to.bias});
    if (to.strength!=softest_spring_percent) steps.push_back({AxisRegister::strength,to.strength});
    return steps;
}
// `r` is buffet's reserve (force/tuning.hpp): `next` and every step towards it stay
// within the margin less it. What the drive holds now need only be within the
// whole margin: a reserve that has just grown is met by these writes, before
// buffet adds its shake.
template<class Transport> void apply_live_tuning(Transport& bridge,Tuning& applied,const Tuning& next,const std::atomic<bool>& stop,
    const DriveReserve& r={}) {
    if (!valid_tuning(applied) || !valid_tuning(next,r)) throw std::invalid_argument("Invalid live tuning");
    // Confirm both drives before any update. Every changed value is read back;
    // a mismatch unwinds through the loading test's stop-and-rest path.
    for (unsigned node=1;node<=2;++node) {
        const auto status=bridge.read(node,Object::status,stop);
        if (!status_ready(static_cast<std::uint16_t>(status)) || (status&0x2108)) throw std::runtime_error("Controller not ready for live tuning (node "+std::to_string(node)+" status "+status_hex(static_cast<std::uint16_t>(status))+")");
        if (decode_bias(bridge.ref_read(node,manual_force_bias,stop))!=(node==2?applied.pitch_bias:0)
            || bridge.ref_read(node,trim_offset,stop)!=0 || bridge.ref_read(node,center_mode,stop)!=0)
            throw std::runtime_error("Unexpected constant force, coordinate offset or position mode during live tuning");
    }
    // Pitch first, all of it: strength, centre, constant force. Roll's
    // strength and centre follow, so a roll write never delays pitch's. Each
    // axis's registers go in an order whose every step stays within the
    // drive's margin (axis_writes).
    const auto write=[&](unsigned node,const std::vector<AxisWrite>& steps) {
        for (const auto& w:steps) {
            auto& strength=node==1?applied.roll_percent:applied.pitch_percent;
            auto& centre=node==1?applied.roll_trim:applied.pitch_trim;
            switch (w.reg) {
            case AxisRegister::strength:
                checked_loading_write(bridge,node,{force_scale,static_cast<std::uint32_t>(w.value),0},stop);strength=w.value;break;
            case AxisRegister::centre:
                checked_loading_write(bridge,node,{trim_position,static_cast<std::uint32_t>(w.value),0},stop);centre=w.value;break;
            case AxisRegister::bias:
                checked_loading_write(bridge,node,{manual_force_bias,encode_bias(w.value),0},stop);applied.pitch_bias=w.value;break;
            }
        }
    };
    write(2,axis_writes({applied.pitch_percent,applied.pitch_trim,applied.pitch_bias},{next.pitch_percent,next.pitch_trim,next.pitch_bias},pitch_travel_bound(),r.pitch));
    write(1,axis_writes({applied.roll_percent,applied.roll_trim,0},{next.roll_percent,next.roll_trim,0},roll_travel_bound(),r.roll));
}
// One update of a session: roll's centre held inside its deadband
// (force/tuning.hpp's roll_centre_deadband), roll's strength lowered if the held
// centre would put it over the drive's margin (within_drive), then nothing at all, not even the
// drives' status reads, unless something is left to write. Returns whether
// apply_live_tuning ran.
template<class Transport> bool update_live_tuning(Transport& bridge,Tuning& applied,const Tuning& next,const std::atomic<bool>& stop,
    const DriveReserve& r={}) {
    const auto held=within_drive(hold_roll_centre(applied,next),r);
    if (same_tuning(held,applied)) return false;
    apply_live_tuning(bridge,applied,held,stop,r);
    return true;
}
// How a force-feel or spring session that was not refused ended. A failure
// before any motor ran keeps the reference: force feel is unavailable but the
// yoke still flies. After a motor ran it is a controller fault.
struct SessionEnd {bool keep_reference=true,fault=false,force_unavailable=false;};
inline SessionEnd classify_session_end(bool failed,bool paused,bool stopping,bool motor_enabled) {
    const bool real=failed && !paused && !stopping;
    return SessionEnd{!failed || paused || !motor_enabled,real && motor_enabled,real && !motor_enabled};
}
// Force feel refused before any write, with both motors confirmed disabled:
// the reference, and so pitch and roll, are left exactly as they were.
inline void record_force_refusal(Snapshot& s,bool flight,const std::string& why) {
    s.reference_active=false;s.loading_active=false;
    if (flight) {
        s.force_unavailable="Force feel unavailable - switch the yoke off and on";
        s.stop_reason="The drives' settings were not what the plugin expects ("+why+"), so it changed nothing. Switch the yoke off and on to reset them, then Retry.";
        s.stop_reason_short=why;s.retry_helps=false;
    }
    s.loading_result="Force feel refused, nothing changed: "+why;
}
// A request that failed. A refusal (changed protection limits, unknown mode)
// comes before any write or motion, so pitch and roll stay as they were and
// only a switch-on clears it. Any other failure loses the reference.
inline void record_request_failure(Snapshot& s,bool refused,const std::string& result) {
    s.reference_active=false;s.loading_active=false;
    if (!refused) s.session_reference=false;
    s.power_cycle_needed=refused;++s.failures;s.loading_fault=true;s.loading_result=result;
}
// After motors stop: pitch's constant force cleared first, whatever the checks
// or roll then say; then the resting settings, never a copy of anyone else's,
// and a model session's cut-off put back. (A centring's own resting settings
// clear it after the checks instead, so a refusal there still writes nothing.)
template<class Transport> bool rest_after_stop(Transport& bridge,std::string* detail=nullptr,std::optional<std::uint32_t> offset_off={}) {
    const std::atomic<bool> cleanup{false};
    clear_pitch_force(bridge);
    try {rest_controllers(bridge,cleanup,offset_off);return true;}
    catch (const std::exception& e) {if (detail) *detail=e.what();return false;}
}
// After a controller fault, motors off: remove any reference request (a
// switch-on can leave it set), put the resting settings in place and clear the
// fault without enabling either drive. Refuses, untouched, only what
// check_controllers refuses.
template<class Transport> void recover_controllers(Transport& bridge,const std::atomic<bool>& stop) {
    check_controllers(bridge,stop,false);
    const std::array<std::uint16_t,2> shutdown_words{{6,6}},shutdown_modes{{spring_mode(),spring_mode()}};
    if (!stop_reference(bridge,shutdown_words,shutdown_modes)) throw std::runtime_error("Recovery could not confirm both motors disabled");
    for (unsigned node=1;node<=2;++node) write_rest(bridge,node,stop);
    if (active_drive().rest.pitch_cutoff>0) ensure_pitch(bridge,manual_offset_off,static_cast<std::uint32_t>(active_drive().rest.pitch_cutoff),stop);
    write_rest_mode(bridge,stop);
    // Do not erase the error-history object or set the initialization bit.
    for (unsigned node=1;node<=2;++node) {
        constexpr std::uint16_t word=6;
        bridge.ref_write(node,{controlword,word,100},stop);
        bridge.ref_write(node,{controlword,static_cast<std::uint16_t>(word|0x80u),100},stop);
        bridge.ref_write(node,{controlword,word,100},stop);
        if (bridge.read(node,Object::status,stop)&0x000c) throw std::runtime_error("Controller fault did not clear with motors disabled");
    }
}
}
