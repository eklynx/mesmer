#include "ui/view/panel.hpp"
#include <iostream>
#include <memory>

using namespace brunner::monitor;
static void require(bool value,const char* message) {if (!value) throw std::runtime_error(message);}
static Frame active(double time,int pitch_scale=25,int roll_scale=30,int trim=0) {
    Frame f;f.time=time;f.age_ms=30;f.fresh=true;f.tuning=true;f.mode=Mode::bench;
    f.motors={true,true};f.applied={roll_scale,pitch_scale,trim};f.requested=f.applied;return f;
}
static bool has(const Canvas& c,const char* text) {
    return std::any_of(c.items.begin(),c.items.end(),[&](const Primitive& p){return p.label==text;});
}
int main() {
    // Native rectangles and thick lines must have identical winding so X-Plane's
    // face culling cannot remove chart grids, traces or event markers.
    for (const auto delta:std::array<Point,6>{{{100,0},{-100,0},{0,100},{0,-100},{40,70},{-40,-70}}}) {
        const auto triangles=line_triangles(30,20,30+delta.x,20+delta.y,2);
        for (unsigned i=0;i<6;i+=3) {
            const auto a=triangles[i],b=triangles[i+1],c=triangles[i+2];
            require((b.x-a.x)*(c.y-a.y)-(b.y-a.y)*(c.x-a.x)>0,"Native lines must match rectangle winding");
        }
    }

    try {
        auto model=std::make_unique<Model>();
        model->observe(active(1,20,30,-600));model->observe(active(1.02,40,35,100));model->observe(active(1.06,20,30,0));
        auto peak=model->history().extent(0,1.06,15);
        require(peak.valid && peak.high==40,"Brief verified peaks must survive the recording throttle");
        auto stopped=active(2,100,100,0);stopped.mode=Mode::input;stopped.motors={false,false};model->observe(stopped);
        require(model->history().extent(0,2,15).high==40,"Stored scale while disabled must not inflate active peak");
        require(has(render(*model),"MOTORS OFF"),"Stopped hardware must be explicit");
        require(has(render(*model),"--"),"Inactive current is unavailable, never invented zero force");
        auto stale=active(3,99,99,999);stale.fresh=false;model->observe(stale);
        require(model->history().extent(0,3,15).high==40,"Stale values must not contribute to peaks");
        require(has(render(*model),"MOTOR STATE UNKNOWN"),"A stale link cannot certify motors off");
        model->freeze();const double frozen_end=model->end();const auto frozen_count=model->history().count;
        model->observe(active(4,35,35,200));
        require(model->end()==frozen_end && model->history().count==frozen_count,"Frozen history must not scroll or mutate");
        require(model->live().time==4,"Hardware status must continue live while history is frozen");
        model->select(0);require(model->displayed().time==1,"Selected history must use contemporaneous provenance");
        model->freeze();require(!model->frozen() && model->end()==4,"Resume rejoins current recording");
        model->observe(active(140,22,22,0));require(model->history().extent(0,140,15).high==22,"Peaks expire with the visible time window");
        require(model->history().at(0).time>=19,"History is bounded by wall time");
        model->range(30);require(model->seconds()==60,"Unsupported history span rejected");
        model->clear();require(model->history().count==0 && !model->frozen(),"Clear affects view/history only");
        model->observe(active(2));require(model->history().count==1,"Clock reset starts a new recording");
        auto f=active(3);f.motors[0]=false;require(valid(f,0) && !valid(f,1),"Pitch and roll motor gates are independent");
        f.mode=Mode::reference;require(!valid(f,0),"Homing force must not masquerade as measured spring telemetry");
        brunner::Snapshot s;s.connected=true;s.cycles=1;s.sampled=brunner::Clock::now();s.loading_active=true;s.flight_session=true;s.tuning_valid=true;
        s.axes[0].status=0x1017;s.axes[1].status=0x1017;s.applied_tuning={28,30,-320};
        const std::array<brunner::Calibration,2> calibration{{{-13000,13000,true,true},{-12000,12000,false,true}}};
        const auto captured=make_frame(s,{40,40,0},brunner::FlightTarget{{29,31,-340},std::nullopt,std::nullopt,brunner::AileronSource::airspeed,0,0},{},{58,.027},true,calibration,s.sampled);
        require(captured.requested.pitch_percent==31 && captured.applied.pitch_percent==30,"Requested and verified values must remain separate");
        require(captured.ceilings.pitch_percent==40 && captured.mode==Mode::flight,"Flight source and ceiling retained");
        {
            // The roll card (the HUD's): each aileron's deflection, air, angle
            // and stalled share; the wheel and its rest; the model's and the
            // drive's force at the grip. A preview until the session solves roll.
            const brunner::AileronFigures ailerons{0.85,0.28,0.30,0.15,20,15,86,0.15,0,0.4};
            const brunner::RollModel roll_model{ailerons,{{1000,4,0},{1000,-2,1}},1000,0};
            const brunner::FlightTarget with_roll{{29,31,-340},std::nullopt,roll_model,brunner::AileronSource::surface,0,0};
            auto rolled=s;rolled.axes[0].encoder=-1300; // a tenth of the wheel's travel, right
            auto card=make_frame(rolled,{40,40,0},with_roll,{},{},true,calibration,rolled.sampled).roll;
            const auto close_to=[](double a,double b) {return std::abs(a-b)<1e-9;};
            require(card.valid && !card.engaged && card.from_panels && close_to(card.wheel_deg,8.6) && card.left.alpha_deg==4 && card.right.alpha_deg==-2
                && card.left.stalled==0 && card.right.stalled==1 && close_to(card.left.kt,brunner::pressure_kt(1000)),"Roll card previews the model at the wheel");
            require(card.right.deflection_deg>0 && card.left.deflection_deg<0 && card.right.deflection_deg>-card.left.deflection_deg,
                "Right wheel: right aileron up, left down, the up-going one further (trailing edge up positive)");
            const auto preview=brunner::solve_roll(roll_model,1300);
            require(card.model_lb==preview.model_lb && card.drive_lb==preview.drive_lb && card.rest_deg==preview.rest_deg && std::isfinite(card.drive_lb),
                "The preview is the solver's at the wheel");
            auto level=roll_model;level.inputs={{1000,0,0},{1000,0,0}};
            require(brunner::solve_roll(level,1300).model_lb<0,"With level air the model pushes a right wheel back left");
            rolled.roll_solved=true;rolled.roll_solution=brunner::solve_roll(roll_model,1300);
            const auto solved=make_frame(rolled,{40,40,0},with_roll,{},{},true,calibration,rolled.sampled);
            require(solved.roll.engaged && solved.roll.saturated==rolled.roll_solution.saturated && solved.roll.floored==rolled.roll_solution.floored
                && solved.requested.roll_percent==rolled.roll_solution.strength && solved.requested.roll_trim==rolled.roll_solution.centre,
                "A solved session's roll request is the solver's");
            require(!make_frame(s,{40,40,0},captured.preview,{},{},true,calibration,s.sampled).roll.valid,"No roll model, no roll card");
        }
        const auto old=make_frame(s,{},{},{},{},true,calibration,s.sampled+std::chrono::milliseconds(251));
        require(!old.fresh && !old.active(),"Age >250ms must create a telemetry gap");
        model->observe(active(200));
        require(has(render(*model,false,201),"MOTOR STATE UNKNOWN"),"Rendering must age hardware status even if flight-loop updates stall");
        // The display has no controller transport or command callback; interactions only mutate Model.
        require(click(*model,700,110) && model->frozen(),"Freeze hit target");
        require(click(*model,980,110) && model->seconds()==120,"Range hit target");
        require(click(*model,1060,110) && !model->history().count,"Clear hit target");
        std::cout<<"Monitor history, provenance, freshness, peak and interaction tests passed\n";
    } catch (const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
