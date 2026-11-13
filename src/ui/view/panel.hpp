#pragma once
#include "ui/view/telemetry.hpp"
#include <cstdio>

namespace brunner::monitor {
struct Color {float r,g,b,a=1;};
inline Color rgb(unsigned c,float a=1) {return {float((c>>16)&255)/255,float((c>>8)&255)/255,float(c&255)/255,a};}
struct Point {float x,y;};
inline std::array<Point,6> line_triangles(float x,float y,float x2,float y2,float width) {
    const float dx=x2-x,dy=y2-y,length=std::hypot(dx,dy);
    if (length<0.001f) return {};
    const float ox=-dy/length*width/2,oy=dx/length*width/2;
    const Point a{x+ox,y+oy},b{x2+ox,y2+oy},c{x2-ox,y2-oy},d{x-ox,y-oy};
    // Match rect() winding in canvas coordinates. X-Plane may enable culling.
    return {a,c,b,a,d,c};
}
inline const Color bg=rgb(0x11191f),surface=rgb(0x1c282f),ink=rgb(0xf1eee5),muted=rgb(0x9baeb6),
    grid=rgb(0x2c3c44),pitch=rgb(0xeeb46d),roll=rgb(0x76c9db),danger=rgb(0xf18b82);
// A triangle is (x, y), (w, h), (x3, y3); a line runs from (x, y) to (w, h).
struct Primitive {
    enum class Kind { rect,line,text,triangle } kind;
    float x=0,y=0,w=0,h=0,size=0;Color color{};std::string label;bool bold=false;float x3=0,y3=0;
};
struct Canvas {
    std::vector<Primitive> items;
    void rect(float x,float y,float w,float h,Color c) {items.push_back({Primitive::Kind::rect,x,y,w,h,0,c,{},false,0,0});}
    void line(float x,float y,float x2,float y2,Color c,float width=1) {items.push_back({Primitive::Kind::line,x,y,x2,y2,width,c,{},false,0,0});}
    void text(float x,float y,float size,std::string text,Color c=ink,bool bold=false) {
        items.push_back({Primitive::Kind::text,x,y,0,0,size,c,std::move(text),bold,0,0});
    }
    // Wound as rect() is in canvas coordinates (y down), whatever the order
    // given: X-Plane may enable culling.
    void triangle(Point a,Point b,Point c,Color color) {
        if ((b.x-a.x)*(c.y-a.y)-(b.y-a.y)*(c.x-a.x)<0) std::swap(b,c);
        items.push_back({Primitive::Kind::triangle,a.x,a.y,b.x,b.y,0,color,{},false,c.x,c.y});
    }
    // A convex polygon, filled as a fan.
    void polygon(const std::vector<Point>& p,Color color) {
        for (std::size_t i=2;i<p.size();++i) triangle(p[0],p[i-1],p[i],color);
    }
    void outline(const std::vector<Point>& p,Color color,float width=1) {
        for (std::size_t i=0;i<p.size();++i) {const auto& a=p[i];const auto& b=p[(i+1)%p.size()];line(a.x,a.y,b.x,b.y,color,width);}
    }
    // Moves everything right by dx.
    void shift(float dx) {
        for (auto& p:items) {p.x+=dx;if (p.kind==Primitive::Kind::line || p.kind==Primitive::Kind::triangle) p.w+=dx;if (p.kind==Primitive::Kind::triangle) p.x3+=dx;}
    }
};
inline std::string fixed(double value,int places=0) {
    char b[64];std::snprintf(b,sizeof(b),places==1?"%.1f":places==2?"%.2f":"%.0f",value);return b;
}
inline const char* mode_name(Mode m) {
    switch (m) {
    case Mode::input:return "Input only";case Mode::reference:return "Referencing";
    case Mode::bench:return "Bench spring";case Mode::flight:return "Flight commissioning";
    case Mode::paused:return "Paused";case Mode::fault:return "Controller fault";
    default:return "Disconnected";
    }
}
inline std::string motor_label(const Frame& f) {
    if (f.mode==Mode::fault) return !f.fresh?"FAULT / STATE UNKNOWN":f.motors[0] || f.motors[1]?"FAULT: MOTOR ENABLED":"FAULT: MOTORS OFF";
    if (!f.fresh) return "MOTOR STATE UNKNOWN";
    if (f.motors[0] && f.motors[1]) return "MOTORS ENABLED";
    if (f.motors[0] || f.motors[1]) return "ONE MOTOR ENABLED";
    return "MOTORS OFF";
}
inline void dash(Canvas& c,float x,float y,float x2,float y2,Color color,float width=1) {
    const float len=std::hypot(x2-x,y2-y);
    if (len<0.01f) return;
    for (float d=0;d<len;d+=8) {
        const float a=d/len,b=std::min(d+3,len)/len;
        c.line(x+(x2-x)*a,y+(y2-y)*a,x+(x2-x)*b,y+(y2-y)*b,color,width);
    }
}
inline void button(Canvas& c,float x,float w,const char* label,bool selected=false) {
    c.rect(x,93,w,32,selected?rgb(0xe9e1d0):surface);
    c.text(x+12,100,15,label,selected?bg:ink,selected);
}
inline const char* event(const Frame& before,const Frame& after) {
    if (before.fresh && !after.fresh) return "STALE";
    if (!before.fresh && after.fresh) return "FRESH";
    if (after.mode==Mode::fault && before.mode!=Mode::fault) return "FAULT";
    if (before.active() && !after.active()) return "STOP";
    if (!before.active() && after.active()) return "ENABLE";
    if (!before.limited && after.limited) return "TRIM LIMIT";
    return nullptr;
}
inline void chart(Canvas& c,const Model& model,unsigned channel,float top,double end) {
    constexpr float left=280,right=988,height=90;
    const auto& h=model.history();
    const double start=end-model.seconds();
    const auto x=[&](double time){return left+float((time-start)/model.seconds())*(right-left);};
    double limit=40;
    if (channel==2) {
        limit=600;
        for (std::size_t i=0;i<h.count;++i) {
            const auto& f=h.at(i);
            if (f.time<start || f.time>end || !valid(f,channel)) continue;
            limit=std::max(limit,double(std::max(std::abs(value(f,channel)),std::abs(value(f,channel,true)))));
        }
        limit=std::ceil(limit/100)*100;
    }
    const double lower=channel==2?-limit:0;
    const auto y=[&](double v){return top+height-float((std::clamp(v,lower,limit)-lower)/(limit-lower))*height;};
    for (int tick=0;tick<=4;++tick) {
        const float yy=top+height*float(tick)/4;
        const auto v=limit-(limit-lower)*tick/4;
        c.line(left,yy,right,yy,grid);
        c.text(230,yy-7,13,fixed(v)+(channel==2?"":"%"),muted);
    }
    for (int tick=0;tick<=4;++tick) {
        const float xx=left+(right-left)*float(tick)/4;
        c.line(xx,top,xx,top+height,grid);
    }
    const Color color=channel==0?pitch:channel==1?roll:ink;
    const Frame* previous=nullptr;
    for (std::size_t i=0;i<h.count;++i) {
        const auto& f=h.at(i);
        if (f.time<start || f.time>end) {previous=nullptr;continue;}
        if (previous && f.time-previous->time<0.3 && valid(f,channel) && valid(*previous,channel)
            && f.mode==previous->mode) {
            // Step traces preserve command changes without implying a ramp between samples.
            const float x1=x(previous->time),x2=x(f.time);
            const float y1=y(value(*previous,channel)),y2=y(value(f,channel));
            c.line(x1,y1,x2,y1,color,2);c.line(x2,y1,x2,y2,color,2);
            if (value(f,channel,true)!=value(f,channel) || value(*previous,channel,true)!=value(*previous,channel)) {
                const Color dim{color.r,color.g,color.b,0.45f};
                dash(c,x1,y(value(*previous,channel,true)),x2,y(value(*previous,channel,true)),dim,1.5f);
                dash(c,x2,y(value(*previous,channel,true)),x2,y(value(f,channel,true)),dim,1.5f);
            }
        }
        previous=&f;
    }
    float last_event_x=-1000;
    for (std::size_t i=1;i<h.count;++i) {
        const auto& f=h.at(i);
        if (f.time<start || f.time>end) continue;
        if (const auto label=event(h.at(i-1),f)) {
            const float xx=x(f.time);
            dash(c,xx,top-4,xx,top+height,muted);
            if (channel==0 && xx-last_event_x>90 && xx<right-75) {c.text(xx+4,top-19,11,label,muted,true);last_event_x=xx;}
        }
    }
    if (model.selected()) {
        const float xx=x(model.displayed().time);
        if (xx>=left && xx<=right) c.line(xx,top-4,xx,top+height,ink);
    }
    if (channel==2) for (int tick=0;tick<=4;++tick) {
        const float xx=left+(right-left)*float(tick)/4;
        c.text(xx-(tick==4?26:0),top+height+13,13,tick==4?"now":("-"+std::to_string(model.seconds()*(4-tick)/4)+"s"),muted);
    }
}
inline Canvas render(const Model& model,bool example=false,double wall_now=-1,const std::string& log_status={},const std::string& yoke_status={}) {
    Canvas c;c.items.reserve(12000);
    Frame live=model.live();
    if (wall_now>=live.time) {
        live.age_ms+=(wall_now-live.time)*1000;
        live.fresh=live.fresh && live.age_ms<=250;
        live.position_valid=live.position_valid && live.fresh;
    }
    const Frame f=model.frozen()?model.displayed():live;
    const double end=model.frozen()?model.end():std::max(model.end(),wall_now);
    c.rect(0,0,1200,800,bg);
    c.text(28,19,34,"Yoke force feedback",ink,true);
    c.text(29,59,13,"BRUNNER DIRECT  /  C172",muted,true);
    c.rect(600,23,320,38,surface);
    c.text(614,32,19,motor_label(live),live.fresh && live.mode!=Mode::fault?ink:danger,true);
    c.text(944,28,15,live.fresh?("USB fresh  "+fixed(live.age_ms)+" ms"):"USB STALE / OFFLINE",live.fresh?roll:danger,true);
    c.text(944,53,12,example?"SYNTHETIC PREVIEW":"LIVE HARDWARE STATUS",muted);
    c.line(28,80,1172,80,grid);
    c.text(29,100,14,model.frozen()?"FROZEN HISTORY - live status above":yoke_status.empty()?std::string(mode_name(f.mode)):std::string(mode_name(f.mode))+"  -  "+yoke_status,model.frozen()?pitch:muted,true);
    button(c,678,126,model.frozen()?"Resume view":"Freeze view",model.frozen());
    button(c,818,65,"15s",model.seconds()==15);button(c,891,65,"60s",model.seconds()==60);
    button(c,964,65,"120s",model.seconds()==120);button(c,1043,129,"Clear history");
    c.line(28,138,1172,138,grid);
    const std::array<const char*,3> names{{"PITCH  /  elevator","ROLL  /  aileron","PITCH NEUTRAL"}};
    for (unsigned channel=0;channel<3;++channel) {
        const float top=158+float(channel)*136;
        const Color color=channel==0?pitch:channel==1?roll:ink;
        c.rect(28,top,3,111,color);
        c.text(42,top,17,names[channel],color,true);
        c.text(42,top+26,13,channel==2?"Controller position, counts":"Controller spring scale",muted);
        const bool active=valid(f,channel);
        c.text(42,top+45,42,active?(std::to_string(value(f,channel))+(channel==2?"":"%")):"--",color,true);
        const char* state=!f.fresh?"stale / unknown":f.mode==Mode::reference?"reference motion":active?"last verified setting":"inactive";
        c.text(42,top+89,13,state,muted);
        if (channel<2) c.text(42,top+108,12,f.position_valid?"Yoke "+fixed(f.position[channel==0?1:0]*100,1)+"% travel":"Yoke position unavailable",muted);
        chart(c,model,channel,top+23,end);
        c.line(1010,top+12,1010,top+112,grid);
        const auto e=model.history().extent(channel,end,model.seconds());
        c.text(1030,top+15,13,channel==2?"Max abs, window":"Peak, window",muted);
        c.text(1030,top+41,32,e.valid?(std::to_string(channel==2?e.absolute:e.high)+(channel==2?"":"%")):"--",color,true);
        c.text(1030,top+84,12,e.valid?("min "+std::to_string(e.low)+(channel==2?" counts":"%")):"no active samples",muted);
        if (channel<2) c.line(28,top+125,1172,top+125,grid);
    }
    c.text(280,578,12,"Solid: verified setting    Dotted: requested target    Gaps: inactive or stale",muted);
    c.text(29,603,13,"PROVENANCE",ink,true);
    const std::string stamp=model.selected()?"Selected sample "+fixed(model.end()-f.time,1)+"s ago":model.frozen()?"Frozen snapshot":"Current command path";
    c.text(280,603,13,stamp,model.frozen()?pitch:muted);
    c.line(28,626,1172,626,grid);
    c.text(42,637,12,"SOURCE",muted,true);c.text(386,637,12,"MAPPING & BOUNDS",muted,true);c.text(856,637,12,"CONTROLLER",muted,true);
    c.line(28,661,1172,661,grid);
    const bool flight=f.mode==Mode::flight || f.mode==Mode::paused;
    const bool applicable=f.mode==Mode::flight || f.mode==Mode::bench;
    c.text(42,673,16,f.mode==Mode::reference?"Reference / centering sequence":flight?(f.sim_valid?"Tail / wing  "+fixed(f.simulator.flow.tail,0)+" / "+fixed(f.simulator.flow.roll,0)+" Pa":"Local flow unavailable"):"Bench spring settings");
    std::string gain=flight && f.sim_valid?"Local flow; ":"Requested ";
    if (flight) gain+="roll floor "+std::to_string(f.settings.roll_minimum_percent)+"%; ";
    gain+=(flight?"caps P/R ":"P / R ")+std::to_string(f.ceilings.pitch_percent)+" / "+std::to_string(f.ceilings.roll_percent)+"%";
    c.text(386,673,15,f.mode==Mode::reference?"Controller initialization; no spring telemetry":gain);
    c.text(856,673,15,applicable && f.active()?"P / R  "+std::to_string(f.applied.pitch_percent)+" / "+std::to_string(f.applied.roll_percent)+"%":"Inactive / not applied",applicable && f.active()?ink:muted);
    c.line(28,701,1172,701,grid);
    c.text(42,713,16,f.mode==Mode::reference?"Position centering":flight?(f.sim_valid?"Aircraft trim  "+fixed(f.simulator.trim*100,1)+"%":"Aircraft trim unavailable"):"Bench neutral  "+std::to_string(f.ceilings.pitch_trim)+" counts");
    c.text(386,713,15,f.mode==Mode::reference?"Reference motion is not a measured load":flight?"Elevator model; centre and force solved":"Signed position; test bound +/-6000");
    c.text(856,713,15,valid(f,2)?std::to_string(f.applied.pitch_trim)+" counts"+(f.limited?"  LIMITED":""):"Inactive / not applied",f.limited?pitch:muted);
    c.line(28,744,1172,744,grid);
    c.text(29,760,14,"Hand force: not measured",ink,true);
    c.text(386,760,14,f.fresh && f.bias_verified?"Bias P / R: "+std::to_string(f.bias[1])+" / "+std::to_string(f.bias[0])+" (last verified)":"Bias: not sampled",muted);
    c.text(856,760,13,log_status.empty()?"Commissioning telemetry":log_status,muted);
    c.text(29,786,11,"Read-only display. Click a trace to inspect history. The simulator and motor control continue while the view is frozen.",muted);
    return c;
}
inline bool click(Model& model,float x,float y) {
    if (y>=93 && y<=125) {
        if (x>=678 && x<=804) model.freeze();
        else if (x>=818 && x<=883) model.range(15);
        else if (x>=891 && x<=956) model.range(60);
        else if (x>=964 && x<=1029) model.range(120);
        else if (x>=1043 && x<=1172) model.clear();
        else return false;
        return true;
    }
    if (x>=280 && x<=988 && y>=158 && y<=550) {model.select((x-280)/708);return true;}
    return false;
}
}
