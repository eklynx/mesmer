#pragma once
#include "loading.hpp"
#include <cmath>

namespace brunner {
// Buffet through the drive's own vibration generator (decision 19,
// docs/brunner/vibration.md). Switched off by default and untested with the
// motors on: the knob is the owner's, for the motors-on test.
struct BuffetKnob {bool on=false;int percent=10,period=80;};
inline bool valid_buffet_knob(const BuffetKnob& k) {
    return k.percent>=0 && k.percent<=100 && k.period>=static_cast<int>(buffet_period_min) && k.period<=static_cast<int>(buffet_period_max);
}
inline bool operator==(const BuffetKnob& a,const BuffetKnob& b) {return a.on==b.on && a.percent==b.percent && a.period==b.period;}
inline bool operator!=(const BuffetKnob& a,const BuffetKnob& b) {return !(a==b);}
// What buffets: a surface's stalled share of q*area, X-Plane's per-panel stall
// flags as aileron_side weights them (flight.hpp), counted only where the
// surface has real air (buffet_minimum_pa, about 30 kt) and its weighted
// angle of attack is past buffet_gate_deg either way, so a flag that flickers
// at a small angle, or on the ground, says nothing. The plugin's own choices;
// the owner may change them.
inline constexpr double buffet_minimum_pa=150,buffet_gate_deg=8;
// The share is held to eighths, so a write happens only when it moves an
// eighth: a few writes a stall, not one an update.
inline constexpr int buffet_levels=8;
inline double buffet_share(const AileronSide& s) {
    if (!std::isfinite(s.pa) || s.pa<buffet_minimum_pa || !std::isfinite(s.alpha_deg) || std::abs(s.alpha_deg)<buffet_gate_deg
        || !std::isfinite(s.stalled)) return 0;
    return std::clamp(s.stalled,0.0,1.0);
}
inline double buffet_share(const AileronSide& a,const AileronSide& b) {return std::max(buffet_share(a),buffet_share(b));}
// The amplitude asked for: the knob's percent of the ceiling times the share.
inline std::uint32_t buffet_units(const BuffetKnob& k,double share) {
    if (!k.on || !valid_buffet_knob(k) || !std::isfinite(share)) return 0;
    const double level=std::round(std::clamp(share,0.0,1.0)*buffet_levels)/buffet_levels;
    return static_cast<std::uint32_t>(std::lround(buffet_ceiling_units*(k.percent/100.0)*level));
}
// Buffet's share of the drive's budget (decision 16, tuning.hpp): spring plus
// constant force plus shake stays within drive_margin_units() on each axis.
// While the knob is on, the session reserves its configured maximum, percent
// of buffet_ceiling_units, on both axes; with it off, nothing.
inline DriveReserve buffet_reserve(const BuffetKnob& k) {
    if (!k.on || !valid_buffet_knob(k)) return {};
    const int units=static_cast<int>(std::lround(buffet_ceiling_units*(k.percent/100.0)));
    return {units,units};
}
// The room the spring line as written leaves under the margin, anywhere in
// the travel: what a shake may add at most.
inline std::uint32_t buffet_headroom(int strength,std::int32_t centre,std::int32_t bias,std::int32_t travel) {
    const double room=drive_margin_units()-worst_force_units(strength,centre,-double(bias),travel);
    return room>0?static_cast<std::uint32_t>(std::floor(room)):0u;
}
// What the flight asks of each axis, before the headroom.
struct BuffetTarget {BuffetKnob knob;std::uint32_t pitch=0,roll=0;};
// The drive side, owned by the reader thread. Nothing is written until the
// knob is on and a surface buffets; every write is bounded
// (vibration_write_allowed) and read back; clear() undoes whatever was set.
class BuffetDrive {
public:
    // One update, after the solvers' writes: pitch, then roll. session_modes
    // are the force modes the session wrote (reference_start: the spring mode
    // with bit 13), roll's first; bit 4 is toggled on exactly those.
    template<class Transport> void update(Transport& bridge,const BuffetTarget& t,const Tuning& applied,
        const std::array<std::uint16_t,2>& session_modes,const std::atomic<bool>& stop) {
        const bool on=t.knob.on && valid_buffet_knob(t.knob);
        const auto period=static_cast<std::uint32_t>(t.knob.period);
        const auto knob_reserve=buffet_reserve(t.knob);
        const auto most=[](int u) {return static_cast<std::uint32_t>(u);};
        axis(bridge,2,on?std::min({t.pitch,most(knob_reserve.pitch),buffet_headroom(applied.pitch_percent,applied.pitch_trim,applied.pitch_bias,pitch_travel_bound())}):0,
            period,session_modes[1],stop);
        axis(bridge,1,on?std::min({t.roll,most(knob_reserve.roll),buffet_headroom(applied.roll_percent,applied.roll_trim,0,roll_travel_bound())}):0,
            period,session_modes[0],stop);
    }
    // The reserve a session's spring writes keep this update: the knob's, and
    // never less than a shake still on the drive, so a knob switched off or
    // turned down frees its room only once the shake is cleared.
    DriveReserve reserve(const BuffetKnob& k) const {
        auto r=buffet_reserve(k);
        r.roll=std::max(r.roll,static_cast<int>(axes_[0].force));r.pitch=std::max(r.pitch,static_cast<int>(axes_[1].force));
        return r;
    }
    // With both motors stopped (the stop writes the spring mode without bit
    // 13), best effort, never throwing: enable bit off, amplitude zero, the
    // time as found, each read back. True when the bit and the amplitude are
    // off on every axis buffet touched. A found time outside 10..140 is never
    // written back (the write bounds refuse it); time_unrestored() says so.
    template<class Transport> bool clear(Transport& bridge) noexcept {
        const std::atomic<bool> cleanup{false};
        bool all=true;
        for (unsigned node=1;node<=2;++node) {
            auto& a=axes_[node-1];
            if (!a.touched) continue;
            // Each step on its own, so one refusal does not leave the others.
            bool done=true;
            try {enable(bridge,node,false,spring_mode,cleanup);} catch (...) {done=false;}
            try {write(bridge,node,vibration_slow_force,0,cleanup);} catch (...) {done=false;}
            // The time as found, when the write bounds allow that value;
            // otherwise it stays as buffet left it, and is reported.
            const bool restorable=a.found>=buffet_period_min && a.found<=buffet_period_max;
            bool restored=a.period==a.found;
            if (!restored && restorable) {
                try {write(bridge,node,vibration_slow_time,a.found,cleanup);restored=true;} catch (...) {}
            }
            if (restored) unrestored_[node-1].reset(); else if (!restorable) unrestored_[node-1]=a.found;
            // Done when the bit and the amplitude are off and the time is back
            // or can never be put back; a failed step is owed to the next clear.
            if (done && (restored || !restorable)) a=Axis{}; else all=false;
        }
        return all;
    }
    bool dirty() const {return axes_[0].touched || axes_[1].touched;}
    // Roll, pitch: the amplitude last read back.
    std::array<std::uint32_t,2> units() const {return {{axes_[0].force,axes_[1].force}};}
    // Roll, pitch: a time buffet changed and could not put back, with the
    // value it found there. Kept until a later clear restores it.
    const std::array<std::optional<std::uint32_t>,2>& time_unrestored() const {return unrestored_;}
private:
    struct Axis {std::uint32_t force=0,period=0,found=0;bool enabled=false,touched=false;};
    std::array<Axis,2> axes_{};
    std::array<std::optional<std::uint32_t>,2> unrestored_{};
    template<class Transport> void axis(Transport& bridge,unsigned node,std::uint32_t want,std::uint32_t period,std::uint16_t session_mode,
        const std::atomic<bool>& stop) {
        auto& a=axes_[node-1];
        if (want==0) {
            if (!a.touched) return; // never on: nothing to write
            if (a.enabled) enable(bridge,node,false,session_mode,stop);
            if (a.force!=0) write(bridge,node,vibration_slow_force,0,stop);
            return;
        }
        if (!a.touched) {
            const auto found=bridge.ref_read(node,vibration_slow_time,stop);
            a.touched=true;a.period=found;
            // A time an earlier session could not put back is still the one to restore.
            a.found=unrestored_[node-1]?*unrestored_[node-1]:found;
        }
        if (a.period!=period) write(bridge,node,vibration_slow_time,period,stop);
        if (a.force!=want) write(bridge,node,vibration_slow_force,want,stop);
        if (!a.enabled) enable(bridge,node,true,session_mode,stop);
    }
    template<class Transport> void write(Transport& bridge,unsigned node,Register reg,std::uint32_t value,const std::atomic<bool>& stop) {
        checked_loading_write(bridge,node,{reg,value,0},stop);
        auto& a=axes_[node-1];
        if (vibration_same(reg,vibration_slow_force)) a.force=value; else a.period=value;
    }
    // Bit 4 of 0x2101:01 alone, as CLS2Sim sets and clears it, on the mode
    // the plugin itself wrote: anything else there is refused before writing,
    // and the word written is computed from the known mode, never copied back.
    template<class Transport> void enable(Transport& bridge,unsigned node,bool on,std::uint16_t expected,const std::atomic<bool>& stop) {
        const auto mode=static_cast<std::uint16_t>(bridge.ref_read(node,force_mode,stop));
        if (static_cast<std::uint16_t>(mode&~vibration_enable_bit)!=expected || (expected&vibration_enable_bit)!=0)
            throw std::runtime_error("Unknown controller spring mode for buffet");
        const auto next=static_cast<std::uint16_t>(on?(expected|vibration_enable_bit):expected);
        if (next!=mode) {
            bridge.ref_write(node,{force_mode,next,0},stop);
            const auto actual=bridge.ref_read(node,force_mode,stop);
            if (actual!=next)
                throw DriveWriteFailed("Readback mismatch node="+std::to_string(node)+" register=2101:1 expected="+std::to_string(next)
                    +" actual="+std::to_string(actual),node,{force_mode,next,0},{},actual);
        }
        axes_[node-1].enabled=on;
    }
};
}
