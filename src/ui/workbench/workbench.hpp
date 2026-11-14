#pragma once
// The yoke feel workbench's pure core (docs/brunner/workbench/api.md): Kept and
// Trying, undo, the profile, notices of what changed the feel, marks and notes, the
// two-minute history, the file formats, a small strict JSON reader and writer,
// HTTP/1.1 parsing and the peer and Host checks. No sockets, no X-Plane SDK:
// the sim thread and the server thread share one Workbench through its mutex.
#include "brunner/yoke_overrides.hpp"
#include "force/flight.hpp"
#include "json.hpp"
#include "force/profiles.hpp"
#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <filesystem>
#include <fstream>
#include <functional>
#include <locale>
#include <map>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

namespace brunner::workbench {
using Clock=std::chrono::steady_clock;

// ---- Units (api.md "Units and signs") ----
inline constexpr double sea_level_rho=1.225,metres_per_knot=0.514444;
// The page's percent is of the active yoke's own pitch travel (the force models' counts run to
// +-full_travel_counts(), 12000 on the CLS-E, 5300 on the NG), forward positive counts, aft positive percent.
inline double counts_per_percent() {return full_travel_counts()/100.0;}
// The notices kept for the page's Details, newest last.
inline constexpr std::size_t notice_limit=8;
inline constexpr std::size_t undo_depth=32,mark_limit=100,history_samples=600,note_limit=200;
inline int pa_from_kt(int kt) {
    const double v=kt*metres_per_knot;
    return static_cast<int>(std::lround(.5*sea_level_rho*v*v));
}
inline int kt_from_pa(double pa) {return static_cast<int>(std::lround(std::sqrt(2*pa/sea_level_rho)/metres_per_knot));}
inline std::optional<int> airflow_kt(double pa) {
    if (!std::isfinite(pa) || pa<0) return {};
    return kt_from_pa(pa);
}
// The drive's counts (forward positive) as the page's percent of travel, aft positive.
inline double travel_percent(double counts) {return -counts/counts_per_percent();}

// ---- Settings object S ----
// Pitch is the elevator model (decision 15): its knobs are the drive's, and
// `figures` overrides the flown profile's figures by key (decisions 5 and 13).
// Roll is the aileron model (decision 6): its knobs are the drive's too, its
// figures the profile's alone.
// Neither axis has a firmest knob: each update's strength goes up to the
// drive's own ceiling (decision 16).
struct Pitch {int units_per_lb=222,drag_divisor=6,move_back=180;std::map<std::string,double> figures;};
struct Roll {int units_per_nm=1000;};
struct Settings {Pitch pitch;Roll roll;};
inline bool operator==(const Pitch& a,const Pitch& b) {
    return a.units_per_lb==b.units_per_lb && a.drag_divisor==b.drag_divisor && a.move_back==b.move_back
        && a.figures==b.figures;
}
inline bool operator!=(const Pitch& a,const Pitch& b) {return !(a==b);}
inline bool operator==(const Roll& a,const Roll& b) {return a.units_per_nm==b.units_per_nm;}
inline bool operator!=(const Roll& a,const Roll& b) {return !(a==b);}
inline bool operator==(const Settings& a,const Settings& b) {return a.pitch==b.pitch && a.roll==b.roll;}
inline bool operator!=(const Settings& a,const Settings& b) {return !(a==b);}
inline bool valid_figure(const std::string& key,double value) {
    const auto* spec=settable_figure(key);
    return spec && std::isfinite(value) && value>=spec->low && value<=spec->high;
}
inline bool valid_pitch(const Pitch& p) {
    for (const auto& [key,value]:p.figures) if (!valid_figure(key,value)) return false;
    return p.units_per_lb>=50 && p.units_per_lb<=1000
        && p.drag_divisor>=1 && p.drag_divisor<=6 && p.move_back>=180 && p.move_back<=720;
}
// The profile's figures with the workbench's overrides.
inline ElevatorFigures with_overrides(ElevatorFigures e,const std::map<std::string,double>& overrides) {
    for (const auto& [key,value]:overrides) if (const auto* spec=settable_figure(key)) e.*spec->field=value;
    return e;
}
inline bool valid_roll(const Roll& r) {
    return r.units_per_nm>=100 && r.units_per_nm<=10000;
}
inline bool valid(const Settings& s) {return valid_pitch(s.pitch) && valid_roll(s.roll);}
inline int changes(const Settings& a,const Settings& b) {
    const auto& p=a.pitch;const auto& q=b.pitch;
    int figures=0;
    for (const auto& spec:figure_table()) {
        if (!spec.field) continue;
        const auto x=p.figures.find(spec.key),y=q.figures.find(spec.key);
        figures+=int((x==p.figures.end())!=(y==q.figures.end()) || (x!=p.figures.end() && x->second!=y->second));
    }
    return figures+int(p.units_per_lb!=q.units_per_lb)+int(p.drag_divisor!=q.drag_divisor)+int(p.move_back!=q.move_back)
        +int(a.roll.units_per_nm!=b.roll.units_per_nm);
}
// The managed BrunnerDirect.ini in page units, rounded and held to the page's bounds.
inline Settings from_managed(const FlightSettings& f) {
    return {{std::clamp(f.pitch_force_units_per_lb,50,1000),std::clamp(f.pitch_force_gain_divisor,1,6),
                std::clamp(f.pitch_move_back_gain,180,720),{}},
        {std::clamp(f.roll_torque_units_per_nm,100,10000)}};
}
// What from_managed changed, for the log.
inline std::vector<std::string> managed_adjustments(const FlightSettings& f) {
    std::vector<std::string> out;
    const auto s=from_managed(f);
    const auto note=[&](const char* what,int from,int to,const char* unit) {
        if (from!=to) out.push_back(std::string("Feel workbench: BrunnerDirect.ini ")+what+" "+std::to_string(from)+unit+" flies as "+std::to_string(to)+unit+" (page bounds)");
    };
    note("pitch units per lb",f.pitch_force_units_per_lb,s.pitch.units_per_lb,"");
    note("pitch drag divisor",f.pitch_force_gain_divisor,s.pitch.drag_divisor,"");
    note("pitch move-back gain",f.pitch_move_back_gain,s.pitch.move_back,"");
    note("roll units per N·m",f.roll_torque_units_per_nm,s.roll.units_per_nm,"");
    return out;
}
inline void apply(const Settings& s,FlightSettings& f) {
    f.roll_torque_units_per_nm=s.roll.units_per_nm;
    f.pitch_force_units_per_lb=s.pitch.units_per_lb;f.pitch_force_gain_divisor=s.pitch.drag_divisor;f.pitch_move_back_gain=s.pitch.move_back;
}
// Whether the plugin can fly these settings. The page never offers one it
// can't: a refused setting used to stop force feel without saying why.
inline bool flyable(const Settings& s) {
    FlightSettings f;apply(s,f);
    return valid(s) && valid_flight_settings(f);
}
inline constexpr const char* not_flyable="The yoke can't fly that setting, so it wasn't changed.";
// What the plugin flies this frame: the version shown, each axis's knobs with
// the count of the feel's edits, and the profile's figures (none until a
// profiles file has been read: then neither axis has a target).
struct Applied {
    Settings settings;
    PitchKnobs knobs;RollKnobs roll_knobs;
    bool has_profile=false,matched=false;
    ElevatorFigures figures;AileronFigures ailerons;
    std::optional<std::array<int,2>> tail_sections,aileron_sections; // the profile's, on its own aircraft only
    std::optional<std::array<int,2>> wing_root_sections; // buffet's pitch source (force/buffet.hpp), likewise
};

// ---- Feel files: BrunnerDirect.feel.ini (Kept) and .feel-draft.ini (Trying) ----
inline constexpr char feel_file[]="BrunnerDirect.feel.ini",draft_file[]="BrunnerDirect.feel-draft.ini",marks_file[]="BrunnerDirect.marks.jsonl";
inline std::string feel_text(const Settings& s,const char* what) {
    std::ostringstream out;
    out<<"# "<<what<<", written by the yoke feel workbench. Page units: percent of the drive's\n"
       <<"# spring scale, drive force units per pound and per N·m. See docs/brunner/workbench/api.md.\n"
       <<"feel_schema=4\n"
       <<"pitch_units_per_lb="<<s.pitch.units_per_lb
       <<"\npitch_drag_divisor="<<s.pitch.drag_divisor<<"\npitch_move_back="<<s.pitch.move_back<<'\n'
       <<"roll_units_per_nm="<<s.roll.units_per_nm<<'\n';
    for (const auto& [key,value]:s.pitch.figures) out<<"pitch_figure_"<<key<<'='<<number_json(value)<<'\n';
    return out.str();
}
// Strict: every key of its schema once, nothing else, all within the page's
// bounds. Older schemas are read for what still exists: schema 1 (before the
// elevator model) loses its pitch floor, pitch halfway and trim strength, and
// schemas 1 and 2 (before the aileron model) roll's floor and halfway and the
// plain spring; those are ignored and listed in `ignored`, and the knobs a
// schema lacks come from `base`. Schemas 1 to 3 (before the drive's own
// ceiling, decision 16) lose both firmests, pitch_firmest_percent and
// roll_firmest_percent, ignored and listed the same way.
// pitch_figure_<key> lines (schema 2 on) are the figure overrides, zero or more.
inline Settings parse_feel(std::istream& input,const Settings& base=Settings{},std::vector<std::string>* ignored=nullptr) {
    Settings s=base;s.pitch.figures.clear();
    std::map<std::string,int> values;std::map<std::string,std::string> figures;bool schema_seen=false;int schema=0;
    std::string line;
    while (std::getline(input,line)) {
        const auto begin=line.find_first_not_of(" \t\r");
        if (begin==std::string::npos || line[begin]=='#') continue;
        const auto end=line.find_last_not_of(" \t\r");
        line=line.substr(begin,end-begin+1);
        const auto equals=line.find('=');
        if (equals==std::string::npos) throw std::invalid_argument("malformed line");
        const auto key=line.substr(0,equals),text=line.substr(equals+1);
        if (values.count(key) || figures.count(key) || (key=="feel_schema" && schema_seen)) throw std::invalid_argument("duplicate key: "+key);
        if (key.rfind("pitch_figure_",0)==0) {figures[key]=text;continue;}
        int value=0;
        const auto parsed=std::from_chars(text.data(),text.data()+text.size(),value);
        if (parsed.ec!=std::errc{} || parsed.ptr!=text.data()+text.size()) throw std::invalid_argument("not an integer: "+key);
        if (key=="feel_schema") {schema_seen=true;schema=value;continue;}
        values[key]=value;
    }
    if (!schema_seen) throw std::invalid_argument("feel_schema missing");
    if (schema<1 || schema>4) throw std::invalid_argument("unsupported feel_schema");
    std::map<std::string,int*> keys;
    std::vector<std::string> retired;
    if (schema<=3) retired={"pitch_firmest_percent","roll_firmest_percent"};
    if (schema>=2) {
        keys.insert({{"pitch_units_per_lb",&s.pitch.units_per_lb},{"pitch_drag_divisor",&s.pitch.drag_divisor},{"pitch_move_back",&s.pitch.move_back}});
    } else for (const char* k:{"pitch_floor_percent","pitch_halfway_kt","trim_percent"}) retired.push_back(k);
    if (schema>=3) keys.insert({"roll_units_per_nm",&s.roll.units_per_nm});
    else for (const char* k:{"roll_floor_percent","roll_halfway_kt","plain_percent"}) retired.push_back(k);
    for (const auto& [key,value]:values) {
        const auto found=keys.find(key);
        if (found!=keys.end()) {*found->second=value;continue;}
        if (std::find(retired.begin(),retired.end(),key)==retired.end()) throw std::invalid_argument("unknown key: "+key);
        if (ignored) ignored->push_back(key);
    }
    for (const auto& k:keys) if (!values.count(k.first)) throw std::invalid_argument("missing key: "+k.first);
    for (const auto& k:retired) if (!values.count(k)) throw std::invalid_argument("missing key: "+k);
    if (schema==1 && !figures.empty()) throw std::invalid_argument("unknown key: "+figures.begin()->first);
    for (const auto& [line_key,text]:figures) {
        const auto key=line_key.substr(13);
        if (!settable_figure(key)) throw std::invalid_argument("unknown figure: "+key);
        std::istringstream in(text);in.imbue(std::locale::classic());
        double value=0;
        if (text.empty() || text.find_first_not_of("0123456789.-+eE")!=std::string::npos || !(in>>value) || !in.eof())
            throw std::invalid_argument("not a number: "+line_key);
        s.pitch.figures[key]=value;
    }
    if (!valid(s)) throw std::invalid_argument("a value is outside the workbench's bounds");
    return s;
}

inline std::string figures_json(const std::map<std::string,double>& figures) {
    std::string o="{";
    for (const auto& [key,value]:figures) o+=(o.size()>1?",":"")+json_string(key)+":"+number_json(value);
    return o+"}";
}
inline std::string settings_json(const Settings& s) {
    return "{\"pitch\":{\"units_per_lb\":"+std::to_string(s.pitch.units_per_lb)
        +",\"drag_divisor\":"+std::to_string(s.pitch.drag_divisor)+",\"move_back\":"+std::to_string(s.pitch.move_back)
        +",\"figures\":"+figures_json(s.pitch.figures)+"},\"roll\":{\"units_per_nm\":"+std::to_string(s.roll.units_per_nm)+"}}";
}

// ---- Request validation: 400 for the wrong shape, 422 for an integer out of bounds ----
struct Outcome {int status=200;std::string error;};
inline Outcome bad_request(std::string why) {return {400,std::move(why)};}
inline Outcome out_of_bounds(std::string why) {return {422,std::move(why)};}
// Applies a partial S to `s`; on failure `s` is unchanged.
inline Outcome merge_settings(const Json& body,Settings& s) {
    if (body.kind!=Json::Kind::object) return bad_request("settings must be an object");
    Settings next=s;
    const auto integer=[](const Json& v,const std::string& name,int low,int high,int& out)->Outcome {
        if (v.kind!=Json::Kind::integer) return bad_request(name+" must be an integer");
        if (v.integer<low || v.integer>high) return out_of_bounds(name+" must be "+std::to_string(low)+"-"+std::to_string(high));
        out=static_cast<int>(v.integer);return {};
    };
    for (const auto& [key,value]:body.members) {
        Outcome o;
        if (key=="pitch") {
            if (value.kind!=Json::Kind::object) return bad_request("pitch must be an object");
            auto& p=next.pitch;
            for (const auto& [field,v]:value.members) {
                if (field=="units_per_lb") o=integer(v,"pitch.units_per_lb",50,1000,p.units_per_lb);
                else if (field=="drag_divisor") o=integer(v,"pitch.drag_divisor",1,6,p.drag_divisor);
                else if (field=="move_back") o=integer(v,"pitch.move_back",180,720,p.move_back);
                else if (field=="figures") {
                    // A number overrides the profile's figure, null removes the override.
                    if (v.kind!=Json::Kind::object) return bad_request("pitch.figures must be an object");
                    for (const auto& [figure,f]:v.members) {
                        const auto* spec=settable_figure(figure);
                        if (!spec) return bad_request("unknown figure: pitch.figures."+figure);
                        if (f.kind==Json::Kind::null) {p.figures.erase(figure);continue;}
                        if (f.kind!=Json::Kind::integer && f.kind!=Json::Kind::real) return bad_request("pitch.figures."+figure+" must be a number or null");
                        const double number=f.kind==Json::Kind::integer?double(f.integer):f.real;
                        if (!valid_figure(figure,number))
                            return out_of_bounds("pitch.figures."+figure+" must be "+number_json(spec->low)+" to "+number_json(spec->high));
                        p.figures[figure]=number;
                    }
                }
                else return bad_request("unknown setting: pitch."+field);
                if (o.status!=200) return o;
            }
        } else if (key=="roll") {
            if (value.kind!=Json::Kind::object) return bad_request("roll must be an object");
            auto& a=next.roll;
            for (const auto& [field,v]:value.members) {
                if (field=="units_per_nm") o=integer(v,"roll.units_per_nm",100,10000,a.units_per_nm);
                else return bad_request("unknown setting: roll."+field);
                if (o.status!=200) return o;
            }
        } else return bad_request("unknown setting: "+key);
        if (o.status!=200) return o;
    }
    s=next;return {};
}
inline bool empty_object(const Json& j) {return j.kind==Json::Kind::object && j.members.empty();}
// A schema 2 or 3 S, as marks saved before the drive's own ceiling hold it:
// today's knobs with each axis's firmest_percent, and in schema 2 (before the
// aileron model) roll's floor and halfway and the plain spring.
inline bool schema2_settings(const Json& j) {
    if (j.kind!=Json::Kind::object) return false;
    Json now=j;now.members.clear();
    for (const auto& [k,v]:j.members) {
        if (k=="plain_percent") {if (v.kind!=Json::Kind::integer) return false;continue;}
        if (k=="roll" || k=="pitch") {
            if (v.kind!=Json::Kind::object) return false;
            Json axis=v;axis.members.clear();
            for (const auto& [f,x]:v.members) {
                if (f=="firmest_percent" || (k=="roll" && (f=="floor_percent" || f=="halfway_kt"))) {
                    if (x.kind!=Json::Kind::integer) return false;
                    continue;
                }
                axis.members.emplace_back(f,x);
            }
            now.members.emplace_back(k,axis);continue;
        }
        now.members.emplace_back(k,v);
    }
    Settings check;
    return merge_settings(now,check).status==200;
}
// A schema 1 S, as marks saved before the elevator model hold it.
inline bool schema1_settings(const Json& j) {
    if (j.kind!=Json::Kind::object) return false;
    const auto integers=[](const Json& o) {
        if (o.kind!=Json::Kind::object) return false;
        for (const auto& [k,v]:o.members) if ((k!="floor_percent" && k!="firmest_percent" && k!="halfway_kt") || v.kind!=Json::Kind::integer) return false;
        return true;
    };
    for (const auto& [k,v]:j.members) {
        if (k=="pitch" || k=="roll") {if (!integers(v)) return false;}
        else if (k=="trim_percent" || k=="plain_percent") {if (v.kind!=Json::Kind::integer) return false;}
        else return false;
    }
    return true;
}

// ---- What the sim thread publishes, ten times a second ----
// Roll: X-Plane's own readings, the aileron model's inputs, and what the
// solver makes of them at the wheel.
struct AxisLive {
    std::optional<int> now_percent;
    double airflow_pa=std::numeric_limits<double>::quiet_NaN(),aoa_deg=std::numeric_limits<double>::quiet_NaN();
    double deflection_deg=std::numeric_limits<double>::quiet_NaN(),yoke_percent=std::numeric_limits<double>::quiet_NaN();
    int stalled=-1; // -1 unknown
    std::optional<AileronInputs> inputs;AileronSource source=AileronSource::airspeed;
    // Each side's stalled share as X-Plane gave it; inputs carry the slewed one.
    std::array<double,2> stalled_raw{std::numeric_limits<double>::quiet_NaN(),std::numeric_limits<double>::quiet_NaN()};
    std::optional<RollSolution> solution;
    const char* phase="off"; // as pitch's
};
// Pitch: the model's inputs and what the solver makes of them at the yoke.
struct PitchLive {
    std::optional<int> now_percent;
    double aoa_deg=std::numeric_limits<double>::quiet_NaN(),deflection_deg=std::numeric_limits<double>::quiet_NaN();
    double yoke_percent=std::numeric_limits<double>::quiet_NaN();
    int stalled=-1;
    std::optional<ElevatorInputs> inputs;TailSource tail_source=TailSource::airspeed;
    // The tail's stalled share as X-Plane gave it; inputs carry the slewed one.
    double tail_stalled_raw=std::numeric_limits<double>::quiet_NaN();
    std::optional<PitchSolution> solution;
    const char* phase="off"; // off, blending (force feel coming on or an edit), following
    double update_hz=std::numeric_limits<double>::quiet_NaN();
    bool drag_waiting=false;
};
struct Live {
    std::string status="Yoke plugin is disabled",view="yoke_off",motors="off";
    std::string reason;bool retry_helps=true; // why force feel stopped, in plain words; empty when nothing failed
    std::string aircraft,aircraft_file;bool aileron_panels=false; // roll reads the profile's aileron sections
    double ias_kt=std::numeric_limits<double>::quiet_NaN(),bank_deg=std::numeric_limits<double>::quiet_NaN();
    double flaps_deg=std::numeric_limits<double>::quiet_NaN(),vertical_fpm=std::numeric_limits<double>::quiet_NaN();
    PitchLive pitch;AxisLive roll;
    std::array<int,9> pitch_curve{};
    // Buffet's amplitude last read back, roll/pitch, in drive units, whether
    // clearing what it set failed, and a vibration time it changed and could
    // not put back, with the value it found (force/buffet.hpp).
    std::array<std::uint32_t,2> buffet_units{};bool buffet_uncleared=false;
    std::array<std::optional<std::uint32_t>,2> buffet_time_unrestored{};
};
// Buffet's knob on the page (decision 19): off by default, untested with the
// motors on. Not part of the kept feel: each X-Plane start takes it from the
// feel settings file, where it is off unless the owner turns it on there.
struct BuffetView {bool on=false;int percent=10,period=80;};
// The active yoke's four direction fields as the page shows them (brunner/yoke_overrides.hpp says what
// each means). `editable` is false when no preset names the yoke; `note` is why the last change failed.
struct DirectionView {
    bool editable=false;std::string model;
    int roll_sign=-1,pitch_sign=-1,roll_spring=-1,pitch_spring=-1;bool roll_reverse=false,pitch_reverse=false;
    std::string note;
};
inline bool valid_buffet_view(const BuffetView& b) {return b.percent>=0 && b.percent<=100 && b.period>=10 && b.period<=140;}
// The plugin's one status line and reader state, as the page's `view`.
struct ViewInputs {bool enabled=true,connected=false,retry_offered=false,lab_lease=false,flight_loading=false,force_feel=false;std::string status;};
inline const char* view_for(const ViewInputs& in) {
    if (in.retry_offered) return "fault";
    if (!in.enabled || (!in.connected && !in.lab_lease)) return "yoke_off";
    if (in.flight_loading && in.force_feel && in.status.rfind("Ready",0)==0) return "ready";
    if (!in.force_feel && in.status=="Ready - force feel off") return "force_feel_off";
    return "busy";
}
inline const char* motors_for(bool retry_offered,bool motors_enabled) {return motors_enabled?"on":retry_offered?"stopped":"off";}
// What the workbench needs from the latest frame.
struct FrameInputs {
    bool valid=false; // a fresh flight sample: the model's inputs are current
    bool force_feel=false; // the pilot's force feel switch
    bool connected=false; // the yoke is connected
    ElevatorInputs inputs;
};
enum class Command {stop_motors,force_feel_on,retry};
struct Mark {
    int id=0;std::int64_t utc_ms=0;bool trying=false;
    std::string settings; // S as JSON, as it was saved (a mark from before schema 2 keeps its own)
    std::optional<std::string> profile;
    double ias_kt=std::numeric_limits<double>::quiet_NaN(),bank_deg=std::numeric_limits<double>::quiet_NaN();
    double flaps_deg=std::numeric_limits<double>::quiet_NaN(),vertical_fpm=std::numeric_limits<double>::quiet_NaN();
    std::string note;
};
inline std::string mark_json(const Mark& m) {
    return "{\"id\":"+std::to_string(m.id)+",\"utc_ms\":"+std::to_string(m.utc_ms)+",\"version\":"+(m.trying?"\"trying\"":"\"kept\"")
        +",\"settings\":"+m.settings+",\"profile\":"+(m.profile?json_string(*m.profile):std::string("null"))
        +",\"ias_kt\":"+number_or_null(rounded(m.ias_kt))+",\"bank_deg\":"+number_or_null(rounded(m.bank_deg))
        +",\"flaps_deg\":"+number_or_null(rounded(m.flaps_deg))+",\"vertical_fpm\":"+number_or_null(rounded(m.vertical_fpm))+",\"note\":"+json_string(m.note)+'}';
}
inline std::size_t code_points(std::string_view s) {
    std::size_t n=0;
    for (const char c:s) if ((static_cast<unsigned char>(c)&0xc0)!=0x80) ++n;
    return n;
}
// One line of BrunnerDirect.marks.jsonl, as /api/marks lists it. Lines saved
// before schema 2 have no profile and schema 1 settings; both are read.
inline Mark parse_mark(std::string_view line) {
    const auto j=parse_json(line);
    if (j.kind!=Json::Kind::object) throw JsonError("mark must be an object");
    Mark m;
    const auto need=[&](const char* key,Json::Kind kind)->const Json& {
        const auto* v=j.get(key);
        if (!v || v->kind!=kind) throw JsonError(std::string("mark field ")+key);
        return *v;
    };
    const auto optional=[&](const char* key) {
        const auto* v=j.get(key);
        if (!v) throw JsonError(std::string("mark field ")+key);
        if (v->kind==Json::Kind::null) return std::numeric_limits<double>::quiet_NaN();
        if (v->kind!=Json::Kind::integer) throw JsonError(std::string("mark field ")+key);
        return double(v->integer);
    };
    const auto id=need("id",Json::Kind::integer).integer;
    if (id<1 || id>2000000000) throw JsonError("mark id");
    m.id=static_cast<int>(id);m.utc_ms=need("utc_ms",Json::Kind::integer).integer;
    const auto& version=need("version",Json::Kind::string).text;
    if (version!="kept" && version!="trying") throw JsonError("mark version");
    m.trying=version=="trying";
    const auto& settings=need("settings",Json::Kind::object);
    Settings check;
    if (merge_settings(settings,check).status!=200 && !schema1_settings(settings) && !schema2_settings(settings)) throw JsonError("mark settings");
    m.settings=write_json(settings);
    std::size_t members=9;
    if (const auto* p=j.get("profile")) {
        ++members;
        if (p->kind==Json::Kind::string) m.profile=p->text;
        else if (p->kind!=Json::Kind::null) throw JsonError("mark field profile");
    }
    m.ias_kt=optional("ias_kt");m.bank_deg=optional("bank_deg");m.flaps_deg=optional("flaps_deg");m.vertical_fpm=optional("vertical_fpm");
    m.note=need("note",Json::Kind::string).text;
    if (code_points(m.note)>note_limit) throw JsonError("mark note");
    if (j.members.size()!=members) throw JsonError("unexpected mark field");
    return m;
}
// Write-then-rename, so a reader never sees half a file.
inline std::filesystem::path write_staged(const std::filesystem::path& path,const std::string& text) {
    auto staged=path;staged+=".tmp";
    std::ofstream out(staged,std::ios::binary|std::ios::trunc);
    if (!out) throw std::runtime_error("cannot write "+staged.filename().string());
    out<<text;out.flush();
    if (!out) throw std::runtime_error("cannot write "+staged.filename().string());
    return staged;
}
inline void write_atomically(const std::filesystem::path& path,const std::string& text) {
    std::filesystem::rename(write_staged(path,text),path); // replaces an existing file, on Windows too
}
// A file read at start, refused above a size no real one reaches.
inline std::string read_bounded(const std::filesystem::path& path,std::uintmax_t limit) {
    std::error_code ec;
    const auto size=std::filesystem::file_size(path,ec);
    if (ec) throw std::runtime_error("cannot read it");
    if (size>limit) throw std::runtime_error("over "+std::to_string(limit/1024)+" KiB");
    std::ifstream in(path,std::ios::binary);
    if (!in) throw std::runtime_error("cannot read it");
    std::string text(static_cast<std::size_t>(size),'\0');
    in.read(text.data(),static_cast<std::streamsize>(text.size()));
    text.resize(static_cast<std::size_t>(in.gcount()));
    return text;
}
inline std::int64_t utc_now_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}
// ---- Notices: what changed the yoke's feel or rest, other than the flight ----
// Nothing waits for the pilot (owner, 2026-11-08): each change applies at
// once, and a notice on the page and in X-Plane says what caused it.
struct Notice {std::uint64_t id=0;std::string cause,text;std::optional<double> rest_move_percent;Clock::time_point at;};
// A figure's key as words: unbalance_inlb is "unbalance".
inline std::string figure_name(std::string key) {
    for (const std::string unit:{"_rad_per_in","_per_rad","_inlb","_kgm2","_mps","_deg","_m2","_m"})
        if (key.size()>unit.size() && key.compare(key.size()-unit.size(),unit.size(),unit)==0) {key.resize(key.size()-unit.size());break;}
    for (auto& c:key) if (c=='_') c=' ';
    return key;
}
// What differs between two feels, in words; empty when nothing the yoke flies did.
inline std::string feel_changes(const Settings& a,const Settings& b,const std::optional<ElevatorFigures>& fa,const std::optional<ElevatorFigures>& fb) {
    std::string o;
    const auto add=[&o](const std::string& what,const std::string& from,const std::string& to,const std::string& unit) {
        o+=(o.empty()?"":", ")+what+" "+from+" → "+to+unit;
    };
    if (fa && fb) for (const auto& spec:figure_table()) if (spec.field && (*fa).*spec.field!=(*fb).*spec.field) {
        const std::string unit=spec.unit;
        add(figure_name(spec.key),number_json((*fa).*spec.field),number_json((*fb).*spec.field),unit.empty() || unit=="°"?unit:" "+unit);
    }
    const auto n=[](int v) {return std::to_string(v);};
    if (a.pitch.units_per_lb!=b.pitch.units_per_lb) add("units",n(a.pitch.units_per_lb),n(b.pitch.units_per_lb)," per lb");
    if (a.pitch.drag_divisor!=b.pitch.drag_divisor) add("drag","1/"+n(a.pitch.drag_divisor),"1/"+n(b.pitch.drag_divisor)," (at the next force-feel start)");
    if (a.pitch.move_back!=b.pitch.move_back) add("move-back",n(a.pitch.move_back),n(b.pitch.move_back)," (at the next force-feel start)");
    if (a.roll.units_per_nm!=b.roll.units_per_nm) add("roll units",n(a.roll.units_per_nm),n(b.roll.units_per_nm)," per N·m");
    return o;
}
// A notice for X-Plane's corner line: ASCII, as its font draws it, with the
// arrows and units spelled out, at most 150 characters.
inline std::string corner_text(const std::string& text) {
    static const std::pair<const char*,const char*> spelled[]{{"→","->"},{"·","-"},{"²","2"},{"°"," deg"}};
    std::string o;
    for (std::size_t i=0;i<text.size();) {
        bool done=false;
        for (const auto& [from,to]:spelled) if (text.compare(i,std::strlen(from),from)==0) {o+=to;i+=std::strlen(from);done=true;break;}
        if (done) continue;
        const auto c=static_cast<unsigned char>(text[i++]);
        if (c<0x80) o+=static_cast<char>(c);
        else if ((c&0xC0)!=0x80) o+='?';
    }
    return o;
}
// The corner window has two lines. Words wrap by the font's own widths
// (measure: a string's width in pixels); what doesn't fit ends in "...".
inline std::vector<std::string> corner_lines(const std::string& text,double width,const std::function<double(const std::string&)>& measure,
    std::size_t rows=2) {
    std::vector<std::string> lines;std::string line;
    std::istringstream words(corner_text(text));std::string word;
    while (words>>word) {
        if (!line.empty() && measure(line+" "+word)<=width) {line+=" "+word;continue;}
        if (!line.empty()) {lines.push_back(line);line.clear();}
        while (word.size()>1 && measure(word)>width) { // a word wider than the window breaks
            std::size_t n=1;
            while (n+1<word.size() && measure(word.substr(0,n+1))<=width) ++n;
            lines.push_back(word.substr(0,n));word.erase(0,n);
        }
        line=word;
    }
    if (!line.empty()) lines.push_back(line);
    if (lines.size()>rows) {
        lines.resize(rows);
        auto& last=lines.back();
        while (!last.empty() && measure(last+"...")>width) last.pop_back();
        while (!last.empty() && last.back()==' ') last.pop_back();
        last+="...";
    }
    return lines;
}
// Where a profile takes the tail's or the ailerons' air from, in words.
inline std::string tail_text(const std::optional<std::array<int,2>>& sections) {
    return sections?"from sections "+std::to_string((*sections)[0])+" and "+std::to_string((*sections)[1]):std::string("from airspeed");
}
inline int aileron_changes(const AileronFigures& a,const AileronFigures& b) {
    int n=0;
    for (const auto& spec:aileron_figure_table()) n+=spec.field && a.*spec.field!=b.*spec.field;
    return n;
}
// Force feel coming on and going off, as notices. The off notice waits, up to
// 3 s, for the session's end to report whether pitch's constant force read
// back as zero, and says "cleared" only then.
class SessionNotices {
    bool was_=false;std::optional<Clock::time_point> ended_;
public:
    void reset() {was_=false;ended_.reset();}
    std::optional<std::string> update(Clock::time_point now,bool in_session,const std::string& why,std::optional<bool> cleared) {
        std::optional<std::string> out;
        if (in_session && !was_) {ended_.reset();out="Force feel on: the yoke now carries the elevator and aileron models' loads, blended in over half a second";}
        if (!in_session && was_) ended_=now;
        was_=in_session;
        if (ended_ && (cleared || now-*ended_>=std::chrono::seconds(3))) {
            out="Force feel off: "+(why.empty()?std::string("motors stopped"):why)+"; the constant force is "
                +(cleared && *cleared?"cleared (read back as zero)":"not confirmed cleared");
            ended_.reset();
        }
        return out;
    }
};
inline std::string notice_json(const Notice& n,Clock::time_point now) {
    const auto age=std::max<std::int64_t>(0,std::chrono::duration_cast<std::chrono::milliseconds>(now-n.at).count());
    return "{\"id\":"+std::to_string(n.id)+",\"cause\":"+json_string(n.cause)+",\"text\":"+json_string(n.text)
        +",\"rest_move_percent\":"+(n.rest_move_percent?decimal_or_null(*n.rest_move_percent):std::string("null"))+",\"age_ms\":"+std::to_string(age)+'}';
}
// The profile as the page shows it: the elevator's figures (flown with the
// workbench's overrides) and the ailerons' (flown as written).
inline std::string profile_json(const std::optional<Profile>& p,const std::optional<ElevatorFigures>& flown,const std::string& error) {
    std::string o="{\"name\":"+(p?json_string(p->name):std::string("null"))+",\"matched\":"+(p && p->matched?"true":"false")
        +",\"error\":"+(error.empty()?std::string("null"):json_string(error))+",\"figures\":[";
    if (p) for (std::size_t i=0;i<p->figures.size();++i) {
        const auto& f=p->figures[i];
        const auto* spec=settable_figure(f.key);
        const double now=spec && flown?(*flown).*spec->field:f.value;
        o+=std::string(i?",":"")+"{\"key\":"+json_string(f.key)+",\"value\":"+number_json(f.value)+",\"flown\":"+number_json(now)+",\"unit\":"+json_string(f.unit)
            +",\"tag\":"+json_string(f.tag)+",\"source\":"+json_string(f.source)+'}';
    }
    o+="],\"aileron_figures\":[";
    if (p) for (std::size_t i=0;i<p->aileron_figures.size();++i) {
        const auto& f=p->aileron_figures[i];
        o+=std::string(i?",":"")+"{\"key\":"+json_string(f.key)+",\"value\":"+number_json(f.value)+",\"unit\":"+json_string(f.unit)
            +",\"tag\":"+json_string(f.tag)+",\"source\":"+json_string(f.source)+'}';
    }
    o+="],\"aileron_sections\":";
    if (p && p->aileron_sections) o+="["+std::to_string((*p->aileron_sections)[0])+","+std::to_string((*p->aileron_sections)[1])+"]";
    else o+="null";
    return o+"}";
}
// The settable figures' bounds and the page's steps; constant.
inline std::string figure_limits_json() {
    std::string o="{";
    for (const auto& spec:figure_table()) {
        if (!spec.field) continue;
        o+=(o.size()>1?",":"")+json_string(spec.key)+":{\"unit\":"+json_string(spec.unit)+",\"min\":"+number_json(spec.low)
            +",\"max\":"+number_json(spec.high)+",\"step\":"+number_json(spec.step)+'}';
    }
    return o+"}";
}
// Pitch as the page shows it (api.md "State fields").
inline std::string pitch_json(const PitchLive& p) {
    std::string o="{\"now_percent\":"+number_or_null(p.now_percent)+",\"yoke_percent\":"+decimal_or_null(p.yoke_percent)
        +",\"aoa_deg\":"+decimal_or_null(p.aoa_deg)+",\"deflection_deg\":"+decimal_or_null(p.deflection_deg)
        +",\"stalled\":"+(p.stalled<0?"null":p.stalled?"true":"false")+",\"inputs\":";
    if (p.inputs) o+="{\"tail_pa\":"+number_or_null(rounded(p.inputs->tail_pa))+",\"tail_kt\":"+number_or_null(airflow_kt(p.inputs->tail_pa))
        +",\"tail_source\":"+(p.tail_source==TailSource::surface?"\"surface\"":"\"airspeed\"")+",\"trim\":"+decimal_or_null(p.inputs->trim,2)
        +",\"g\":"+decimal_or_null(p.inputs->load_factor,2)
        // X-Plane's tail angle as read and the tail-plane's stalled share
        // (decision 17), slewed as the model takes it, and as read; 0 and
        // null on airspeed's.
        +",\"tail_alpha_deg\":"+decimal_or_null(p.inputs->tail_alpha_deg,2)+",\"tail_stalled_xp\":"+decimal_or_null(p.inputs->tail_stalled_xp,2)
        +",\"tail_stalled_xp_raw\":"+decimal_or_null(p.tail_stalled_raw,2)+'}';
    else o+="null";
    o+=",\"model\":";
    if (p.solution) {
        const auto& s=*p.solution;
        o+="{\"weight_lb\":"+decimal_or_null(s.weight_lb)+",\"stiffness_lb_per_in\":"+decimal_or_null(s.stiffness_lb_per_in)
            +",\"centre_percent\":"+decimal_or_null(travel_percent(s.centre_counts))+",\"rest_percent\":"+decimal_or_null(travel_percent(s.rest_counts))
            +",\"force_lb\":"+decimal_or_null(s.force_lb,2)+",\"slope_lb_per_percent\":"+decimal_or_null(s.slope_lb_per_count*counts_per_percent(),3)
            // The tail at the yoke: the angle the model takes; the
            // tail-plane's stall (X-Plane's) and the elevator's own (alpha +
            // tau*d), kept apart; the larger, which the hinge moment uses;
            // air from behind; the float.
            +",\"tail\":{\"alpha_deg\":"+decimal_or_null(s.tail_alpha_deg,2)+",\"tail_stalled_xp\":"+decimal_or_null(s.tail_stalled_xp,2)
            +",\"elevator_stalled\":"+decimal_or_null(s.elevator_stalled,2)+",\"hinge_stalled\":"+decimal_or_null(s.hinge_stalled,2)
            +",\"reversed\":"+(s.tail_reversed?"true":"false")+",\"float_lb\":"+decimal_or_null(s.float_lb,2)+"}}";
        const double unit=counts_per_percent();
        o+=",\"drive\":{\"strength_percent\":"+std::to_string(s.strength)+",\"ceiling_percent\":"+std::to_string(s.ceiling)
            +",\"centre_percent\":"+decimal_or_null(travel_percent(s.centre))
            +",\"bias_lb\":"+decimal_or_null(s.bias_lb,2)+",\"rest_percent\":"+decimal_or_null(travel_percent(s.drive_rest_counts))
            +",\"force_lb\":"+decimal_or_null(s.drive_force_lb,2)+",\"slope_lb_per_percent\":"+decimal_or_null(s.drive_slope_lb_per_count*unit,3)
            +",\"forward_stop_lb\":"+decimal_or_null(s.bias_lb+s.drive_slope_lb_per_count*unit*(-100-travel_percent(s.centre)),2)
            +",\"aft_stop_lb\":"+decimal_or_null(s.bias_lb+s.drive_slope_lb_per_count*unit*(100-travel_percent(s.centre)),2)
            +",\"shrink_ratio\":"+decimal_or_null(s.shrink_ratio,3)+",\"saturated\":"+(s.saturated?"true":"false")+",\"floored\":"+(s.floored?"true":"false")
            +",\"bias_limited\":"+(s.bias_limited?"true":"false")+",\"registers\":{\"strength\":"+std::to_string(s.strength)
            +",\"trim_position\":"+std::to_string(s.centre)+",\"bias\":"+std::to_string(s.bias)+"},\"phase\":"+json_string(p.phase)
            +",\"update_hz\":"+decimal_or_null(p.update_hz)+'}';
    } else o+="null,\"drive\":null";
    return o+",\"drag_waiting\":"+(p.drag_waiting?"true":"false")+'}';
}
// Roll as the page shows it (api.md "State fields"): X-Plane's readings, then
// the aileron model's inputs per side and its solution at the wheel.
inline std::string roll_json(const AxisLive& a) {
    std::string o="{\"now_percent\":"+number_or_null(a.now_percent)+",\"airflow_kt\":"+number_or_null(airflow_kt(a.airflow_pa))
        +",\"airflow_pa\":"+number_or_null(std::isfinite(a.airflow_pa) && a.airflow_pa>=0?rounded(a.airflow_pa):std::nullopt)
        +",\"aoa_deg\":"+decimal_or_null(a.aoa_deg)+",\"deflection_deg\":"+decimal_or_null(a.deflection_deg)
        +",\"stalled\":"+(a.stalled<0?"null":a.stalled?"true":"false")+",\"yoke_percent\":"+decimal_or_null(a.yoke_percent)+",\"inputs\":";
    // The stalled share slewed as the model takes it, and as X-Plane gave it.
    const auto side=[](const AileronSide& s,double raw) {
        return "{\"pa\":"+number_or_null(rounded(s.pa))+",\"kt\":"+number_or_null(airflow_kt(s.pa))+",\"alpha_deg\":"+decimal_or_null(s.alpha_deg)
            +",\"stalled\":"+decimal_or_null(s.stalled,2)+",\"stalled_raw\":"+decimal_or_null(raw,2)+'}';
    };
    if (a.inputs) o+="{\"source\":"+std::string(a.source==AileronSource::surface?"\"surface\"":"\"airspeed\"")+",\"left\":"+side(a.inputs->left,a.stalled_raw[0])
        +",\"right\":"+side(a.inputs->right,a.stalled_raw[1])+'}';
    else o+="null";
    o+=",\"model\":";
    if (a.solution) {
        const auto& s=*a.solution;
        o+="{\"wheel_deg\":"+decimal_or_null(s.wheel_deg)+",\"rest_deg\":"+decimal_or_null(s.rest_deg)+",\"left_deg\":"+decimal_or_null(s.left_deg)
            +",\"right_deg\":"+decimal_or_null(s.right_deg)+",\"torque_lb\":"+decimal_or_null(s.model_lb,2)+'}';
        o+=",\"drive\":{\"strength_percent\":"+std::to_string(s.strength)+",\"ceiling_percent\":"+std::to_string(s.ceiling)
            +",\"centre_counts\":"+std::to_string(s.centre)
            +",\"torque_lb\":"+decimal_or_null(s.drive_lb,2)+",\"saturated\":"+(s.saturated?"true":"false")+",\"floored\":"+(s.floored?"true":"false")
            +",\"force_exact\":"+(s.force_exact?"true":"false")+",\"phase\":"+json_string(a.phase)+'}';
    } else o+="null,\"drive\":null";
    return o+'}';
}

