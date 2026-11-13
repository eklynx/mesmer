#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <stdexcept>
#include <string>
#include "brunner/drive.hpp"

namespace brunner {
enum class TuningField { roll_percent, pitch_percent, pitch_trim };
// The pitch drive's constant force, 0x211a, in the drive's force units (the
// same units as its 4500 protection limit); negative pushes the yoke forward.
// Bounded to half that limit. Only a force-feel session following the
// elevator model sends it (decision 15).
inline std::int32_t max_pitch_bias() {return active_drive().limit_units/2;}
// The softest spring strength (0x2114) the drives take. Measured motors off
// on 2026-11-08 (docs/brunner/spring-range-probe-20261108.jsonl): both refuse
// 0 to 19 (SDO abort 0x06000000) and take 20 and every value tried up to 1000.
// 20 is also Brunner's own configured minimum (AXIS_FORCE_MINSCALEFACTOR).
inline constexpr int softest_spring_percent=20;
// Each axis's spring centre, 0x2116, is bounded to its travel: pitch 12000
// counts either way, roll 13000 (node 1's soft limits, calibration-control.md).
inline std::int32_t pitch_travel_bound() {return static_cast<std::int32_t>(std::lround(active_drive().pitch_travel_counts));}
inline std::int32_t roll_travel_bound() {return static_cast<std::int32_t>(std::lround(active_drive().roll_travel_counts));}
// The drive's own ceiling (decision 16). Neither axis has a firmest setting:
// each update the strength may go as high as the drive can hold without its
// protection limit (0x211b and 0x6072, 4500 force units, read and never
// written) being reached anywhere in the travel. What a drive does when the
// limit is reached is unknown (it may fault, cut torque or hold), so the
// plugin never asks for more than drive_margin_units(): 500 units, 11 %, under
// it. That headroom also covers what is inferred rather than measured: the
// spring's 1000 units at full travel at 100 % (the straight-line curve,
// model_curve, whose point positions were never surveyed, extended in a
// straight line past its last point) and the constant force adding to it
// unit for unit. Both axes' springs give 10 units per percent at full travel.
// The margin is the whole budget for every force the plugin asks of a drive
// at once: anything else added on top of the line (a vibration, say) must
// come out of it, not out of the 500 under the protection limit.
inline int protection_limit_units() {return active_drive().limit_units;}
inline int drive_margin_units() {return active_drive().limit_units-active_drive().margin_units;}
// Pitch's constant-force cut-off, 0x211f, as a model session sets it: the
// strength (0x2114 percent) at which the firmware drops the constant force,
// 0x211a (brunner/loading.hpp). Raising it is untested, so a strength with a constant
// force stays below it: at most strongest_with_constant_force_percent.
inline constexpr std::uint32_t bias_offset_off=101;
inline constexpr int strongest_with_constant_force_percent=static_cast<int>(bias_offset_off)-1;
// Whether the drive keeps a constant force at this strength.
inline bool keeps_constant_force(int strength,std::int32_t bias) {return bias==0 || strength<=strongest_with_constant_force_percent;}
inline constexpr double spring_units_per_percent_at_travel=10;
// The strongest 0x2114 any write may carry: the margin with the centre
// mid-travel and no constant force (400 %). The drives took up to 1000 with
// the motors off (spring-range-probe-20261108.jsonl).
inline int strongest_spring_percent() {return static_cast<int>(drive_margin_units()/spring_units_per_percent_at_travel);}
// The most force the drive's line F(x) = held - k*(x - centre) gives
// anywhere in [-travel, travel]: at the stop farther from its zero. `held`
// is the constant force, the same sign as F (pitch's 0x211a negated); k is
// the spring, strength*10/travel units per count.
inline double worst_force_units(int strength,double centre,double held,double travel) {
    const double k=strength*spring_units_per_percent_at_travel/travel;
    return (std::max)(std::abs(held-k*(travel-centre)),std::abs(held-k*(-travel-centre)));
}
// What else a session asks of each drive at once, in force units, taken out
// of drive_margin_units() before the spring and constant force: buffet's shake
// (force/buffet.hpp) while its knob is on, else 0. At most max_drive_reserve_units(),
// so the softest spring (20 % gives at most 400 units, plus at most 2250 of
// constant force) always stays within what is left.
struct DriveReserve {int roll=0,pitch=0;};
inline int max_drive_reserve_units() {return drive_margin_units()-(static_cast<int>(2*softest_spring_percent*spring_units_per_percent_at_travel)+max_pitch_bias());}
inline bool valid_drive_reserve(int units) {return units>=0 && units<=max_drive_reserve_units();}
inline int drive_budget_units(int reserve) {return drive_margin_units()-(valid_drive_reserve(reserve)?reserve:max_drive_reserve_units());}
// The strongest whole percent, softest to strongest_spring_percent(), at which
// the line stays within the margin less `reserve`; `line(s)` gives the centre
// and constant force a solver would send with strength s. The softest is
// always within it. The solvers and their pages reserve nothing: they never
// see buffet, and a reserve lowers the strength after them (within_drive).
template<class Line> int drive_ceiling(double travel,Line line,int reserve=0) {
    for (int s=strongest_spring_percent();s>softest_spring_percent;--s) {
        const auto [centre,held]=line(s);
        if (worst_force_units(s,centre,held,travel)<=drive_budget_units(reserve)) return s;
    }
    return softest_spring_percent;
}
// The strength a solver sends for the spring it wants (percent): want
// rounded within the drive's softest and the ceiling; the softest for a
// non-finite want, never std::lround of a NaN, whose result is unspecified.
inline int spring_percent(double want,int ceiling) {
    if (!std::isfinite(want)) return softest_spring_percent;
    const int top=(std::max)(ceiling,softest_spring_percent);
    return static_cast<int>(std::clamp<long>(std::lround(std::clamp(want,double(softest_spring_percent),double(top))),softest_spring_percent,top));
}
struct Tuning {
    int roll_percent=40, pitch_percent=40;
    std::int32_t pitch_trim=0;
    std::int32_t pitch_bias=0;
    // Roll's spring centre, right positive; only a force-feel session following
    // the aileron model moves it from 0 (decision 15).
    std::int32_t roll_trim=0;
};
// Both strengths stay from 20, the drive's softest, even where a model wants
// a softer spring (force/elevator.hpp's and force/aileron.hpp's floored), to
// strongest_spring_percent(); and within the drive's margin over the whole travel, each axis with all its
// registers together: pitch's spring, centre and constant force; roll's
// spring and centre (decision 16).
inline bool within_margin(int strength,std::int32_t centre,std::int32_t bias,std::int32_t travel,int reserve=0) {
    return worst_force_units(strength,centre,-double(bias),travel)<=drive_budget_units(reserve);
}
inline bool valid_tuning(const Tuning& t,const DriveReserve& r={}) {
    return t.roll_percent>=softest_spring_percent && t.roll_percent<=strongest_spring_percent()
        && t.pitch_percent>=softest_spring_percent && t.pitch_percent<=strongest_spring_percent()
        && t.pitch_trim>=-pitch_travel_bound() && t.pitch_trim<=pitch_travel_bound() && t.pitch_bias>=-max_pitch_bias() && t.pitch_bias<=max_pitch_bias()
        && t.roll_trim>=-roll_travel_bound() && t.roll_trim<=roll_travel_bound()
        && within_margin(t.pitch_percent,t.pitch_trim,t.pitch_bias,pitch_travel_bound(),r.pitch)
        && within_margin(t.roll_percent,t.roll_trim,0,roll_travel_bound(),r.roll)
        && keeps_constant_force(t.pitch_percent,t.pitch_bias);
}
// A tuning made by mixing two within the margin (FeelBlend's blend, or roll's
// centre held inside its deadband while its strength moved) can sit over it:
// the force at a stop grows with strength times centre. Lowering a strength
// only ever lowers that axis's worst case towards its constant force, which
// at 20 % is always inside, so each strength comes down until its axis is
// within the margin. Lighter, never misplaced (decision 16).
inline Tuning within_drive(Tuning t,const DriveReserve& r={}) {
    while (t.pitch_percent>softest_spring_percent && (!within_margin(t.pitch_percent,t.pitch_trim,t.pitch_bias,pitch_travel_bound(),r.pitch)
        || !keeps_constant_force(t.pitch_percent,t.pitch_bias))) --t.pitch_percent;
    while (t.roll_percent>softest_spring_percent && !within_margin(t.roll_percent,t.roll_trim,0,roll_travel_bound(),r.roll)) --t.roll_percent;
    return t;
}
// The commissioning channel's springs (the light spring bench test) stay
// at 100 % or less: above it is a force-feel session's alone.
inline constexpr int commissioning_spring_percent=100;
inline int tuning_value(const Tuning& t,TuningField field) {
    switch (field) {
    case TuningField::roll_percent:return t.roll_percent;
    case TuningField::pitch_percent:return t.pitch_percent;
    case TuningField::pitch_trim:return t.pitch_trim;
    }
    throw std::invalid_argument("Unknown tuning field");
}
inline bool set_tuning(Tuning& t,TuningField field,int value) {
    auto next=t;
    switch (field) {
    case TuningField::roll_percent:
        if (value>commissioning_spring_percent) return false;
        next.roll_percent=value;break;
    case TuningField::pitch_percent:
        if (value<20 || value>commissioning_spring_percent) return false;
        next.pitch_percent=value;break;
    case TuningField::pitch_trim:
        if (value < -600 || value > 600) return false; // manual commissioning channel
        next.pitch_trim=value;break;
    default:return false;
    }
    if (!valid_tuning(next)) return false;
    t=next;return true;
}
// Roll's centre (0x2116 on node 1) is written only once the solution has
// moved it 20 counts or more, about 0.13 degrees of wheel and well inside the
// drive's own position error. Below the floor roll tracks the encoder and in
// flight its rest moves with angle of attack, so without this nearly every
// update would add a checked roll write, with its readback, to the loop pitch
// shares, and pitch would lose update rate (decision 15).
inline constexpr std::int32_t roll_centre_deadband=20;
inline Tuning hold_roll_centre(const Tuning& applied,Tuning next) {
    if (std::abs(static_cast<std::int64_t>(next.roll_trim)-applied.roll_trim)<roll_centre_deadband) next.roll_trim=applied.roll_trim;
    return next;
}
inline bool same_tuning(const Tuning& a,const Tuning& b) {
    return a.roll_percent==b.roll_percent && a.pitch_percent==b.pitch_percent && a.pitch_trim==b.pitch_trim && a.pitch_bias==b.pitch_bias
        && a.roll_trim==b.roll_trim;
}
// Preserve sub-integer scale increments at the normal 40 Hz poll rate. A stall
// never banks a large future jump. Percent values are controller percentages.
// The resting point moves at most 600 counts a second and the constant force
// at most 2000 units a second. This is the light spring bench test's; a
// force-feel session follows the elevator model with FeelBlend instead.
inline constexpr double resting_point_counts_per_second=600,pitch_bias_units_per_second=2000;
class TuningRamp {
    Tuning current_;
    double scale_credit_=0,trim_rate_;
public:
    explicit TuningRamp(Tuning current):current_(current),trim_rate_(resting_point_counts_per_second) {
        if (!valid_tuning(current)) throw std::invalid_argument("Invalid initial tuning");
    }
    Tuning advance(const Tuning& target,double seconds) {
        if (!valid_tuning(target) || !std::isfinite(seconds) || seconds<0)
            throw std::invalid_argument("Invalid live tuning update");
        const auto elapsed=(std::min)(seconds,0.05);
        if (target.roll_percent==current_.roll_percent && target.pitch_percent==current_.pitch_percent) scale_credit_=0;
        else scale_credit_=(std::min)(1.0,scale_credit_+elapsed*10.0);
        const auto step=static_cast<int>(std::floor(scale_credit_+1e-9));
        scale_credit_=(std::max)(0.0,scale_credit_-step);
        current_.roll_percent+=std::clamp(target.roll_percent-current_.roll_percent,-step,step);
        current_.pitch_percent+=std::clamp(target.pitch_percent-current_.pitch_percent,-step,step);
        const auto trim_step=static_cast<std::int32_t>(std::floor(elapsed*trim_rate_));
        current_.pitch_trim+=std::clamp(target.pitch_trim-current_.pitch_trim,-trim_step,trim_step);
        current_.roll_trim+=std::clamp(target.roll_trim-current_.roll_trim,-trim_step,trim_step);
        const auto bias_step=static_cast<std::int32_t>(std::floor(elapsed*pitch_bias_units_per_second));
        current_.pitch_bias+=std::clamp(target.pitch_bias-current_.pitch_bias,-bias_step,bias_step);
        return current_;
    }
};
// A force-feel session follows the model with no added lag: what the
// aircraft does reaches the drive in the update it is read. Only a jump the
// aircraft didn't cause blends in: force feel coming on (from the line the
// drive has, zero force where the yoke is), and an edit of the feel (figures,
// profile, units; `edit` counts them). Those go from
// the line the drive had to the solution over blend_seconds, following a
// moving target, so the motor doesn't snap. However long an update took (a
// stall, a slow USB round), it moves at most blend_step of the way: an
// eighth, so a blend is never fewer than eight updates.
inline constexpr double blend_seconds=0.5,blend_step=0.125;
class FeelBlend {
    Tuning from_,current_;
    double elapsed_=0;
    std::uint64_t edit_;
public:
    FeelBlend(Tuning drive,std::uint64_t edit):from_(drive),current_(drive),edit_(edit) {
        if (!valid_tuning(drive)) throw std::invalid_argument("Invalid initial tuning");
    }
    bool blending() const {return elapsed_<blend_seconds;}
    Tuning advance(const Tuning& target,std::uint64_t edit,double seconds) {
        if (!valid_tuning(target) || !std::isfinite(seconds) || seconds<0)
            throw std::invalid_argument("Invalid live tuning update");
        if (edit!=edit_) {edit_=edit;from_=current_;elapsed_=0;}
        elapsed_=(std::min)(blend_seconds,elapsed_+(std::min)(seconds,blend_seconds*blend_step));
        if (!blending()) return current_=target;
        const double w=elapsed_/blend_seconds;
        const auto blend=[w](double from,double to) {return static_cast<std::int32_t>(std::lround(from+w*(to-from)));};
        current_.roll_percent=blend(from_.roll_percent,target.roll_percent);
        current_.pitch_percent=blend(from_.pitch_percent,target.pitch_percent);
        current_.pitch_trim=blend(from_.pitch_trim,target.pitch_trim);
        current_.pitch_bias=blend(from_.pitch_bias,target.pitch_bias);
        current_.roll_trim=blend(from_.roll_trim,target.roll_trim);
        return current_=within_drive(current_);
    }
};
// Where a force-feel session's drive starts, motors still off: both axes at
// the softest spring, pitch with no constant force, each centred where the
// yoke is, so no force on it.
inline Tuning session_start(Tuning requested,double yoke_counts,double wheel_counts) {
    const auto at=[](double counts,std::int32_t bound) {
        return static_cast<std::int32_t>(std::lround(std::clamp(std::isfinite(counts)?counts:0.0,-double(bound),double(bound))));
    };
    requested.pitch_percent=softest_spring_percent;requested.pitch_bias=0;requested.pitch_trim=at(yoke_counts,pitch_travel_bound());
    requested.roll_percent=softest_spring_percent;requested.roll_trim=at(wheel_counts,roll_travel_bound());
    return requested;
}
// What a force-feel session writes before its motors start, and where it then
// centres each axis: session_start's values with the trims held back, since
// the spring is written at zero trim (prepare_loading) and the centres after it.
// The one source of a session's starting values; the Windows session uses it.
struct SessionLoad {Tuning written;std::int32_t centre=0,roll_centre=0;};
inline SessionLoad session_load(const Tuning& requested,double yoke_counts,double wheel_counts) {
    SessionLoad l{session_start(requested,yoke_counts,wheel_counts)};
    l.centre=l.written.pitch_trim;l.written.pitch_trim=0;
    l.roll_centre=l.written.roll_trim;l.written.roll_trim=0;
    return l;
}
// A tuning in words, for a refusal's reason.
inline std::string tuning_words(const Tuning& t) {
    return "roll spring "+std::to_string(t.roll_percent)+"% centred at "+std::to_string(t.roll_trim)+" counts, pitch spring "
        +std::to_string(t.pitch_percent)+"%, trim "+std::to_string(t.pitch_trim)+" counts, constant force "+std::to_string(t.pitch_bias);
}
}
