#pragma once
// Aircraft profiles (decision 5): BrunnerDirect.profiles.json beside the
// plugin, written by Install-Brunner from ansible/brunner.yml. Every aircraft
// flies one: its own, matched by aircraft file, else the default (the stock
// C172 SP). The plugin reloads the file when it changes; one that does not
// parse is refused whole and the figures already flown stay.
#include "force/aileron.hpp"
#include "force/elevator.hpp"
#include "json.hpp"
#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace brunner {
inline constexpr char profiles_file[]="BrunnerDirect.profiles.json";
// One figure as the page shows it: its value, unit, how sure, and from where.
struct Figure {std::string key,unit,tag,source;double value=0;};
struct Profile {
    std::string name;
    bool matched=false; // the aircraft is one of this profile's own
    std::vector<std::string> aircraft; // paths under X-Plane's folder, '/' separated
    // X-Plane wing indices whose per-element dynamic pressure is the tail's;
    // read only on the profile's own aircraft.
    std::optional<std::array<int,2>> tail_sections;
    ElevatorFigures elevator;
    std::vector<Figure> figures; // in figure_table order
    // The ailerons (schema 2): the left wing's index first, then the right's;
    // read only on the profile's own aircraft.
    std::optional<std::array<int,2>> aileron_sections;
    // The wing roots, whose stall makes the buffet felt in pitch (force/buffet.hpp):
    // the wing's separated wake striking the tail. Read only on the profile's
    // own aircraft; without them pitch has no buffet.
    std::optional<std::array<int,2>> wing_root_sections;
    AileronFigures aileron;
    std::vector<Figure> aileron_figures; // in aileron_figure_table order
};
struct ProfileSet {std::string default_name;std::vector<Profile> profiles;};
// low/high bound the file and the workbench's overrides alike; step is the
// page's control (api.md "figure_limits").
struct FigureSpec {const char* key;const char* unit;double ElevatorFigures::*field;double low,high,step;};
// The figures the plugin uses, then the elevator page's own (field nullptr).
// The tail's angle and stall (decision 17): the float slope, the share a
// stalled tail keeps, the elevator's share of the tail's angle (tau), the
// tail section's stall angle and the width its stalled share grows over (0:
// X-Plane's share alone), and the share of the deflection's moment that turns
// over with air from behind.
inline const std::array<FigureSpec,19>& figure_table() {
    static const std::array<FigureSpec,19> table{{
        {"area_m2","m²",&ElevatorFigures::area_m2,0.05,20,0.01},
        {"chord_m","m",&ElevatorFigures::chord_m,0.02,3,0.001},
        {"hinge_slope_per_rad","/rad",&ElevatorFigures::hinge_slope_per_rad,0,5,0.01},
        {"up_deg","°",&ElevatorFigures::up_deg,1,60,0.5},
        {"down_deg","°",&ElevatorFigures::down_deg,1,60,0.5},
        {"gearing_rad_per_in","rad/in",&ElevatorFigures::gearing_rad_per_in,0.01,2,0.001},
        {"unbalance_inlb","in·lb",&ElevatorFigures::unbalance_inlb,-200,200,0.5},
        {"tab_ratio","",&ElevatorFigures::tab_ratio,0,3,0.01},
        {"tab_up_deg","°",&ElevatorFigures::tab_up_deg,0,60,0.5},
        {"tab_down_deg","°",&ElevatorFigures::tab_down_deg,0,60,0.5},
        {"alpha_slope_per_rad","/rad",&ElevatorFigures::alpha_slope_per_rad,-2,2,0.01},
        {"stall_factor","",&ElevatorFigures::stall_factor,0,1,0.01},
        {"tau","",&ElevatorFigures::tau,0,1,0.01},
        {"stall_deg","°",&ElevatorFigures::stall_deg,0,60,0.5},
        {"stall_width_deg","°",&ElevatorFigures::stall_width_deg,0,30,0.5},
        {"k_rev","",&ElevatorFigures::k_rev,0,3,0.05},
        {"inertia_kgm2","kg·m²",nullptr,0,100,0.01},
        {"wash_mps","m/s",nullptr,0,100,0.1},
        {"damping_ratio","",nullptr,0,10,0.01}}};
    return table;
}
// The aileron's figures, then the aileron page's own (field nullptr).
struct AileronFigureSpec {const char* key;const char* unit;double AileronFigures::*field;double low,high,step;};
inline const std::array<AileronFigureSpec,15>& aileron_figure_table() {
    static const std::array<AileronFigureSpec,15> table{{
        {"area_m2","m²",&AileronFigures::area_m2,0.05,10,0.01},
        {"chord_m","m",&AileronFigures::chord_m,0.02,2,0.001},
        {"hinge_slope_per_rad","/rad",&AileronFigures::hinge_slope_per_rad,0,5,0.01},
        {"alpha_slope_per_rad","/rad",&AileronFigures::alpha_slope_per_rad,-2,2,0.01},
        {"up_deg","°",&AileronFigures::up_deg,1,60,0.5},
        {"down_deg","°",&AileronFigures::down_deg,1,60,0.5},
        {"wheel_deg","°",&AileronFigures::wheel_deg,10,180,0.5},
        {"rim_m","m",&AileronFigures::rim_m,0.05,0.5,0.005},
        {"unbalance_inlb","in·lb",&AileronFigures::unbalance_inlb,-200,200,0.5},
        {"stall_factor","",&AileronFigures::stall_factor,0,1,0.01},
        {"k_rev","",&AileronFigures::k_rev,0,3,0.05},
        {"inertia_kgm2","kg·m²",nullptr,0,100,0.01},
        {"wheel_inertia_kgm2","kg·m²",nullptr,0,10,0.001},
        {"damping_ratio","",nullptr,0,10,0.01},
        {"wash_mps","m/s",nullptr,0,100,0.1}}};
    return table;
}
// A figure the workbench may override: one the plugin uses.
inline const FigureSpec* settable_figure(std::string_view key) {
    for (const auto& spec:figure_table()) if (spec.field && key==spec.key) return &spec;
    return nullptr;
}
struct ProfileError:std::runtime_error {using std::runtime_error::runtime_error;};
inline ProfileSet parse_profiles(std::string_view text) {
    Json root;
    try {root=parse_json(text);} catch (const JsonError& e) {throw ProfileError(std::string("not JSON: ")+e.what());}
    const auto fail=[](const std::string& why) {throw ProfileError(why);};
    const auto need=[&](const Json& o,const char* key,Json::Kind kind,const std::string& where)->const Json& {
        const auto* v=o.get(key);
        if (!v || v->kind!=kind) fail(where+" needs "+key);
        return *v;
    };
    if (root.kind!=Json::Kind::object) fail("not an object");
    for (const auto& m:root.members) if (m.first!="schema" && m.first!="default" && m.first!="profiles") fail("unknown key "+m.first);
    // Schema 2 (2026-11-09): every profile has its ailerons. Schema 1, the
    // elevator alone, is refused: roll has no other feel (decision 6).
    if (need(root,"schema",Json::Kind::integer,"the file").integer!=2) fail("unsupported schema");
    ProfileSet set;
    set.default_name=need(root,"default",Json::Kind::string,"the file").text;
    for (const auto& entry:need(root,"profiles",Json::Kind::object,"the file").members) {
        const auto& name=entry.first;const auto& p=entry.second; // lambdas capture these (no captured bindings in C++17)
        const std::string where="profile "+name;
        if (name.empty() || p.kind!=Json::Kind::object) fail(where+" is not an object");
        Profile out;out.name=name;
        for (const auto& m:p.members) if (m.first!="aircraft" && m.first!="tail_sections" && m.first!="elevator" && m.first!="aileron_sections" && m.first!="aileron"
            && m.first!="wing_root_sections")
            fail(where+": unknown key "+m.first);
        for (const auto& a:need(p,"aircraft",Json::Kind::array,where).items) {
            if (a.kind!=Json::Kind::string || a.text.empty() || a.text.size()>512) fail(where+": an aircraft is not a path");
            out.aircraft.push_back(a.text);
        }
        const auto sections=[&](const char* key)->std::optional<std::array<int,2>> {
            const auto* t=p.get(key);
            if (!t) return {};
            if (t->kind!=Json::Kind::array || t->items.size()!=2) fail(where+": "+key+" needs two wings");
            std::array<int,2> wings{};
            for (std::size_t i=0;i<2;++i) {
                if (t->items[i].kind!=Json::Kind::integer || t->items[i].integer<0 || t->items[i].integer>9) fail(where+": a section of "+key+" is not a wing 0-9");
                wings[i]=static_cast<int>(t->items[i].integer);
            }
            if (wings[0]==wings[1]) fail(where+": "+key+" names one wing twice");
            return wings;
        };
        out.tail_sections=sections("tail_sections");
        out.aileron_sections=sections("aileron_sections");
        out.wing_root_sections=sections("wing_root_sections");
        // One surface's figures: each exactly value, tag and source, within its spec.
        const auto surface=[&](const char* key,const auto& table,auto& figures,std::vector<Figure>& shown) {
            const auto& block=need(p,key,Json::Kind::object,where);
            for (const auto& m:block.members) {
                bool known=false;
                for (const auto& spec:table) known=known || m.first==spec.key;
                if (!known) fail(where+": unknown "+key+" figure "+m.first);
            }
            for (const auto& spec:table) {
                const auto* f=block.get(spec.key);
                if (!f) {
                    if (spec.field) fail(where+" needs "+key+" "+spec.key);
                    continue;
                }
                const std::string fwhere=where+" "+key+" "+spec.key;
                if (f->kind!=Json::Kind::object || f->members.size()!=3) fail(fwhere+" needs value, tag and source only");
                const auto* v=f->get("value");
                if (!v || (v->kind!=Json::Kind::integer && v->kind!=Json::Kind::real)) fail(fwhere+" needs a number");
                const double value=v->kind==Json::Kind::integer?double(v->integer):v->real;
                if (!std::isfinite(value) || value<spec.low || value>spec.high)
                    fail(fwhere+" must be "+decimal_or_null(spec.low,2)+" to "+decimal_or_null(spec.high,2));
                const auto& tag=need(*f,"tag",Json::Kind::string,fwhere).text;
                if (tag!="published" && tag!="derived" && tag!="guessed") fail(fwhere+": tag must be published, derived or guessed");
                const auto& source=need(*f,"source",Json::Kind::string,fwhere).text;
                if (source.empty() || source.size()>600) fail(fwhere+" needs a source");
                if (spec.field) figures.*spec.field=value;
                shown.push_back({spec.key,spec.unit,tag,source,value});
            }
        };
        surface("elevator",figure_table(),out.elevator,out.figures);
        surface("aileron",aileron_figure_table(),out.aileron,out.aileron_figures);
        if (!valid_figures(out.elevator)) fail(where+": figures out of bounds");
        if (!valid_figures(out.aileron)) fail(where+": aileron figures out of bounds");
        set.profiles.push_back(std::move(out));
    }
    bool found=false;
    for (const auto& p:set.profiles) found=found || p.name==set.default_name;
    if (!found) fail("no default profile "+set.default_name);
    return set;
}
// The aircraft's profile: the first naming its file (the path, or a path
// ending in '/' and one of the profile's), else the default.
inline Profile profile_for(const ProfileSet& set,const std::string& aircraft_path) {
    std::string path=aircraft_path;
    for (auto& c:path) if (c=='\\') c='/';
    const auto ends=[&](const std::string& tail) {
        if (path==tail) return true;
        return path.size()>tail.size() && path.compare(path.size()-tail.size(),tail.size(),tail)==0 && path[path.size()-tail.size()-1]=='/';
    };
    for (const auto& p:set.profiles) for (const auto& a:p.aircraft) if (ends(a)) {auto out=p;out.matched=true;return out;}
    for (const auto& p:set.profiles) if (p.name==set.default_name) {auto out=p;out.matched=false;return out;}
    throw ProfileError("no default profile");
}
inline bool same_figures(const ElevatorFigures& a,const ElevatorFigures& b) {
    return a.area_m2==b.area_m2 && a.chord_m==b.chord_m && a.hinge_slope_per_rad==b.hinge_slope_per_rad && a.up_deg==b.up_deg
        && a.down_deg==b.down_deg && a.gearing_rad_per_in==b.gearing_rad_per_in && a.unbalance_inlb==b.unbalance_inlb
        && a.tab_ratio==b.tab_ratio && a.tab_up_deg==b.tab_up_deg && a.tab_down_deg==b.tab_down_deg
        && a.alpha_slope_per_rad==b.alpha_slope_per_rad && a.stall_factor==b.stall_factor && a.tau==b.tau
        && a.stall_deg==b.stall_deg && a.stall_width_deg==b.stall_width_deg && a.k_rev==b.k_rev;
}
inline bool same_figures(const AileronFigures& a,const AileronFigures& b) {
    return a.area_m2==b.area_m2 && a.chord_m==b.chord_m && a.hinge_slope_per_rad==b.hinge_slope_per_rad && a.alpha_slope_per_rad==b.alpha_slope_per_rad
        && a.up_deg==b.up_deg && a.down_deg==b.down_deg && a.wheel_deg==b.wheel_deg && a.rim_m==b.rim_m
        && a.unbalance_inlb==b.unbalance_inlb && a.stall_factor==b.stall_factor && a.k_rev==b.k_rev;
}
}
