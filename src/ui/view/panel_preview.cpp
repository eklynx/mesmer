#include "ui/view/hud.hpp"
#include <iostream>
#include <memory>

using namespace brunner::monitor;
static std::string escape(std::string s) {
    std::string out;
    for (char c:s) {if (c=='&') out+="&amp;";else if (c=='<') out+="&lt;";else if (c=='>') out+="&gt;";else if (c=='\"') out+="&quot;";else out+=c;}
    return out;
}
static std::string color(Color c) {
    char out[20];std::snprintf(out,sizeof(out),"#%02x%02x%02x",unsigned(c.r*255),unsigned(c.g*255),unsigned(c.b*255));return out;
}
// The pitch card's scenes (hud-pitch-<scene>): the C172's figures and the
// solver's own output, except the force limit, which the defaults never reach.
static std::optional<brunner::tail::View> pitch_scene(const std::string& scene) {
    using namespace brunner;
    const ElevatorFigures c172{1.35,0.427,0.45,28,23,0.137,30,0.6,22,19};
    const auto pa=[](double kt){const double v=kt*0.514444;return .5*1.225*v*v;};
    tail::View v;v.figures=c172;v.force_feel=v.session=true;
    const auto solve=[&](ElevatorInputs in,double yoke) {
        v.inputs=in;v.yoke_counts=yoke;v.solution=solve_pitch(c172,in,yoke,222);
    };
    if (scene=="parked") solve({0,0,1},12000);
    else if (scene=="parked-held") solve({0,0,1},0);
    else if (scene=="takeoff") solve({0,pa(58),1},0);
    else if (scene=="slow") solve({.2,pa(48),1},-5000);
    else if (scene=="climb") solve({.25,pa(70),1.1},-1500);
    else if (scene=="limit") {
        solve({0,pa(10),1},12000);
        v.solution.bias_limited=true;v.solution.drive_rest_counts=v.solution.rest_counts-720;
    } else if (scene=="waiting") return std::nullopt;
    else {
        // Cruise: the yoke 3% forward of where it rests, trimmed 7% nose up.
        const ElevatorInputs in{.07,pa(98),1.02};
        solve(in,solve_pitch(c172,in,std::numeric_limits<double>::quiet_NaN(),222).rest_counts+360);
        if (scene=="off") v.session=v.force_feel=false;
        if (scene=="starting") v.session=false;
        if (scene=="blending") v.blending=true;
    }
    return v;
}
// The card with the ailerons (hud-roll-<scene>), from the wired path: a frame
// whose target carries the C172 aileron model (the aileron page's figures,
// no firmest since decision 16) with each side's air as that page's preset reports it, and
// whose roll card is the session's solution at the wheel, as make_frame does.
// Parked has the wheel left 12 degrees left.
struct RollScene {brunner::RollModel model;double wheel_deg=0;};
static std::optional<RollScene> roll_scene(const std::string& scene,std::string& pitch_name) {
    using namespace brunner;
    // The page's reported(): air over each aileron from airspeed and a little
    // prop wash, each wing's angle from the roll rate (p·y/V, y 4.3 m).
    const auto conditions=[](double kt,double throttle,double aoa,double roll_rate,double stalled_left,double stalled_right) {
        const double v=kt*metres_per_second_per_knot,wash=5*throttle*std::max(0.0,1-v/80),q=.5*sea_level_density*(v*v+wash*wash);
        const double dp=roll_rate*radians_per_degree*4.3/std::max(5.0,v)/radians_per_degree;
        return AileronInputs{{q,aoa-dp,stalled_left},{q,aoa+dp,stalled_right}};
    };
    RollScene r;
    r.model.figures={0.85,0.28,0.30,0.15,20,15,86,0.15,0,0.4};r.model.units_per_nm=1000;
    if (scene=="parked") {pitch_name="parked";r.model.inputs=conditions(0,0,0,0,0,0);r.wheel_deg=-12;}
    else if (scene=="takeoff") {pitch_name="takeoff";r.model.inputs=conditions(55,1,2,0,0,0);}
    else if (scene=="cruise") {pitch_name="cruise";r.model.inputs=conditions(110,.75,2,0,0,0);}
    else if (scene=="bank") {pitch_name="cruise";r.model.inputs=conditions(100,.75,3,30,0,0);r.wheel_deg=20;}
    else if (scene=="stall") {pitch_name="slow";r.model.inputs=conditions(45,.2,17,-20,1,.4);r.wheel_deg=6;}
    else return std::nullopt;
    return r;
}
int main(int argc,char** argv) {
    std::string state=argc>1?argv[1]:"stopped";
    const bool is_hud=state.substr(0,4)=="hud-";if (is_hud) state=state.substr(4);
    const bool is_pitch=is_hud && state.substr(0,6)=="pitch-";
    std::optional<brunner::tail::View> pitch_view;
    if (is_pitch) {pitch_view=pitch_scene(state.substr(6));state="detail";}
    std::optional<RollScene> roll_case;
    if (is_hud && state.substr(0,5)=="roll-") {
        std::string pitch_name;roll_case=roll_scene(state.substr(5),pitch_name);
        pitch_view=pitch_scene(pitch_name);state="detail";
    }
    if (is_hud && (state=="trim" || state=="limit")) pitch_view=pitch_scene(state=="limit"?"limit":"cruise");
    std::string status,reason;
    if (state=="fault") {status="Yoke stopped - Retry to resume";reason="A drive left its ready state";}
    if (state=="disconnected") status="Yoke off or unplugged - check its power switch and USB cable";
    if (state=="paused") status="Yoke waiting - X-Plane is paused";
    brunner::hud::State context;brunner::hud::Settings settings;
    auto recorder=brunner::recording::State::recording;
    const int span=argc>2?std::stoi(argv[2]):60;
    auto model=std::make_unique<Model>();model->range(span);
    const double duration=120;
    for (int tick=0;tick<=2400;++tick) {
        Frame f;f.time=tick*.05;f.age_ms=29;f.fresh=true;f.tuning=true;f.motors={true,true};f.mode=Mode::bench;
        f.position_valid=true;f.position={.12*std::sin(f.time*.2),.25*std::sin(f.time*.13)};
        f.ceilings={40,40,0};f.requested={40,40,0};f.bias_verified=true;f.bias={0,0};
        const double phase=std::fmod(f.time,60.0);
        const int p=phase<12?20:phase<30?40:20;
        f.applied={p==40?38:20,p,int(std::lround(-600*std::sin(phase/60*3.14159)))};
        f.requested={p,p,int(std::lround(-600*std::sin((phase+2)/60*3.14159)))};
        if (phase>16 && phase<19) f.fresh=false;
        f.ceilings=f.requested;
        if (f.time>duration-12 && state=="stopped") {f.mode=Mode::input;f.motors={false,false};f.bias_verified=false;f.requested={40,40,0};f.ceilings=f.requested;}
        if (f.time>duration-2 && state=="stale") {f.fresh=false;f.age_ms=2600;}
        if (state=="empty") {f.mode=Mode::input;f.motors={false,false};f.bias_verified=false;}
        if (state=="active" || state=="frozen") {
            f.mode=Mode::flight;f.sim_valid=true;f.simulator={58.0,.027};f.settings={0,316,20,600};
            f.ceilings={40,40,0};f.requested={28,28,-324};
            if (f.time>duration-3) f.applied={28,28,-324};
        }
        if (state=="reference" && f.time>duration-12) {f.mode=Mode::reference;f.motors={true,true};}
        f.position_valid=f.fresh && f.mode!=Mode::reference;
        if (is_hud) {
            f.owns_axes=true;
            f.fresh=true;f.age_ms=20;f.mode=Mode::flight;f.sim_valid=true;f.simulator={60,0};
            f.applied=f.requested={28,28,0};f.motors={true,true};f.position_valid=true;
            if (roll_case) {
                const double counts=brunner::wheel_exact_counts(roll_case->model.figures,roll_case->wheel_deg*brunner::radians_per_degree);
                f.encoder[0]=int(std::lround(-counts));f.preview.roll=roll_case->model;f.preview.aileron_source=brunner::AileronSource::surface;
                f.roll_solved=true;f.roll_solution=brunner::solve_roll(roll_case->model,counts);
                f.roll=roll_card(roll_case->model,f.roll_solution,true,true);
            }
            if (state=="trim" || state=="limit") {
                f.simulator.trim=f.time>115?.07:0;
                f.applied.pitch_trim=f.requested.pitch_trim=f.time>115?42:0;
                if (state=="limit") {f.limited=true;f.applied.pitch_trim=f.requested.pitch_trim=-600;}
            }
            if (state=="mismatch") {f.requested.pitch_percent=40;f.applied.pitch_percent=28;}
            if (state=="stopped" || state=="pinned-off") {f.mode=Mode::input;f.motors={false,false};}
            if (state=="pinned-off") context.pinned=true;
            if (state=="paused") {f.mode=Mode::paused;f.motors={false,false};}
            if (state=="partial") f.motors={false,true};
            if (state=="stale") {f.fresh=false;f.age_ms=1200;}
            if (state=="reference") {f.mode=Mode::reference;f.position_valid=false;}
            if (state=="fault") f.mode=Mode::fault;
            if (state=="disconnected") {f.mode=Mode::disconnected;f.fresh=false;}
            if (state=="recording") recorder=brunner::recording::State::failed;
            if (state=="detail") context.pinned=true;
            context.observe(f,settings);
        }
        model->observe(f);
    }
    if (state=="frozen") {model->freeze();model->select(.74);}
    if (argc>3 && std::string(argv[3])=="freeze" && !model->frozen()) model->freeze();
    if (argc>3 && std::string(argv[3])=="clear") model->clear();
    if (argc>4) model->select(std::stod(argv[4]));
    const auto canvas=is_hud?brunner::hud::render(*model,context,settings,recorder,0,duration,status,pitch_view,reason):
        render(*model,true,-1,brunner::recording::status_label(recorder,0));
    const int height=is_hud?brunner::hud::window_height:800;
    std::cout<<"<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 "<<(is_hud?380:1200)<<" "<<height<<"\" width=\""<<(is_hud?380:1200)<<"\" height=\""<<height<<"\" role=\"img\" aria-label=\"Synthetic force monitor preview\">\n";
    for (const auto& p:canvas.items) {
        const auto col=color(p.color);
        if (p.kind==Primitive::Kind::rect) std::cout<<"<rect x=\""<<p.x<<"\" y=\""<<p.y<<"\" width=\""<<p.w<<"\" height=\""<<p.h<<"\" fill=\""<<col<<"\" opacity=\""<<p.color.a<<"\"/>\n";
        else if (p.kind==Primitive::Kind::triangle) std::cout<<"<path d=\"M "<<p.x<<' '<<p.y<<" L "<<p.w<<' '<<p.h<<" L "<<p.x3<<' '<<p.y3<<" Z\" fill=\""<<col<<"\" opacity=\""<<p.color.a<<"\""<<(p.color.a>=1?" stroke=\""+col+"\" stroke-width=\"0.4\"":"")<<"/>\n";
        else if (p.kind==Primitive::Kind::line) std::cout<<"<path d=\"M "<<p.x<<' '<<p.y<<" L "<<p.w<<' '<<p.h<<"\" stroke=\""<<col<<"\" stroke-width=\""<<p.size<<"\" opacity=\""<<p.color.a<<"\"/>\n";
        else std::cout<<"<text x=\""<<p.x<<"\" y=\""<<p.y+p.size*.85f<<"\" fill=\""<<col<<"\" font-family=\"Segoe UI, Arial, sans-serif\" font-size=\""<<p.size<<"\" font-weight=\""<<(p.bold?600:400)<<"\">"<<escape(p.label)<<"</text>\n";
    }
    std::cout<<"</svg>\n";
}
