#pragma once
#include "force/tuning.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <utility>
#include <limits>
#include <optional>

namespace brunner {
// The elevator model (decision 15), the same as elevator.html's
// <script id="model">; elevator_vectors.json holds the two together. The
// figures are the aircraft's profile (BrunnerDirect.profiles.json, declared in
// ansible/brunner.yml): code holds none (decision 5).
//
// The elevator about its hinge: its weight moment W*n*cos(d) against the
// air's K(q)*(d - d_trim), K(q) = q*area*chord*|Ch_d|. Positive d is trailing
// edge down, the yoke forward. The trim tab floats the elevator tab_ratio of
// the tab's angle the other way. One gearing figure both ways, so the yoke's
// inches differ either side of neutral; the drive's counts are 12000 to
// either stop, forward positive.
//
// The tail's angle of attack and stall (decision 17), elevator.html's
// TailAir: X-Plane's angle over the profile's tail sections floats the
// elevator, -q*area*chord*Ch_a*alpha. Two different stalls, kept apart:
// - the tail-plane's, as a lifting surface: X-Plane's (element_is_stalled
//   over the tail sections, tail_stalled_xp), read, never recomputed; its
//   angle already carries load factor, pitch rate and downwash;
// - the elevator's own: the flow letting go of the surface deflected too far
//   against the air, where the tail's angle plus tau times the deflection
//   passes stall_deg, the share ramping to 1 over stall_width_deg
//   (elevator_stalled). Always computed here: X-Plane computes nothing about
//   hinge moments. stall_deg 0 turns it off.
// The hinge moment keeps stall_factor of the air's moments on the larger of
// the two shares (hinge_stalled). With air from behind the deflection's
// moment turns over, k_rev of it, so a free elevator runs to a stop; it turns
// over smoothly with the air's direction (air_turn), never across a hair's
// width at ±90°. With the defaults below, no angle and nothing stalled the
// model is exactly the one before them (elevator_vectors.json).
struct ElevatorFigures {
    double area_m2=0,chord_m=0,hinge_slope_per_rad=0;
    double up_deg=0,down_deg=0,gearing_rad_per_in=0;
    double unbalance_inlb=0,tab_ratio=0,tab_up_deg=0,tab_down_deg=0;
    double alpha_slope_per_rad=0,stall_factor=1,tau=0,stall_deg=0,stall_width_deg=0,k_rev=0;
};
inline bool valid_figures(const ElevatorFigures& e) {
    const auto in=[](double v,double lo,double hi) {return std::isfinite(v) && v>=lo && v<=hi;};
    return in(e.area_m2,0.05,20) && in(e.chord_m,0.02,3) && in(e.hinge_slope_per_rad,0,5)
        && in(e.up_deg,1,60) && in(e.down_deg,1,60) && in(e.gearing_rad_per_in,0.01,2)
        && in(e.unbalance_inlb,-200,200) && in(e.tab_ratio,0,3) && in(e.tab_up_deg,0,60) && in(e.tab_down_deg,0,60)
        && in(e.alpha_slope_per_rad,-2,2) && in(e.stall_factor,0,1) && in(e.tau,0,1) && in(e.stall_deg,0,60)
        && in(e.stall_width_deg,0,30) && in(e.k_rev,0,3);
}
// trim: X-Plane's elevator trim, -1 nose down to 1 nose up. tail_pa: dynamic
// pressure over the tail. load_factor: normal g. tail_alpha_deg: X-Plane's
// angle of attack over the tail's panels, q*area weighted, as read (beyond
// ±90° the air is from behind); tail_stalled_xp: X-Plane's stalled share of the
// panels' q*area, 0..1. A missing value counts as centred trim, still air,
// level flight, no angle and nothing stalled.
struct ElevatorInputs {double trim=0,tail_pa=0,load_factor=1,tail_alpha_deg=0,tail_stalled_xp=0;};
inline constexpr double radians_per_degree=3.14159265358979323846/180,newton_metres_per_inlb=0.112985;
inline double full_travel_counts() {return active_drive().pitch_travel_counts;}
// The model's own constants, as the ailerons' (decision 5's amendment): under
// this pressure over the tail its angle and stall say nothing; the angle is
// clamped before it enters the float.
inline constexpr double tail_minimum_pa=2.5,tail_alpha_clamp_deg=30;
// The deflection's share of its moment by the air's direction, raw the angle
// as X-Plane reports it (rad): whole within the angle clamp; past it
// cos(raw)/cos(clamp), held to ±1, its reversed half times k_rev, so 0 with
// the air across the surface (±90°) and -k_rev from ±150° (decision 17's air
// from behind). ahead is the same without its reversed half: it fades the
// elevator's own stall, a stall of air from ahead. Continuous in the angle,
// so a noisy angle near ±90° cannot throw the rest from trim to a stop.
// elevator.html's and aileron.html's turnOf.
// Over the gate the turn's departure from 1 fades in with the pressure, from
// none at the gate to all of it at twice the gate, so air from behind does
// not step in where the angle starts to count (q the dynamic pressure, gate
// the minimum it counts at).
struct AirTurn {double turn=1,ahead=1;};
inline AirTurn air_turn(double raw,double clamp_rad,double k_rev,double q,double gate) {
    if (std::abs(raw)<=clamp_rad) return {};
    const double c=std::clamp(std::cos(raw)/std::cos(clamp_rad),-1.0,1.0),turn=c>=0?c:k_rev*c;
    const double share=std::clamp((q-gate)/gate,0.0,1.0);
    return {share>=1?turn:1+(turn-1)*share,c>0?c:0.0};
}
struct ElevatorState {
    double up=0,down=0,trim_rad=0,k=0,w=0,gearing=0;
    // The tail's air as the model takes it (tail_flow).
    double qsc=0,cha=0,keep=1,k_rev=0,tau=0,stall=0,width=0,raw=0,tail_stalled_xp=0,turn=1,ahead=1;
    // reversed: the air mostly from behind (reported; nothing in the moments
    // steps on it); beyond: past the angle clamp, where the rest is scanned.
    bool on=false,reversed=false,surface=false,beyond=false,stall_set=false; // stall_set: the figures give the section's own stall
};
inline ElevatorState elevator_state(const ElevatorFigures& e,const ElevatorInputs& in) {
    const double t=std::isfinite(in.trim)?std::clamp(in.trim,-1.0,1.0):0.0;
    const double q=std::isfinite(in.tail_pa) && in.tail_pa>0?in.tail_pa:0.0;
    const double n=std::isfinite(in.load_factor)?std::clamp(in.load_factor,-1.0,4.0):1.0;
    ElevatorState s;
    s.up=e.up_deg*radians_per_degree;s.down=e.down_deg*radians_per_degree;
    const double tab=t>=0?t*e.tab_down_deg*radians_per_degree:t*e.tab_up_deg*radians_per_degree;
    s.trim_rad=std::clamp(-e.tab_ratio*tab,-s.up,s.down);
    s.k=q*e.area_m2*e.chord_m*e.hinge_slope_per_rad;
    s.w=e.unbalance_inlb*newton_metres_per_inlb*n;
    s.gearing=e.gearing_rad_per_in;
    // The tail's angle and stall: under the gate they say nothing; beyond
    // ±90° the air is from behind, and the angle is taken against it.
    constexpr double pi=3.14159265358979323846;
    s.qsc=q*e.area_m2*e.chord_m;s.cha=e.alpha_slope_per_rad;s.keep=std::clamp(e.stall_factor,0.0,1.0);s.k_rev=e.k_rev;
    s.on=q>=tail_minimum_pa;
    const double raw=std::isfinite(in.tail_alpha_deg)?in.tail_alpha_deg*radians_per_degree:0.0;
    s.reversed=s.on && std::abs(raw)>pi/2;
    const double limit=tail_alpha_clamp_deg*radians_per_degree;
    const auto turn=s.on?air_turn(raw,limit,e.k_rev,q,tail_minimum_pa):AirTurn{};
    s.turn=turn.turn;s.ahead=turn.ahead;s.beyond=s.on && std::abs(raw)>limit;
    s.raw=s.reversed?(raw>0?pi-raw:-pi-raw):raw;
    s.tail_stalled_xp=s.on && std::isfinite(in.tail_stalled_xp)?std::clamp(in.tail_stalled_xp,0.0,1.0):0.0;
    s.stall_set=e.stall_deg>0 && e.stall_width_deg>0;
    s.surface=s.on && s.ahead>0 && s.stall_set;
    s.tau=e.tau;s.stall=e.stall_deg*radians_per_degree;s.width=e.stall_width_deg*radians_per_degree;
    return s;
}
// The tail's air at elevator angle d (rad): the gated, clamped angle; the
// tail-plane's stall (X-Plane's share); the elevator's own (alpha + tau*d past
// stall_deg); the larger of the two, which the hinge moment uses; eff, the
// share of the air's moments kept. Under the gate, as parked, X-Plane's flags
// and angle say nothing: it reports every element stalled at ±160-178° there.
struct TailFlow {double alpha=0,tail_stalled_xp=0,elevator_stalled=0,hinge_stalled=0,eff=1,effective=0;bool reversed=false;};
inline TailFlow tail_flow(const ElevatorState& s,double d) {
    TailFlow t;
    const double limit=tail_alpha_clamp_deg*radians_per_degree;
    t.alpha=s.on?std::clamp(s.raw,-limit,limit):0.0;
    t.tail_stalled_xp=s.tail_stalled_xp;t.reversed=s.reversed;
    if (s.surface) {
        t.effective=s.raw+s.tau*d;
        t.elevator_stalled=s.ahead*std::clamp((std::abs(t.effective)-s.stall)/s.width,0.0,1.0);
    }
    t.hinge_stalled=t.elevator_stalled>t.tail_stalled_xp?t.elevator_stalled:t.tail_stalled_xp;
    t.eff=1-(1-s.keep)*t.hinge_stalled;
    return t;
}
// The hinge moments at d, N·m trailing edge down positive.
struct ElevatorMoments {double weight=0,air=0,float_=0,net=0;};
inline ElevatorMoments elevator_moments(const ElevatorState& s,double rad) {
    const auto t=tail_flow(s,rad);
    ElevatorMoments m;
    m.weight=s.w*std::cos(rad);
    m.air=-(s.k*s.turn*t.eff*(rad-s.trim_rad));
    m.float_=-s.qsc*s.cha*t.eff*t.alpha;
    m.net=m.weight+m.air+m.float_;
    return m;
}
inline double elevator_rad(const ElevatorState& s,double counts) {
    const double x=std::clamp(counts,-full_travel_counts(),full_travel_counts())/full_travel_counts();
    return x>=0?x*s.down:x*s.up;
}
inline double elevator_counts(const ElevatorState& s,double rad) {
    return std::clamp(rad>=0?rad/s.down:rad/s.up,-1.0,1.0)*full_travel_counts();
}
// The force at the grip, lb, forward positive: the hinge moment through the gearing.
inline double grip_lb(const ElevatorState& s,double rad) {
    return elevator_moments(s,rad).net/newton_metres_per_inlb*s.gearing;
}
// With air from behind, or the section's stall somewhere in the travel: from
// where the elevator is (from_rad; trim when it is not finite), follow the net
// moment in quarter degrees to where it turns over (then bisect) or to a stop:
// the balance a free elevator reaches from there. Where the stall makes two
// balances, the elevator keeps to the one on its side of the unstable point
// between them, so the rest holds still as the air changes until that balance
// goes; from trim every update, the rest could hop between the two. With air
// from behind the balance is unstable, so it runs to the stop it is pushed
// toward. elevator.html's scan, step for step.
inline double elevator_scan_rad(const ElevatorState& s,double from_rad=std::numeric_limits<double>::quiet_NaN()) {
    const auto sign=[&](double d) {const double n=elevator_moments(s,d).net;return n>0?1:n<0?-1:0;};
    const double step=0.25*radians_per_degree;
    double d=std::clamp(std::isfinite(from_rad)?from_rad:s.trim_rad,-s.up,s.down);
    const int way=sign(d);
    if (way==0) return d;
    for (;;) {
        const double e=std::clamp(d+way*step,-s.up,s.down);
        if (e==d) return d;
        if (sign(e)!=way) {
            double lo=d,hi=e;
            for (int i=0;i<60;++i) {const double m=(lo+hi)/2;if (sign(m)==way) lo=m;else hi=m;}
            return (lo+hi)/2;
        }
        d=e;
    }
}
// Where weight, air and float balance, friction aside; a stop when one wins
// at every deflection. With the figures' own stall, or air past the clamp,
// it is the balance reached from from_rad (the scan's), as on the page, which
// scans whenever its stall is set. Otherwise the share kept is one figure
// across the travel, and the rest is found exactly as it always was (with no
// angle and nothing stalled, bit for bit).
inline double elevator_rest_rad(const ElevatorState& s,double from_rad=std::numeric_limits<double>::quiet_NaN()) {
    if (s.beyond || s.stall_set) return elevator_scan_rad(s,from_rad);
    const auto t=tail_flow(s,0);
    const double k=s.k*t.eff,w=s.w,f=-(-s.qsc*s.cha*t.eff*t.alpha);
    if (k<1e-9) { // no centring: weight and float alone, the side they push from neutral
        const double n=w-f;
        if (n>0) return w*std::cos(s.down)-f>=0?s.down:std::acos(f/w);
        if (n<0) return w*std::cos(s.up)-f<=0?-s.up:-std::acos(f/w);
        return s.trim_rad;
    }
    double rest=std::clamp(s.trim_rad+(w-f)/k,-s.up,s.down);
    for (int i=0;i<30;++i) {
        const double g=w*std::cos(rest)-k*(rest-s.trim_rad)-f,dg=-w*std::sin(rest)-k;
        rest=std::clamp(rest-g/dg,-s.up,s.down);
    }
    return rest;
}
// The yoke's position in the force models' counts (forward positive) from the pitch
// encoder (0x6064). Reading only: what is written to the controller's spring registers
// is converted by controller_counts (drive.hpp), which also uses the model's
// pitch_spring_sign, so this sign alone cannot mirror the spring. The CLS-E's encoder
// rises when the yoke is pulled (sign -1); the NG's falls (sign +1).
inline double drive_counts_from_encoder(std::int32_t encoder) {return active_drive().pitch_encoder_sign*static_cast<double>(encoder);}
// The drive's spring is the straight-line curve (model_curve in brunner/loading.hpp):
// 1000 units 12000 counts from its centre at 100 %.
inline double curve_units_per_count_percent() {return 1000.0/full_travel_counts()/100;}
inline constexpr double secant_minimum_counts=60;
// What the model wants at the yoke's position x0, and the drive's strength s,
// centre c and constant force b that give it. The drive's force is the line
// F(x) = b - k_s*(x - c), k_s = s*curve_units_per_count_percent(), forward
// positive; b and c enter only as b + k_s*c, so a line through one point has
// one free choice, which puts c on the rest and b to zero inside the travel.
// - Anchor: the model's rest, with the model's force there (zero inside the
//   travel, the weight's push at a stop), so the yoke rests exactly where the
//   model says.
// - Slope: the secant from the anchor to (x0, model force), at least 60
//   counts long, on x0's side; s the nearest whole percent from 20 to the
//   drive's ceiling. The force at x0 is then the model's within half a
//   percent of the curve times the displacement.
// - Ceiling (decision 16): no firmest setting; the strongest s at which the
//   line, constant force included, stays within the drive's margin over the
//   whole travel (force/tuning.hpp's drive_ceiling). The constant force is placed
//   first, since it holds the rest exactly (decision 15), and the spring takes
//   what is left. While there is a constant force the ceiling is also at
//   most strongest_with_constant_force_percent (100), where the firmware
//   still keeps it (0x211f). Past the ceiling the slope is held there (saturated): the
//   rest stays exact and the force is the most the drive gives. Below
//   20%, the drive's softest, it is held there too (floored): the rest stays
//   exact and the yoke is firmer than the model away from it.
struct PitchSolution {
    // The model, at x0.
    double weight_lb=0,stiffness_lb_per_in=0,centre_counts=0,rest_counts=0,force_lb=0,slope_lb_per_count=0;
    bool at_stop=false;
    // The drive: its registers, then its line at x0.
    int strength=0;std::int32_t centre=0,bias=0; // 0x2114, 0x2116 (forward positive), 0x211a (negative forward)
    double bias_lb=0,drive_force_lb=0,drive_slope_lb_per_count=0,drive_rest_counts=0; // bias_lb forward positive
    double shrink_ratio=std::numeric_limits<double>::quiet_NaN();
    // floored: the model wants a softer spring than the drive's softest (20%),
    // so away from the rest the yoke is firmer than the model; the rest stays exact.
    bool saturated=false,floored=false,bias_limited=false;
    // The drive's ceiling this update, percent: saturated is at it.
    int ceiling=0;
    // The tail at the yoke (x0): its angle as the model takes it (gated,
    // clamped, against the stream); the share the hinge moment uses, the
    // larger of the tail-plane's stall (X-Plane's) and the elevator's own
    // (alpha + tau*d); air from behind; the float at the grip, lb forward positive.
    double tail_alpha_deg=0,hinge_stalled=0,tail_stalled_xp=0,elevator_stalled=0,float_lb=0;
    bool tail_reversed=false;
};
inline PitchSolution solve_pitch(const ElevatorFigures& e,const ElevatorInputs& in,double x0,int units_per_lb) {
    const auto s=elevator_state(e,in);
    const double u=std::clamp(units_per_lb,1,100000);
    PitchSolution r;
    r.weight_lb=s.w/newton_metres_per_inlb*s.gearing;
    r.centre_counts=elevator_counts(s,s.trim_rad);
    // The rest a free elevator reaches from the yoke (from trim with no yoke).
    const double rest=elevator_rest_rad(s,std::isfinite(x0)?elevator_rad(s,x0):std::numeric_limits<double>::quiet_NaN());
    // The stiffness at the rest, the stalled share's there and the air's
    // direction's: through 0 with the air across the tail, negative from behind.
    const auto at_rest=tail_flow(s,rest);
    r.stiffness_lb_per_in=s.k*s.turn*at_rest.eff/newton_metres_per_inlb*s.gearing*s.gearing;
    r.rest_counts=elevator_counts(s,rest);
    r.at_stop=std::abs(r.rest_counts)>=full_travel_counts()-0.5;
    const double anchor_lb=r.at_stop?grip_lb(s,rest):0.0;
    const double x=std::isfinite(x0)?std::clamp(x0,-full_travel_counts(),full_travel_counts()):r.rest_counts;
    r.force_lb=grip_lb(s,elevator_rad(s,x));
    {
        const auto at_yoke=tail_flow(s,elevator_rad(s,x));
        r.tail_alpha_deg=at_yoke.alpha/radians_per_degree;r.hinge_stalled=at_yoke.hinge_stalled;
        r.tail_stalled_xp=at_yoke.tail_stalled_xp;r.elevator_stalled=at_yoke.elevator_stalled;r.tail_reversed=at_yoke.reversed;
        r.float_lb=elevator_moments(s,elevator_rad(s,x)).float_/newton_metres_per_inlb*s.gearing;
    }
    double dx=x-r.rest_counts;
    if (std::abs(dx)<secant_minimum_counts) {
        const double side=dx>0?1.0:dx<0?-1.0:(r.rest_counts>0?-1.0:1.0);
        dx=side*secant_minimum_counts;
        if (std::abs(r.rest_counts+dx)>full_travel_counts()) dx=-dx;
    }
    r.slope_lb_per_count=(anchor_lb-grip_lb(s,elevator_rad(s,r.rest_counts+dx)))/dx;
    const double want=r.slope_lb_per_count*u/curve_units_per_count_percent();
    const double anchor=r.rest_counts;
    r.centre=static_cast<std::int32_t>(std::lround(anchor));
    // The constant force a strength would be sent with, as held below.
    const auto held_at=[&](int strength) {
        const double b=anchor_lb*u+strength*curve_units_per_count_percent()*(anchor-r.centre);
        return static_cast<std::int32_t>(std::lround(std::clamp(b,-double(max_pitch_bias()),double(max_pitch_bias()))));
    };
    r.ceiling=drive_ceiling(full_travel_counts(),[&](int strength) {return std::pair<double,double>{double(r.centre),double(held_at(strength))};});
    // With a constant force the strength stays where the firmware keeps it
    // (0x211f's cut-off, force/tuning.hpp): the push at a stop is never lost.
    while (r.ceiling>strongest_with_constant_force_percent && held_at(r.ceiling)!=0) --r.ceiling;
    r.saturated=want>r.ceiling+1e-9;
    r.floored=!std::isfinite(want) || want<softest_spring_percent-0.5;
    r.strength=spring_percent(want,r.ceiling);
    const double ks=r.strength*curve_units_per_count_percent();
    const double b=anchor_lb*u+ks*(anchor-r.centre);
    r.bias_limited=std::abs(b)>max_pitch_bias()+0.5;
    const auto held=held_at(r.strength);
    r.bias=-held;r.bias_lb=held/u;
    r.drive_force_lb=(held-ks*(x-r.centre))/u;
    r.drive_slope_lb_per_count=ks/u;
    r.drive_rest_counts=ks>0?std::clamp(r.centre+held/ks,-full_travel_counts(),full_travel_counts())
        :held>0?full_travel_counts():held<0?-full_travel_counts():double(r.centre);
    if (r.slope_lb_per_count>0) r.shrink_ratio=r.drive_slope_lb_per_count/r.slope_lb_per_count;
    return r;
}
// What a force-feel session solves pitch from, every update, with the
// yoke's position read in that update. `edit` counts the workbench's edits of
// the feel; a new one blends in (FeelBlend), nothing else does.
struct PitchModel {
    ElevatorFigures figures;ElevatorInputs inputs;
    int units_per_lb=222;
    std::uint64_t edit=0;
};
inline bool valid_pitch_model(const PitchModel& m) {
    return valid_figures(m.figures) && m.units_per_lb>=50 && m.units_per_lb<=1000;
}
inline PitchSolution solve_pitch(const PitchModel& m,double x0) {
    return solve_pitch(m.figures,m.inputs,x0,m.units_per_lb);
}
// The model's rest alone, counts forward positive: the one reached from the
// yoke at x0 counts (from trim when x0 is not finite).
inline double model_rest_counts(const ElevatorFigures& e,const ElevatorInputs& in,double x0=std::numeric_limits<double>::quiet_NaN()) {
    const auto s=elevator_state(e,in);
    return elevator_counts(s,elevator_rest_rad(s,std::isfinite(x0)?elevator_rad(s,x0):std::numeric_limits<double>::quiet_NaN()));
}
}
