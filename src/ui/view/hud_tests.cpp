#include "ui/view/hud.hpp"
#include <iostream>
#include <memory>

using namespace brunner;
static void require(bool value,const char* message) {if (!value) throw std::runtime_error(message);}
static monitor::Frame frame(double time) {
    monitor::Frame f;f.time=time;f.age_ms=20;f.fresh=true;f.tuning=true;f.position_valid=true;
    f.mode=monitor::Mode::flight;f.motors={true,true};f.sim_valid=true;f.owns_axes=true;f.requested=f.applied={30,30,0};return f;
}
static bool has(const monitor::Canvas& c,const char* text) {
    return std::any_of(c.items.begin(),c.items.end(),[&](const auto& p){return p.label==text;});
}
int main() {
    try {
        hud::Settings s;hud::State state;auto f=frame(1);state.observe(f,s);
        require(state.view(f,s,recording::State::recording,1).context==hud::Context::quiet,"Steady flight stays quiet");
        f.time=2;f.simulator.trim=.02;state.observe(f,s);
        require(state.view(f,s,recording::State::recording,2,true).context==hud::Context::trim,"Trim activates context");
        require(state.view(f,s,recording::State::recording,2).context==hud::Context::quiet,"No model, no pitch card for a trim change");
        for (int i=0;i<130;++i) {f.time=2+i*.05;state.observe(f,s);}
        auto v=state.view(f,s,recording::State::recording,f.time,true);
        require(v.context==hud::Context::trim && v.alpha<1 && v.alpha>0,"Settled trim fades");
        f.time=10;state.observe(f,s);require(state.view(f,s,recording::State::recording,10,true).context==hud::Context::quiet,"Trim context expires");
        f.requested.pitch_percent=40;state.observe(f,s);
        require(state.view(f,s,recording::State::recording,10).context==hud::Context::quiet,"Brief command latency stays quiet");
        f.time=11;state.observe(f,s);require(state.view(f,s,recording::State::recording,11).context==hud::Context::mismatch,"Persistent mismatch appears");
        f.mode=monitor::Mode::fault;state.observe(f,s);
        require(state.view(f,s,recording::State::recording,11).context==hud::Context::fault,"Fault overrides detail");
        {
            // A solved session asks the drive for the solver's strength and
            // centre, not the target's commissioning spring and zero: with
            // readback matching the solution, nothing differs.
            Snapshot snap;snap.connected=true;snap.cycles=1;snap.loading_active=true;snap.flight_session=true;snap.tuning_valid=true;
            snap.axes[0].status=snap.axes[1].status=0x1017;snap.pitch_solved=true;snap.pitch_engaged=true;
            snap.pitch_solution.strength=91;snap.pitch_solution.centre=-180;snap.pitch_solution.bias=-40;
            const FlightTarget target{{30,95,0,0},std::nullopt,std::nullopt,AileronSource::airspeed,0,0};
            snap.applied_tuning=solved_tuning(target.tuning,snap.pitch_solution);
            const std::array<Calibration,2> cal{{{-13000,13000,true,true},{-12000,12000,false,true}}};
            hud::State solved;
            const auto at=[&](double t) {
                snap.sampled=Clock::time_point{}+std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(t));
                auto g=monitor::make_frame(snap,{30,95,0},target,{},{},true,cal,snap.sampled);g.owns_axes=true;g.position_valid=true;return g;
            };
            for (int i=0;i<=40;++i) solved.observe(at(100+i*.05),s);
            require(at(102).requested.pitch_percent==91 && at(102).requested.pitch_trim==-180,"A solved session's request is the solver's");
            require(solved.view(at(102),s,recording::State::recording,102).context==hud::Context::quiet,"A solved session matching its solution is no mismatch");
            // Blending toward a new feel: readback differs on purpose.
            snap.pitch_engaged=false;snap.applied_tuning.pitch_percent=60;snap.applied_tuning.pitch_trim=-900;
            for (int i=0;i<=40;++i) solved.observe(at(103+i*.05),s);
            require(solved.view(at(105),s,recording::State::recording,105).context==hud::Context::quiet,"A feel blend is no mismatch");
            // Engaged again and still different: a real, persistent difference.
            snap.pitch_engaged=true;
            for (int i=0;i<=40;++i) solved.observe(at(106+i*.05),s);
            const auto seen=solved.view(at(108),s,recording::State::recording,108);
            require(seen.context==hud::Context::mismatch && seen.channel==0,"A persistent difference from the solver's request still shows");
        }
        f=frame(12);state.observe(f,s);
        require(state.view(f,s,recording::State::recording,12.3).context==hud::Context::stale,"Draw-time staleness survives stalled flight callback");
        state=hud::State{};state.observe(f,s);
        auto model=std::make_unique<monitor::Model>();model->observe(f);
        require(hud::render(*model,state,s,recording::State::recording,0,12).items.empty(),"Steady force feedback draws nothing");
        f.mode=monitor::Mode::input;f.motors={false,false};model->observe(f);state.observe(f,s);
        require(hud::render(*model,state,s,recording::State::recording,0,12).items.empty(),"Ordinary input draws no idle strip or recording badge");
        state.pinned=true;
        auto canvas=hud::render(*model,state,s,recording::State::recording,0,12);
        require(has(canvas,"PITCH") && !has(canvas,"DEBUG"),"The kept pitch card names pitch, not debugging");
        require(has(canvas,"Waiting for the model") && has(canvas,"No flight data or aircraft profile yet"),"A kept card without a model says why");
        require(!has(canvas,"--") && !has(canvas,"REC"),"The kept card has no empty metrics or recording jargon");
        state.pinned=false;f.simulator.trim=.2;state.observe(f,s);model->observe(f);
        require(hud::render(*model,state,s,recording::State::recording,0,12).items.empty(),"Trim movement without a model draws nothing");
        f.owns_axes=false;f.position_valid=false;model->observe(f);state.observe(f,s);
        require(has(hud::render(*model,state,s,recording::State::recording,0,12),"Yoke not initialized"),"Idle unreferenced hardware must not claim centering is running");
        // One status source: when the plugin supplies its line, the HUD heading shows it instead of its own guess.
        canvas=hud::render(*model,state,s,recording::State::recording,0,12,"Yoke off or unplugged - check its power switch and USB cable");
        require(has(canvas,"Yoke off or unplugged") && has(canvas,"Check its power switch and USB cable") && !has(canvas,"Yoke not initialized"),
            "HUD heading shows the plugin's shared status line, its remedy on the line below");
        // Every status the supervisor writes fits the 320 px alert card at the
        // window's right. text_width's 0.55 em bold is wider than the measured
        // 0.48 em of X-Plane's bold font.
        for (const char* line:{"Yoke off or unplugged - check its power switch and USB cable","Yoke is in use by another program - close CLS2Sim and Simux",
                "Yoke not recognised - see X-Plane's Log.txt","Yoke fault - switch the yoke off and on, then Retry","Yoke fault - switch the yoke off, then Retry",
                "Force feel unavailable - switch the yoke off and on","Yoke waiting - X-Plane is paused","Yoke centring - hands off"}) {
            const auto shown=hud::render(*model,state,s,recording::State::recording,0,12,line);
            for (const auto& p:shown.items) if (p.kind==monitor::Primitive::Kind::text)
                require(p.x+hud::text_width(p.label,p.size,p.bold)<=372,"HUD text overflows its panel");
            require(has(shown,hud::status_parts(line).first.c_str()) && has(shown,hud::status_parts(line).second.c_str()),"HUD shows heading and remedy");
        }
        f.position_valid=true;model->observe(f);state.observe(f,s);
        require(has(hud::render(*model,state,s,recording::State::recording,0,12),"Yoke input is unavailable"),"Unowned axes must not disappear as healthy input");
        f.owns_axes=true;f.mode=monitor::Mode::paused;model->observe(f);state.observe(f,s);
        require(has(hud::render(*model,state,s,recording::State::recording,0,12),"Yoke forces are paused"),"Suspended feedback remains visible");
        f.mode=monitor::Mode::flight;f.motors={true,false};model->observe(f);state.observe(f,s);
        canvas=hud::render(*model,state,s,recording::State::recording,0,12);
        require(has(canvas,"Check yoke motor state") && !has(canvas,"Forces are off"),"Partial motor enable must not be hidden or described as motors off");
        f=frame(12);state=hud::State{};state.observe(f,s);model->observe(f);
        f.time=13;f.simulator.trim=.01;state.observe(f,s);model->observe(f);
        f.time=19.4;state.observe(f,s);model->observe(f);
        {
            tail::View cruise;cruise.figures={1.35,0.427,0.45,28,23,0.137,30,0.6,22,19};cruise.inputs={.07,1557,1};
            cruise.solution=solve_pitch(cruise.figures,cruise.inputs,0,222);cruise.session=cruise.force_feel=true;
            canvas=hud::render(*model,state,s,recording::State::recording,0,19.4,{},cruise);
        }
        // At the drive's limit (decision 16: 395 % here, not a firmest of 100) the
        // yoke is 7 % as stiff as this deliberately stiff model.
        require(has(canvas,"PITCH") && has(canvas,"Saturated \xC2\xB7 yoke 7% as stiff"),"A trim change shows the pitch card");
        require(std::all_of(canvas.items.begin(),canvas.items.end(),[](const auto& item){return item.color.a<=.51f;}),"The entire card, including identity, fades away together");
        for (auto mode:{monitor::Mode::reference,monitor::Mode::fault,monitor::Mode::disconnected}) {
            f.mode=mode;model->observe(f);state.observe(f,s);
            require(has(hud::render(*model,state,s,recording::State::recording,0,19.4),"YOKE"),"Every actionable status identifies the yoke");
        }
        f=frame(20);state=hud::State{};state.observe(f,s);model->observe(f);
        canvas=hud::render(*model,state,s,recording::State::recording,0,20.3);
        require(has(canvas,"Yoke data is stale") && !has(canvas,"Forces are off"),"Unknown motor state is never described as off");
        canvas=hud::render(*model,state,s,recording::State::failed,0,20);
        require(has(canvas,"Debug log unavailable") && has(canvas,"YOKE FORCE FEEDBACK"),"Logging failures are contextual and plain language");
        require(recording::status_label(recording::State::recording,0)=="Debug log: recording" &&
            recording::status_label(recording::State::recording,4)=="Debug log: 4 dropped" &&
            recording::status_label(recording::State::failed,0)=="Debug log: unavailable","Inspector explains recording state and gaps");
        {
            // The pitch card (docs/brunner/hud/pitch-card.md): the solver's
            // words at their edges, the tail against the solver, and the card
            // inside the window.
            const ElevatorFigures c172{1.35,0.427,0.45,28,23,0.137,30,0.6,22,19};
            const auto pa=[](double kt){const double w=kt*0.514444;return .5*1.225*w*w;};
            const auto dot=[](std::string a,const std::string& b){return a+" \xC2\xB7 "+b;};
            tail::View w;w.figures=c172;w.inputs={0,0,1};
            require(tail::status(w)==dot("Force feel off","the yoke is free"),"Force feel off says the yoke is free");
            w.force_feel=true;require(tail::status(w)=="Force feel starting","Force feel on without a session is starting");
            w.session=true;w.blending=true;require(tail::status(w)=="Blending to the new feel","A blend says so");
            w.blending=false;
            w.solution.bias_limited=true;w.solution.rest_counts=12000;w.solution.drive_rest_counts=11280;
            require(tail::status(w)==dot("Force limit","rests 6% aft of model"),"Force limit says how far and which way");
            w.solution.drive_rest_counts=12030;
            require(tail::status(w)==dot("Force limit","rests 1% fwd of model"),"A small force-limit offset still reads 1%");
            w.solution.bias_limited=false;w.solution.saturated=true;
            require(tail::status(w)==dot("Saturated","lighter than the model"),"No ratio: lighter than the model");
            w.solution.shrink_ratio=.004;require(tail::status(w)==dot("Saturated","yoke under 1% as stiff"),"Under 1%");
            w.solution.shrink_ratio=.0229;require(tail::status(w)==dot("Saturated","yoke 2% as stiff"),"Saturated says how stiff");
            w.solution.saturated=false;w.solution.floored=true;w.solution.force_lb=4.11;w.solution.drive_force_lb=4.12;
            require(tail::status(w)==dot("Floored","no difference here"),"Floored at the rest: no difference");
            w.solution.drive_force_lb=4.68468;require(tail::status(w)==dot("Floored","0.6 lb firmer than model"),"Floored says how much firmer");
            w.solution.drive_force_lb=16.2;require(tail::status(w)==dot("Floored","12 lb firmer than model"),"Whole pounds from 10 lb");
            w.solution.floored=false;require(tail::status(w)=="Matches the model","Otherwise it matches");
            w.inputs={.07,pa(98),1};require(tail::context(w)==dot("Tail 98 kt","trim 7% nose up"),"Context in the page's words");
            w.inputs={-.25,pa(0),1};require(tail::context(w)==dot("Tail 0 kt","trim 25% nose down"),"Nose-down trim");
            w.inputs={.001,std::numeric_limits<double>::quiet_NaN(),1};require(tail::context(w)=="trim neutral","No airflow, no tail figure");
            // The drawing's angles and moments are the solver's.
            const double deg=radians_per_degree;
            for (double kt:{0.0,14.0,40.0,98.0}) for (double trim:{-1.0,0.0,.3,1.0}) for (double yoke:{-12000.0,-3000.0,0.0,6000.0,12000.0}) {
                tail::View t;t.figures=c172;t.inputs={trim,pa(kt),1};t.session=true;t.yoke_counts=yoke;
                t.solution=solve_pitch(c172,t.inputs,yoke,222);
                const auto g=tail::geometry(t);
                require(std::abs(g.net/newton_metres_per_inlb*c172.gearing_rad_per_in-t.solution.force_lb)<1e-6,"The held force is the solver's");
                require(yoke!=12000 || std::abs(g.elevator-23*deg)<1e-9,"The forward stop is the elevator's down stop");
                require(yoke!=-12000 || std::abs(g.elevator+28*deg)<1e-9,"The aft stop is the elevator's up stop");
                require(std::abs(g.tab-(trim>=0?trim*19:trim*22)*deg)<1e-9,"The tab follows the trim wheel");
                require(g.rest>=-28*deg-1e-9 && g.rest<=23*deg+1e-9,"The rest stays between the stops");
                if (kt==0) require(std::abs(g.rest-23*deg)<1e-9,"Parked, the elevator rests on its down stop");
                monitor::Canvas c;tail::draw(c,t,135,84);
                for (const auto& p:c.items) {
                    const bool tri=p.kind==monitor::Primitive::Kind::triangle;
                    for (const auto& q:{std::pair<float,float>{p.x,p.y},{p.w,p.h},{tri?p.x3:p.x,tri?p.y3:p.y}})
                        require(q.first>=8 && q.first<=312 && q.second>=26 && q.second<=142,"The tail stays inside its band of the card");
                    if (tri) require((p.w-p.x)*(p.y3-p.y)-(p.h-p.y)*(p.x3-p.x)>=0,"Triangles are wound as rect is");
                }
            }
            tail::View held;held.figures=c172;held.inputs={0,0,1};held.yoke_counts=0;held.session=true;
            held.solution=solve_pitch(c172,held.inputs,0,222);
            require(tail::geometry(held).elevator==0 && tail::geometry(held).rest>0,"Held level while parked: the rest is below");
            held.solution.drive_rest_counts=held.solution.rest_counts-720;
            require(tail::geometry(held).drive_differs,"A drive rest off the model's is drawn");
            held.session=false;require(!tail::geometry(held).drive_differs,"Without a session the drive has no rest to draw");
            // The card: only when useful, inside the window, every line fitted.
            hud::Settings cs;hud::State quiet;auto g=frame(30);quiet.observe(g,cs);
            auto m=std::make_unique<monitor::Model>();m->observe(g);
            tail::View cruise;cruise.figures=c172;cruise.inputs={.07,pa(98),1.02};cruise.session=cruise.force_feel=true;
            cruise.solution=solve_pitch(c172,cruise.inputs,-131.78+360,222);cruise.yoke_counts=-131.78+360;
            require(hud::render(*m,quiet,cs,recording::State::recording,0,30,{},cruise).items.empty(),"Saturated in cruise is no reason to show a card");
            quiet.pinned=true;
            const auto card=hud::render(*m,quiet,cs,recording::State::recording,0,30,{},cruise);
            require(has(card,"PITCH") && has(card,dot("Saturated","yoke 8% as stiff").c_str()) && has(card,dot("Tail 98 kt","trim 7% nose up").c_str()),
                "Kept on screen, the card shows the tail's words");
            for (const auto& p:card.items) {
                if (p.kind==monitor::Primitive::Kind::text) require(p.x+hud::text_width(p.label,p.size,p.bold)<=372,"Pitch card text fits");
                require(p.x>=60 && p.x<=380 && p.y>=0 && p.y<=200,"The pitch card keeps the window's right edge");
            }
            cs.visible=0;require(hud::render(*m,quiet,cs,recording::State::recording,0,30,{},cruise).items.empty(),"Hidden, the card draws nothing");
            cs.visible=1;g.limited=true;quiet.pinned=false;quiet.observe(g,cs);m->observe(g);g.time=30.5;quiet.observe(g,cs);m->observe(g);
            require(quiet.view(g,cs,recording::State::recording,30.5,true).context==hud::Context::limit,"The force limit shows the card");
            g=frame(31);g.mode=monitor::Mode::fault;m->observe(g);quiet.observe(g,cs);
            const auto stop=hud::render(*m,quiet,cs,recording::State::recording,0,31,"Yoke stopped - Retry to resume",cruise,"A drive left its ready state");
            require(has(stop,"YOKE") && has(stop,"Yoke stopped") && has(stop,"A drive left its ready state") && has(stop,"Retry in the Yoke menu")
                && !has(stop,"DEBUG") && !has(stop,"PITCH"),"A stop outranks the pitch card: what stopped and where to retry");
            {
                // The ailerons join the pitch card when the aileron model publishes: the same show
                // rules, both axes in one card, everything inside the taller window and fitted.
                // Rolling into a bank: the aileron page's C172 at 100 kt, rolling right at 30°/s, the wheel 20° right.
                RollModel wing_model;wing_model.figures={0.85,0.28,0.30,0.15,20,15,86,0.15,0,0.4};
                const double q_bank=.5*1.225*std::pow(100*.514444,2);
                wing_model.inputs={{q_bank,0.4914,0},{q_bank,5.5086,0}};
                const double counts=double(std::lround(wheel_exact_counts(wing_model.figures,20*radians_per_degree))); // whole counts, as the encoder reads
                auto h=frame(40);h.preview.roll=wing_model;h.encoder[0]=int(-counts);
                h.roll=monitor::roll_card(wing_model,solve_roll(wing_model,counts),true,true);
                const auto mapped=hud::roll_view(h);
                require(mapped.has_value(),"A roll card maps to the card's view");
                {
                    const auto l=aileron_moment(wing_model.figures,wing_model.inputs.left,h.roll.left.deflection_deg*radians_per_degree);
                    require(std::abs(mapped->left.air-l.air)<1e-12 && std::abs(mapped->left.lift-l.float_)<1e-12 && mapped->left.weight==l.weight
                        && mapped->left.air>0 && mapped->right.air<0,"The arrows are force/aileron.hpp's moments, pushing each aileron back");
                    require(std::abs(mapped->wheel-20)<0.01 && std::abs(mapped->rest-h.roll.rest_deg)<1e-12 && mapped->rest>0,"Wheel and rest are the card's");
                    require(mapped->shrink>0 && mapped->shrink<1 && mapped->saturated,"Before roll is solved, the share is the preview solve's");
                    auto solved=h;solved.roll_solved=true;solved.roll_solution=solve_roll(wing_model,counts);
                    require(std::abs(hud::roll_view(solved)->shrink-mapped->shrink)<1e-12,"Solved, the same share from the session's solution");
                    auto parked=h;parked.preview.roll->inputs={};parked.roll=monitor::roll_card(*parked.preview.roll,solve_roll(*parked.preview.roll,counts),false,false);
                    require(!std::isfinite(hud::roll_view(parked)->rest) && hud::roll_view(parked)->left.air==0,"No air, no weight: no rest and no arrows");
                    auto none=h;none.roll.valid=false;require(!hud::roll_view(none),"No roll target, no roll half");
                    none=h;none.preview.roll.reset();require(!hud::roll_view(none),"No wing_model, no roll half");
                }
                hud::State pinned;pinned.observe(h,cs);auto mm=std::make_unique<monitor::Model>();mm->observe(h);
                require(hud::render(*mm,pinned,cs,recording::State::recording,0,40,{},cruise).items.empty(),"Quiet with the ailerons too");
                {
                    auto starting=h;starting.roll.engaged=false;
                    require(!hud::roll_view(starting)->engaged && hud::roll_status(*hud::roll_view(starting),cruise)=="Force feel starting",
                        "Until the session solves roll, the wheel's line says force feel is starting");
                }
                pinned.pinned=true;
                const auto both=hud::render(*mm,pinned,cs,recording::State::recording,0,40,{},cruise);
                require(has(both,"PITCH") && has(both,"ROLL") && has(both,dot("Wings 100 kt","rests 6\xC2\xB0 right").c_str())
                    // At each drive's own limit (decision 16): roll 372 % here, pitch 395 %.
                    && has(both,dot("Saturated","wheel "+std::to_string(std::lround(mapped->shrink*100))+"% as stiff").c_str()) && mapped->shrink>0.2
                    && has(both,dot("Saturated","yoke 8% as stiff").c_str())
                    && has(both,dot("Tail 98 kt","trim 7% nose up").c_str()),"Kept on screen, the card shows both axes");
                for (const auto& p:both.items) {
                    if (p.kind==monitor::Primitive::Kind::text) require(p.x+hud::text_width(p.label,p.size,p.bold)<=372,"Roll card text fits");
                    require(p.x>=60 && p.x<=380 && p.y>=0 && p.y<=hud::roll_card_height,"The roll card keeps the window's right edge");
                }
                auto bare=frame(40);mm->observe(bare);
                const auto alone=hud::render(*mm,pinned,cs,recording::State::recording,0,40,{},cruise);
                require(has(alone,"PITCH") && !has(alone,"ROLL"),"Without the aileron model the pitch card is as before");
                for (const auto& p:alone.items) require(p.y<=200,"Without it the card is the shipped 200 tall");
                // The words at their edges.
                tail::View on=cruise,off=cruise;off.session=off.force_feel=false;
                hud::RollView r;r.floored=true;
                require(hud::roll_status(r,on)==dot("Floored","no difference here"),"Floored, matched at the rim");
                r.model_lb=1;r.drive_lb=.6;require(hud::roll_status(r,on)==dot("Floored","0.4 lb lighter than model"),"Floored with the centre clamped: lighter");
                r.model_lb=-1;r.drive_lb=-1.4;require(hud::roll_status(r,on)==dot("Floored","0.4 lb firmer than model"),"Floored, firmer by magnitude");
                r={};r.saturated=true;require(hud::roll_status(r,on)==dot("Saturated","lighter than the model"),"Saturated, no ratio");
                r.shrink=.004;require(hud::roll_status(r,on)==dot("Saturated","wheel under 1% as stiff"),"Saturated, under 1%");
                r.shrink=std::numeric_limits<double>::quiet_NaN();r.model_lb=2;r.drive_lb=.5;
                require(hud::roll_status(r,on)==dot("Saturated","wheel 25% as stiff"),"Saturated, the ratio from the rim forces");
                require(hud::roll_status(hud::RollView{},on)=="Matches the model","Matches");
                require(hud::roll_status(r,off)==dot("Force feel off","the wheel is free"),"Force feel off");
                r={};require(hud::roll_note(r)==dot("Wings 0 kt","stays where left"),"No air: no rest");
                r.left.kt=98;r.right.kt=112;r.rest=.3;require(hud::roll_note(r)==dot("Wings 98/112 kt","rests level"),"The wings' air differs");
                r.rest=-6.3;r.right.kt=100;require(hud::roll_note(r)==dot("Wings 99 kt","rests 6\xC2\xB0 left"),"Rests left");
                // Too wide for the header, the note keeps the direction: the mean air, then R or L.
                r.left.kt=100;r.right.kt=112;r.rest=28;
                require(hud::roll_note(r)==dot("Wings 106 kt","rests 28\xC2\xB0 right"),"Too wide: the wings' mean air");
                r.left.kt=148;r.right.kt=186;r.rest=-86;
                for (const float width:{hud::note_width,150.0f,40.0f}) {
                    const auto note=hud::roll_note(r,width);
                    require(note.find("left")!=std::string::npos || note.substr(note.size()-2)==" L","The direction survives a narrow note");
                }
                require(hud::text_width(hud::roll_note(r),13,false)<=hud::note_width,"The widest note fits the header");
                // At the stops, with the largest moments and angles, the row stays between the
                // ROLL header and the status line.
                for (const double wheel:{-86.0,0.0,86.0}) for (const double big:{-50.0,50.0}) {
                    hud::RollView x{{30,140,30,1,big,big,big},{-30,140,-30,.5,-big,-big,-big},wheel,wheel/2,big,big,.5,true,false};
                    monitor::Canvas c;hud::draw_aileron(c,x.left,72,hud::roll_top+54,"Left wing");
                    hud::draw_aileron(c,x.right,248,hud::roll_top+54,"Right wing");hud::draw_wheel(c,x,160,hud::roll_top+54);
                    x.left.deflection=-30;x.right.deflection=30;hud::draw_aileron(c,x.left,72,hud::roll_top+54,"Left wing");hud::draw_aileron(c,x.right,248,hud::roll_top+54,"Right wing");
                    for (const auto& p:c.items) {
                        const bool tri=p.kind==monitor::Primitive::Kind::triangle,text=p.kind==monitor::Primitive::Kind::text;
                        for (const auto& corner:{std::pair<float,float>{p.x,p.y},{text?p.x:p.w,text?p.y+p.size:p.h},{tri?p.x3:p.x,tri?p.y3:p.y}})
                            require(corner.first>=8 && corner.first<=312 && corner.second>=hud::roll_top+22 && corner.second<=hud::roll_top+84,"The ailerons and wheel stay inside their row");
                        if (tri) require((p.w-p.x)*(p.y3-p.y)-(p.h-p.y)*(p.x3-p.x)>=0,"Roll triangles are wound as rect is");
                    }
                }
            }
            const std::string longest="Force feel needs the aircraft profiles; see X-Plane's Log.txt";
            const auto cut=hud::fit(longest,15,false,hud::line_width);
            require(hud::text_width(cut,15,false)<=hud::line_width && cut.size()>3 && cut.substr(cut.size()-3)=="..."
                && longest.compare(0,cut.size()-3,cut,0,cut.size()-3)==0 && longest[cut.size()-3]==' ',"A line too long for the card is cut at a word");
        }
        s.visible=0;require(hud::render(*model,state,s,recording::State::recording,0,12).items.empty(),"Hidden HUD emits no graphics");
        recording::Queue<int,4> q;int item=0;
        require(q.push(1)&&q.push(2)&&q.push(3)&&!q.push(4),"Bounded queue rejects overflow");
        require(q.pop(item)&&item==1&&q.push(4),"Queue wraps without overwriting unread data");
        for (int i=2;i<=4;++i) require(q.pop(item)&&item==i,"FIFO ordering preserved");
        require(!q.pop(item),"Empty queue returns immediately");
        auto recorder=std::make_unique<recording::Recorder>();
        const auto root=std::filesystem::temp_directory_path()/("brunner-recorder-test-"+std::to_string(recording::utc_ms()));
        std::filesystem::create_directory(root);std::ofstream(root/"unrelated.txt")<<"keep";
        recorder->start(root,{2200,3});const auto t=recording::seconds();
        for (int i=0;i<100;++i) {f=frame(t+i*.06);recorder->observe(f);}
        recorder->event("quoted \"value\"\nnext");recorder->stop();
        require(recorder->state()==recording::State::off && recorder->written()>90,"Background writer drains on clean shutdown");
        std::vector<std::filesystem::path> files;
        for (const auto& e:std::filesystem::directory_iterator(root)) if (recording::owned(e.path())) files.push_back(e.path());
        require(files.size()==3 && std::filesystem::exists(root/"unrelated.txt"),"Rotation bounds disk and preserves foreign files");
        std::sort(files.begin(),files.end());std::ifstream input(files.back());std::string data((std::istreambuf_iterator<char>(input)),{});
        input.close(); // Windows does not permit deleting an open test fixture.
        require(data.find("\"mapping\":\"elevator-model-v1\"")!=std::string::npos,"Recorder header identifies the loading model");
        require(data.find("\"schema\":1")!=std::string::npos && data.find("session_end")!=std::string::npos,"Each segment is self-describing; shutdown explicit");
        require(data.find("quoted \\\"value\\\" next")!=std::string::npos,"Event text is JSON-safe");
        recorder->start(root/"unrelated.txt");recorder->stop();
        require(recorder->state()==recording::State::failed,"Disk path failure is observable, not fatal");
        recorder->start(root);recorder->observe(frame(recording::seconds()));recorder->stop();
        require(recorder->state()==recording::State::off && recorder->written()==1,"Recorder recovers on next session");
        std::filesystem::remove_all(root);
        recording::Record r;r.frame=frame(2);r.frame.simulator.airspeed=std::numeric_limits<double>::quiet_NaN();
        require(recording::encode(r,1,1000,1).find("\"ias_knots\":null")!=std::string::npos,"Nonfinite values stay valid JSON");
        require(recording::encode(r,1,1000,1).find("\"sampling_gap_s\":1")!=std::string::npos,"Sampling stalls have explicit gaps");
        // The tail (decision 17): X-Plane's angle and the tail-plane's stall as
        // read, raw, and at the yoke both stalls apart and the share the hinge uses.
        r.frame.sim_valid=true;r.frame.simulator.tail=AileronSide{1200,-2.5,0.5};
        require(recording::encode(r,1,1000,1).find("\"tail\":{\"pa\":1200,\"alpha_deg\":-2.5,\"tail_stalled_xp\":0.5}")!=std::string::npos,"X-Plane's tail is recorded raw");
        {
            ElevatorFigures e{1.35,0.427,0.45,28,23,0.137,30,0.6,22,19,0.2,0.4,0.5,13,4,1};
            r.frame.preview.pitch=PitchModel{e,{0,1200,1,-2.5,0.5},222,0};r.frame.position_valid=true;r.frame.encoder[1]=12000; // full aft
            const auto line=recording::encode(r,1,1000,1);
            require(line.find("\"tail_model\":{\"alpha_deg\":-2.5,\"tail_stalled_xp\":0.5,\"elevator_stalled\":0.875,\"hinge_stalled\":0.875,\"reversed\":0}")!=std::string::npos,
                "full aft the section sees -2.5 - 0.5*28 = -16.5°, 3.5° past its stall: the elevator's own 0.875 beats the tail-plane's 0.5");
        }
        std::cout<<"HUD context and bounded session recorder passed\n";
    } catch (const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