// ---- The shared state ----
class Workbench {
public:
    explicit Workbench(Settings managed=Settings{}):start_(Clock::now()) {
        if (!valid(managed)) managed=Settings{};
        model_.kept=model_.trying=managed;
    }
    // At start, before the server: Kept is the managed settings overlaid by the
    // feel file; Trying is the draft if one exists. Invalid files are ignored.
    // Never throws: whatever is wrong with a file is logged and the file ignored.
    std::vector<std::string> load(const std::filesystem::path& dir) noexcept {
        std::vector<std::string> log;
        try {load_locked(dir,log);}
        catch (const std::exception& e) {log.push_back(std::string("Feel workbench: files not loaded (")+e.what()+")");}
        catch (...) {log.push_back("Feel workbench: files not loaded");}
        return log;
    }
private:
    void load_locked(const std::filesystem::path& dir,std::vector<std::string>& log) {
        std::lock_guard<std::mutex> lock(mutex_);
        dir_=dir;
        const auto read=[&](const char* name,const char* what,const Settings& base)->std::optional<Settings> {
            const auto path=dir/name;
            std::error_code ec;
            if (!std::filesystem::exists(path,ec)) return {};
            try {
                std::istringstream in(read_bounded(path,64*1024));
                std::vector<std::string> ignored;
                auto s=parse_feel(in,base,&ignored);
                if (!ignored.empty()) {
                    std::string keys;
                    for (const auto& k:ignored) keys+=(keys.empty()?"":", ")+k;
                    // Schema 1 lists trim_percent; schema 2 roll's retired spring;
                    // schema 3 only the firmests the drive's own ceiling retired.
                    const auto has=[&](const char* k) {return std::find(ignored.begin(),ignored.end(),k)!=ignored.end();};
                    const char* before=has("trim_percent")?"the elevator model":has("plain_percent")?"the aileron model":"the drive's own ceiling (decision 16)";
                    log.push_back(std::string("Feel workbench: ")+name+" is from before "+before+"; ignored "+keys
                        +", and the drive settings it lacks come from BrunnerDirect.ini");
                }
                return s;
            } catch (const std::exception& e) {log.push_back(std::string("Feel workbench: ")+name+" ignored ("+e.what()+"); "+what);}
            return {};
        };
        if (const auto kept=read(feel_file,"flying BrunnerDirect.ini's values",model_.kept)) {model_.kept=*kept;log.push_back("Feel workbench: Kept settings loaded from BrunnerDirect.feel.ini");}
        model_.trying=model_.kept;
        if (const auto draft=read(draft_file,"Trying starts as Kept",model_.kept)) {model_.trying=*draft;log.push_back("Feel workbench: Trying restored from the draft");}
        // Flights start flying Kept. With no draft, Trying is Kept, so it is shown
        // and the page opens ready to edit.
        model_.showing_trying=model_.trying==model_.kept;model_.undo.clear();
        const auto marks=dir/marks_file;
        std::error_code ec;
        if (std::filesystem::exists(marks,ec)) try {
            // 100 marks with full notes are under 100 KiB.
            std::istringstream in(read_bounded(marks,1024*1024));
            std::string line;unsigned skipped=0;
            while (std::getline(in,line)) {
                if (!line.empty() && line.back()=='\r') line.pop_back();
                if (line.empty()) continue;
                try {
                    auto m=parse_mark(line);
                    if (std::any_of(marks_.begin(),marks_.end(),[&](const Mark& x){return x.id==m.id;})) throw JsonError("duplicate id");
                    next_mark_=std::max(next_mark_,m.id+1);marks_.push_back(std::move(m));
                } catch (const std::exception&) {++skipped;}
            }
            std::sort(marks_.begin(),marks_.end(),[](const Mark& a,const Mark& b){return a.id<b.id;});
            while (marks_.size()>mark_limit) marks_.pop_front();
            if (skipped) log.push_back("Feel workbench: "+std::to_string(skipped)+" unreadable line(s) in BrunnerDirect.marks.jsonl ignored");
        } catch (const std::exception& e) {
            marks_.clear();next_mark_=1;
            log.push_back(std::string("Feel workbench: BrunnerDirect.marks.jsonl ignored (")+e.what()+"); marks start again");
        }
        draft_version_=written_draft_=marks_version_=written_marks_=0;
    }
public:
    // ---- Sim thread ----
    // Every frame: what to fly, with the count of the feel's edits: anything
    // that changed what is flown other than the flight itself.
    Applied frame(Clock::time_point,const FrameInputs& in) {
        std::lock_guard<std::mutex> lock(mutex_);
        inputs_=in;
        if (in.valid) sample_=in.inputs;
        Applied a;
        a.settings=flown(model_);
        const auto figures=effective();
        if (figures) {
            a.has_profile=true;a.matched=profile_->matched;a.figures=*figures;a.ailerons=profile_->aileron;
            if (profile_->matched) {
                a.tail_sections=profile_->tail_sections;a.aileron_sections=profile_->aileron_sections;a.wing_root_sections=profile_->wing_root_sections;
            }
        }
        const bool same=last_flown_ && *last_flown_==a.settings && last_figures_.has_value()==figures.has_value()
            && (!figures || (same_figures(*last_figures_,*figures) && same_figures(last_ailerons_,a.ailerons)))
            && last_sections_==a.tail_sections && last_aileron_sections_==a.aileron_sections;
        if (!same) ++edit_;
        last_flown_=a.settings;last_figures_=figures;last_sections_=a.tail_sections;last_ailerons_=a.ailerons;last_aileron_sections_=a.aileron_sections;
        a.knobs={a.settings.pitch.units_per_lb,edit_};
        a.roll_knobs={a.settings.roll.units_per_nm,edit_};
        return a;
    }
    // A profiles file read, or the aircraft changed: flown at once, with a
    // notice of what changed. `aircraft` names the aircraft a default flies on.
    void set_profile(Clock::time_point now,Profile p,bool new_flight,const std::string& aircraft={}) {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto before=effective();
        const auto after=with_overrides(p.elevator,flown(model_).pitch.figures);
        const bool sections=profile_ && profile_->tail_sections==p.tail_sections && profile_->aileron_sections==p.aileron_sections && profile_->matched==p.matched
            && profile_->wing_root_sections==p.wing_root_sections;
        const bool same=before && sections && same_figures(*before,after) && same_figures(profile_->aileron,p.aileron);
        const bool renamed=!profile_ || profile_->name!=p.name;
        if (!renamed && same) {fly(std::move(p));return;}
        const std::string name=p.name+(p.matched?"":aircraft.empty()?" (the default)":" (the default for "+aircraft+")");
        // The first read, at X-Plane's start, is the file's; later ones say
        // what made them: a new flight, or the file again (a deploy).
        const bool first=!profile_;
        std::string text;
        if (first) text="Profile file read: flying "+name;
        else if (new_flight) text="Aircraft changed: flying "+name;
        else {
            std::vector<std::string> parts;
            if (profile_->matched!=p.matched) parts.push_back(p.matched?"now this aircraft's own":"now the default for this aircraft");
            if (profile_->tail_sections!=p.tail_sections) parts.push_back("tail air "+tail_text(profile_->tail_sections)+" → "+tail_text(p.tail_sections));
            if (profile_->aileron_sections!=p.aileron_sections)
                parts.push_back("aileron air "+tail_text(profile_->aileron_sections)+" → "+tail_text(p.aileron_sections));
            if (profile_->wing_root_sections!=p.wing_root_sections)
                parts.push_back("buffet's wing roots "+tail_text(profile_->wing_root_sections)+" → "+tail_text(p.wing_root_sections));
            if (const int n=aileron_changes(profile_->aileron,p.aileron)) parts.push_back(std::to_string(n)+(n==1?" aileron figure":" aileron figures")+" changed");
            int changed=0;
            if (before) for (const auto& spec:figure_table()) changed+=spec.field && (*before).*spec.field!=after.*spec.field;
            const auto what=before?feel_changes(flown(model_),flown(model_),before,after):std::string();
            if (changed) parts.push_back(std::to_string(changed)+(changed==1?" figure":" figures")+" changed"+(what.empty()?"":": "+what));
            text="Profile file reloaded: "+std::string(renamed?"flying ":"")+p.name;
            for (const auto& part:parts) text+=", "+part;
        }
        fly(std::move(p));
        // A new flight's rest follows from its own air and trim, not the last
        // flight's sample, so only a reload says how far the rest moves.
        notice_locked(now,new_flight && !first?"aircraft":"profile",text,before && !new_flight?rest_move(*before,after):std::nullopt);
    }
    // After each read of the profiles file: why it was refused, or empty when
    // it parsed. The page shows the reason until a good file arrives.
    void profile_refused(const std::string& why) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!why.empty() && why!=profile_error_)
            event("Feel profiles refused: "+why+(profile_?"; the figures already flown stay":"; force feel waits for a profile"));
        profile_error_=why;
    }
    void publish(Clock::time_point now,const Live& live) {
        std::lock_guard<std::mutex> lock(mutex_);
        live_=live;published_=now;has_live_=true;
        if (history_.empty() || now-history_.back().at>=std::chrono::milliseconds(190)) {
            Sample h{now,live.pitch.now_percent,live.roll.now_percent,live.pitch.inputs?airflow_kt(live.pitch.inputs->tail_pa):std::nullopt,
                live.pitch.yoke_percent};
            if (live.pitch.solution) {
                h.rest_percent=travel_percent(live.pitch.solution->rest_counts);h.model_lb=live.pitch.solution->force_lb;h.drive_lb=live.pitch.solution->drive_force_lb;
            }
            history_.push_back(h);
            while (history_.size()>history_samples) history_.pop_front();
        }
        while (!history_.empty() && now-history_.front().at>std::chrono::seconds(120)) history_.pop_front();
    }
    // In the order asked. A Stop has already removed what was asked before it.
    std::vector<Command> take_commands() {
        std::lock_guard<std::mutex> lock(mutex_);
        return std::exchange(commands_,{});
    }
    std::vector<std::string> take_events() {std::lock_guard<std::mutex> lock(mutex_);return std::exchange(events_,{});}
    BuffetView buffet() const {std::lock_guard<std::mutex> lock(mutex_);return buffet_;}
    // Direction changes the page asked for, in order, for the sim thread to apply (it owns the presets
    // file); the plugin then publishes the result with set_directions.
    std::vector<DirectionChange> take_directions() {
        std::lock_guard<std::mutex> lock(mutex_);
        return std::exchange(direction_requests_,{});
    }
    void set_directions(const DirectionView& d) {std::lock_guard<std::mutex> lock(mutex_);directions_=d;}
    // {"axis": "roll"|"pitch", "field": "encoder_sign"|"spring_sign"|"input_reverse", "value": +1|-1 or true|false}.
    Outcome direction_request(const Json& body,Clock::time_point) {
        const auto* axis=body.get("axis");const auto* field=body.get("field");const auto* value=body.get("value");
        const char* shape="expected {\"axis\": \"roll\"|\"pitch\", \"field\": \"encoder_sign\"|\"spring_sign\"|\"input_reverse\", \"value\": 1|-1 or true|false}";
        if (body.kind!=Json::Kind::object || body.members.size()!=3 || !axis || !field || !value
            || axis->kind!=Json::Kind::string || field->kind!=Json::Kind::string) return bad_request(shape);
        if (axis->text!="roll" && axis->text!="pitch") return out_of_bounds("axis is roll or pitch");
        if (field->text!="encoder_sign" && field->text!="spring_sign" && field->text!="input_reverse") return out_of_bounds("field is encoder_sign, spring_sign or input_reverse");
        DirectionChange c;
        c.pitch=axis->text=="pitch";
        c.field=field->text=="input_reverse"?DirectionChange::Field::input_reverse:field->text=="spring_sign"?DirectionChange::Field::spring_sign:DirectionChange::Field::encoder_sign;
        if (c.input_reverse()) {
            if (value->kind!=Json::Kind::boolean) return bad_request("input_reverse takes true or false");
            c.value=value->boolean?1:0;
        } else {
            if (value->kind!=Json::Kind::integer) return bad_request("a sign takes 1 or -1");
            if (value->integer!=1 && value->integer!=-1) return out_of_bounds("a sign is 1 or -1");
            c.value=static_cast<int>(value->integer);
        }
        std::lock_guard<std::mutex> lock(mutex_);
        if (!directions_.editable) return out_of_bounds("no yoke model is active to change");
        if (direction_requests_.size()>=8) return {503,"Too many changes waiting. Try again."};
        direction_requests_.push_back(c);
        event("Feel workbench asked for "+std::string(c.pitch?"pitch ":"roll ")+(c.input_reverse()?"input reverse ":c.field==DirectionChange::Field::spring_sign?"spring sign ":"encoder sign ")+std::to_string(c.value));
        return {};
    }
    void set_buffet(const BuffetView& b) {if (valid_buffet_view(b)) {std::lock_guard<std::mutex> lock(mutex_);buffet_=b;}}
    // {"on": bool, "percent": 0..100, "period": 10..140}, all three.
    Outcome buffet_request(const Json& body,Clock::time_point) {
        const auto* on=body.get("on");const auto* percent=body.get("percent");const auto* period=body.get("period");
        if (body.kind!=Json::Kind::object || body.members.size()!=3 || !on || !percent || !period || on->kind!=Json::Kind::boolean
            || percent->kind!=Json::Kind::integer || period->kind!=Json::Kind::integer)
            return bad_request("expected {\"on\": true|false, \"percent\": 0..100, \"period\": 10..140}");
        if (percent->integer<0 || percent->integer>100) return out_of_bounds("buffet percent is 0 to 100");
        if (period->integer<10 || period->integer>140) return out_of_bounds("buffet period is 10 to 140");
        const BuffetView b{on->boolean,static_cast<int>(percent->integer),static_cast<int>(period->integer)};
        std::lock_guard<std::mutex> lock(mutex_);
        if (b.on!=buffet_.on || b.percent!=buffet_.percent || b.period!=buffet_.period)
            event(std::string("Feel workbench: buffet ")+(b.on?"on":"off")+", "+std::to_string(b.percent)+"%, period "+std::to_string(b.period)+" (untested with the motors on)");
        buffet_=b;
        return {};
    }
    // A live dataref edit: an edit of Trying, which is then shown.
    bool edit_live(Clock::time_point now,const std::function<bool(Settings&)>& edit,const std::string& what) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto next=model_;
        if (!edit(next.trying) || !flyable(next.trying)) return false;
        if (next.trying!=model_.trying) push_undo(next,model_.trying);
        next.showing_trying=true;
        change(std::move(next),now,"dataref","Edited from X-Plane");
        event("Feel Trying edited by dataref: "+what);
        return true;
    }
    // brunner_direct/compare_feel toggles Kept and Trying, for a yoke button.
    void compare(Clock::time_point now) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto next=model_;next.showing_trying=!next.showing_trying;
        const std::string label=next.showing_trying?"Showing your changes":"Showing your saved settings";
        change(std::move(next),now,"show",label);
        event(std::string("Feel compare: showing ")+(model_.showing_trying?"Trying":"Kept"));
    }
    void mark(Clock::time_point now) {std::lock_guard<std::mutex> lock(mutex_);mark_locked(now);}
    // A change the plugin makes or sees, such as force feel coming on or the
    // constant force cleared at a stop.
    void notify(Clock::time_point now,const std::string& cause,const std::string& text) {
        std::lock_guard<std::mutex> lock(mutex_);
        notice_locked(now,cause,text,std::nullopt);
    }
    // A change to what is flown made outside the workbench: counted as an
    // edit, so the session blends it in, with its notice. (Its one caller,
    // the commissioning firmness, retired with decision 16.)
    void outside_change(Clock::time_point now,const std::string& cause,const std::string& text) {
        std::lock_guard<std::mutex> lock(mutex_);
        ++edit_;notice_locked(now,cause,text,std::nullopt);
    }
    // ---- Server thread ----
    Outcome settings(const Json& body,Clock::time_point now) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto next=model_;
        auto o=merge_settings(body,next.trying);
        if (o.status!=200) return o;
        if (next.trying==model_.trying) return {};
        if (!flyable(next.trying)) return {422,not_flyable};
        next.showing_trying=true; // an edit always flies your changes, even while comparing
        push_undo(next,model_.trying);
        const auto what=describe(next.trying);
        change(std::move(next),now,"edit","Your edit");
        event("Feel Trying edited: "+what);
        return {};
    }
    Outcome show(const Json& body,Clock::time_point now) {
        const auto* which=body.get("which");
        if (body.kind!=Json::Kind::object || body.members.size()!=1 || !which || which->kind!=Json::Kind::string) return bad_request("expected {\"which\": \"kept\" or \"trying\"}");
        if (which->text!="kept" && which->text!="trying") return out_of_bounds("which must be kept or trying");
        std::lock_guard<std::mutex> lock(mutex_);
        auto next=model_;next.showing_trying=which->text=="trying";
        const bool changed=next.showing_trying!=model_.showing_trying;
        change(std::move(next),now,"show",which->text=="trying"?"Showing your changes":"Showing your saved settings");
        if (changed) event("Feel showing "+which->text);
        return {};
    }
    Outcome undo(Clock::time_point now) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (model_.undo.empty()) return {};
        auto next=model_;next.trying=next.undo.back();next.undo.pop_back();
        const auto what=describe(next.trying);
        change(std::move(next),now,"undo","Undo");
        event("Feel undo: Trying "+what);
        return {};
    }
    Outcome discard(Clock::time_point now) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (model_.trying==model_.kept) return {};
        auto next=model_;push_undo(next,model_.trying);next.trying=next.kept;
        change(std::move(next),now,"revert","Revert");
        event("Feel discard: Trying is Kept again");
        return {};
    }
    // Kept becomes Trying and is saved first; a failed write changes nothing.
    // Keeping never changes what is flown, so it never moves the yoke.
    Outcome keep(Clock::time_point now) {
        Settings copy;std::filesystem::path dir;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!model_.showing_trying) return {409,"You're feeling your saved settings. Go back to your changes to save them."};
            copy=model_.trying;dir=dir_;
        }
        std::filesystem::path staged;
        if (!dir.empty()) {
            try {staged=write_staged(dir/feel_file,feel_text(copy,"Kept: every flight starts with these"));}
            catch (const std::exception& e) {
                std::lock_guard<std::mutex> lock(mutex_);
                event(std::string("Feel Keep failed: ")+e.what());
                return {500,std::string("Could not save: ")+e.what()};
            }
        }
        std::lock_guard<std::mutex> lock(mutex_);
        // X-Plane may have changed Trying while the file was written: keep
        // nothing then, rather than a version nobody is looking at.
        if (!model_.showing_trying || model_.trying!=copy) {
            std::error_code ec;if (!staged.empty()) std::filesystem::remove(staged,ec);
            return {409,"Trying changed while saving. Nothing was kept."};
        }
        if (!staged.empty()) {
            std::error_code ec;std::filesystem::rename(staged,dir/feel_file,ec);
            if (ec) {
                std::filesystem::remove(staged,ec);
                event("Feel Keep failed: cannot replace "+std::string(feel_file));
                return {500,"Could not save: cannot replace "+std::string(feel_file)};
            }
        }
        model_.kept=copy;++draft_version_;
        toast("kept",now);event("Feel kept: "+describe(copy));
        return {};
    }
    Outcome mark_request(Clock::time_point now) {std::lock_guard<std::mutex> lock(mutex_);mark_locked(now);return {};}
    Outcome note(const Json& body,Clock::time_point) {
        const auto* id=body.get("id");const auto* text=body.get("note");
        if (body.kind!=Json::Kind::object || body.members.size()!=2 || !id || !text || id->kind!=Json::Kind::integer || text->kind!=Json::Kind::string)
            return bad_request("expected {\"id\": <mark>, \"note\": \"...\"}");
        if (code_points(text->text)>note_limit) return out_of_bounds("note is over 200 characters");
        for (const char c:text->text) if (static_cast<unsigned char>(c)<0x20 && c!='\n') return out_of_bounds("note has a control character");
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto& m:marks_) if (m.id==id->integer) {
            m.note=text->text;++marks_version_;
            event("Feel mark "+std::to_string(m.id)+" note: "+m.note);
            return {};
        }
        return out_of_bounds("no mark "+std::to_string(id->integer));
    }
    Outcome command(const Json& body,Clock::time_point) {
        const auto* name=body.get("name");
        if (body.kind!=Json::Kind::object || body.members.size()!=1 || !name || name->kind!=Json::Kind::string) return bad_request("expected {\"name\": \"...\"}");
        Command c;
        if (name->text=="stop_motors") c=Command::stop_motors;
        else if (name->text=="force_feel_on") c=Command::force_feel_on;
        else if (name->text=="retry") c=Command::retry;
        else return out_of_bounds("unknown command: "+name->text);
        std::lock_guard<std::mutex> lock(mutex_);
        // Stop is never refused, and it supersedes everything still waiting: a
        // queued Turn force feel on or Retry must not run after it and undo it.
        if (c==Command::stop_motors) {commands_.clear();commands_.push_back(c);}
        else if (commands_.size()>=8) return {503,"Too many commands waiting. Try again."};
        else commands_.push_back(c);
        event("Feel workbench asked for "+name->text);
        return {};
    }
    void set_listen(std::vector<std::string> addresses,int port) {std::lock_guard<std::mutex> lock(mutex_);listen_=std::move(addresses);port_=port;}
    // Writes the draft and marks when they changed. Server thread only.
    void persist() {
        std::optional<Settings> draft;bool write_draft=false,write_marks=false;std::string marks_text;
        std::filesystem::path dir;std::uint64_t dv=0,mv=0;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (dir_.empty()) return;
            dir=dir_;dv=draft_version_;mv=marks_version_;
            write_draft=dv!=written_draft_;write_marks=mv!=written_marks_;
            if (write_draft && model_.trying!=model_.kept) draft=model_.trying;
            if (write_marks) for (const auto& m:marks_) marks_text+=mark_json(m)+'\n';
        }
        std::string failure;
        try {
            if (write_draft) {
                if (draft) write_atomically(dir/draft_file,feel_text(*draft,"Trying: a draft, not yet kept"));
                else {std::error_code ec;std::filesystem::remove(dir/draft_file,ec);if (ec) throw std::runtime_error("cannot remove the draft");}
            }
        } catch (const std::exception& e) {failure=e.what();write_draft=false;}
        try {if (write_marks) write_atomically(dir/marks_file,marks_text);}
        catch (const std::exception& e) {failure=e.what();write_marks=false;}
        std::lock_guard<std::mutex> lock(mutex_);
        if (write_draft) written_draft_=dv;
        if (write_marks) written_marks_=mv;
        if (!failure.empty() && failure!=last_failure_) event("Feel workbench file not written: "+failure);
        last_failure_=failure;
    }
    std::string state_json(Clock::time_point now) {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto ms=[](Clock::duration d) {return std::chrono::duration_cast<std::chrono::milliseconds>(d).count();};
        const auto age=std::clamp<std::int64_t>(ms(now-(has_live_?published_:start_)),0,1000000000);
        const auto& l=live_;
        std::string o="{\"schema\":3,\"sim_age_ms\":"+std::to_string(age)+",\"status\":"+json_string(l.status)+",\"view\":"+json_string(l.view)
            +",\"motors\":"+json_string(l.motors)+",\"reason\":"+(l.reason.empty()?std::string("null"):json_string(l.reason))
            +",\"retry_helps\":"+(l.retry_helps?"true":"false")+",\"aircraft\":{\"name\":"+json_string(l.aircraft)+",\"file\":"+json_string(l.aircraft_file)
            +",\"aileron_panels\":"+(l.aileron_panels?"true":"false")+'}'
            +",\"profile\":"+profile_json(profile_,effective(),profile_error_)+",\"figure_limits\":"+figure_limits_json()
            +",\"ias_kt\":"+number_or_null(rounded(l.ias_kt))+",\"pitch\":"+pitch_json(l.pitch);
        o+=",\"roll\":"+roll_json(l.roll);
        const auto unrestored=[](const std::optional<std::uint32_t>& v) {return v?std::to_string(*v):std::string("null");};
        o+=",\"buffet\":{\"on\":"+std::string(buffet_.on?"true":"false")+",\"percent\":"+std::to_string(buffet_.percent)
            +",\"period\":"+std::to_string(buffet_.period)+",\"roll_units\":"+std::to_string(l.buffet_units[0])
            +",\"pitch_units\":"+std::to_string(l.buffet_units[1])+",\"uncleared\":"+(l.buffet_uncleared?"true":"false")
            +",\"time_unrestored\":["+unrestored(l.buffet_time_unrestored[0])+","+unrestored(l.buffet_time_unrestored[1])+"],\"tested_motors_on\":false}";
        const auto axis_directions=[](int sign,int spring,bool reverse) {return "{\"encoder_sign\":"+std::to_string(sign)+",\"spring_sign\":"+std::to_string(spring)+",\"input_reverse\":"+(reverse?"true":"false")+'}';};
        o+=",\"directions\":{\"editable\":"+std::string(directions_.editable?"true":"false")+",\"model\":"+json_string(directions_.model)
            +",\"roll\":"+axis_directions(directions_.roll_sign,directions_.roll_spring,directions_.roll_reverse)+",\"pitch\":"+axis_directions(directions_.pitch_sign,directions_.pitch_spring,directions_.pitch_reverse)
            +",\"note\":"+(directions_.note.empty()?std::string("null"):json_string(directions_.note))+'}';
        o+=",\"pitch_curve\":[";
        for (std::size_t i=0;i<l.pitch_curve.size();++i) o+=(i?",":"")+std::to_string(l.pitch_curve[i]);
        o+="],\"showing\":"+std::string(model_.showing_trying?"\"trying\"":"\"kept\"")+",\"kept\":"+settings_json(model_.kept)
            +",\"trying\":"+settings_json(model_.trying)+",\"changes\":"+std::to_string(changes(model_.kept,model_.trying))
            +",\"undo\":"+(model_.undo.empty()?"false":"true")+",\"notice\":"+(notices_.empty()?std::string("null"):notice_json(notices_.back(),now))
            +",\"notices\":[";
        for (auto i=notices_.rbegin();i!=notices_.rend();++i) o+=(i==notices_.rbegin()?"":",")+notice_json(*i,now);
        o+="]";
        o+=",\"toast\":";
        if (toast_.empty()) o+="null";
        else o+="{\"text\":"+json_string(toast_)+",\"age_ms\":"+std::to_string(std::max<std::int64_t>(0,ms(now-toast_at_)))+'}';
        o+=",\"marks\":"+std::to_string(marks_.size())+",\"listen\":{\"addresses\":[";
        for (std::size_t i=0;i<listen_.size();++i) o+=(i?",":"")+json_string(listen_[i]);
        o+="],\"port\":"+std::to_string(port_)+"}}";
        return o;
    }
    std::string history_json(Clock::time_point now) {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto ms=[](Clock::duration d) {return std::chrono::duration_cast<std::chrono::milliseconds>(d).count();};
        std::string o="{\"now_ms\":"+std::to_string(ms(now-start_))+",\"samples\":[";
        bool first=true;
        for (const auto& h:history_) {
            if (now-h.at>std::chrono::seconds(120) || h.at>now) continue;
            o+=std::string(first?"":",")+"["+std::to_string(-ms(now-h.at))+","+number_or_null(h.pitch)+","+number_or_null(h.roll)+","+number_or_null(h.tail_kt)
                +","+decimal_or_null(h.yoke_percent)+","+decimal_or_null(h.rest_percent)+","+decimal_or_null(h.model_lb,2)+","+decimal_or_null(h.drive_lb,2)+"]";
            first=false;
        }
        o+="],\"marks\":[";first=true;
        const auto utc=utc_now_ms();
        for (const auto& m:marks_) {
            const auto t=m.utc_ms-utc;
            if (t<-120000 || t>0) continue;
            o+=std::string(first?"":",")+"{\"id\":"+std::to_string(m.id)+",\"t_ms\":"+std::to_string(t)+'}';first=false;
        }
        return o+"]}";
    }
    std::string marks_json() {
        std::lock_guard<std::mutex> lock(mutex_);
        std::string o="[";
        for (auto i=marks_.rbegin();i!=marks_.rend();++i) o+=(i==marks_.rbegin()?"":",")+mark_json(*i);
        return o+"]";
    }
    // For tests and the demo.
    Settings kept() const {std::lock_guard<std::mutex> lock(mutex_);return model_.kept;}
    Settings trying() const {std::lock_guard<std::mutex> lock(mutex_);return model_.trying;}
    // The version being flown.
    Settings shown() const {std::lock_guard<std::mutex> lock(mutex_);return model_.showing_trying?model_.trying:model_.kept;}
    bool showing_trying() const {std::lock_guard<std::mutex> lock(mutex_);return model_.showing_trying;}
    // The newest notice, for X-Plane's corner line; none once it is older than `within`.
    std::optional<Notice> latest_notice(Clock::time_point now,Clock::duration within) const {
        std::lock_guard<std::mutex> lock(mutex_);
        if (notices_.empty() || now-notices_.back().at>within) return {};
        return notices_.back();
    }
    std::vector<Notice> notices() const {std::lock_guard<std::mutex> lock(mutex_);return {notices_.begin(),notices_.end()};}
    std::optional<std::string> profile_name() const {std::lock_guard<std::mutex> lock(mutex_);if (!profile_) return {};return profile_->name;}
