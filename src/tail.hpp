#pragma once
#include "panel.hpp"

// The HUD's pitch card: the feel page's side view of the tail (workbench.html,
// drawTail), compact, with the solver's words for how close the yoke is to
// the model. Display only (docs/brunner/hud/pitch-card.md).
namespace brunner::tail {
struct View {
    ElevatorFigures figures{};ElevatorInputs inputs{};PitchSolution solution{};
    double yoke_counts=std::numeric_limits<double>::quiet_NaN(); // the drive's counts, forward positive; NaN when unknown
    bool force_feel=false,session=false,blending=false;
};
// Radians, trailing edge down positive, and moments in N·m at the hinge,
// trailing edge down positive: what the page draws, from the same model.
struct Geometry {double up=0,down=0,elevator=0,rest=0,drive_rest=0,tab=0,weight=0,air=0,net=0;bool drive_differs=false;};
inline Geometry geometry(const View& v) {
    const auto s=elevator_state(v.figures,v.inputs);
    const auto& p=v.solution;
    Geometry g;g.up=s.up;g.down=s.down;
    g.elevator=elevator_rad(s,std::isfinite(v.yoke_counts)?v.yoke_counts:p.rest_counts);
    g.rest=elevator_rad(s,p.rest_counts);
    g.drive_rest=std::isfinite(p.drive_rest_counts)?elevator_rad(s,p.drive_rest_counts):g.rest;
    // The page marks the drive's rest where it is half a percent of travel or more from the model's.
    g.drive_differs=v.session && std::isfinite(p.drive_rest_counts) && std::abs(p.drive_rest_counts-p.rest_counts)>=full_travel_counts()/200;
    const double t=std::isfinite(v.inputs.trim)?std::clamp(v.inputs.trim,-1.0,1.0):0.0;
    g.tab=(t>=0?t*v.figures.tab_down_deg:t*v.figures.tab_up_deg)*radians_per_degree;
    g.weight=s.w*std::cos(g.elevator);g.air=-s.k*(g.elevator-s.trim_rad);g.net=g.weight+g.air;
    return g;
}
inline std::string pounds(double lb) {return monitor::fixed(lb,lb<9.95?1:0);}
// "Saturated · yoke 2% as stiff": how close the yoke is to the model here.
inline std::string status(const View& v) {
    const auto& p=v.solution;
    if (!v.session) return v.force_feel?"Force feel starting":"Force feel off \xC2\xB7 the yoke is free";
    if (v.blending) return "Blending to the new feel";
    if (p.bias_limited) {
        const double off=(p.drive_rest_counts-p.rest_counts)/(full_travel_counts()/100);
        return "Force limit \xC2\xB7 rests "+std::to_string(std::max(1L,std::lround(std::abs(off))))+"% "+(off>0?"fwd":"aft")+" of model";
    }
    if (p.saturated) {
        const double r=p.shrink_ratio*100;
        if (!std::isfinite(r)) return "Saturated \xC2\xB7 lighter than the model";
        if (r<1) return "Saturated \xC2\xB7 yoke under 1% as stiff";
        return "Saturated \xC2\xB7 yoke "+std::to_string(std::min(99L,std::lround(r)))+"% as stiff";
    }
    if (p.floored) {
        const double d=std::abs(p.drive_force_lb-p.force_lb);
        return d<0.05?"Floored \xC2\xB7 no difference here":"Floored \xC2\xB7 "+pounds(d)+" lb firmer than model";
    }
    return "Matches the model";
}
// "Tail 98 kt · trim 7% nose up", in the page's words.
inline std::string context(const View& v) {
    std::string out;
    const double pa=v.inputs.tail_pa;
    if (std::isfinite(pa)) out="Tail "+std::to_string(std::lround(std::sqrt(2*std::max(0.0,pa)/1.225)/0.514444))+" kt \xC2\xB7 ";
    const double t=std::isfinite(v.inputs.trim)?std::clamp(v.inputs.trim,-1.0,1.0):0.0;
    out+=std::abs(t)<0.005?"trim neutral":"trim "+std::to_string(std::lround(std::abs(t)*100))+"% nose "+(t>0?"up":"down");
    return out;
}
// The pages' arrow at `k` times their size: a 5 px shaft and a 16 px head,
// none under 6 px.
inline void arrow(monitor::Canvas& c,float x1,float y1,float x2,float y2,monitor::Color col,float k) {
    const float dx=x2-x1,dy=y2-y1,len=std::hypot(dx,dy);
    if (len<6*k) return;
    const float ux=dx/len,uy=dy/len,head=std::min(16*k,len),bx=x2-ux*head,by=y2-uy*head;
    c.line(x1,y1,bx,by,col,5*k);
    c.triangle({x2,y2},{bx-uy*9*k,by+ux*9*k},{bx+uy*9*k,by-ux*9*k},col);
}
// The page's tail at `scale` about the hinge (hx, hy), nose to the right.
// Page sizes: elevator 230 with a 52 tab, stabilizer 342, arc radius 58,
// 26 px per 10 in·lb of moment.
inline void draw(monitor::Canvas& c,const View& v,float hx,float hy,float scale=0.45f) {
    using monitor::Point;using monitor::rgb;
    const auto g=geometry(v);
    const float k=scale,elen=230*k,tab_len=52*k;
    // A point in a frame turned by angle a (trailing edge down positive) about (ox, oy).
    const auto turn=[](float ox,float oy,double a,float px,float py) {
        const float ca=float(std::cos(a)),sa=float(std::sin(a));
        return Point{ox+px*ca+py*sa,oy-px*sa+py*ca};
    };
    const auto shape=[&](double a,const std::vector<Point>& local) {
        std::vector<Point> out;for (const auto& p:local) out.push_back(turn(hx,hy,a,p.x,p.y));return out;
    };
    // The stabilizer: the page's two cubics and a rounded nose, sampled.
    std::vector<Point> stab;
    const auto cubic=[&](Point a,Point b,Point cc,Point d) {
        for (int i=0;i<12;++i) {
            const float t=float(i)/12,u=1-t;
            stab.push_back({u*u*u*a.x+3*u*u*t*b.x+3*u*t*t*cc.x+t*t*t*d.x,u*u*u*a.y+3*u*u*t*b.y+3*u*t*t*cc.y+t*t*t*d.y});
        }
    };
    const auto at=[&](float x,float y) {return Point{hx+x*k,hy+y*k};};
    cubic(at(0,-12),at(120,-26),at(260,-26),at(330,-4));stab.push_back(at(342,0));
    cubic(at(330,4),at(260,18),at(120,14),at(0,12));
    c.polygon(stab,rgb(0x39424d));c.outline(stab,rgb(0x56606b),1);
    // Where the model rests hands-off: the page's dashed outline.
    const std::vector<Point> ghost{{0,-12*k},{-elen,-2*k},{-elen,2*k},{0,12*k}};
    const auto ghost_at=shape(g.rest,ghost);
    for (std::size_t i=0;i<ghost_at.size();++i) {
        const auto& a=ghost_at[i];const auto& b=ghost_at[(i+1)%ghost_at.size()];
        const float len=std::hypot(b.x-a.x,b.y-a.y);
        for (float d=0;d<len;d+=5.5f) {
            const float e=std::min(d+2.5f,len);
            c.line(a.x+(b.x-a.x)*d/len,a.y+(b.y-a.y)*d/len,a.x+(b.x-a.x)*e/len,a.y+(b.y-a.y)*e/len,rgb(0xc9d0d6),1.2f);
        }
    }
    // The drive's own rest, where the force limit holds it off the model's.
    if (g.drive_differs) c.outline(shape(g.drive_rest,ghost),monitor::ink,1.2f);
    // The elevator at the yoke, and the tab at the trim wheel's setting.
    const auto elevator=shape(g.elevator,{{0,-12*k},{-(elen-tab_len),-5*k},{-(elen-tab_len),5*k},{0,12*k}});
    c.polygon(elevator,rgb(0x5b6672));c.outline(elevator,rgb(0x7a8591),1);
    const Point tab_hinge=turn(hx,hy,g.elevator,-(elen-tab_len),0);
    std::vector<Point> tab;
    for (const auto& p:std::vector<Point>{{0,-5*k},{-tab_len+6*k,-1.5f*k},{-tab_len,0},{-tab_len+6*k,1.5f*k},{0,5*k}})
        tab.push_back(turn(tab_hinge.x,tab_hinge.y,g.elevator+g.tab,p.x,p.y));
    c.polygon(tab,rgb(0x3fb950));
    std::vector<Point> hinge;
    for (int i=0;i<12;++i) hinge.push_back({hx+3.2f*float(std::cos(i*3.14159265/6)),hy+3.2f*float(std::sin(i*3.14159265/6))});
    c.polygon(hinge,rgb(0x15181c));c.outline(hinge,rgb(0xc9d0d6),1.2f);
    // Arrows: the page's 26 px per 10 in·lb, capped so they stay in the band.
    const double px_per_nm=26/newton_metres_per_inlb/10*k;
    const auto cap=[&](double m) {return float(std::clamp(m*px_per_nm,-40.0,40.0));};
    const Point cg=turn(hx,hy,g.elevator,-40*k,0);
    arrow(c,cg.x,cg.y,cg.x,cg.y+cap(g.weight),rgb(0xc9d0d6),k);
    const Point cp=turn(hx,hy,g.elevator,-elen*.45f,0);
    const float air=cap(g.air),nx=float(std::sin(g.elevator)),ny=float(std::cos(g.elevator));
    arrow(c,cp.x,cp.y,cp.x+nx*air,cp.y+ny*air,rgb(0x58a6ff),k);
    // The force the pilot holds: the net moment, round the hinge.
    const double mag=std::min(1.6,std::abs(g.net)*px_per_nm/k/120);
    if (std::abs(g.net)>=0.05 && mag>=.04) {
        const double a0=3.14159265358979*1.15,sg=g.net>0?-1:1,a1=a0+sg*mag*2.2;
        const float r=58*k;
        const auto on=[&](double a) {return Point{hx+r*float(std::cos(a)),hy+r*float(std::sin(a))};};
        const int steps=std::max(4,int(std::abs(a1-a0)/0.12));
        for (int i=0;i<steps;++i) {
            const auto p=on(a0+(a1-a0)*i/steps),q=on(a0+(a1-a0)*(i+1)/steps);
            c.line(p.x,p.y,q.x,q.y,monitor::pitch,5*k);
        }
        const auto e=on(a1);const float tx=float(-std::sin(a1)*sg),ty=float(std::cos(a1)*sg);
        c.triangle({e.x+tx*14*k,e.y+ty*14*k},{e.x-ty*9*k,e.y+tx*9*k},{e.x+ty*9*k,e.y-tx*9*k},monitor::pitch);
    }
}
}
