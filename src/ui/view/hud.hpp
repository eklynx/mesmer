#pragma once
#include "ui/view/tail.hpp"
#include "recorder.hpp"
#include <utility>

namespace brunner::hud {
struct Settings {
    int visible=1,opacity=72,hold_ms=6000,fade_ms=800,trim_milli=3,mismatch_ms=750;
    int spring_error_percent=2,neutral_error_counts=30,right=24,top=36;
};
enum class Context {quiet,trim,detail,limit,mismatch,initializing,recording,fault,stale,disconnected,motorstate,paused,inputunavailable};
struct View {Context context=Context::quiet;float alpha=1;unsigned channel=0;};
class State {
    double trim_anchor_=0,trim_at_=-1,limit_at_=-1;
    std::array<double,3> mismatch_at_{{-1,-1,-1}};
    bool observed_=false;
    std::array<bool,3> mismatch_{};
public:
    bool pinned=false,moving=false;
    void observe(const monitor::Frame& f,const Settings& s) {
        if (!observed_) {trim_anchor_=f.simulator.trim;observed_=true;}
        if (f.sim_valid && std::isfinite(f.simulator.trim) && std::abs(f.simulator.trim-trim_anchor_)>=s.trim_milli/1000.0) {
            trim_anchor_=f.simulator.trim;trim_at_=f.time;
        }
        for (unsigned i=0;i<3;++i) {
            const int tolerance=i==2?s.neutral_error_counts:s.spring_error_percent;
            const int error=std::abs(monitor::value(f,i)-monitor::value(f,i,true));
            // A feel blend moves the drive toward the solution on purpose.
            mismatch_[i]=monitor::valid(f,i) && !f.blending && error>(mismatch_[i]?tolerance/2:tolerance);
            if (!mismatch_[i]) mismatch_at_[i]=-1;else if (mismatch_at_[i]<0) mismatch_at_[i]=f.time;
        }
        if (!f.limited || !f.active(1)) limit_at_=-1;else if (limit_at_<0) limit_at_=f.time;
    }
    // pitch_model: the pitch card has a model to draw, so a trim change shows it.
    View view(const monitor::Frame& f,const Settings& s,recording::State recorder,double now,bool pitch_model=false) const {
        if (f.mode==monitor::Mode::fault) return {Context::fault,1,0};
        if (f.mode==monitor::Mode::disconnected) return {Context::disconnected,1,0};
        if (!f.fresh || now-f.time+f.age_ms/1000>.25) return {Context::stale,1,0};
        if (recorder==recording::State::failed) return {Context::recording,1,0};
        if (f.mode==monitor::Mode::reference || (!f.position_valid && f.mode==monitor::Mode::input)) return {Context::initializing,1,0};
        if ((f.motors[0] || f.motors[1]) && !f.active()) return {Context::motorstate,1,0};
        if (f.mode==monitor::Mode::paused) return {Context::paused,1,0};
        if (!f.owns_axes && f.mode==monitor::Mode::input) return {Context::inputunavailable,1,0};
        for (unsigned i=0;i<3;++i) if (mismatch_at_[i]>=0 && (now-mismatch_at_[i])*1000>=s.mismatch_ms)
            return {Context::mismatch,1,i};
        if (limit_at_>=0 && now-limit_at_>=.4) return {Context::limit,1,2};
        const double elapsed=(now-trim_at_)*1000;
        if (pitch_model && trim_at_>=0 && elapsed<s.hold_ms+s.fade_ms && elapsed>=0) {
            return {Context::trim,float(elapsed<=s.hold_ms?1:1-(elapsed-s.hold_ms)/std::max(s.fade_ms,1)),2};
        }
        return {pinned || moving?Context::detail:Context::quiet,1,0};
    }
};
inline const char* label(Context c) {
    switch (c) {
    case Context::trim:case Context::limit:return "Pitch";
    case Context::mismatch:return "Readback differs from target";
    case Context::initializing:return "Centering the yoke";
    case Context::recording:return "Debug log unavailable";
    case Context::fault:return "Controller fault";
    case Context::stale:return "Yoke data is stale";
    case Context::disconnected:return "Yoke disconnected";
    case Context::motorstate:return "Check yoke motor state";
    case Context::paused:return "Yoke forces are paused";
    case Context::inputunavailable:return "Yoke input is unavailable";
    default:return "Controller spring scale";
    }
}
inline void trace(monitor::Canvas& c,const monitor::History& h,unsigned channel,double now,monitor::Color color) {
    constexpr float left=18,right=362,top=137,bottom=179;
    const double range=channel==2?6000:40,low=channel==2?-range:0;
    const auto y=[&](double v){return bottom-float((std::clamp(v,low,range)-low)/(range-low))*(bottom-top);};
    const auto x=[&](double t){return left+float((t-(now-12))/12)*(right-left);};
    double lo=range,hi=low;
    for (std::size_t i=0;i<h.count;++i) {
        const auto& f=h.at(i);if (f.time<now-12 || f.time>now || !monitor::valid(f,channel)) continue;
        lo=std::min(lo,double(std::min(monitor::value(f,channel),monitor::value(f,channel,true))));
        hi=std::max(hi,double(std::max(monitor::value(f,channel),monitor::value(f,channel,true))));
    }
    // Local range makes small neutral changes legible. Numeric labels above
    // supply units; this sparkline conveys timing, never measured force.
    const double pad=channel==2?30:2;
    const auto yy=[&](double v){return hi<lo?y(v):bottom-float((v-(lo-pad))/(hi-lo+2*pad))*(bottom-top);};
    c.line(left,bottom,right,bottom,monitor::rgb(0x9baeb6,.2f));
    const monitor::Frame* prev=nullptr;
    for (std::size_t i=0;i<h.count;++i) {
        const auto& f=h.at(i);
        if (f.time<now-12 || f.time>now || !monitor::valid(f,channel)) {prev=nullptr;continue;}
        if (prev && f.time-prev->time<.3 && f.mode==prev->mode) {
            const auto x1=x(prev->time),x2=x(f.time);
            const auto y1=yy(monitor::value(*prev,channel)),y2=yy(monitor::value(f,channel));
            c.line(x1,y1,x2,y1,color,1.8f);c.line(x2,y1,x2,y2,color,1.8f);
            if (monitor::value(f,channel)!=monitor::value(f,channel,true) || monitor::value(*prev,channel)!=monitor::value(*prev,channel,true)) {
                // Keep the dash phase across samples; restarting it for each
                // short segment would merge the target into a solid line.
                const float target_y=yy(monitor::value(*prev,channel,true));
                for (float d=std::floor(x1/6)*6;d<x2;d+=6)
                    if (std::min(x2,d+2)>std::max(x1,d)) c.line(std::max(x1,d),target_y,std::min(x2,d+2),target_y,monitor::muted,1.2f);
            }
        }
        prev=&f;
    }
}
// Splits "Yoke off or unplugged - check its power switch and USB cable" into
// the heading and a capitalised remedy; a line without " - " is all heading.
inline std::pair<std::string,std::string> status_parts(const std::string& line) {
    const auto cut=line.find(" - ");
    if (cut==std::string::npos) return {line,{}};
    std::string remedy=line.substr(cut+3);
    if (!remedy.empty() && remedy[0]>='a' && remedy[0]<='z') remedy[0]=char(remedy[0]-'a'+'A');
    return {line.substr(0,cut),remedy};
}
// Estimated width at the atlas's Segoe UI: 0.55 em bold, 0.50 em regular,
// both wider than measured. The middle dot counts as two characters.
inline float text_width(const std::string& text,float size,bool bold) {return float(text.size())*size*(bold?.55f:.50f);}
// Cuts a line at a word to fit, ending "...": a status the card did not plan
// for still never overflows it.
inline std::string fit(std::string text,float size,bool bold,float width) {
    if (text_width(text,size,bold)<=width) return text;
    while (!text.empty() && text_width(text+"...",size,bold)>width) {
        const auto cut=text.find_last_of(' ');
        text.resize(cut==std::string::npos?text.size()-1:cut);
    }
    return text+"...";
}
// The card's roll half: both ailerons and the wheel, as the aileron page
// (aileron.html, #wings and #wheel) draws them. Display only. Angles in
// degrees; filled by roll_view from the aileron model's telemetry.
struct RollSide {
    double deflection=0;            // the aileron, trailing edge up positive
    double kt=0;                    // air over it
    double alpha=0;                 // its panel's angle of attack
    double stalled=0;               // stalled share, 0..1
    double air=0,lift=0,weight=0;   // moments about its hinge, N·m, trailing edge up positive: the page's air, float and weight
};
struct RollView {
    RollSide left,right;
    double wheel=0;                                      // right positive
    double rest=std::numeric_limits<double>::quiet_NaN(); // hands-off; NaN when nothing turns it (it stays where it is left)
    double model_lb=0,drive_lb=0;                        // force at the rim, turning it right positive
    double shrink=std::numeric_limits<double>::quiet_NaN(); // the drive's slope over the model's, when known
    bool saturated=false,floored=false;
    bool engaged=true;                                   // the session solves roll; until then the card is a preview
};
// The card's one mapping from telemetry: the frame's roll card (ui/view/telemetry.hpp,
// RollCard) with the model it came from (the frame's target). The arrows'
// moments are force/aileron.hpp's own, for each aileron where the card has it; the
// stiffness share is the drive's spring over the model's slope, from the
// session's solution or, before it solves roll, the same preview solve the
// card came from. Null without a roll target.
inline std::optional<RollView> roll_view(const monitor::Frame& f) {
    if (!f.roll.valid || !f.preview.roll) return std::nullopt;
    const auto& m=*f.preview.roll;const auto& card=f.roll;
    const auto side=[&](const monitor::AileronCard& a,const AileronSide& in) {
        const auto moment=aileron_moment(m.figures,in,a.deflection_deg*radians_per_degree);
        return RollSide{a.deflection_deg,a.kt,a.alpha_deg,a.stalled,moment.air,moment.float_,moment.weight};
    };
    RollView v;
    v.left=side(card.left,m.inputs.left);v.right=side(card.right,m.inputs.right);
    v.wheel=card.wheel_deg;
    // With no air and no weight nothing turns the wheel: the solver's rest of 0 is no rest.
    if (!aileron_indifferent(m.figures,m.inputs)) v.rest=card.rest_deg;
    v.model_lb=card.model_lb;v.drive_lb=card.drive_lb;v.saturated=card.saturated;v.floored=card.floored;v.engaged=card.engaged;
    const auto solution=f.roll_solved?f.roll_solution:solve_roll(m,roll_counts_from_encoder(f.encoder[0]));
    const double drive=solution.strength*roll_units_per_count_percent()/std::clamp(double(m.units_per_nm),1.0,1e6);
    if (solution.slope_nm_per_count>0 && solution.strength>0) v.shrink=drive/solution.slope_nm_per_count;
    return v;
}
inline std::string degrees(double d) {return std::to_string(std::lround(std::abs(d)))+"\xC2\xB0";}
// The roll header's note, beside "ROLL": from note_x to the card's inner edge, at 13 px.
inline constexpr float note_x=74,note_width=320-16-note_x;
// "Wings 110 kt · rests 6° right": the air over the ailerons and where the wheel rests.
// Too wide for the header, it drops each wing's air for their mean, then
// shortens the side to R or L, so the direction is never cut off.
inline std::string roll_note(const RollView& r,float width=note_width) {
    const auto kt=[](double v){return std::to_string(std::lround(std::isfinite(v)?std::max(0.0,v):0.0));};
    const std::string mean="Wings "+kt((r.left.kt+r.right.kt)/2)+" kt";
    const std::string air=std::abs(r.left.kt-r.right.kt)>=5?"Wings "+kt(r.left.kt)+"/"+kt(r.right.kt)+" kt":mean;
    const bool level=std::isfinite(r.rest) && std::abs(r.rest)<0.5;
    const std::string side=r.rest>0?"right":"left";
    const std::string rest=!std::isfinite(r.rest)?"stays where left":level?"rests level":"rests "+degrees(r.rest)+" "+side;
    const std::string brief=!std::isfinite(r.rest) || level?rest:"rests "+degrees(r.rest)+(r.rest>0?" R":" L");
    for (const auto& candidate:{air+" \xC2\xB7 "+rest,mean+" \xC2\xB7 "+rest,mean+" \xC2\xB7 "+brief})
        if (text_width(candidate,13,false)<=width) return candidate;
    return mean+" \xC2\xB7 "+brief;
}
// The pitch line's words, for the wheel. Session, force feel and blending are the yoke's, shared with pitch.
inline std::string roll_status(const RollView& r,const tail::View& p) {
    if (!p.session) return p.force_feel?"Force feel starting":"Force feel off \xC2\xB7 the yoke is free";
    if (p.blending) return "Blending to the new feel";
    if (!r.engaged) return "Force feel starting"; // roll not yet solved: the card is the preview solve's
    if (r.saturated) {
        double ratio=r.shrink*100;
        if (!std::isfinite(ratio) && std::abs(r.model_lb)>=0.1) ratio=std::abs(r.drive_lb/r.model_lb)*100;
        if (!std::isfinite(ratio)) return "Saturated \xC2\xB7 lighter than the model";
        if (ratio<1) return "Saturated \xC2\xB7 yoke under 1% as stiff";
        return "Saturated \xC2\xB7 yoke "+std::to_string(std::min(99L,std::lround(ratio)))+"% as stiff";
    }
    if (r.floored) {
        // Force-exact, the drive differs only where the centre is clamped, and is then lighter.
        const double d=std::abs(r.drive_lb-r.model_lb);
        if (d<0.05) return "Floored \xC2\xB7 no difference here";
        return "Floored \xC2\xB7 "+tail::pounds(d)+(std::abs(r.drive_lb)>std::abs(r.model_lb)?" lb firmer than model":" lb lighter than model");
    }
    return "Matches the model";
}
// One aileron seen from its wingtip, nose to the right, hinge at (hx, hy), at
// 0.36 of the page's size: the wing's trailing part, the aileron at its
// deflection, a red tint for the stalled share, air streaks at the panel's
// angle, and the page's weight, air and float arrows.
// Everything stays within the row's band, hy-32..hy+30, between the ROLL
// header and the status line: the aileron is drawn to at most 30 degrees and
// an arrow that would leave the band is shortened along its own line.
inline constexpr float aileron_band_up=32,aileron_band_down=30;
inline void draw_aileron(monitor::Canvas& c,const RollSide& s,float hx,float hy,const char* label) {
    using monitor::Point;using monitor::rgb;
    constexpr float k=0.36f,len=150*k;
    constexpr double deg=3.14159265358979323846/180;
    const double stalled=std::isfinite(s.stalled)?std::clamp(s.stalled,0.0,1.0):0.0;
    // Streaks: the page's, still, each turned to the panel's angle; fainter
    // with less air and with the stall.
    const double v=std::isfinite(s.kt)?std::max(0.0,s.kt)*0.514444:0.0;
    const float fade=float(std::min(1.0,v/22)*(1-.6*stalled));
    if (fade>0.02f) {
        const double a=(std::isfinite(s.alpha)?std::clamp(s.alpha,-30.0,30.0):0.0)*deg;
        const float L=float(std::min(60.0,v*2))*k,ca=float(std::cos(a)),sa=float(std::sin(a));
        constexpr float at[][2]={{-50,-20},{-14,-14},{14,-21},{-36,15},{-2,21},{24,13}};
        for (const auto& p:at) {
            const float mx=hx+p[0]+L/2,my=hy+p[1];
            c.line(mx-ca*L/2,my-sa*L/2,mx+ca*L/2,my+sa*L/2,rgb(0x3a4450,fade),1.4f);
        }
    }
    // The wing's trailing part, fixed: the page's path, sampled.
    std::vector<Point> wing;
    const auto at=[&](float x,float y){return Point{hx+x*k,hy+y*k};};
    const auto cubic=[&](Point a,Point b,Point cc,Point d) {
        for (int i=0;i<8;++i) {
            const float t=float(i)/8,u=1-t;
            wing.push_back({u*u*u*a.x+3*u*u*t*b.x+3*u*t*t*cc.x+t*t*t*d.x,u*u*u*a.y+3*u*u*t*b.y+3*u*t*t*cc.y+t*t*t*d.y});
        }
    };
    cubic(at(0,-16),at(50,-30),at(110,-30),at(140,-6));wing.push_back(at(150,0));
    cubic(at(140,6),at(110,18),at(50,16),at(0,16));
    c.polygon(wing,rgb(0x39424d));c.outline(wing,rgb(0x56606b),1);
    // The aileron, turned about the hinge: trailing edge up for a positive deflection.
    const double u=(std::isfinite(s.deflection)?std::clamp(s.deflection,-30.0,30.0):0.0)*deg;
    const float cu=float(std::cos(u)),su=float(std::sin(u));
    const auto turn=[&](float x,float y){return Point{hx+x*cu-y*su,hy+x*su+y*cu};};
    const std::vector<Point> aileron{turn(0,-16*k),turn(-len,-3*k),turn(-len,3*k),turn(0,16*k)};
    c.polygon(aileron,rgb(0x5b6672));c.outline(aileron,rgb(0x7a8591),1);
    if (stalled>0.01) c.polygon(aileron,rgb(0xe5484d,float(stalled*.55)));
    std::vector<Point> hinge;
    for (int i=0;i<12;++i) hinge.push_back({hx+2.8f*float(std::cos(i*3.14159265/6)),hy+2.8f*float(std::sin(i*3.14159265/6))});
    c.polygon(hinge,rgb(0x15181c));c.outline(hinge,rgb(0xc9d0d6),1.1f);
    // Arrows grow with the square root of the moment, as on the page, but
    // reach their 19 px at 8 N·m (the page's 110 px cap is at 3.8), so a
    // roll's moments still differ and the row holds them at the stops.
    const auto px=[&](double nm){return std::isfinite(nm)?float((nm<0?-1:1)*19*std::sqrt(std::min(std::abs(nm),8.0)/8)):0.0f;};
    const auto chord=[&](float f){return turn(-len*f,0);};
    const Point w=chord(.35f),a=chord(.5f),f=chord(.75f);
    const float nx=-su,ny=cu; // normal to the chord, down on screen when level
    // A slightly smaller head than the tail's, so short arrows still show a
    // shaft. Its wings stand 2.7 px off the shaft, so the end keeps that
    // margin, as much of it as is vertical, inside the band.
    const auto bounded=[&](Point from,float dx,float dy,monitor::Color col) {
        const float slack=2.7f*std::abs(dx)/std::max(std::hypot(dx,dy),1e-3f);
        const float top=hy-aileron_band_up+slack,bottom=hy+aileron_band_down-slack;
        float t=1;
        if (from.y+dy<top && dy<0) t=std::max(0.0f,(top-from.y)/dy);
        if (from.y+dy>bottom && dy>0) t=std::max(0.0f,(bottom-from.y)/dy);
        tail::arrow(c,from.x,from.y,from.x+dx*t,from.y+dy*t,col,.3f);
    };
    bounded(w,0,-px(s.weight),rgb(0xc9d0d6));
    bounded(a,-nx*px(s.air),-ny*px(s.air),rgb(0x58a6ff));
    bounded(f,-nx*px(s.lift),-ny*px(s.lift),rgb(0x3fb950));
    c.text(hx+6,hy+10,11,label,monitor::muted);
}
// The wheel from the pilot's seat at (cx, cy), at 0.14 of the page's: its
// travel ring with the level and stop ticks, the wheel turned, a white tick
// where it rests, and an amber arc on the ring for which way the ailerons turn it.
inline void draw_wheel(monitor::Canvas& c,const RollView& r,float cx,float cy) {
    using monitor::Point;using monitor::rgb;
    constexpr float s=0.14f,ring=(118+22)*s;
    constexpr double pi=3.14159265358979323846,deg=pi/180;
    for (int i=0;i<40;++i) {
        const double a0=i*2*pi/40,a1=a0+2*pi/40*.3;
        c.line(cx+ring*float(std::cos(a0)),cy+ring*float(std::sin(a0)),cx+ring*float(std::cos(a1)),cy+ring*float(std::sin(a1)),rgb(0x3a444f),1);
    }
    const auto tick=[&](double a,float r0,float r1,monitor::Color col,float width) {
        const float sa=float(std::sin(a)),ca=float(std::cos(a));
        c.line(cx+sa*r0,cy-ca*r0,cx+sa*r1,cy-ca*r1,col,width);
    };
    for (const double a:{-86.0,0.0,86.0}) tick(a*deg,(118+12)*s,ring,rgb(0x56606b),1.2f);
    // The page's wheel outline as a strip between its outer and inner edges, mirrored.
    const double t=(std::isfinite(r.wheel)?std::clamp(r.wheel,-86.0,86.0):0.0)*deg;
    const float ct=float(std::cos(t)),st=float(std::sin(t));
    const auto place=[&](Point p){return Point{cx+(p.x*ct-p.y*st)*s,cy+(p.x*st+p.y*ct)*s};};
    const auto bez=[](Point a,Point b,Point cc,Point d,float q) {
        const float u=1-q;
        return Point{u*u*u*a.x+3*u*u*q*b.x+3*u*q*q*cc.x+q*q*q*d.x,u*u*u*a.y+3*u*u*q*b.y+3*u*q*q*cc.y+q*q*q*d.y};
    };
    const monitor::Color body=rgb(0xc9d0d6);
    for (const float m:{-1.0f,1.0f}) {
        const auto P=[&](float x,float y){return Point{m*x,y};};
        constexpr int n=10;
        std::vector<Point> outer,inner,cap;
        for (int i=0;i<=n;++i) {
            const float q=float(i)/n;
            outer.push_back(place(bez(P(-26,24),P(-120,46),P(-162,-30),P(-140,-96),q)));
            // Inner edge, bottom to top: the curve from the centre, then the straight to the horn's top.
            inner.push_back(place(i<=6?bez(P(0,14),P(-30,14),P(-66,6),P(-78,-30),float(i)/6):
                Point{m*(-78-16*float(i-6)/4),-30-66*float(i-6)/4}));
            cap.push_back(place(bez(P(-140,-96),P(-130,-124),P(-100,-124),P(-94,-96),q)));
        }
        for (int i=0;i<n;++i) c.polygon({outer[i],outer[i+1],inner[i+1],inner[i]},body);
        c.polygon(cap,body);
    }
    c.polygon({place({-26,24}),place({0,14}),place({26,24})},body);
    std::vector<Point> hub;
    for (int i=0;i<12;++i) hub.push_back({cx+30*s*float(std::cos(i*pi/6)),cy+30*s*float(std::sin(i*pi/6))});
    c.polygon(hub,rgb(0x39424d));c.outline(hub,rgb(0x7a8591),1);
    if (std::isfinite(r.rest)) tick(std::clamp(r.rest,-86.0,86.0)*deg,ring-5,ring+3,monitor::ink,1.6f);
    // Which way the ailerons turn the wheel, and how hard: the page's arc, clockwise for right.
    const double f=std::isfinite(r.model_lb)?r.model_lb:0.0,mag=std::min(1.6,std::abs(f)/1.5);
    if (std::abs(f)>=0.005 && mag>=.03) {
        const double a0=-pi/2-0.3,sg=f>0?1:-1,a1=a0+sg*mag*1.6;
        const float rr=ring+1;
        const auto on=[&](double a){return Point{cx+rr*float(std::cos(a)),cy+rr*float(std::sin(a))};};
        const int steps=std::max(4,int(std::abs(a1-a0)/0.15));
        for (int i=0;i<steps;++i) {const auto p=on(a0+(a1-a0)*i/steps),q=on(a0+(a1-a0)*(i+1)/steps);c.line(p.x,p.y,q.x,q.y,monitor::pitch,1.8f);}
        const auto e=on(a1);const float tx=float(-std::sin(a1)*sg),ty=float(std::cos(a1)*sg);
        c.triangle({e.x+tx*5,e.y+ty*5},{e.x-ty*3,e.y+tx*3},{e.x+ty*3,e.y-tx*3},monitor::pitch);
    }
}
// The pitch card and the alert cards are 320 wide, drawn at the right of the
// 380-wide HUD window so they keep its right edge; the developer cards
// (readback mismatch, debug log) keep the full 380.
inline constexpr float card_width=320,card_offset=380-card_width,line_width=card_width-26;
// With the ailerons the card is taller: pitch above the hairline at roll_top,
// roll below. The HUD window is that tall, whatever card it shows.
inline constexpr float roll_card_height=266,roll_top=152;
inline constexpr int window_width=380,window_height=int(roll_card_height);
// The window's height before the ailerons: the saved place is kept on screen at this height.
inline constexpr int saved_height=216;
// yoke_status is the plugin's single plain-language status line; when given it
// heads every alert so the HUD never disagrees with the menu or screen line.
// reason is the stop's short reason while Retry is offered, else empty.
inline monitor::Canvas render(const monitor::Model& model,const State& state,const Settings& s,recording::State recorder,
    std::uint64_t,double now,const std::string& yoke_status={},const std::optional<tail::View>& card={},const std::string& reason={}) {
    using namespace monitor;
    Canvas c;if (!s.visible) return c;
    auto f=model.live();const auto v=state.view(f,s,recorder,now,card.has_value());
    // Quiet really means no overlay, including while ordinary input is active
    // and the motors are intentionally off. Logging is independent of drawing.
    if (v.context==Context::quiet) return c;
    if (now-f.time+f.age_ms/1000>.25) f.fresh=false;
    const Color panel=rgb(0x11191f,float(s.opacity)/100),rule=rgb(0x9baeb6,.28f);
    if (v.context==Context::trim || v.context==Context::limit || v.context==Context::detail) {
        // The pitch card: the feel page's tail, and how close the yoke is to the model.
        const auto wings=roll_view(f);
        const bool both=card && wings;
        const float height=both?roll_card_height:card?200.0f:126.0f;
        c.rect(0,0,card_width,height,panel);
        if (both) {c.rect(0,0,3,roll_top,monitor::pitch);c.rect(0,roll_top,3,height-roll_top,monitor::roll);}
        else c.rect(0,0,3,height,monitor::pitch);
        c.text(18,11,13,"PITCH",ink,true);
        if (both) {
            // Each axis: its header with the air and where it rests, the drawing, then the solver's words.
            c.text(note_x,11,13,fit(tail::context(*card),13,false,note_width),muted);
            tail::draw(c,*card,135,78);
            if (const auto words=tail::float_words(*card);!words.empty()) c.text(175,91,11,words,rgb(0x3fb950));
            c.text(18,126,16,fit(state.moving?"Move HUD, then finish in menu":tail::status(*card),16,false,line_width),ink);
            c.line(16,roll_top,card_width-16,roll_top,rule);
            c.text(18,roll_top+8,13,"ROLL",ink,true);
            c.text(note_x,roll_top+8,13,fit(roll_note(*wings,note_width),13,false,note_width),muted);
            draw_aileron(c,wings->left,72,roll_top+54,"Left wing");
            draw_aileron(c,wings->right,248,roll_top+54,"Right wing");
            draw_wheel(c,*wings,160,roll_top+54);
            c.text(18,roll_top+86,16,fit(roll_status(*wings,*card),16,false,line_width),ink);
        } else if (card) {
            tail::draw(c,*card,135,84);
            if (const auto words=tail::float_words(*card);!words.empty()) c.text(175,97,11,words,rgb(0x3fb950));
            c.line(16,139,card_width-16,139,rule);
            c.text(18,148,16,fit(state.moving?"Move HUD, then finish in menu":tail::status(*card),16,false,line_width),ink);
            c.text(18,174,15,fit(tail::context(*card),15,false,line_width),muted);
        } else {
            c.text(18,40,16,state.moving?"Move HUD, then finish in menu":"Waiting for the model",ink,true);
            c.text(18,70,15,"No flight data or aircraft profile yet",muted);
        }
        c.shift(card_offset);
    } else if (v.context==Context::mismatch || v.context==Context::recording) {
        // Developer cards, as before.
        const Color accent=v.context==Context::recording?danger:v.channel==1?roll:monitor::pitch;
        const float height=v.context==Context::recording?172.0f:216.0f;
        c.rect(0,0,380,height,panel);c.rect(0,0,3,height,accent);
        c.text(18,11,13,"YOKE FORCE FEEDBACK",ink,true);c.text(326,12,11,"DEBUG",muted);
        c.line(16,36,364,36,rule);
        c.text(18,49,18.0f,label(v.context),ink,true);
        if (v.context==Context::mismatch) {
            const auto unit=v.channel==2?" ct":"%";
            c.text(18,78,28,std::to_string(value(f,v.channel,true))+unit,accent);
            c.text(207,78,28,std::to_string(value(f,v.channel))+unit,ink);
            c.text(18,114,12,"Requested target",muted);c.text(207,114,12,"Verified readback",muted);
            trace(c,model.history(),v.channel,now,accent);
            c.text(18,194,12,v.channel==0?"Pitch spring scale":v.channel==1?"Roll spring scale":"Pitch neutral counts",muted);c.text(341,194,12,"12s",muted);
        } else {
            c.text(18,84,15,"Debug history could not be saved.",ink);
            c.text(18,111,14,"Flight controls are independent of recording.",muted);
            c.text(18,147,13,motor_label(f),danger);
        }
    } else {
        // Alerts: what is wrong, then what happened or what to do, in three lines.
        const bool alert=v.context==Context::fault || v.context==Context::disconnected || v.context==Context::stale ||
            v.context==Context::motorstate || v.context==Context::inputunavailable;
        const bool shared_line=!yoke_status.empty();
        // The shared line reads "what is wrong - what to do": the heading, then the remedy.
        const auto parts=status_parts(shared_line?yoke_status:std::string{});
        const bool reference=f.mode==Mode::reference;
        const std::string heading=shared_line?parts.first:v.context==Context::initializing && !reference?"Yoke not initialized":label(v.context);
        const char* first=v.context==Context::initializing?(reference?"Finding the yoke's center and travel.":"Position input is not ready."):
            v.context==Context::stale?"No recent readings from the yoke.":v.context==Context::disconnected?"No USB connection to the yoke.":
            v.context==Context::motorstate?"Motors do not match the loading.":v.context==Context::paused?"Force feedback is suspended.":
            v.context==Context::inputunavailable?"Movement is not reaching the aircraft.":"Controller protection stopped loading.";
        const char* second=v.context==Context::initializing?(reference?"Input returns when it finishes.":"Check the Yoke (Brunner) plugin menu."):
            "Open the force monitor for details.";
        c.rect(0,0,card_width,126,panel);c.rect(0,0,3,126,alert?danger:monitor::pitch);
        c.text(18,11,13,"YOKE",ink,true);
        c.text(18,36,16,fit(heading,16,true,line_width),ink,true);
        if (!reason.empty()) {
            c.text(18,64,15,fit(reason,15,false,line_width),danger);
            c.text(18,90,15,"Retry in the Yoke menu",muted);
        } else {
            c.text(18,64,15,fit(parts.second.empty()?std::string(first):parts.second,15,false,line_width),ink);
            c.text(18,90,15,second,muted);
        }
        c.shift(card_offset);
    }
    // Fade identity and content together; never leave an unexplained strip.
    for (auto& item:c.items) item.color.a*=v.alpha;
    return c;
}
}