private:
    struct Model {Settings kept,trying;bool showing_trying=false;std::vector<Settings> undo;};
    struct Sample {
        Clock::time_point at;std::optional<int> pitch,roll,tail_kt;
        double yoke_percent=std::numeric_limits<double>::quiet_NaN(),rest_percent=std::numeric_limits<double>::quiet_NaN();
        double model_lb=std::numeric_limits<double>::quiet_NaN(),drive_lb=std::numeric_limits<double>::quiet_NaN();
    };
    mutable std::mutex mutex_;
    Clock::time_point start_,last_frame_{},published_{},toast_at_{};
    Model model_;
    std::optional<Profile> profile_; // flown
    std::string profile_error_;
    // What the last frame flew, and the count of the feel's edits since start.
    std::optional<Settings> last_flown_;std::optional<ElevatorFigures> last_figures_;std::optional<std::array<int,2>> last_sections_;
    AileronFigures last_ailerons_;std::optional<std::array<int,2>> last_aileron_sections_;
    std::uint64_t edit_=0,next_notice_=1;
    std::deque<Notice> notices_;
    FrameInputs inputs_;
    std::optional<ElevatorInputs> sample_; // the last fresh one, kept through pauses and stale frames
    Live live_;bool has_live_=false;
    BuffetView buffet_{};
    DirectionView directions_;std::vector<DirectionChange> direction_requests_;
    std::deque<Sample> history_;
    std::deque<Mark> marks_;int next_mark_=1;
    std::vector<Command> commands_;std::vector<std::string> events_;
    std::vector<std::string> listen_;int port_=0;
    std::string toast_,last_failure_;
    std::filesystem::path dir_;
    std::uint64_t draft_version_=0,written_draft_=0,marks_version_=0,written_marks_=0;
    static const Settings& flown(const Model& m) {return m.showing_trying?m.trying:m.kept;}
    void fly(Profile p) {profile_=std::move(p);}
    // The figures flown: the profile's with the shown version's overrides.
    std::optional<ElevatorFigures> effective(const Model& m) const {
        if (!profile_) return {};
        return with_overrides(profile_->elevator,flown(m).pitch.figures);
    }
    std::optional<ElevatorFigures> effective() const {return effective(model_);}
    // How far, in counts forward, new figures move the rest; unknown without a sample.
    std::optional<double> rest_move(const ElevatorFigures& from,const ElevatorFigures& to) const {
        if (!sample_) return {};
        return model_rest_counts(to,*sample_)-model_rest_counts(from,*sample_);
    }
    // The feel just shown changes to `next`: flown at once, with a notice when
    // anything the yoke flies changed.
    void change(Model next,Clock::time_point now,const std::string& cause,const std::string& label) {
        const auto before=effective();const auto from=flown(model_);
        commit(std::move(next));
        const auto after=effective();
        const auto what=feel_changes(from,flown(model_),before,after);
        if (what.empty()) return;
        notice_locked(now,cause,label+": "+what,before && after?rest_move(*before,*after):std::nullopt);
    }
    // A move of the rest under 0.05 % of travel is not mentioned.
    void notice_locked(Clock::time_point now,const std::string& cause,std::string text,std::optional<double> move) {
        std::optional<double> percent;
        if (move && std::abs(*move)>=6) {
            percent=std::round(travel_percent(*move)*10)/10;
            text+="; the rest moves "+decimal_or_null(std::abs(*percent))+"% "+(*move<0?"aft":"forward");
        }
        notices_.push_back({next_notice_++,cause,text,percent,now});
        while (notices_.size()>notice_limit) notices_.pop_front();
        event("Feel notice: "+text);
    }
    void event(std::string text) {if (events_.size()>=64) events_.erase(events_.begin());events_.push_back(std::move(text));}
    void toast(std::string text,Clock::time_point now) {toast_=std::move(text);toast_at_=now;}
    static void push_undo(Model& next,const Settings& previous) {
        next.undo.push_back(previous);
        if (next.undo.size()>undo_depth) next.undo.erase(next.undo.begin());
    }
    static std::string describe(const Settings& s) {
        return "pitch "+std::to_string(s.pitch.units_per_lb)+" units/lb drag 1/"
            +std::to_string(s.pitch.drag_divisor)+" move-back "+std::to_string(s.pitch.move_back)+", roll "
            +std::to_string(s.roll.units_per_nm)+" units/N·m";
    }
    void commit(Model next) {
        if (next.trying!=model_.trying || next.kept!=model_.kept) ++draft_version_;
        model_=std::move(next);
    }
    void mark_locked(Clock::time_point now) {
        Mark m;m.id=next_mark_++;m.utc_ms=utc_now_ms();m.trying=model_.showing_trying && !(model_.trying==model_.kept); // identical to Kept is Kept
        m.settings=settings_json(flown(model_));
        if (profile_) m.profile=profile_->name;
        m.ias_kt=live_.ias_kt;m.bank_deg=live_.bank_deg;m.flaps_deg=live_.flaps_deg;m.vertical_fpm=live_.vertical_fpm;
        marks_.push_back(m);
        while (marks_.size()>mark_limit) marks_.pop_front();
        ++marks_version_;toast("marked",now);
        event("Feel mark "+std::to_string(m.id)+": "+(m.trying?"Trying ":"Kept ")+describe(flown(model_))+"; IAS "+number_or_null(rounded(m.ias_kt))
            +" kt, bank "+number_or_null(rounded(m.bank_deg))+", flaps "+number_or_null(rounded(m.flaps_deg))+", "+number_or_null(rounded(m.vertical_fpm))+" fpm");
    }
};

