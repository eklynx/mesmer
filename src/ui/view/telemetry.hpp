#pragma once
#include "force/flight.hpp"
#include <vector>
#include <limits>

namespace brunner::monitor {
enum class Mode { input, reference, bench, flight, paused, fault, disconnected };
// The aileron model for the in-sim card (decision 11's HUD): each aileron, the
// wheel and the two torques at the grip. From the session's solver while
// force feel follows the model (`engaged`), else the same solver run on the
// flight's target for the wheel where it is, nothing sent to the drive.
struct AileronCard {
    double deflection_deg=0; // the model's aileron at the wheel, trailing edge up positive
    double kt=0;             // equivalent airspeed of the dynamic pressure over its panels
    double alpha_deg=0;      // angle of attack weighted by each panel's q*area, as read (the model clamps it to ±30°)
    double stalled=0;        // stalled share of the panels' q*area, 0..1
};
struct RollCard {
    bool valid=false,engaged=false;
    bool from_panels=false;  // the profile's aileron sections; else indicated airspeed, level and unstalled
    AileronCard left,right;
    double wheel_deg=0,rest_deg=0; // right positive
    double model_lb=0,drive_lb=0;  // at the grip (rim_m), right positive
    bool saturated=false,floored=false;
};
inline RollCard roll_card(const RollModel& m,const RollSolution& r,bool engaged,bool from_panels) {
    RollCard c;
    c.valid=true;c.engaged=engaged;c.from_panels=from_panels;
    const auto side=[](const AileronSide& in,double deflection) {return AileronCard{deflection,pressure_kt(in.pa),in.alpha_deg,in.stalled};};
    c.left=side(m.inputs.left,r.left_deg);c.right=side(m.inputs.right,r.right_deg);
    c.wheel_deg=r.wheel_deg;c.rest_deg=r.rest_deg;c.model_lb=r.model_lb;c.drive_lb=r.drive_lb;
    c.saturated=r.saturated;c.floored=r.floored;
    return c;
}
struct Frame {
    double time=0, age_ms=-1;
    std::uint64_t cycles=0;
    std::array<int,2> encoder{}, status{};
    bool owns_axes=false, owns_trim=false;
    bool fresh=false, tuning=false, limited=false, sim_valid=false;
    bool bias_verified=false, position_valid=false;
    std::array<bool,2> motors{}; // roll, pitch; meaningful only while fresh
    std::array<int,2> bias{};
    std::array<double,2> position{};
    Mode mode=Mode::disconnected;
    Tuning requested{}, applied{}, ceilings{};
    FlightSettings settings{};
    FlightSample simulator{};FlightTarget preview{};
    bool solved=false;PitchSolution solution{}; // the session's pitch solver, its last update
    bool roll_solved=false;RollSolution roll_solution{}; // the session's roll solver, its last update
    RollCard roll{};
    // A force-feel session blending to a new feel (FeelBlend): the drive is
    // between its old line and the solution, so readback differs on purpose.
    bool blending=false;
    bool active(unsigned axis) const {
        return fresh && tuning && motors.at(axis) && (mode==Mode::bench || mode==Mode::flight);
    }
    bool active() const {return active(0) && active(1);}
};
inline Frame make_frame(const Snapshot& s,const Tuning& requested,const FlightTarget& target,
    const FlightSettings& settings,const FlightSample& simulator,bool sim_valid,
    const std::array<Calibration,2>& calibration,Clock::time_point now) {
    Frame f;
    f.time=std::chrono::duration<double>(now.time_since_epoch()).count();
    f.cycles=s.cycles;
    f.age_ms=s.connected?std::chrono::duration<double,std::milli>(now-s.sampled).count():-1;
    f.fresh=s.connected && s.cycles && f.age_ms>=0 && f.age_ms<=250;
    f.tuning=s.tuning_valid;f.applied=s.applied_tuning;f.ceilings=requested;
    // A solved session's request is the solver's (solved_tuning, as the
    // reader sends it), not the target's commissioning strength and zero
    // centre; its ceilings are the drive's own this update (decision 16).
    f.solved=s.flight_session && s.pitch_solved;f.solution=s.pitch_solution;
    f.roll_solved=s.flight_session && s.roll_solved;f.roll_solution=s.roll_solution;
    if (f.solved) f.ceilings.pitch_percent=s.pitch_solution.ceiling;
    if (f.roll_solved) f.ceilings.roll_percent=s.roll_solution.ceiling;
    auto solved=target.tuning;
    if (f.solved) solved=solved_tuning(solved,s.pitch_solution);
    if (f.roll_solved) solved=solved_tuning(solved,s.roll_solution);
    f.requested=!s.flight_session?requested:solved;
    if (target.roll) {
        const bool panels=target.aileron_source==AileronSource::surface;
        f.roll=f.roll_solved?roll_card(*target.roll,s.roll_solution,true,panels)
            :roll_card(*target.roll,solve_roll(*target.roll,roll_counts_from_encoder(s.axes[0].encoder)),false,panels);
    }
    f.blending=s.flight_session && s.loading_active && !s.pitch_engaged;
    f.preview=target;f.settings=settings;f.simulator=simulator;f.sim_valid=sim_valid;
    // Limited: the constant force is held at its bound, so the yoke rests
    // short of the model's rest (force/elevator.hpp).
    f.limited=f.solved && s.pitch_solution.bias_limited;
    f.position_valid=f.fresh && s.session_reference && !s.reference_active;
    f.bias_verified=s.force_bias_valid;f.bias=s.force_bias;
    f.mode=!s.connected?Mode::disconnected:s.loading_fault?Mode::fault:
        s.reference_active?Mode::reference:s.flight_suspended?Mode::paused:
        s.loading_active?(s.flight_session?Mode::flight:Mode::bench):Mode::input;
    for (unsigned i=0;i<2;++i) {
        f.encoder[i]=s.axes[i].encoder;f.status[i]=s.axes[i].status;
        f.motors[i]=(s.axes[i].status&4)!=0;
        f.position[i]=normalize(s.axes[i].encoder,calibration[i]);
    }
    return f;
}
inline int value(const Frame& f,unsigned channel,bool requested=false) {
    const auto& t=requested?f.requested:f.applied;
    return channel==0?t.pitch_percent:channel==1?t.roll_percent:t.pitch_trim;
}
inline bool valid(const Frame& f,unsigned channel) {return f.active(channel==1?0:1);}
struct Extent { bool valid=false; int low=0,high=0,absolute=0; };
struct History {
    static constexpr std::size_t capacity=8192;
    std::array<Frame,capacity> samples{};
    std::size_t start=0,count=0;
    const Frame& at(std::size_t i) const {return samples.at((start+i)%capacity);}
    void push(const Frame& f) {
        if (count && f.time<at(count-1).time) {start=0;count=0;}
        // Bounded 20 Hz recording. Mode/freshness changes are retained immediately.
        if (count) {
            const auto& last=at(count-1);
            if (f.time-last.time<0.05 && f.mode==last.mode && f.fresh==last.fresh
                && f.limited==last.limited && f.motors==last.motors && same_tuning(f.applied,last.applied)) return;
        }
        if (count==capacity) {start=(start+1)%capacity;--count;}
        samples[(start+count++)%capacity]=f;
        while (count>1 && at(0).time<f.time-121) {start=(start+1)%capacity;--count;}
    }
    Extent extent(unsigned channel,double end,double seconds) const {
        Extent e;
        for (std::size_t i=0;i<count;++i) {
            const auto& f=at(i);
            if (f.time<end-seconds || f.time>end || !valid(f,channel)) continue;
            const int v=value(f,channel);
            if (!e.valid) {e.low=v;e.high=v;e.valid=true;}
            e.low=std::min(e.low,v);e.high=std::max(e.high,v);e.absolute=std::max(e.absolute,std::abs(v));
        }
        return e;
    }
};
class Model {
    History history_,frozen_history_;
    Frame latest_{},frozen_frame_{};
    bool frozen_=false;
    int seconds_=60;
    std::optional<double> selected_;
public:
    void observe(const Frame& f) {
        if (!std::isfinite(f.time)) return;
        if (f.time<latest_.time) clear();
        latest_=f;history_.push(f);
    }
    const Frame& live() const {return latest_;}
    const History& history() const {return frozen_?frozen_history_:history_;}
    double end() const {return frozen_?frozen_frame_.time:latest_.time;}
    bool frozen() const {return frozen_;}
    int seconds() const {return seconds_;}
    void range(int seconds) {if (seconds==15 || seconds==60 || seconds==120) {seconds_=seconds;if (selected_) selected_=std::clamp(*selected_,end()-seconds_,end());}}
    void freeze() {
        frozen_=!frozen_;selected_.reset();
        if (frozen_) {frozen_history_=history_;frozen_frame_=latest_;}
    }
    void clear() {history_.count=history_.start=0;frozen_history_.count=frozen_history_.start=0;frozen_=false;selected_.reset();}
    void select(double fraction) {
        if (!frozen_) freeze();
        selected_=end()-seconds_+std::clamp(fraction,0.0,1.0)*seconds_;
    }
    std::optional<double> selected() const {return selected_;}
    const Frame& displayed() const {
        if (!selected_) return frozen_?frozen_frame_:latest_;
        const auto& h=history();
        const Frame* result=&frozen_frame_;
        double distance=std::numeric_limits<double>::max();
        for (std::size_t i=0;i<h.count;++i) {
            const auto& f=h.at(i);const double d=std::abs(f.time-*selected_);
            if (d<distance) {distance=d;result=&f;}
        }
        return *result;
    }
};
}
