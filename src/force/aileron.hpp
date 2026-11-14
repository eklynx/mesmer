#pragma once
#include "force/elevator.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>

namespace brunner {
// The aileron model (decision 15 for roll), the same as aileron.html's
// <script id="model">; aileron_vectors.json holds the two together. The
// figures are the aircraft's profile (BrunnerDirect.profiles.json, declared in
// ansible/brunner.yml): code holds none (decision 5).
//
// Each aileron about its hinge: q*area*chord*(-Ch_d*u + Ch_a*alpha), both
// slopes kept stall_factor's share on a stalled panel, plus its weight,
// -W*cos(u). u is that aileron's angle, trailing edge up positive; the
// linkage is a parabola through neutral and both stops (up_deg going up,
// down_deg going down, equal rates at neutral). With air from behind (an
// angle beyond ±90° with air over the gate; decision 17, 2026-11-09) the
// deflection's moment turns over, k_rev of it, so the aileron runs away from
// neutral to a stop, and the float takes the angle against the reversed
// stream: aileron.html's side(). It turns over smoothly with the angle
// (force/elevator.hpp's air_turn): 0 with the air across the aileron (±90°),
// -k_rev from ±150°, never across a hair's width. The wheel's torque is the two
// moments through the linkage, by virtual work, right positive; the drive's
// counts are 13000 to either stop, right positive.
struct AileronFigures {
    double area_m2=0,chord_m=0,hinge_slope_per_rad=0,alpha_slope_per_rad=0;
    double up_deg=0,down_deg=0,wheel_deg=0,rim_m=0;
    double unbalance_inlb=0,stall_factor=0,k_rev=0;
};
inline bool valid_figures(const AileronFigures& a) {
    const auto in=[](double v,double lo,double hi) {return std::isfinite(v) && v>=lo && v<=hi;};
    return in(a.area_m2,0.05,10) && in(a.chord_m,0.02,2) && in(a.hinge_slope_per_rad,0,5) && in(a.alpha_slope_per_rad,-2,2)
        && in(a.up_deg,1,60) && in(a.down_deg,1,60) && in(a.wheel_deg,10,180) && in(a.rim_m,0.05,0.5)
        && in(a.unbalance_inlb,-200,200) && in(a.stall_factor,0,1) && in(a.k_rev,0,3);
}
// The model's own constants, not the aircraft's (section 3 of
// docs/brunner/aileron-and-aoa-plan.md): a side or panel with less dynamic
// pressure than this says nothing (X-Plane's angle is noise with no air), and
// the angle is clamped before it enters the coefficients.
inline constexpr double aileron_minimum_panel_pa=2.5,aileron_alpha_clamp_deg=30;
inline constexpr double newtons_per_lbf=4.44822,metres_per_second_per_knot=0.514444,sea_level_density=1.225;
inline double roll_travel_counts() {return active_drive().roll_travel_counts;}
// One aileron's air as X-Plane reports it over its panels: dynamic pressure,
// Pa; angle of attack, degrees, weighted by each panel's q*area; the stalled
// share of that q*area, 0..1. A missing or non-finite value counts as none.
struct AileronSide {double pa=0,alpha_deg=0,stalled=0;};
struct AileronInputs {AileronSide left,right;};
// One aileron's hinge moment, N·m, trailing edge up positive.
struct AileronMoment {double air=0,float_=0,weight=0;};
inline AileronMoment aileron_moment(const AileronFigures& f,const AileronSide& p,double u) {
    const double q=p.pa>=aileron_minimum_panel_pa?p.pa:0.0;
    const double stalled=std::isfinite(p.stalled)?std::clamp(p.stalled,0.0,1.0):0.0;
    const double eff=1-(1-f.stall_factor)*stalled;
    constexpr double pi=3.14159265358979323846;
    const double clamp_rad=aileron_alpha_clamp_deg*radians_per_degree;
    const double raw=std::isfinite(p.alpha_deg)?p.alpha_deg*radians_per_degree:0.0;
    const bool reversed=q>0 && std::abs(raw)>pi/2;
    const double turn=q>0?air_turn(raw,clamp_rad,f.k_rev,q,aileron_minimum_panel_pa).turn:1.0;
    const double alpha=std::clamp(reversed?(raw>0?pi-raw:-pi-raw):raw,-clamp_rad,clamp_rad);
    const double qsc=q*f.area_m2*f.chord_m;
    return {-qsc*f.hinge_slope_per_rad*turn*eff*u,qsc*f.alpha_slope_per_rad*eff*alpha,
        -f.unbalance_inlb*newton_metres_per_inlb*std::cos(u)};
}
// The linkage at wheel angle t (rad, right positive): each aileron's angle and
// its rate with the wheel.
struct AileronAngles {double left=0,right=0,d_left=0,d_right=0;};
inline AileronAngles aileron_angles(const AileronFigures& f,double t) {
    const double up=f.up_deg*radians_per_degree,down=f.down_deg*radians_per_degree,wheel=f.wheel_deg*radians_per_degree;
    const double a=(up+down)/(2*wheel),b=(up-down)/(2*wheel*wheel);
    return {-a*t+b*t*t,a*t+b*t*t,-a+2*b*t,a+2*b*t};
}
// The wheel's torque, N·m right positive: each aileron's air, both weights.
struct WheelTorque {double left=0,right=0,weight=0,total=0;};
inline WheelTorque wheel_torque(const AileronFigures& f,const AileronInputs& in,double t) {
    const auto g=aileron_angles(f,t);
    const auto l=aileron_moment(f,in.left,g.left),r=aileron_moment(f,in.right,g.right);
    WheelTorque w;
    w.left=(l.air+l.float_)*g.d_left;w.right=(r.air+r.float_)*g.d_right;w.weight=l.weight*g.d_left+r.weight*g.d_right;
    w.total=w.left+w.right+w.weight;
    return w;
}
inline double wheel_nm(const AileronFigures& f,const AileronInputs& in,double t) {return wheel_torque(f,in,t).total;}
// N·m at the wheel to pounds at the grip.
inline double rim_lb(const AileronFigures& f,double nm) {return nm/f.rim_m/newtons_per_lbf;}
// No air over either aileron: nothing but the weights turns the wheel.
inline bool aileron_airless(const AileronInputs& in) {
    return !(in.left.pa>=aileron_minimum_panel_pa || in.right.pa>=aileron_minimum_panel_pa);
}
// Zero torque everywhere: the wheel rests where it is left.
inline bool aileron_indifferent(const AileronFigures& f,const AileronInputs& in) {
    return aileron_airless(in) && std::abs(f.unbalance_inlb)<1e-12;
}
// Where the torque is zero, friction aside, rad right positive; a stop when
// one sign wins everywhere. Pushed outward at both stops (air from behind, or
// a negative float slope) the wheel is bistable: from where it is (from_rad)
// it is followed, in quarter degrees, the way the torque turns it, to a stop
// or to a balance it meets first (then bisected), so a wheel at a stop stays
// there while that stop pushes outward, however hard the other pushes. With
// no wheel given, or no torque where it is, the harder-pushed stop, 0 on a
// tie. aileron.html's rest, step for step.
inline double wheel_rest_rad(const AileronFigures& f,const AileronInputs& in,double from_rad=std::numeric_limits<double>::quiet_NaN()) {
    if (aileron_indifferent(f,in)) return 0;
    const double w=f.wheel_deg*radians_per_degree;
    double lo=-w,hi=w;
    const double tlo=wheel_nm(f,in,lo),thi=wheel_nm(f,in,hi);
    if (tlo<0 && thi>0) {
        const auto sign=[&](double t) {const double n=wheel_nm(f,in,t);return n>0?1:n<0?-1:0;};
        double t=std::isfinite(from_rad)?std::clamp(from_rad,-w,w):0.0;
        const int way=std::isfinite(from_rad)?sign(t):0;
        if (way==0) return -tlo>thi?-w:thi>-tlo?w:0.0;
        for (;;) {
            const double e=std::clamp(t+way*0.25*radians_per_degree,-w,w);
            if (e==t) return t;
            if (sign(e)!=way) {
                double a=t,b=e;
                for (int i=0;i<60;++i) {const double m=(a+b)/2;if (sign(m)==way) a=m;else b=m;}
                return (a+b)/2;
            }
            t=e;
        }
    }
    if (tlo<0) return -w;
    if (thi>0) return w;
    for (int i=0;i<60;++i) {const double m=(lo+hi)/2;if (wheel_nm(f,in,m)>0) lo=m;else hi=m;}
    return (lo+hi)/2;
}
// Wheel angle (rad) and the roll drive's counts, 13000 at either stop.
inline double wheel_exact_counts(const AileronFigures& f,double t) {
    return std::clamp(t/(f.wheel_deg*radians_per_degree),-1.0,1.0)*roll_travel_counts();
}
inline double wheel_rad(const AileronFigures& f,double counts) {
    return std::clamp(counts,-roll_travel_counts(),roll_travel_counts())/roll_travel_counts()*(f.wheel_deg*radians_per_degree);
}
// The wheel's position in the force models' counts (right positive) from the roll
// encoder (0x6064), which turning right decreases (plugin.cpp's roll calibration is
// reversed). Reading only: writes to the spring registers go through controller_counts
// (drive.hpp), which also uses roll_spring_sign.
inline double roll_counts_from_encoder(std::int32_t encoder) {return active_drive().roll_encoder_sign*static_cast<double>(encoder);}
// The roll drive's spring is the straight-line curve (model_curve): 1000
// units 13000 counts from its centre at 100 %.
inline double roll_units_per_count_percent() {return 1000.0/roll_travel_counts()/100;}
// What the model wants at the wheel's position x0, and the strength s and
// centre c that give it; solve_pitch's way, with no constant force. The
// drive's torque is -k_s*(x - c), k_s = s*roll_units_per_count_percent().
// - Anchor: the model's rest (with the torque there when it is a stop).
// - Slope: the secant from the anchor to x0, at least 60 counts, on x0's
//   side; s the nearest whole percent from 20 to the drive's ceiling: no
//   firmest setting (decision 16), the strongest s at which the spring, with
//   the centre it would be sent with, stays within the drive's margin over
//   the whole travel (force/tuning.hpp's drive_ceiling). Past the ceiling
//   (saturated) the rest stays exact and the wheel is lighter than the model.
// - Below 20 % (floored), roll is force-exact (decision 15): the centre goes
//   where the 20 % spring gives the model's torque at x0, c = x0 + F(x0)/k_20,
//   so with no air the centre is the wheel itself and it stays where it is
//   left. With force_exact false the rest stays exact instead (pitch's rule).
struct RollSolution {
    // The wheel and the model there.
    double x=0,model_nm=0,rest_counts=0,slope_nm_per_count=0,want=0;
    // The drive: its registers, then its torque at x.
    int strength=0;std::int32_t centre=0; // 0x2114, 0x2116 (right positive)
    double drive_nm=0;
    bool saturated=false,floored=false,force_exact=false,centre_limited=false;
    // The drive's ceiling this update, percent: saturated is at it.
    int ceiling=0;
    // For the card: each aileron at x, trailing edge up positive, and the air
    // the model read for it; the wheel and its rest; torques at the grip.
    double left_deg=0,right_deg=0,wheel_deg=0,rest_deg=0,model_lb=0,drive_lb=0;
};
inline RollSolution solve_roll(const AileronFigures& f,const AileronInputs& in,double x0,double units_per_nm,bool force_exact) {
    const double u=std::clamp(units_per_nm,1.0,1e6);
    RollSolution s;
    // The rest the wheel reaches from x0 (bistable: the stop it is pushed toward).
    const double rest=wheel_rest_rad(f,in,std::isfinite(x0)?wheel_rad(f,x0):std::numeric_limits<double>::quiet_NaN());
    s.rest_counts=wheel_exact_counts(f,rest);
    const bool at_stop=std::abs(s.rest_counts)>=roll_travel_counts()-0.5;
    const double anchor_nm=at_stop?wheel_nm(f,in,rest):0.0;
    s.x=std::isfinite(x0)?std::clamp(x0,-roll_travel_counts(),roll_travel_counts()):s.rest_counts;
    double dx=s.x-s.rest_counts;
    if (std::abs(dx)<secant_minimum_counts) {
        const double side=dx>0?1.0:dx<0?-1.0:(s.rest_counts>0?-1.0:1.0);
        dx=side*secant_minimum_counts;
        if (std::abs(s.rest_counts+dx)>roll_travel_counts()) dx=-dx;
    }
    s.slope_nm_per_count=(anchor_nm-wheel_nm(f,in,wheel_rad(f,s.rest_counts+dx)))/dx;
    s.want=s.slope_nm_per_count*u/roll_units_per_count_percent();
    // The centre a strength above the floor is sent with, as below.
    const auto centre_at=[&](int strength) {
        const double c=std::clamp(s.rest_counts+anchor_nm*u/(strength*roll_units_per_count_percent()),-roll_travel_counts(),roll_travel_counts());
        return std::pair<double,double>{double(std::lround(c)),0.0};
    };
    s.ceiling=drive_ceiling(roll_travel_counts(),centre_at);
    s.saturated=s.want>s.ceiling+1e-9;
    s.floored=!std::isfinite(s.want) || s.want<softest_spring_percent-0.5;
    s.strength=spring_percent(s.want,s.ceiling);
    const double ks=s.strength*roll_units_per_count_percent();
    const double t=wheel_rad(f,s.x);
    s.model_nm=wheel_nm(f,in,t);
    s.force_exact=s.floored && force_exact;
    double c=s.force_exact?s.x+s.model_nm*u/ks:s.rest_counts+anchor_nm*u/ks;
    s.centre_limited=std::abs(c)>roll_travel_counts()+0.5;
    c=std::clamp(c,-roll_travel_counts(),roll_travel_counts());
    s.centre=static_cast<std::int32_t>(std::lround(c));
    s.drive_nm=-ks*(s.x-s.centre)/u;
    const auto g=aileron_angles(f,t);
    s.left_deg=g.left/radians_per_degree;s.right_deg=g.right/radians_per_degree;
    s.wheel_deg=t/radians_per_degree;s.rest_deg=rest/radians_per_degree;
    s.model_lb=rim_lb(f,s.model_nm);s.drive_lb=rim_lb(f,s.drive_nm);
    return s;
}
// What a force-feel session solves roll from, every update, with the wheel's
// position read in that update; `edit` as PitchModel's. Roll is force-exact
// below the floor (decision 15).
struct RollModel {
    AileronFigures figures;AileronInputs inputs;
    int units_per_nm=1000;
    std::uint64_t edit=0;
};
inline bool valid_roll_model(const RollModel& m) {
    return valid_figures(m.figures) && m.units_per_nm>=100 && m.units_per_nm<=10000;
}
inline RollSolution solve_roll(const RollModel& m,double x0) {
    return solve_roll(m.figures,m.inputs,x0,m.units_per_nm,true);
}
// A session's target: the tuning outside force feel, and each axis's model
// when a force-feel session follows it.
struct SessionTarget {Tuning tuning;std::optional<PitchModel> pitch;std::optional<RollModel> roll;};
// The tuning the solutions ask for; an axis without one keeps `base`'s.
inline Tuning solved_tuning(const Tuning& base,const PitchSolution& p) {
    Tuning t=base;t.pitch_percent=p.strength;t.pitch_trim=p.centre;t.pitch_bias=p.bias;return t;
}
inline Tuning solved_tuning(const Tuning& base,const RollSolution& r) {
    Tuning t=base;t.roll_percent=r.strength;t.roll_trim=r.centre;return t;
}
// The equivalent airspeed of a dynamic pressure, knots, for the card.
inline double pressure_kt(double pa) {
    return std::isfinite(pa) && pa>0?std::sqrt(2*pa/sea_level_density)/metres_per_second_per_knot:0.0;
}
}