// ---- HTTP/1.1 ----
struct Request {
    std::string method,target,path,version;
    std::vector<std::pair<std::string,std::string>> headers; // names lower-cased
    std::string body;
    const std::string* header(std::string_view name) const {
        for (const auto& h:headers) if (h.first==name) return &h.second;
        return nullptr;
    }
};
enum class ParseResult {incomplete,complete,bad,too_large};
inline constexpr std::size_t max_header_bytes=8192,max_body_bytes=4096;
inline std::string lower(std::string s) {for (auto& c:s) if (c>='A' && c<='Z') c=char(c-'A'+'a');return s;}
inline std::string trim_space(std::string_view s) {
    const auto b=s.find_first_not_of(" \t");
    if (b==std::string_view::npos) return {};
    return std::string(s.substr(b,s.find_last_not_of(" \t")-b+1));
}
// Parses one request from `buffer`. On too_large the headers are filled in.
inline ParseResult parse_request(const std::string& buffer,Request& r) {
    const auto end=buffer.find("\r\n\r\n");
    if (end==std::string::npos) return buffer.size()>max_header_bytes?ParseResult::bad:ParseResult::incomplete;
    if (end>max_header_bytes) return ParseResult::bad;
    r=Request{};
    std::size_t pos=0;bool first=true;
    while (pos<end) {
        auto eol=buffer.find("\r\n",pos);
        if (eol==std::string::npos || eol>end) eol=end;
        const std::string_view line(buffer.data()+pos,eol-pos);
        pos=eol+2;
        for (const char c:line) if ((static_cast<unsigned char>(c)<0x20 && c!='\t') || c==0x7f) return ParseResult::bad;
        if (first) {
            first=false;
            const auto a=line.find(' '),b=a==std::string_view::npos?a:line.find(' ',a+1);
            if (a==std::string_view::npos || b==std::string_view::npos || line.find(' ',b+1)!=std::string_view::npos) return ParseResult::bad;
            r.method=std::string(line.substr(0,a));r.target=std::string(line.substr(a+1,b-a-1));r.version=std::string(line.substr(b+1));
            if (r.version!="HTTP/1.1" && r.version!="HTTP/1.0") return ParseResult::bad;
            if (r.method.empty() || r.target.empty() || r.target[0]!='/') return ParseResult::bad;
            r.path=r.target.substr(0,r.target.find('?'));
            continue;
        }
        const auto colon=line.find(':');
        if (colon==std::string_view::npos || colon==0) return ParseResult::bad;
        const auto name=lower(std::string(line.substr(0,colon)));
        if (name.find_first_of(" \t")!=std::string::npos) return ParseResult::bad;
        r.headers.emplace_back(name,trim_space(line.substr(colon+1)));
    }
    if (first) return ParseResult::bad;
    std::size_t length=0;int lengths=0;
    for (const auto& h:r.headers) {
        if (h.first=="transfer-encoding") return ParseResult::bad;
        if (h.first!="content-length") continue;
        if (++lengths>1 || h.second.empty() || h.second.size()>9 || h.second.find_first_not_of("0123456789")!=std::string::npos) return ParseResult::bad;
        length=static_cast<std::size_t>(std::stoul(h.second));
    }
    if (length>max_body_bytes) return ParseResult::too_large;
    if (buffer.size()<end+4+length) return ParseResult::incomplete;
    if (buffer.size()>end+4+length) return ParseResult::bad; // one request per connection; no pipelining
    r.body=buffer.substr(end+4,length);
    return ParseResult::complete;
}
struct Response {int status=200;std::string content_type="application/json",body;std::string allow;};
inline const char* reason(int status) {
    switch (status) {
    case 200:return "OK";case 400:return "Bad Request";case 403:return "Forbidden";case 404:return "Not Found";
    case 405:return "Method Not Allowed";case 409:return "Conflict";case 413:return "Content Too Large";
    case 422:return "Unprocessable Content";case 500:return "Internal Server Error";case 503:return "Service Unavailable";default:return "Error";
    }
}
inline std::string build_response(const Response& r) {
    std::string out="HTTP/1.1 "+std::to_string(r.status)+" "+reason(r.status)+"\r\nContent-Type: "+r.content_type
        +"\r\nContent-Length: "+std::to_string(r.body.size())+"\r\nCache-Control: no-store\r\nX-Content-Type-Options: nosniff\r\nConnection: close\r\n";
    if (!r.allow.empty()) out+="Allow: "+r.allow+"\r\n";
    return out+"\r\n"+r.body;
}
inline Response error_response(int status,const std::string& why) {return {status,"application/json","{\"error\":"+json_string(why)+"}",{}};}

