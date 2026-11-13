#pragma once
// The promo film's frames for the preview harness (panel_preview.cpp's
// `frames` mode and the feel page's demo): X-Plane's stand-in. The film's
// timeline (docs/brunner/video/promo/timeline.mjs) writes each frame's flight
// conditions as the site's presets (elevator.html's and aileron.html's
// sliders) and the yoke where the site's model left it. Here those conditions
// become what X-Plane would report to the plugin, by the conversions the
// timeline's MIRROR block makes (docs/brunner/video/timeline.mjs, the site's
// own lines): the air over the tail with the propeller's wash, the trim
// wheel's fraction, the tail's angle and the load factor from the elevator
// page's flight chain (elevator-model.js's Flight), and each wing's air and
// angle from the roll rate, sideslip and gust (rollState). The plugin's own
// solvers (solve_pitch, solve_roll) then take it from there.
//
// Preview and demo only: nothing in the plugin includes this. In the aircraft
// X-Plane computes all of it (decision 4); the plugin computes no air.
#include "ui/view/hud.hpp"
#include "json.hpp"
#include <cmath>
#include <optional>
#include <string>

namespace brunner::preview {
inline constexpr double pi=3.14159265358979323846,deg=pi/180,kt_ms=0.514444,rho=1.225,gravity=9.80665;
// The plugin's defaults (flight.hpp's pitch_force_units_per_lb and roll_torque_units_per_nm).
inline constexpr int units_per_lb=222,units_per_nm=1000;
// The sliders the timeline keys: pitch {n %, ground, kt, throttle %, trim %},
// roll {kt, throttle %, aoa °, rollRate °/s, beta °, gust °, stallL %, stallR %, dq %}.
struct PitchConditions {double n=100;bool ground=false;double kt=0,throttle=0,trim=0;};
struct RollConditions {double kt=0,throttle=0,aoa=0,roll_rate=0,beta=0,gust=0,stall_left=0,stall_right=0,dq=0;};

// Air pressure over the tail, Pa: airspeed plus the propeller's wash, 30 m/s
// at full power parked, gone by 80 m/s (Elevator.tailQ). The ailerons see 5
// m/s of it (Aileron.wingQ). throttle 0..1.
inline double washed_pa(double kt,double throttle,double wash) {
    const double v=kt*kt_ms,w=wash*throttle*std::max(0.0,1-v/80);
    return .5*rho*(v*v+w*w);
}
inline double tail_pa(double kt,double throttle) {return washed_pa(kt,throttle,30);}
inline double wing_pa(double kt,double throttle) {return washed_pa(kt,throttle,5);}
inline double pa_kt(double pa) {return std::sqrt(2*std::max(0.0,pa)/rho)/kt_ms;}

// elevator-model.js's Flight.of with no pitch rate beyond a steady pull's and
// no tailwind (the timeline sets both to 0): the tail's angle as X-Plane would
// report it and the load factor reached. A C172S at 2550 lb, flaps up.
struct TailFlight {double alpha_rad=0,n=1,q=0;};
inline TailFlight tail_flight(double kt,double throttle,double n_asked,bool ground) {
    constexpr double weight=11343,area=16.17,span=11.0,a2d=6.0,e=0.8,alpha0=-2.1*deg,stall_kt=53;
    constexpr double width=4*deg,post_lift=1.15,max_alpha=24*deg,downwash=1.085,incidence=-2.5*deg,tail_arm=4.5;
    constexpr double ground_attitude=1.5*deg,power=134000,prop=0.75,static_thrust=2400,min_kt=35;
    const double ar=span*span/area,slope=a2d/(1+a2d/(pi*e*ar));
    const double cl_max=weight/(.5*rho*std::pow(stall_kt*kt_ms,2)*area),critical=alpha0+cl_max/slope;
    const auto stalled_share=[&](double a) {return std::clamp((a-critical)/width,0.0,1.0);};
    const auto lift=[&](double a) {const double x=stalled_share(a);return (1-x)*slope*(a-alpha0)+x*post_lift*std::sin(2*a);};
    const auto thrust=[&](double v) {return throttle*std::min(static_thrust,prop*power/std::max(v,1.0));};
    const double n=std::isfinite(n_asked)?n_asked:1,v=std::max(kt,0.0)*kt_ms;
    const auto parked=[&]() {
        const double aw=ground_attitude,cl=slope*(aw-alpha0),eps=downwash*2*cl/(pi*ar);
        return aw+incidence-eps;
    };
    if (ground) return {parked(),1,tail_pa(std::max(kt,0.0),throttle)};
    const double floor=min_kt*kt_ms,vv=std::max(v,floor),qw=.5*rho*vv*vv,t=thrust(vv);
    double aw=alpha0+n*weight/(qw*area)/slope;
    for (int i=0;i<20;++i) aw=std::clamp(alpha0+(n*weight-t*std::sin(std::clamp(aw,-max_alpha,max_alpha)))/(qw*area)/slope,-max_alpha,max_alpha);
    const double wing_stalled=stalled_share(aw),cl=lift(aw);
    const double reached=wing_stalled>0 || aw>=max_alpha?(cl*qw*area+t*std::sin(aw))/weight:n;
    const double eps=downwash*2*cl/(pi*ar);
    const double pitch_rate=gravity*(reached-1)/vv,pitch_share=pitch_rate*tail_arm/vv;
    const double tail=aw+incidence-eps+pitch_share,q=tail_pa(kt,throttle);
    if (v<floor) {
        const double slow=std::pow(v/floor,2),still=parked();
        return {still+slow*(tail-still),1+slow*(reached-1),q};
    }
    return {tail,reached,q};
}
// What the plugin reads for pitch: the timeline's pitchState as ElevatorInputs.
inline ElevatorInputs pitch_inputs(const PitchConditions& c) {
    const auto f=tail_flight(c.kt,c.throttle/100,c.n/100,c.ground);
    return {c.trim/100,f.q,f.n,f.alpha_rad/deg,0};
}
// What the plugin reads for roll: the timeline's rollState, each wing's air,
// its angle from the roll rate (p·y/V, y 4.3 m), sideslip through 1.73° of
// dihedral and a gust on the left wing, and its stalled share.
inline AileronInputs roll_inputs(const RollConditions& c) {
    const double v=std::max(5.0,c.kt*kt_ms),dp=c.roll_rate*deg*4.3/v,db=c.beta*deg*(1.73*deg),q=wing_pa(c.kt,c.throttle/100);
    return {{q*(1+c.dq/100),(c.aoa*deg-dp-db+c.gust*deg)/deg,c.stall_left/100},{q,(c.aoa*deg+dp+db)/deg,c.stall_right/100}};
}

// A frame of the timeline's frames.json, as far as the plugin's graphics need it.
struct FilmFrame {
    int i=0;double t=0;std::string seg;
    std::optional<PitchConditions> pitch;double pitch_counts=0; // the yoke, the drive's counts forward positive
    std::optional<RollConditions> roll;double wheel_deg=0;      // the wheel, right positive
    std::string tablet;                                         // the feel page's scenario, if the frame names one
};
inline double json_number(const Json* j,double fallback=0) {
    if (!j) return fallback;
    if (j->kind==Json::Kind::integer) return double(j->integer);
    if (j->kind==Json::Kind::real) return j->real;
    if (j->kind==Json::Kind::boolean) return j->boolean?1:0;
    return fallback;
}
inline FilmFrame film_frame(const Json& f) {
    FilmFrame out;
    out.i=int(json_number(f.get("i")));out.t=json_number(f.get("t"));
    if (const auto* s=f.get("seg"); s && s->kind==Json::Kind::string) out.seg=s->text;
    if (const auto* s=f.get("tablet"); s && s->kind==Json::Kind::string) out.tablet=s->text;
    if (const auto* p=f.get("pitch")) if (const auto* c=p->get("C")) {
        PitchConditions pc;
        pc.n=json_number(c->get("n"),100);pc.ground=json_number(c->get("ground"))!=0;
        pc.kt=json_number(c->get("kt"));pc.throttle=json_number(c->get("throttle"));pc.trim=json_number(c->get("trim"));
        out.pitch=pc;out.pitch_counts=json_number(p->get("counts"));
    }
    if (const auto* r=f.get("roll")) if (const auto* c=r->get("C")) {
        RollConditions rc;
        rc.kt=json_number(c->get("kt"));rc.throttle=json_number(c->get("throttle"));rc.aoa=json_number(c->get("aoa"));
        rc.roll_rate=json_number(c->get("rollRate"));rc.beta=json_number(c->get("beta"));rc.gust=json_number(c->get("gust"));
        rc.stall_left=json_number(c->get("stallL"));rc.stall_right=json_number(c->get("stallR"));rc.dq=json_number(c->get("dq"));
        out.roll=rc;out.wheel_deg=json_number(r->get("deg"));
    }
    return out;
}
// The wheel's counts as the timeline takes them (Aileron.counts: rounded).
inline double wheel_counts(const AileronFigures& f,double wheel_deg) {
    return std::round(wheel_exact_counts(f,wheel_deg*radians_per_degree));
}
// The HUD's frame for a roll card, as the plugin's make_frame builds one in a
// force-feel session: the model, the wheel's encoder (the counts' negative),
// the session's solution there and the card from it.
inline monitor::Frame roll_frame(const RollModel& m,double counts) {
    monitor::Frame f;
    f.encoder[0]=int(std::lround(-counts));f.preview.roll=m;f.preview.aileron_source=AileronSource::surface;
    f.roll_solved=true;f.roll_solution=solve_roll(m,counts);
    f.roll=monitor::roll_card(m,f.roll_solution,true,true);
    return f;
}
// One frame's cards, solved by the plugin's code from the frame's conditions
// and the timeline's yoke: pitch at the yoke's counts, roll at the wheel's.
// Force feel on, in a session, following the model (the film's state).
struct FrameCards {
    std::optional<tail::View> pitch;
    std::optional<RollModel> roll;double wheel_counts=0;
};
inline FrameCards solve_frame(const ElevatorFigures& e,const AileronFigures& a,const FilmFrame& f) {
    FrameCards c;
    if (f.pitch) {
        tail::View v;v.figures=e;v.force_feel=v.session=true;
        v.inputs=pitch_inputs(*f.pitch);v.yoke_counts=f.pitch_counts;
        v.solution=solve_pitch(e,v.inputs,f.pitch_counts,units_per_lb);
        c.pitch=v;
    }
    if (f.roll) {
        RollModel m;m.figures=a;m.units_per_nm=units_per_nm;m.inputs=roll_inputs(*f.roll);
        c.roll=m;c.wheel_counts=wheel_counts(a,f.wheel_deg);
    }
    return c;
}
}