// ---- Who may ask ----
// Loopback and private ranges only (api.md "Listener"). Host order.
inline bool allowed_peer(std::uint32_t a) {
    return (a>>24)==127 || (a>>24)==10 || (a>>20)==((172u<<4)|1) || (a>>16)==((192u<<8)|168) || (a>>16)==((169u<<8)|254);
}
inline bool ipv4_literal(std::string_view s) {
    int parts=0;std::size_t i=0;
    while (i<=s.size()) {
        std::size_t j=i;
        while (j<s.size() && s[j]>='0' && s[j]<='9') ++j;
        if (j==i || j-i>3) return false;
        if (std::stoi(std::string(s.substr(i,j-i)))>255) return false;
        ++parts;
        if (j==s.size()) break;
        if (s[j]!='.') return false;
        i=j+1;
    }
    return parts==4;
}
// localhost, an IP literal, or this machine's own name with or without a domain.
inline bool allowed_host(std::string_view header,std::string_view machine) {
    std::string host=lower(std::string(header));
    if (host.empty()) return false;
    if (host.front()=='[') {
        const auto close=host.find(']');
        if (close==std::string::npos) return false;
        const auto rest=host.substr(close+1);
        if (!rest.empty() && (rest[0]!=':' || rest.size()<2 || rest.find_first_not_of("0123456789",1)!=std::string::npos)) return false;
        const auto inside=host.substr(1,close-1);
        return !inside.empty() && inside.find_first_not_of("0123456789abcdef:.")==std::string::npos;
    }
    const auto colon=host.find(':');
    if (colon!=std::string::npos) {
        const auto port=host.substr(colon+1);
        if (port.empty() || port.size()>5 || port.find_first_not_of("0123456789")!=std::string::npos) return false;
        host.resize(colon);
    }
    if (!host.empty() && host.back()=='.') host.pop_back();
    if (host=="localhost" || ipv4_literal(host)) return true;
    const auto name=lower(std::string(machine));
    if (name.empty()) return false;
    return host==name || (host.size()>name.size() && host.compare(0,name.size(),name)==0 && host[name.size()]=='.');
}
inline bool json_content_type(const std::string* value) {
    if (!value) return false;
    const auto v=lower(*value);
    const auto semi=v.find(';');
    return trim_space(std::string_view(v).substr(0,semi))=="application/json";
}

// One request, already parsed, from an allowed peer.
struct Site {
    Workbench& bench;
    std::function<std::string()> page; // the page's bytes
    std::string machine_name;
};
inline Response respond(Site& site,const Request& r,Clock::time_point now) {
    const auto* host=r.header("host");
    if (!host || !allowed_host(*host,site.machine_name)) return error_response(403,"Host not allowed");
    struct Route {const char* path;const char* method;};
    static const Route routes[]{{"/","GET"},{"/api/state","GET"},{"/api/history","GET"},{"/api/marks","GET"},
        {"/api/settings","POST"},{"/api/show","POST"},{"/api/undo","POST"},{"/api/discard","POST"},{"/api/keep","POST"},
        {"/api/mark","POST"},{"/api/note","POST"},{"/api/command","POST"},{"/api/buffet","POST"},{"/api/direction","POST"}};
    const Route* route=nullptr;
    for (const auto& x:routes) if (r.path==x.path) route=&x;
    if (!route) return error_response(404,"No such page");
    if (r.method!=route->method) {auto e=error_response(405,"Use "+std::string(route->method));e.allow=route->method;return e;}
    auto& wb=site.bench;
    if (r.method=="GET") {
        if (r.path=="/") return {200,"text/html; charset=utf-8",site.page?site.page():std::string(),{}};
        if (r.path=="/api/state") return {200,"application/json",wb.state_json(now),{}};
        if (r.path=="/api/history") return {200,"application/json",wb.history_json(now),{}};
        return {200,"application/json",wb.marks_json(),{}};
    }
    const auto* marker=r.header("x-workbench");
    if (!json_content_type(r.header("content-type")) || !marker || *marker!="1") return error_response(403,"POST needs Content-Type: application/json and X-Workbench: 1");
    Json body;
    try {body=parse_json(r.body);} catch (const JsonError& e) {return error_response(400,std::string("Malformed JSON: ")+e.what());}
    if (body.kind!=Json::Kind::object) return error_response(400,"The body must be a JSON object");
    Outcome o;
    const auto bare=[&](Outcome (Workbench::*action)(Clock::time_point))->Outcome {
        if (!empty_object(body)) return bad_request("expected {}");
        return (wb.*action)(now);
    };
    if (r.path=="/api/settings") o=wb.settings(body,now);
    else if (r.path=="/api/show") o=wb.show(body,now);
    else if (r.path=="/api/undo") o=bare(&Workbench::undo);
    else if (r.path=="/api/discard") o=bare(&Workbench::discard);
    else if (r.path=="/api/keep") o=bare(&Workbench::keep);
    else if (r.path=="/api/mark") o=bare(&Workbench::mark_request);
    else if (r.path=="/api/note") o=wb.note(body,now);
    else if (r.path=="/api/buffet") o=wb.buffet_request(body,now);
    else if (r.path=="/api/direction") o=wb.direction_request(body,now);
    else o=wb.command(body,now);
    if (o.status!=200) return error_response(o.status,o.error);
    return {200,"application/json",wb.state_json(now),{}};
}
}
