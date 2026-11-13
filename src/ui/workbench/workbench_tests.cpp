// The feel workbench: pure core, file formats, HTTP handling and a real
// loopback round trip through the server. No hardware, no X-Plane.
#include "ui/workbench/workbench_server.hpp"
#include "config.hpp"
#include "ui/workbench/demo_state.hpp"
#include <algorithm>
#include <clocale>
#include <cstring>
#include <cmath>
#include <fstream>
#include <iostream>
#include <sstream>
#include <thread>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

using namespace brunner;
using namespace brunner::workbench;
namespace {
int failures=0;
void require(bool ok,const char* what) {if (!ok) {std::cerr<<"FAILED: "<<what<<'\n';++failures;}}
void require(bool ok,const std::string& what) {require(ok,what.c_str());}
Json parse(const std::string& s) {return parse_json(s);}
bool rejects_json(const std::string& s) {try {parse_json(s);} catch (const JsonError&) {return true;}return false;}
bool contains(const std::string& text,const std::string& part) {return text.find(part)!=std::string::npos;}
bool has_event(const std::vector<std::string>& events,const std::string& part) {
    return std::any_of(events.begin(),events.end(),[&](const std::string& e){return contains(e,part);});
}
std::filesystem::path scratch(const char* name) {
    auto dir=std::filesystem::temp_directory_path()/("brunner-workbench-test-"+std::string(name)+"-"+std::to_string(utc_now_ms()));
    std::filesystem::remove_all(dir);std::filesystem::create_directories(dir);
    return dir;
}
using ms=std::chrono::milliseconds;
// The page's C172 figures (elevator_vectors.json checks they are), weight moment 30 in·lb.
const ElevatorFigures c172_figures{1.35,0.427,0.45,28,23,0.137,30,0.6,22,19};
// The page's C172 aileron figures (aileron_vectors.json checks they are).
const AileronFigures c172_ailerons{0.85,0.28,0.30,0.15,20,15,86,0.15,0,0.4,1};
const std::string c172_path="Aircraft/Laminar Research/Cessna 172 SP/Cessna_172SP.acf";
Profile profile(const std::string& name,const ElevatorFigures& e,bool matched=true,const AileronFigures& a=c172_ailerons) {
    Profile p;p.name=name;p.matched=matched;p.aircraft={c172_path};
    if (matched) {p.tail_sections=std::array<int,2>{8,9};p.aileron_sections=std::array<int,2>{2,3};}
    p.elevator=e;p.aileron=a;
    for (const auto& spec:figure_table()) if (spec.field) p.figures.push_back({spec.key,spec.unit,"guessed","test",e.*spec.field});
    for (const auto& spec:aileron_figure_table()) if (spec.field) p.aileron_figures.push_back({spec.key,spec.unit,"guessed","test",a.*spec.field});
    return p;
}
Profile c172() {return profile("c172",c172_figures);}
// At 2000 Pa over the tail with trim centred, the weight holds the model's
// rest 195 counts (1.6 %) forward of trim; without the weight, on trim.
FrameInputs flying(bool force_feel=true,double trim=0,double tail_pa=2000) {
    FrameInputs f;f.valid=true;f.force_feel=force_feel;f.connected=true;f.inputs={trim,tail_pa,1};return f;
}

void units() {
    require(pa_from_kt(35)==199 && kt_from_pa(199)==35 && kt_from_pa(200)==35,"halfway kt <-> Pa as api.md says");
    require(pa_from_kt(10)==16 && pa_from_kt(90)==1313,"halfway range in Pa");
    require(!airflow_kt(-1) && !airflow_kt(std::nan("")) && *airflow_kt(0)==0,"airflow null when X-Plane has none");
    require(travel_percent(-1200)==10 && travel_percent(12000)==-100,"the page's percent of travel is aft positive");
    const auto s=from_managed(FlightSettings{});
    require(s==Settings{} && s.pitch.units_per_lb==222 && s.pitch.drag_divisor==6 && s.pitch.move_back==180
        && s.pitch.figures.empty() && s.roll.units_per_nm==1000,"defaults: pitch 222 units/lb, drag 1/6 and 180; roll 1000 units/N·m; no firmest (decision 16)");
    FlightSettings f;f.roll_torque_units_per_nm=20000;
    const auto clamped=from_managed(f);
    require(clamped.roll.units_per_nm==10000,"managed values held to the page's bounds");
    Settings e=s;e.pitch.units_per_lb=300;e.pitch.drag_divisor=3;e.pitch.move_back=360;e.roll.units_per_nm=1500;
    FlightSettings out;apply(e,out);
    require(out.roll_torque_units_per_nm==1500 && out.pitch_force_units_per_lb==300 && out.pitch_force_gain_divisor==3 && out.pitch_move_back_gain==360
        && valid_flight_settings(out),"applied settings are valid flight settings");
    std::cout<<"units: ok\n";
}
void configuration() {
    const auto read=[](const std::string& text) {std::istringstream in(text);return parse_configuration(in);};
    const auto rejected=[&](const std::string& text) {try {read(text);} catch (const std::exception&) {return true;}return false;};
    const std::string common="roll_spring_percent=40\npitch_spring_percent=40\npitch_trim_counts=0\nauto_start=1\nreference_airspeed_knots=316\n";
    const std::vector<std::string> firmests{"pitch_spring_percent","roll_spring_percent"};
    const auto with_firmests=[&](std::vector<std::string> v) {v.insert(v.end(),firmests.begin(),firmests.end());std::sort(v.begin(),v.end());return v;};
    const std::string four="schema_version=4\n"+common+"roll_minimum_spring_percent=24\nroll_half_gain_pa=150\npitch_force_units_per_lb=300\n"
        "pitch_force_gain_divisor=6\npitch_move_back_gain=180\n";
    const auto c=read(four+"workbench_port=8087\n");
    require(c.flight.roll_torque_units_per_nm==1000 && c.flight.pitch_force_units_per_lb==300 && c.workbench_port==8087
        && c.retired==with_firmests({"roll_half_gain_pa","roll_minimum_spring_percent"}),"schema 4: the drive's calibration, the port; roll's spring and both firmests retired");
    require(!rejected(four+"workbench_port=0\n"),"port 0 is off");
    require(rejected(four),"schema 4 needs workbench_port");
    require(rejected(four+"workbench_port=80\n") && rejected(four+"workbench_port=65536\n"),"port bounds");
    std::string no_units=four;no_units.erase(no_units.find("pitch_force_units_per_lb"),std::string("pitch_force_units_per_lb=300\n").size());
    require(rejected(no_units+"workbench_port=8087\n"),"schema 4 needs the drive's calibration");
    // Schema 5: roll's scale for the aileron model, required (decision 6).
    std::string five=four+"workbench_port=8087\n";five.replace(0,16,"schema_version=5");
    require(rejected(five),"schema 5 needs roll's drive scale");
    require(read(five+"roll_torque_units_per_nm=2000\n").flight.roll_torque_units_per_nm==2000,"schema 5: roll's drive scale");
    require(rejected(five+"roll_torque_units_per_nm=10001\n"),"roll's drive scale bounds");
    // Main's schema 3: its firmness keys and roll's spring start retired.
    const auto three=read("schema_version=3\n"+common+"maximum_trim_counts=600\npitch_minimum_spring_percent=22\nroll_minimum_spring_percent=24\n"
        "pitch_half_gain_pa=300\nroll_half_gain_pa=150\nworkbench_port=8087\n");
    require(three.retired==with_firmests({"maximum_trim_counts","pitch_half_gain_pa","pitch_minimum_spring_percent","roll_half_gain_pa","roll_minimum_spring_percent"})
        && three.flight.pitch_force_units_per_lb==222 && three.workbench_port==8087,"schema 3 starts, its firmness keys retired");
    const auto two=read("schema_version=2\n"+common+"maximum_trim_counts=600\nminimum_spring_percent=25\nhalf_gain_pa=250\n");
    require(two.workbench_port==0 && two.retired.size()==5,"schema 2's shared floor and pressure were roll's: retired; no workbench");
    require(rejected("schema_version=2\n"+common+"minimum_spring_percent=20\nworkbench_port=8087\n"),"schema 2 has no port");
    // Schema 6 (decision 16): no firmest on either axis. The old keys are
    // ignored at any value and listed for the log; the springs stay the
    // commissioning channel's 40 %.
    std::string six=five+"roll_torque_units_per_nm=2000\n";six.replace(0,16,"schema_version=6");
    for (const char* k:{"roll_spring_percent=40\n","pitch_spring_percent=40\n"}) six.erase(six.find(k),std::strlen(k));
    const auto c6=read(six);
    require(c6.retired==std::vector<std::string>({"roll_half_gain_pa","roll_minimum_spring_percent"}) && c6.tuning.pitch_percent==40 && c6.tuning.roll_percent==40
        && c6.flight.roll_torque_units_per_nm==2000,"schema 6 parses without the firmests");
    const auto old6=read(six+"pitch_spring_percent=400\nroll_spring_percent=100\n");
    require(old6.retired==with_firmests({"roll_half_gain_pa","roll_minimum_spring_percent"}) && old6.tuning.pitch_percent==40 && old6.tuning.roll_percent==40,
        "an old firmest in schema 6 is ignored and logged");
    require(rejected("schema_version=7\n"+common),"unknown schema");
    std::cout<<"configuration schemas 2-6: ok\n";
}
void feel_files() {
    Settings s;s.pitch={300,4,240,{{"unbalance_inlb",25.5},{"tab_ratio",0.55}}};s.roll={1500};
    const auto text=feel_text(s,"Kept");
    {std::istringstream in(text);require(parse_feel(in)==s,"feel file round trip, figures and all");}
    require(contains(text,"feel_schema=4\n") && contains(text,"roll_units_per_nm=1500\n") && contains(text,"pitch_figure_unbalance_inlb=25.5\n")
        && contains(text,"pitch_figure_tab_ratio=0.55\n") && !contains(text,"firmest"),"schema 4, one line per figure override, no firmest");
    const auto rejects=[](const std::string& t) {try {std::istringstream i(t);parse_feel(i);} catch (const std::exception&) {return true;}return false;};
    require(rejects(text+"roll_units_per_nm=1000\n") && rejects(text+"pitch_figure_tab_ratio=0.5\n"),"duplicate key");
    require(rejects(text+"roll_firmest_percent=20\n") && rejects(text+"pitch_firmest_percent=20\n"),"the firmests are unknown in 4 (decision 16)");
    require(rejects(text+"extra=1\n") && rejects(text+"pitch_floor_percent=20\n"),"unknown key; schema 1's retired keys are unknown in 4");
    require(rejects(text+"roll_floor_percent=20\n") && rejects(text+"plain_percent=20\n"),"roll's spring keys are unknown in 4 (decision 6)");
    require(rejects(text+"pitch_figure_inertia_kgm2=0.4\n") && rejects(text+"pitch_figure_x=1\n"),"only the figures the plugin flies");
    require(rejects(text+"pitch_figure_area_m2=25\n") && rejects(text+"pitch_figure_area_m2=1,4\n") && rejects(text+"pitch_figure_area_m2=\n"),"figure bounds and numbers");
    std::string missing=text;missing.erase(missing.find("pitch_move_back"),std::string("pitch_move_back=240\n").size());require(rejects(missing),"missing key");
    std::string high=text;high.replace(high.find("pitch_units_per_lb=300"),22,"pitch_units_per_lb=1001");require(rejects(high),"out of bounds");
    std::string low=text;low.replace(low.find("roll_units_per_nm=1500"),22,"roll_units_per_nm=99");require(rejects(low),"roll's scale out of bounds");
    std::string schema=text;schema.replace(schema.find("feel_schema=4"),13,"feel_schema=5");require(rejects(schema),"unknown feel schema");
    {
        // Schema 3, before the drive's own ceiling: both firmests ignored and listed.
        std::string three=text;three.replace(three.find("feel_schema=4"),13,"feel_schema=3");
        three+="pitch_firmest_percent=38\nroll_firmest_percent=30\n";
        std::vector<std::string> ignored;std::istringstream in(three);
        require(parse_feel(in,Settings{},&ignored)==s && ignored==std::vector<std::string>{"pitch_firmest_percent","roll_firmest_percent"},
            "schema 3: the firmests ignored and listed");
        require(rejects(three.substr(0,three.find("roll_firmest_percent"))),"schema 3 is strict too");
    }
    // Schema 2, before the aileron model: roll's floor, halfway and the plain
    // spring are ignored and listed; roll's scale comes from the base.
    const std::string two="feel_schema=2\npitch_firmest_percent=38\npitch_units_per_lb=300\npitch_drag_divisor=4\npitch_move_back=240\n"
        "roll_floor_percent=22\nroll_firmest_percent=30\nroll_halfway_kt=30\nplain_percent=25\npitch_figure_tab_ratio=0.55\n";
    {
        Settings base;base.roll.units_per_nm=1200;
        std::vector<std::string> ignored;std::istringstream in(two);const auto o=parse_feel(in,base,&ignored);
        require(o.pitch.units_per_lb==300 && o.pitch.figures.at("tab_ratio")==.55 && o.roll.units_per_nm==1200
            && ignored==std::vector<std::string>{"pitch_firmest_percent","plain_percent","roll_firmest_percent","roll_floor_percent","roll_halfway_kt"},
            "schema 2: roll's spring and both firmests ignored, roll's scale from the base");
        require(rejects(two.substr(0,two.find("plain_percent"))),"schema 2 is strict too");
    }
    // From before the elevator model: what still exists is read, the rest listed.
    const std::string old="feel_schema=1\npitch_floor_percent=22\npitch_firmest_percent=38\npitch_halfway_kt=40\nroll_floor_percent=20\n"
        "roll_firmest_percent=30\nroll_halfway_kt=30\ntrim_percent=12\nplain_percent=25\n";
    Settings base;base.pitch.units_per_lb=300;base.pitch.figures={{"tab_ratio",.5}};
    std::vector<std::string> ignored;
    std::istringstream in(old);const auto o=parse_feel(in,base,&ignored);
    require(o.pitch.units_per_lb==300 && o.pitch.figures.empty() && o.roll.units_per_nm==1000
        && ignored==std::vector<std::string>{"pitch_firmest_percent","pitch_floor_percent","pitch_halfway_kt","plain_percent","roll_firmest_percent","roll_floor_percent",
            "roll_halfway_kt","trim_percent"},
        "schema 1: pitch's floor, halfway and trim, roll's spring and both firmests ignored; the drive's knobs from the base");
    require(rejects(old+"pitch_figure_tab_ratio=0.5\n") && rejects(old.substr(0,old.find("trim_percent"))+"plain_percent=25\n"),"schema 1 is strict too");
    {
        // The pilot's Kept on the PC as read on 2026-11-09 (schema 1): it
        // still loads; both firmests, 100, are ignored and listed, since the
        // drive's own ceiling replaced them (decision 16).
        std::istringstream pc("feel_schema=1\npitch_floor_percent=20\npitch_firmest_percent=100\npitch_halfway_kt=10\nroll_floor_percent=20\n"
            "roll_firmest_percent=100\nroll_halfway_kt=10\ntrim_percent=100\nplain_percent=20\n");
        std::vector<std::string> pc_ignored;
        const auto kept=parse_feel(pc,Settings{},&pc_ignored);
        require(kept==Settings{} && std::count(pc_ignored.begin(),pc_ignored.end(),"pitch_firmest_percent")==1
            && std::count(pc_ignored.begin(),pc_ignored.end(),"roll_firmest_percent")==1,"the PC's Kept: both firmests ignored and listed");
    }
    std::cout<<"feel files: ok\n";
}
void json() {
    const auto j=parse(R"( {"a": [1, -2, true, false, null, "x\u00e9\ud83d\ude00\n"], "b": {"c": 0}} )");
    require(j.get("a")->items.size()==6 && j.get("a")->items[1].integer==-2 && j.get("a")->items[5].text=="x\xc3\xa9\xf0\x9f\x98\x80\n","values and escapes");
    require(parse("2.5").kind==Json::Kind::real && parse("1e3").kind==Json::Kind::real,"reals are recognised, not integers");
    {
        // Under a decimal-comma locale (X-Plane's process may run in one) a
        // real still reads with its point: strtod read "2.5" as 2 there.
        const char* found=nullptr;
        for (const char* name:{"de_DE.UTF-8","de_DE","German_Germany.1252","German"}) if (std::setlocale(LC_ALL,name)) {found=name;break;}
        if (found) {
            require(parse("2.5").real==2.5 && parse("-0.125e1").real==-1.25 && parse(R"({"x":0.6})").get("x")->real==0.6,
                std::string("reals read the same under the ")+found+" locale");
            std::setlocale(LC_ALL,"C");
        } else std::cout<<"json: no decimal-comma locale here; locale check skipped\n";
    }
    for (const char* bad:{"","{","{\"a\":1,}","[1,]","{\"a\":1,\"a\":2}","01","-","\"\\x\"","\"\x01\"","\"\\ud800\"","\"\\udc00\"","{} x","tru","[[[[[[[[[[1]]]]]]]]]]","\"\xc3\"","\"\xc0\xaf\"","99999999999999999999"})
        require(rejects_json(bad),bad);
    require(json_string("a\"b\\\n\x01")=="\"a\\\"b\\\\\\u000a\\u0001\"","string escapes");
    require(json_string("ok \xe2\x80\x94 \xff")=="\"ok \xe2\x80\x94 ?\"","invalid UTF-8 replaced, valid kept");
    Settings s;
    require(merge_settings(parse(R"({"pitch":{"units_per_lb":300,"figures":{"unbalance_inlb":20,"tab_ratio":0.5}},"roll":{"units_per_nm":1500}})"),s).status==200
        && s.pitch.units_per_lb==300 && s.pitch.figures.size()==2 && s.pitch.figures.at("tab_ratio")==.5 && s.roll.units_per_nm==1500,
        "partial S merges, figures by key");
    require(merge_settings(parse(R"({"pitch":{"figures":{"tab_ratio":null,"area_m2":1.4}}})"),s).status==200 && s.pitch.figures.size()==2
        && !s.pitch.figures.count("tab_ratio") && s.pitch.figures.at("area_m2")==1.4 && s.pitch.figures.at("unbalance_inlb")==20,"null removes an override; the others stay");
    const auto status=[&](const char* body) {Settings x;return merge_settings(parse(body),x).status;};
    for (const char* body:{R"({"pitch":{"units_per_lb":49}})",R"({"pitch":{"units_per_lb":1001}})",
            R"({"pitch":{"drag_divisor":7}})",R"({"pitch":{"move_back":179}})",R"({"roll":{"units_per_nm":99}})",R"({"roll":{"units_per_nm":10001}})",
            R"({"pitch":{"figures":{"area_m2":25}}})",R"({"pitch":{"figures":{"unbalance_inlb":-201}}})"})
        require(status(body)==422,std::string("422: ")+body);
    for (const char* body:{R"({"pitch":{"firmest_percent":40}})",R"({"roll":{"firmest_percent":40}})",R"({"yaw":{}})",R"({"pitch":{"x":1}})",R"({"pitch":3})","[]",
            R"({"pitch":{"floor_percent":20}})",R"({"trim_percent":5})",R"({"pitch":{"figures":3}})",R"({"pitch":{"figures":{"inertia_kgm2":1}}})",
            R"({"pitch":{"figures":{"area_m2":"1"}}})",R"({"roll":{"floor_percent":20}})",R"({"roll":{"halfway_kt":35}})",R"({"plain_percent":20})"})
        require(status(body)==400,std::string("400: ")+body);
    Settings t;t.pitch.figures={{"area_m2",1.4}};const auto before=t;
    require(merge_settings(parse(R"({"pitch":{"figures":{"area_m2":null,"chord_m":9}}})"),t).status==422 && t==before,"a refused body changes nothing");
    std::cout<<"json: ok\n";
}
std::string figure_json(const char* key,double v) {return json_string(key)+":{\"value\":"+number_json(v)+",\"tag\":\"published\",\"source\":\"a source\"}";}
std::string elevator_json(const ElevatorFigures& e,const std::string& skip="") {
    std::string o="{";
    for (const auto& spec:figure_table()) if (spec.key!=skip) o+=(o.size()>1?",":"")+figure_json(spec.key,spec.field?e.*spec.field:0.4);
    return o+"}";
}
std::string aileron_json(const AileronFigures& a,const std::string& skip="") {
    std::string o="{";
    for (const auto& spec:aileron_figure_table()) if (spec.key!=skip) o+=(o.size()>1?",":"")+figure_json(spec.key,spec.field?a.*spec.field:0.4);
    return o+"}";
}
std::string profiles_text(const std::string& elevator,const std::string& more="",const std::string& aileron=aileron_json(c172_ailerons)) {
    return "{\"schema\":2,\"default\":\"c172\",\"profiles\":{\"c172\":{\"aircraft\":[\""+c172_path+"\"],\"tail_sections\":[8,9],\"elevator\":"+elevator
        +",\"aileron_sections\":[2,3],\"aileron\":"+aileron+"}"+more+"}}";
}
void profiles_parse() {
    const auto text=profiles_text(elevator_json(c172_figures));
    const auto set=parse_profiles(text);
    require(set.default_name=="c172" && set.profiles.size()==1 && same_figures(set.profiles[0].elevator,c172_figures)
        && set.profiles[0].tail_sections==std::array<int,2>{8,9} && set.profiles[0].figures.size()==19,"a profiles file parses");
    require(same_figures(set.profiles[0].aileron,c172_ailerons) && set.profiles[0].aileron_sections==std::array<int,2>{2,3}
        && set.profiles[0].aileron_figures.size()==15 && set.profiles[0].aileron_figures[9].key=="stall_factor"
        && set.profiles[0].aileron_figures[0].source=="a source","its aileron figures and sections too (decisions 4 and 5)");
    const auto& f=set.profiles[0].figures;
    require(f[0].key=="area_m2" && f[0].unit=="m²" && f[0].tag=="published" && f[0].source=="a source" && f[10].key=="alpha_slope_per_rad" && f[15].key=="k_rev" && f[16].key=="inertia_kgm2","figures in table order, tagged, with sources");
    const auto refused=[](const std::string& t) {try {parse_profiles(t);} catch (const ProfileError&) {return true;}return false;};
    const auto swap=[&](const std::string& from,const std::string& to) {auto t=text;t.replace(t.find(from),from.size(),to);return t;};
    require(refused("{") && refused("[]") && refused(swap("\"schema\":2","\"schema\":1")) && refused(swap("\"schema\":2","\"schema\":3")) && refused(swap("\"schema\":2","\"schema\":2,\"x\":1")),"the file's own shape");
    require(refused(swap("\"default\":\"c172\"","\"default\":\"baron\"")),"the default must exist");
    require(refused(swap("\"tag\":\"published\"","\"tag\":\"measured\"")) && refused(swap("\"source\":\"a source\"","\"source\":\"\"")),"every figure tagged, with a source");
    require(refused(swap("\"value\":1.35","\"value\":25")) && refused(swap("\"value\":1.35","\"value\":\"1.35\"")),"figure bounds and numbers");
    require(refused(swap("[8,9]","[8]")) && refused(swap("[8,9]","[8,10]")),"two tail sections, wings 0-9");
    require(refused(swap("[2,3]","[2,2]")) && refused(swap("[2,3]","[2]")) && refused(swap("[2,3]","[-1,3]")),"two aileron sections, two wings 0-9");
    require(refused(profiles_text(elevator_json(c172_figures),"",aileron_json(c172_ailerons,"stall_factor")))
        && !refused(profiles_text(elevator_json(c172_figures),"",aileron_json(c172_ailerons,"wash_mps"))),"the aileron's flown figures are required, the page's own optional");
    require(refused(swap("\"value\":0.85","\"value\":11")) && refused(swap(",\"aileron_sections\":[2,3],\"aileron\":"+aileron_json(c172_ailerons),"")),
        "aileron bounds; roll has no other feel, so the aileron block is required (decision 6)");
    require(refused(profiles_text(elevator_json(c172_figures,"area_m2"))) && !refused(profiles_text(elevator_json(c172_figures,"inertia_kgm2"))),
        "the plugin's figures are required, the page's own optional");
    require(refused(swap("\"tail_sections\"","\"tail\"")) && refused(swap("{\"value\":1.35,","{\"value\":1.35,\"x\":1,")),"unknown keys are refused");
    {
        // Buffet's pitch source (force/buffet.hpp): optional, two wings 0-9 like the others.
        const auto roots=parse_profiles(swap("\"tail_sections\":[8,9]","\"tail_sections\":[8,9],\"wing_root_sections\":[0,1]"));
        require(roots.profiles[0].wing_root_sections==std::array<int,2>{0,1} && !set.profiles[0].wing_root_sections,"wing roots are read, and optional");
        require(refused(swap("\"tail_sections\":[8,9]","\"tail_sections\":[8,9],\"wing_root_sections\":[0,0]"))
            && refused(swap("\"tail_sections\":[8,9]","\"tail_sections\":[8,9],\"wing_root_sections\":[0]")),"two wing roots, two wings");
    }
    auto light=c172_figures;light.unbalance_inlb=0;
    const auto two=parse_profiles(profiles_text(elevator_json(c172_figures),",\"baron\":{\"aircraft\":[\"Beechcraft Baron 58/Baron_58.acf\"],\"elevator\":"+elevator_json(light)
        +",\"aileron\":"+aileron_json(c172_ailerons)+"}"));
    require(profile_for(two,"C:\\X-Plane 12\\Aircraft\\Laminar Research\\Cessna 172 SP\\Cessna_172SP.acf").matched && profile_for(two,c172_path).matched,
        "the C172 by its file, full or relative, either slash");
    const auto b=profile_for(two,"D:/X-Plane 12/Aircraft/Laminar Research/Beechcraft Baron 58/Baron_58.acf");
    require(b.name=="baron" && b.matched && !b.tail_sections && !b.aileron_sections && b.elevator.unbalance_inlb==0,"an aircraft with its own profile flies it");
    const auto other=profile_for(two,"C:/X-Plane 12/Aircraft/Extra Aircraft/Other/Other.acf");
    require(other.name=="c172" && !other.matched,"anything else flies the default, unmatched");
    require(!profile_for(two,"C:/X/MyAircraft/Laminar Research/Cessna 172 SP/Cessna_172SP.acf").matched,"a partial folder name is not a match");
    std::cout<<"profiles file: ok\n";
}
void compare_and_edit() {
    const auto now=Clock::now();
    Workbench wb;
    wb.frame(now,flying(false));
    require(wb.settings(parse(R"({"roll":{"units_per_nm":1500}})"),now).status==200 && wb.showing_trying() && wb.trying().roll.units_per_nm==1500,"an edit while Kept is shown flies your changes");
    require(wb.undo(now).status==200 && wb.trying()==wb.kept(),"undo it");
    require(wb.settings(parse(R"({"pitch":{"units_per_lb":1000}})"),now).status==200 && wb.settings(parse(R"({"pitch":{"units_per_lb":1001}})"),now).status==422,"pitch's units to 1000");
    require(wb.undo(now).status==200 && wb.trying()==wb.kept(),"undo that too");
    wb.show(parse(R"({"which":"kept"})"),now);
    require(wb.show(parse(R"({"which":"trying"})"),now).status==200 && wb.showing_trying(),"show Trying");
    require(wb.show(parse(R"({"which":"both"})"),now).status==422 && wb.show(parse(R"({"which":1})"),now).status==400,"show validates");
    require(wb.settings(parse(R"({"roll":{"units_per_nm":1500}})"),now).status==200,"edit Trying");
    require(wb.settings(parse(R"({"pitch":{"drag_divisor":3,"move_back":240}})"),now).status==200,"edit two more");
    require(wb.settings(parse(R"({"pitch":{"figures":{"tab_ratio":0.5}}})"),now).status==200,"and a figure: no profile yet, nothing to fly it on");
    auto state=wb.state_json(now);
    require(contains(state,"\"changes\":4") && contains(state,"\"undo\":true") && contains(state,"\"showing\":\"trying\"") && contains(state,"\"figures\":{\"tab_ratio\":0.5}"),
        "four changes, undo available");
    const auto applied=wb.frame(now,flying(false));
    require(applied.settings.roll.units_per_nm==1500 && applied.settings.pitch.drag_divisor==3 && applied.roll_knobs.units_per_nm==1500
        && !applied.has_profile,"Trying is flown; without a profile, neither axis");
    require(wb.undo(now).status==200 && wb.trying().pitch.figures.empty(),"undo reverses the last edit");
    require(wb.undo(now).status==200 && wb.trying().pitch.drag_divisor==6 && wb.trying().pitch.move_back==180,"and the one before");
    require(wb.discard(now).status==200 && wb.trying()==wb.kept(),"discard: Trying is Kept");
    require(wb.undo(now).status==200 && wb.trying().roll.units_per_nm==1500,"undo reverses a discard");
    for (int i=0;i<40;++i) wb.settings(parse("{\"roll\":{\"units_per_nm\":"+std::to_string(1000+10*(i%20))+"}}"),now);
    int undone=0;
    while (contains(wb.state_json(now),"\"undo\":true") && undone<100) {wb.undo(now);++undone;}
    require(undone==32,"undo holds 32 steps");
    require(wb.show(parse(R"({"which":"kept"})"),now).status==200 && wb.frame(now,flying(false)).settings==wb.kept(),"Kept is flown again");
    std::cout<<"compare and edit: ok\n";
}
// Pitch's units take effect at once, force feel on or off. The
// session blends the jump (force/tuning.hpp's FeelBlend): the edit count says when.
void knobs() {
    auto now=Clock::now();
    Workbench wb;auto a=wb.frame(now,flying());
    const auto first=a.knobs.edit;
    require(wb.settings(parse(R"({"pitch":{"units_per_lb":444,"move_back":240}})"),now).status==200,"knob edits go ahead");
    now+=ms(50);a=wb.frame(now,flying());
    require(a.knobs.units_per_lb==444 && a.knobs.edit==first+1,"at once, counted as one edit");
    now+=ms(50);a=wb.frame(now,flying(true,0.5,3000));
    require(a.knobs.edit==first+1,"the flight moving is not an edit");
    const auto n=wb.notices();
    require(n.size()==1 && n.back().cause=="edit" && n.back().text=="Your edit: units 222 → 444 per lb, move-back 180 → 240 (at the next force-feel start)" && !n.back().rest_move_percent,
        "the notice says what changed; neither knob moves the rest");
    wb.settings(parse(R"({"pitch":{"drag_divisor":4}})"),now);
    require(wb.notices().back().text=="Your edit: drag 1/6 → 1/4 (at the next force-feel start)","a drag change says when it reaches the yoke");
    wb.settings(parse(R"({"pitch":{"drag_divisor":4}})"),now);
    require(wb.notices().size()==2,"an edit that changes nothing has no notice");
    std::cout<<"knobs: ok\n";
}
// The owner, withdrawing decision 7: the yoke moves with the loads on the
// aircraft's surfaces, a normal part of flight. Figures, profiles and versions
// change what is flown at once, with nothing to confirm; every change that is
// not the flight's own leaves a notice saying what caused it.
void figures_in_flight() {
    auto now=Clock::now();
    const auto c=c172();
    const double rest30=model_rest_counts(c172_figures,{0,2000,1});
    auto light=c172_figures;light.unbalance_inlb=0;
    require(rest30>190 && rest30<200 && std::abs(model_rest_counts(light,{0,2000,1}))<1e-9,"at 2000 Pa the weight holds the rest 1.6 % forward of trim");
    const auto latest=[](const Workbench& wb) {const auto n=wb.notices();return n.empty()?Notice{}:n.back();};
    {
        Workbench wb;
        auto a=wb.frame(now,flying());
        auto state=wb.state_json(now);
        require(!a.has_profile && contains(state,"\"profile\":{\"name\":null,\"matched\":false,\"error\":null,\"figures\":[],\"aileron_figures\":[],\"aileron_sections\":null}"),
            "no profile: no target for either axis");
        require(contains(state,"\"notice\":null,\"notices\":[]") && !contains(state,"\"pending\""),"no notices yet, and nothing ever waits");
        wb.set_profile(now,c,false);
        a=wb.frame(now,flying());
        require(a.has_profile && a.matched && a.tail_sections==std::array<int,2>{8,9} && same_figures(a.figures,c172_figures) && wb.profile_name()==std::string("c172"),
            "the first profile flies at once");
        require(latest(wb).cause=="profile" && latest(wb).text=="Profile file read: flying c172","and says so");
        state=wb.state_json(now);
        require(contains(state,"\"profile\":{\"name\":\"c172\",\"matched\":true,\"error\":null,\"figures\":[{\"key\":\"area_m2\",\"value\":1.35,"
            "\"flown\":1.35,\"unit\":\"m²\",\"tag\":\"guessed\",\"source\":\"test\"}"),"the profile's figures as the page shows them");
        require(contains(state,"\"figure_limits\":{\"area_m2\":{\"unit\":\"m²\",\"min\":0.05,\"max\":20,\"step\":0.01},") && !contains(state,"\"inertia_kgm2\":{"),
            "bounds and steps for each figure the plugin flies");
        wb.show(parse(R"({"which":"trying"})"),now);
        const auto before=a.knobs.edit;
        require(wb.settings(parse(R"({"pitch":{"figures":{"unbalance_inlb":0}}})"),now).status==200,"an override that moves the rest 1.6 % goes ahead");
        a=wb.frame(now,flying());
        require(a.figures.unbalance_inlb==0 && a.knobs.edit>before,"flown at once, as an edit the session blends in");
        require(contains(wb.state_json(now),"{\"key\":\"unbalance_inlb\",\"value\":30,\"flown\":0,"),"the page sees the profile's figure and the one flown");
        auto n=latest(wb);
        require(n.cause=="edit" && n.text=="Your edit: unbalance 30 → 0 in·lb; the rest moves 1.6% aft" && n.rest_move_percent==1.6,"the notice: cause, change and rest move");
        state=wb.state_json(now+ms(1200));
        require(contains(state,"\"notice\":{\"id\":"+std::to_string(n.id)+",\"cause\":\"edit\",\"text\":\"Your edit: unbalance 30 → 0 in·lb; the rest moves 1.6% aft\","
            "\"rest_move_percent\":1.6,\"age_ms\":1200}") && contains(state,"\"notices\":[{\"id\":"+std::to_string(n.id)+","),"the state's notice and its age; the list newest first");
        require(wb.undo(now).status==200 && latest(wb).cause=="undo" && latest(wb).text=="Undo: unbalance 0 → 30 in·lb; the rest moves 1.6% forward"
            && latest(wb).rest_move_percent==-1.6,"Undo: forward is negative, as the page's percent is aft positive");
        wb.settings(parse(R"({"pitch":{"figures":{"unbalance_inlb":0}}})"),now);
        require(wb.settings(parse(R"({"roll":{"units_per_nm":1500}})"),now).status==200 && latest(wb).text=="Your edit: roll units 1000 → 1500 per N·m","an edit that leaves pitch alone says so too");
        require(wb.discard(now).status==200 && latest(wb).cause=="revert" && contains(latest(wb).text,"Revert: unbalance 0 → 30 in·lb, roll units 1500 → 1000 per N·m; the rest moves"),
            "Revert lists everything it puts back");
        wb.undo(now);
        require(wb.show(parse(R"({"which":"kept"})"),now).status==200 && latest(wb).cause=="show" && contains(latest(wb).text,"Showing your saved settings: unbalance 0 → 30"),
            "Show says which version and what changed");
        const auto count=wb.notices().size();
        for (int i=0;i<40;++i) {now+=ms(50);wb.frame(now,flying(i%2==0,0.02*i,2000+50.0*i));}
        require(wb.notices().size()==count,"the flight moving the yoke has no notice");
        require(wb.show(parse(R"({"which":"kept"})"),now).status==200 && wb.notices().size()==count,"showing what is already shown has none either");
        for (int i=0;i<10;++i) wb.settings(parse("{\"roll\":{\"units_per_nm\":"+std::to_string(1000+10*i)+"}}"),now);
        n=latest(wb);
        require(wb.notices().size()==notice_limit && n.text=="Your edit: roll units 1080 → 1090 per N·m","the last eight are kept");
        require(has_event(wb.take_events(),"Feel notice: Your edit: roll units 1080 → 1090 per N·m"),"each notice is logged");
        auto off=flying();off.connected=false;wb.frame(now,off);
        wb.notify(now,"stop","Force feel off: stopped; the constant force is cleared");
        require(latest(wb).cause=="stop" && !latest(wb).rest_move_percent,"the plugin's own notices");
        // A change from outside the page: an edit the session blends, with
        // its notice. (Decision 16 retired the commissioning firmness that
        // used it; the channel stays for the next.)
        const auto edits=wb.frame(now,flying()).knobs.edit;
        wb.outside_change(now,"dataref","Roll units from X-Plane: 40 → 60%");
        require(wb.frame(now,flying()).knobs.edit==edits+1 && latest(wb).cause=="dataref" && contains(latest(wb).text,"40 → 60%"),"a commissioning write blends and says so");
        require(corner_text("Your edit: unbalance 30 → 0 in·lb, area 1.35 → 1.4 m², up 28 → 30°")=="Your edit: unbalance 30 -> 0 in-lb, area 1.35 -> 1.4 m2, up 28 -> 30 deg"
            && corner_text("é")=="?","X-Plane's corner line is ASCII");
        {
            // Its two lines, fitted by the font's widths. As hud_tests model X-Plane's
            // fonts: 0.55 em a character, here of a 14 px proportional font
            // (7.7 px), wider than its average; the plugin measures each string.
            const double width=496; // the window's 520 px less its margins
            const auto em=[](const std::string& t) {return 7.7*t.size();};
            const int fits=static_cast<int>(width/7.7);
            require(fits==64,"64 characters a line at that width");
            const std::string longest="Profile file reloaded: c172, now this aircraft's own, tail air from sections 8 and 9 → from airspeed, "
                "3 figures changed: area 1.35 → 1.4 m², tab ratio 0.6 → 0.3, unbalance 30 → 24.5 in·lb; the rest moves 21.4% forward";
            auto lines=corner_lines(longest,width,em);
            require(lines.size()==2 && em(lines[0])<=width && em(lines[1])<=width && lines[1].size()>=3 && lines[1].compare(lines[1].size()-3,3,"...")==0
                && lines[0].rfind("Profile file reloaded: c172, now this",0)==0,"a long notice keeps to two lines and says it was cut");
            const std::string short_one="Your edit: unbalance 30 → 0 in·lb; the rest moves 1.6% aft";
            lines=corner_lines(short_one,width,em);
            require(lines.size()==1 && lines[0]=="Your edit: unbalance 30 -> 0 in-lb; the rest moves 1.6% aft","a short one is one line, whole");
            lines=corner_lines("Force feel off: Yoke link lost during motor operation; the constant force is not confirmed cleared",width,em);
            require(lines.size()==2 && lines[0]=="Force feel off: Yoke link lost during motor operation; the" && lines[1]=="constant force is not confirmed cleared","two lines, wrapped at a word");
            const auto wide=[](const std::string& t) {double w=0;for (const char c:t) w+=c=='W' || c=='m'?12:6;return w;};
            lines=corner_lines(std::string(100,'W'),width,wide);
            require(lines.size()==2 && wide(lines[0])<=width && wide(lines[1])<=width && lines[1].compare(lines[1].size()-3,3,"...")==0,"a word wider than the window breaks");
        }
    }
    {
        // With no sample from X-Plane yet, the move is unknown and not mentioned.
        Workbench wb;auto starting=flying();starting.valid=false;
        wb.frame(now,starting);wb.set_profile(now,c,true);wb.show(parse(R"({"which":"trying"})"),now);
        wb.settings(parse(R"({"pitch":{"figures":{"unbalance_inlb":0}}})"),now);
        require(latest(wb).text=="Your edit: unbalance 30 → 0 in·lb" && !latest(wb).rest_move_percent
            && contains(wb.state_json(now),"\"rest_move_percent\":null"),"no sample: no rest move");
        require(wb.frame(now,starting).figures.unbalance_inlb==0,"flown at once all the same");
        require(latest(wb).cause=="edit" && wb.notices().front().cause=="profile" && wb.notices().front().text=="Profile file read: flying c172",
            "the first read, though X-Plane's start is a new flight, says the file was read");
    }
    {
        // A new profiles file (a CI deploy) flies at once, even straight after
        // another change, and says what changed.
        Workbench wb;wb.frame(now,flying(true,1));wb.set_profile(now,c,true);
        auto halved=c172_figures;halved.tab_ratio=0.3;
        const double move=model_rest_counts(halved,{1,2000,1})-model_rest_counts(c172_figures,{1,2000,1});
        require(move>2000,"halving the tab's authority moves the nose-up rest forward");
        wb.show(parse(R"({"which":"trying"})"),now);
        wb.settings(parse(R"({"pitch":{"figures":{"unbalance_inlb":0}}})"),now);
        (void)wb.take_events();
        auto lighter=halved;lighter.unbalance_inlb=0;
        const double moved=model_rest_counts(lighter,{1,2000,1})-model_rest_counts(light,{1,2000,1});
        wb.set_profile(now,profile("c172",halved),false);
        auto a=wb.frame(now,flying(true,1));
        const auto n=latest(wb);
        const double percent=std::round(travel_percent(moved)*10)/10;
        require(a.figures.tab_ratio==0.3 && a.figures.unbalance_inlb==0,"flown at once, with your override still on it");
        require(n.cause=="profile" && n.text=="Profile file reloaded: c172, 1 figure changed: tab ratio 0.6 → 0.3; the rest moves "+number_json(-percent)+"% forward"
            && n.rest_move_percent==percent && percent<-10,"the notice names the file's change and the rest move");
        require(has_event(wb.take_events(),"Feel notice: Profile file reloaded"),"and is logged");
        wb.set_profile(now,profile("c172",halved),false);
        require(latest(wb).id==n.id && wb.take_events().empty(),"the same figures again: nothing to say");
        wb.set_profile(now,c,true);
        require(latest(wb).cause=="aircraft" && latest(wb).text=="Aircraft changed: flying c172" && wb.frame(now,flying(true,1)).figures.tab_ratio==0.6,"a new flight flies its profile");
        wb.profile_refused("not JSON: line 3");
        require(contains(wb.state_json(now),"\"error\":\"not JSON: line 3\"") && has_event(wb.take_events(),"the figures already flown stay"),
            "a refused file is shown until a good one arrives");
        wb.profile_refused("not JSON: line 3");
        require(wb.take_events().empty(),"and logged once");
        wb.profile_refused("");
        require(contains(wb.state_json(now),"\"error\":null"),"a good file clears it");
        wb.set_profile(now,profile("c172",c172_figures,false),true,"Baron 58");
        a=wb.frame(now,flying(true,1));
        require(a.has_profile && !a.matched && !a.tail_sections && contains(wb.state_json(now),"\"name\":\"c172\",\"matched\":false"),
            "another aircraft flies the default, without the C172's tail sections");
        require(latest(wb).text=="Aircraft changed: flying c172 (the default for Baron 58)","and says it is the default, and for what");
        // A reload that changes only where the tail's air comes from, or whose
        // the profile is, says that, not "0 figures changed".
        wb.set_profile(now,c,false);
        require(latest(wb).cause=="profile" && latest(wb).text=="Profile file reloaded: c172, now this aircraft's own, tail air from airspeed → from sections 8 and 9, aileron air from airspeed → from sections 2 and 3",
            "a reload that matches the aircraft now");
        auto resectioned=c;resectioned.tail_sections=std::array<int,2>{10,11};
        const auto edits=wb.frame(now,flying(true,1)).knobs.edit;
        wb.set_profile(now,resectioned,false);
        require(latest(wb).text=="Profile file reloaded: c172, tail air from sections 8 and 9 → from sections 10 and 11"
            && wb.frame(now,flying(true,1)).knobs.edit==edits+1,"only the sections: said, and blended as an edit");
    }
    {
        // compare_feel switches at once, both ways, with a notice each time.
        Workbench wb;wb.frame(now,flying());wb.set_profile(now,c,true);wb.show(parse(R"({"which":"trying"})"),now);
        wb.settings(parse(R"({"pitch":{"figures":{"unbalance_inlb":0}}})"),now);
        wb.compare(now);
        require(!wb.showing_trying() && latest(wb).cause=="show" && contains(latest(wb).text,"unbalance 0 → 30 in·lb; the rest moves 1.6% forward"),"compare shows Kept at once");
        wb.compare(now);
        require(wb.showing_trying() && contains(latest(wb).text,"Showing your changes: unbalance 30 → 0 in·lb"),"and back");
    }
    std::cout<<"figures in flight: ok\n";
}
// Force feel on and off. The off notice says "cleared" only on the read-back,
// and a re-enable starts afresh.
void session_notices() {
    auto now=Clock::now();
    SessionNotices n;
    require(!n.update(now,false,"",{}),"no session, nothing said");
    auto said=n.update(now,true,"Spring active",{});
    require(said && contains(*said,"Force feel on:"),"on");
    require(!n.update(now,true,"",{}),"once");
    require(!n.update(now,false,"Flight loading stopped; motors disabled",{}),"off waits for the session's end to report");
    now+=ms(200);said=n.update(now,false,"Flight loading stopped; motors disabled",true);
    require(said && *said=="Force feel off: Flight loading stopped; motors disabled; the constant force is cleared (read back as zero)","cleared, read back");
    n.update(now,true,"",{});
    said=n.update(now,false,"Yoke link lost during motor operation",false);
    require(said && *said=="Force feel off: Yoke link lost during motor operation; the constant force is not confirmed cleared","not read back: not confirmed");
    n.update(now,true,"",{});n.update(now,false,"",{});
    require(!n.update(now+ms(2999),false,"",{}),"waits up to 3 s");
    said=n.update(now+ms(3000),false,"",{});
    require(said && contains(*said,"motors stopped; the constant force is not confirmed cleared"),"then says it is not confirmed");
    // The review's probe: disabled mid-session, re-enabled with no session.
    // Without a reset the first update said "Force feel off".
    SessionNotices kept;kept.update(now,true,"",{});
    kept.update(now+std::chrono::seconds(5),false,"",{});
    require(kept.update(now+std::chrono::seconds(8),false,"",{}).has_value(),"without a reset, the old session's end is said");
    n.update(now,true,"",{});
    n.reset();
    require(!n.update(now+std::chrono::seconds(5),false,"",{}) && !n.update(now+std::chrono::seconds(8),false,"",{}),"after a reset, no stale off notice");
    std::cout<<"session notices: ok\n";
}
void commands_and_start_up() {
    const auto now=Clock::now();
    Workbench wb;
    for (int i=0;i<8;++i) require(wb.command(parse(R"({"name":"force_feel_on"})"),now).status==200,"queued");
    require(wb.command(parse(R"({"name":"retry"})"),now).status==503,"a full queue answers 503");
    require(wb.command(parse(R"({"name":"stop_motors"})"),now).status==200,"stop is never refused");
    auto taken=wb.take_commands();
    require(taken.size()==1 && taken.front()==Command::stop_motors,"stop supersedes everything asked before it");
    require(wb.take_commands().empty(),"and only once");
    require(wb.command(parse(R"({"name":"retry"})"),now).status==200 && wb.command(parse(R"({"name":"stop_motors"})"),now).status==200
        && wb.command(parse(R"({"name":"force_feel_on"})"),now).status==200,"queued around a stop");
    taken=wb.take_commands();
    require(taken.size()==2 && taken[0]==Command::stop_motors && taken[1]==Command::force_feel_on,"a queued Retry never runs after a later Stop");
    for (int i=0;i<8;++i) wb.command(parse(R"({"name":"retry"})"),now);
    wb.command(parse(R"({"name":"stop_motors"})"),now);
    require(wb.command(parse(R"({"name":"force_feel_on"})"),now).status==200,"after a stop the queue has room again");
    (void)wb.take_commands();
    FlightSettings f;f.roll_torque_units_per_nm=20000;
    const auto adjusted=managed_adjustments(f);
    require(adjusted.size()==1 && contains(adjusted[0],"roll units per N·m 20000 flies as 10000"),"clamped managed values are logged");
    require(managed_adjustments(FlightSettings{}).empty(),"the defaults need no adjustment");
    // Every setting the page allows is one the plugin flies.
    for (const int units:{50,1000}) for (const int drag:{1,6}) for (const int back:{180,720}) for (const int nm:{100,10000}) {
        Settings e;e.pitch={units,drag,back,{}};e.roll={nm};
        FlightSettings g;apply(e,g);
        require(valid(e) && flyable(e) && valid_flight_settings(g),"every setting the page allows is one the plugin flies");
    }
    // Every figure at either bound, at the stops and in any air, solves inside the drive's registers.
    for (const auto& spec:figure_table()) if (spec.field) for (const double v:{spec.low,spec.high}) {
        Settings e;e.pitch.figures[spec.key]=v;
        const auto fig=with_overrides(c172_figures,e.pitch.figures);
        bool inside=valid(e) && flyable(e) && valid_figures(fig);
        for (const double x:{-12000.0,0.0,12000.0}) for (const double q:{0.0,2000.0,20000.0}) for (const double g:{-1.0,4.0})
            inside=inside && valid_tuning(solved_tuning(Tuning{},solve_pitch(fig,{1,q,g},x,1000)));
        require(inside,std::string("a figure at its bound flies: ")+spec.key);
    }
    const auto dir=scratch("startup");
    {std::ofstream big(dir/marks_file,std::ios::binary);big<<std::string(1100*1024,'x');}
    {std::ofstream feel(dir/feel_file,std::ios::binary);feel<<std::string(70*1024,'#');}
    Workbench b;
    const auto log=b.load(dir);
    require(log.size()==2 && contains(log[0],"ignored (over 64 KiB)") && contains(log[1],"marks.jsonl ignored (over 1024 KiB)"),"oversized files are refused, not read");
    require(b.load(dir/"no"/"such").empty(),"a missing folder loads nothing");
    b.show(parse(R"({"which":"trying"})"),now);b.settings(parse(R"({"roll":{"units_per_nm":1700}})"),now);
    b.mark(now);
    require(contains(b.marks_json(),"\"version\":\"trying\",\"settings\":{\"pitch\":{\"units_per_lb\":222,\"drag_divisor\":6,"
        "\"move_back\":180,\"figures\":{}},\"roll\":{\"units_per_nm\":1700}}"),"a mark records the settings flown");
    std::filesystem::remove_all(dir);
    std::cout<<"commands and start-up: ok\n";
}
void marks_history_and_files() {
    const auto dir=scratch("files");
    auto now=Clock::now();
    {
        Workbench wb(Settings{});
        require(wb.load(dir).empty(),"nothing to load");
        wb.set_profile(now,c172(),true);
        Live live;live.ias_kt=62.4;live.bank_deg=3.2;live.flaps_deg=10;live.vertical_fpm=-450;live.pitch.now_percent=31;live.pitch.inputs=ElevatorInputs{0,2380,1};
        for (int i=0;i<30;++i) {wb.publish(now,live);now+=ms(100);}
        const auto history=wb.history_json(now);
        require(std::count(history.begin(),history.end(),'[')==1+15+1,"five samples a second");
        require(contains(history,",31,null,121,null,null,null,null]"),"a sample carries pitch, roll and tail kt; no solution yet");
        const auto p=solve_pitch(c172_figures,{0,2380,1},0,222);
        live.pitch.solution=p;live.pitch.phase="following";live.pitch.update_hz=6.24;live.pitch.yoke_percent=0;
        live.buffet_time_unrestored[1]=300; // a time buffet changed and could not put back, said with what it found
        wb.publish(now,live);
        const auto state=wb.state_json(now);
        require(contains(state,"\"time_unrestored\":[null,300]"),"an unrestored vibration time is shown");
        require(contains(state,"\"inputs\":{\"tail_pa\":2380,\"tail_kt\":121,\"tail_source\":\"airspeed\",\"trim\":0.00,\"g\":1.00,\"tail_alpha_deg\":0.00,\"tail_stalled_xp\":0.00,\"tail_stalled_xp_raw\":null}"),"the model's inputs, no raw share on airspeed's");
        require(contains(state,"\"tail\":{\"alpha_deg\":0.00,\"tail_stalled_xp\":0.00,\"elevator_stalled\":"+decimal_or_null(p.elevator_stalled,2)
            +",\"hinge_stalled\":"+decimal_or_null(p.hinge_stalled,2)+",\"reversed\":false,\"float_lb\":0.00}}"),"the tail at the yoke");
        // The stalled shares the models take are slewed (force/slew.hpp); X-Plane's as read sit beside them.
        live.pitch.tail_stalled_raw=1;live.roll.inputs=AileronInputs{{500,3,0.4},{500,3,0}};live.roll.stalled_raw={1,0};
        wb.publish(now,live);
        const auto raw_state=wb.state_json(now);
        require(contains(raw_state,"\"tail_stalled_xp\":0.00,\"tail_stalled_xp_raw\":1.00}")
            && contains(raw_state,"\"stalled\":0.40,\"stalled_raw\":1.00}") && contains(raw_state,"\"stalled\":0.00,\"stalled_raw\":0.00}"),
            "the slewed stalled shares and X-Plane's as read");
        // No firmest (decision 16): the strength is the model's, up to the
        // drive's own limit this update, which the state carries.
        require(contains(state,"\"registers\":{\"strength\":"+std::to_string(p.strength)+",\"trim_position\":"+std::to_string(p.centre)+",\"bias\":"+std::to_string(p.bias)
            +"},\"phase\":\"following\",\"update_hz\":6.2}") && contains(state,std::string("\"saturated\":")+(p.saturated?"true":"false"))
            && contains(state,"\"ceiling_percent\":"+std::to_string(p.ceiling)) && p.ceiling>100 && p.strength<=p.ceiling,"the drive's registers, its limit, phase and update rate");
        require(contains(wb.history_json(now),",0.0,"+decimal_or_null(travel_percent(p.rest_counts))+","+decimal_or_null(p.force_lb,2)+","+decimal_or_null(p.drive_force_lb,2)+"]"),
            "the strip carries the yoke, the rest and both forces");
        wb.mark(now);
        require(wb.mark_request(now).status==200,"mark from the page");
        auto marks=wb.marks_json();
        require(marks.rfind("[{\"id\":2,",0)==0 && contains(marks,"\"profile\":\"c172\",\"ias_kt\":62,\"bank_deg\":3,\"flaps_deg\":10,\"vertical_fpm\":-450"),
            "newest first, with the profile and flight state");
        require(contains(wb.history_json(now),"\"marks\":[{\"id\":1,"),"marks flagged on the strip");
        require(contains(wb.state_json(now),"\"toast\":{\"text\":\"marked\""),"mark toast");
        require(wb.note(parse(R"({"id":1,"note":"heavy in the flare \u2014 ok"})"),now).status==200,"note");
        require(wb.note(parse(R"({"id":9,"note":"x"})"),now).status==422,"unknown mark");
        require(wb.note(parse("{\"id\":1,\"note\":\""+std::string(201,'x')+"\"}"),now).status==422,"note over 200 characters");
        require(wb.note(parse("{\"id\":1,\"note\":\""+std::string(200,'x')+"\"}"),now).status==200,"note of 200 characters");
        require(wb.note(parse(R"({"id":1,"note":"a\u0007b"})"),now).status==422,"control characters refused");
        require(wb.note(parse(R"({"id":1,"note":"heavy in the flare \u2014 ok"})"),now).status==200,"note again");
        wb.show(parse(R"({"which":"trying"})"),now);
        wb.settings(parse(R"({"roll":{"units_per_nm":1800}})"),now);
        wb.persist();
        require(std::filesystem::exists(dir/draft_file) && !std::filesystem::exists(dir/feel_file),"draft written, nothing kept");
        require(wb.keep(now).status==200 && contains(wb.state_json(now),"\"toast\":{\"text\":\"kept\""),"keep");
        wb.persist();
        require(std::filesystem::exists(dir/feel_file) && !std::filesystem::exists(dir/draft_file),"kept file written, draft gone");
        wb.settings(parse(R"({"pitch":{"figures":{"tab_ratio":0.5}}})"),now);
        wb.persist();
        std::ifstream draft(dir/draft_file);std::stringstream drafted;drafted<<draft.rdbuf();
        require(std::filesystem::exists(dir/marks_file) && contains(drafted.str(),"pitch_figure_tab_ratio=0.5\n"),"marks and draft written, the figure with it");
        require(wb.show(parse(R"({"which":"kept"})"),now).status==200 && wb.keep(now).status==409,"keep only while Trying is shown");
        const auto events=wb.take_events();
        require(std::any_of(events.begin(),events.end(),[](const std::string& e){return e.rfind("Feel mark 1: Kept",0)==0;}),"marks reach the recorder");
        require(wb.take_events().empty(),"events drain");
    }
    {
        Workbench wb(Settings{});
        const auto log=wb.load(dir);
        require(log.size()==2,"kept and draft loaded");
        require(wb.kept().roll.units_per_nm==1800 && wb.trying().pitch.figures.at("tab_ratio")==.5 && !wb.showing_trying(),"flights start flying Kept; Trying is the draft");
        require(contains(wb.marks_json(),"heavy in the flare \xe2\x80\x94 ok"),"marks survive a restart");
        {
            const auto empty=scratch("empty");
            Workbench fresh(Settings{});fresh.load(empty);
            require(fresh.showing_trying() && fresh.trying()==fresh.kept(),"with no draft the page opens on Trying, ready to edit");
            std::filesystem::remove_all(empty);
        }
        wb.mark_request(Clock::now());
        require(wb.marks_json().rfind("[{\"id\":3,",0)==0,"ids continue");
        for (int i=0;i<120;++i) wb.mark(Clock::now());
        require(contains(wb.state_json(Clock::now()),"\"marks\":100,"),"at most 100 marks");
    }
    {
        std::ofstream(dir/feel_file)<<"feel_schema=1\npitch_floor_percent=99\n";
        std::ofstream(dir/marks_file)<<"{\"id\":1}\nnot json\n";
        Workbench wb(Settings{});
        const auto log=wb.load(dir);
        require(log.size()==3 && contains(log[0],"ignored") && contains(log.back(),"2 unreadable"),"invalid files are logged and ignored");
        require(wb.kept()==Settings{},"Kept falls back to the managed values");
    }
    {
        // Files from before the elevator model: both firmests and roll's
        // spring are retired (decisions 6 and 16); the marks list as they were saved.
        std::filesystem::remove(dir/draft_file);
        std::ofstream(dir/feel_file)<<"feel_schema=1\npitch_floor_percent=22\npitch_firmest_percent=38\npitch_halfway_kt=40\nroll_floor_percent=20\n"
            "roll_firmest_percent=30\nroll_halfway_kt=30\ntrim_percent=12\nplain_percent=25\n";
        std::ofstream(dir/marks_file)<<"{\"id\":7,\"utc_ms\":1700000000000,\"version\":\"kept\",\"settings\":{\"pitch\":{\"floor_percent\":20,\"firmest_percent\":40,"
            "\"halfway_kt\":35},\"roll\":{\"floor_percent\":20,\"firmest_percent\":40,\"halfway_kt\":35},\"trim_percent\":5,\"plain_percent\":20},\"ias_kt\":null,"
            "\"bank_deg\":null,\"flaps_deg\":null,\"vertical_fpm\":null,\"note\":\"before\"}\n"
            // Schema 2, before the aileron model: roll's floor and halfway, the plain spring.
            "{\"id\":8,\"utc_ms\":1700000000001,\"version\":\"kept\",\"settings\":{\"pitch\":{\"firmest_percent\":40,\"units_per_lb\":222,\"drag_divisor\":6,"
            "\"move_back\":180,\"figures\":{}},\"roll\":{\"floor_percent\":20,\"firmest_percent\":40,\"halfway_kt\":35},\"plain_percent\":20},\"ias_kt\":null,"
            "\"bank_deg\":null,\"flaps_deg\":null,\"vertical_fpm\":null,\"note\":\"aileron\"}\n";
        Workbench wb(Settings{});
        const auto log=wb.load(dir);
        require(log.size()==2 && contains(log[0],"from before the elevator model; ignored pitch_firmest_percent, pitch_floor_percent, pitch_halfway_kt, plain_percent, roll_firmest_percent, "
            "roll_floor_percent, roll_halfway_kt, trim_percent"),
            "a schema 1 Kept loads, and says what it ignored");
        require(wb.kept()==Settings{},"what still exists is kept: nothing a schema 1 file holds is flown now");
        const auto marks=wb.marks_json();
        require(contains(marks,"\"settings\":{\"pitch\":{\"floor_percent\":20,") && contains(marks,"\"profile\":null,") && contains(marks,"\"note\":\"before\""),"old marks list as saved");
        wb.mark_request(Clock::now());wb.persist();
        Workbench again(Settings{});again.load(dir);
        require(contains(marks,"\"note\":\"aileron\""),"schema 2 marks list as saved too");
        require(contains(again.marks_json(),"\"note\":\"before\"") && again.marks_json().rfind("[{\"id\":9,",0)==0,"old and new mark lines live in one file");
    }
    {
        Workbench wb(Settings{});wb.load(dir/"missing"/"deeper");
        wb.show(parse(R"({"which":"trying"})"),Clock::now());
        require(wb.keep(Clock::now()).status==500 && wb.kept()==Settings{},"a failed Keep changes nothing");
    }
    std::filesystem::remove_all(dir);
    std::cout<<"marks, history and files: ok\n";
}
void views() {
    ViewInputs v;v.connected=true;v.status="Ready - force feel off";
    require(std::string(view_for(v))=="force_feel_off","force feel off");
    v.force_feel=true;v.flight_loading=true;v.status="Ready - force feel on";
    require(std::string(view_for(v))=="ready","ready");
    v.status="Yoke centring - hands off";require(std::string(view_for(v))=="busy","centring is busy");
    v.flight_loading=false;v.status="Ready - force feel starting";require(std::string(view_for(v))=="busy","starting is busy");
    v.retry_offered=true;require(std::string(view_for(v))=="fault","retry is a fault");
    v.retry_offered=false;v.connected=false;require(std::string(view_for(v))=="yoke_off","no yoke");
    v.lab_lease=true;require(std::string(view_for(v))=="busy","developer session");
    require(std::string(motors_for(true,false))=="stopped" && std::string(motors_for(false,true))=="on","motors");
    std::array<float,100> aoa{},area{},stalled{};
    aoa.fill(1);area.fill(1);
    for (int i=0;i<9;++i) {aoa[80+i]=2;aoa[90+i]=4;area[90+i]=3;}
    aoa[89]=100; // the tip element is excluded
    require(std::abs(c172_section_mean(aoa.data(),area.data(),99,8,9)-3.5)<1e-9,"area-weighted angle of attack");
    require(std::isnan(c172_section_mean(aoa.data(),area.data(),50,8,9)),"too short: unknown");
    require(c172_section_stalled(stalled.data(),99,2,3)==0,"not stalled");
    stalled[33]=1;require(c172_section_stalled(stalled.data(),99,2,3)==1,"any element stalled");
    require(c172_section_stalled(nullptr,99,2,3)==-1,"unknown");
    std::cout<<"views and surface data: ok\n";
}
Request request(const std::string& text) {Request r;require(parse_request(text,r)==ParseResult::complete,"request parses");return r;}
void http() {
    Request r;
    require(parse_request("GET / HTTP/1.1\r\nHost: x\r\n",r)==ParseResult::incomplete,"incomplete headers");
    require(parse_request("POST /a HTTP/1.1\r\nContent-Length: 4\r\n\r\n{}",r)==ParseResult::incomplete,"incomplete body");
    require(parse_request("POST /a HTTP/1.1\r\nContent-Length: 4097\r\nHost: localhost\r\n\r\n",r)==ParseResult::too_large && r.header("host"),"over 4 KiB, headers kept");
    for (const char* bad:{"GET / HTTP/2\r\n\r\n","GET  / HTTP/1.1\r\n\r\n","GET x HTTP/1.1\r\n\r\n","GET / HTTP/1.1\r\nBad header\r\n\r\n",
            "POST / HTTP/1.1\r\nTransfer-Encoding: chunked\r\n\r\n","POST / HTTP/1.1\r\nContent-Length: 1\r\nContent-Length: 1\r\n\r\nx",
            "POST / HTTP/1.1\r\nContent-Length: -1\r\n\r\n","GET / HTTP/1.1\r\n\r\nGET / HTTP/1.1\r\n\r\n","GET / HTTP/1.1\r\nX: a\x01\r\n\r\n"})
        require(parse_request(bad,r)==ParseResult::bad,bad);
    require(parse_request("GET / HTTP/1.1\r\n"+std::string(9000,'a')+"\r\n\r\n",r)==ParseResult::bad,"huge headers");
    const auto ok=request("GET /api/state?t=1 HTTP/1.1\r\nHOST:  PC.lan:8087 \r\n\r\n");
    require(ok.path=="/api/state" && *ok.header("host")=="PC.lan:8087","path and header");
    require(contains(build_response({200,"application/json","{}",{}}),"Cache-Control: no-store\r\n") && contains(build_response({200,"text/html","",{}}),"Content-Length: 0\r\n"),"no-store and length");
    require(allowed_peer(0x7f000001) && allowed_peer(0x0a000001) && allowed_peer(0xac100001) && allowed_peer(0xac1fffff) && allowed_peer(0xc0a85601) && allowed_peer(0xa9fe0001),"private peers");
    require(!allowed_peer(0xac200001) && !allowed_peer(0x08080808) && !allowed_peer(0xc0a90001),"public peers refused");
    for (const char* host:{"localhost","localhost:8087","172.31.255.254:8087","127.0.0.1","[::1]:8087","simpc","SIMPC.lan","simpc.local:8087"})
        require(allowed_host(host,"simpc"),host);
    for (const char* host:{"","evil.com","simpcx","simpc.evil.com:80x","1.2.3","1.2.3.256","localhost:","[::1","simpc:99999999"})
        require(!allowed_host(host,"simpc") || std::string(host)=="simpc.evil.com:80x",host);
    require(!allowed_host("simpc.evil.com:80x","simpc"),"bad port");
    require(!allowed_host("anything","") && allowed_host("localhost",""),"no machine name: localhost and literals only");
    Workbench wb;Site site{wb,[]{return std::string("<html>page</html>");},"simpc"};
    const auto now=Clock::now();
    const auto post=[&](const std::string& path,const std::string& body,const std::string& extra="Content-Type: application/json\r\nX-Workbench: 1\r\n") {
        return respond(site,request("POST "+path+" HTTP/1.1\r\nHost: localhost\r\n"+extra+"Content-Length: "+std::to_string(body.size())+"\r\n\r\n"+body),now);
    };
    require(respond(site,request("GET / HTTP/1.1\r\nHost: evil.com\r\n\r\n"),now).status==403,"Host refused");
    const auto page=respond(site,request("GET / HTTP/1.1\r\nHost: localhost\r\n\r\n"),now);
    require(page.status==200 && page.body=="<html>page</html>" && contains(page.content_type,"text/html"),"page");
    require(respond(site,request("GET /nope HTTP/1.1\r\nHost: localhost\r\n\r\n"),now).status==404,"404");
    const auto wrong=respond(site,request("GET /api/keep HTTP/1.1\r\nHost: localhost\r\n\r\n"),now);
    require(wrong.status==405 && wrong.allow=="POST","405 with Allow");
    require(respond(site,request("OPTIONS /api/state HTTP/1.1\r\nHost: localhost\r\n\r\n"),now).status==405,"no preflight");
    require(post("/api/show","{\"which\":\"trying\"}","Content-Type: text/plain\r\nX-Workbench: 1\r\n").status==403,"POST needs JSON");
    require(post("/api/show","{\"which\":\"trying\"}","Content-Type: application/json\r\n").status==403,"POST needs X-Workbench");
    require(post("/api/show","{\"which\":","Content-Type: application/json; charset=utf-8\r\nX-Workbench: 1\r\n").status==400,"malformed JSON");
    const auto back=post("/api/settings","{\"roll\":{\"units_per_nm\":1500}}");
    require(back.status==200 && contains(back.body,"\"showing\":\"trying\""),"an edit returns to your changes");
    post("/api/undo","{}");post("/api/show","{\"which\":\"kept\"}");
    const auto shown=post("/api/show","{\"which\":\"trying\"}");
    require(shown.status==200 && contains(shown.body,"\"showing\":\"trying\""),"POST answers with the state");
    require(post("/api/settings","{\"roll\":{\"units_per_nm\":10001}}").status==422,"422");
    require(contains(post("/api/settings","{\"roll\":{\"units_per_nm\":1500}}").body,"\"changes\":1"),"edit");
    require(post("/api/undo","{\"x\":1}").status==400 && post("/api/undo","{}").status==200,"bare actions take {}");
    require(post("/api/command","{\"name\":\"launch\"}").status==422 && post("/api/command","{\"name\":\"stop_motors\"}").status==200,"commands");
    const auto queued=wb.take_commands();
    require(queued.size()==1 && queued[0]==Command::stop_motors,"command queued for the sim thread");
    require(post("/api/confirm","{\"move\":true}").status==404,"nothing is ever confirmed: no such action");
    // Buffet's knob: off by default and said to be untested with the motors on.
    require(!wb.buffet().on && contains(wb.state_json(now),"\"buffet\":{\"on\":false,\"percent\":10,\"period\":80")
        && contains(wb.state_json(now),"\"time_unrestored\":[null,null],\"tested_motors_on\":false"),"buffet off by default");
    require(post("/api/buffet","{\"on\":true}").status==400 && post("/api/buffet","{\"on\":1,\"percent\":10,\"period\":80}").status==400,"buffet takes all three");
    require(post("/api/buffet","{\"on\":true,\"percent\":101,\"period\":80}").status==422
        && post("/api/buffet","{\"on\":true,\"percent\":10,\"period\":9}").status==422 && !wb.buffet().on,"buffet bounds");
    const auto buffeted=post("/api/buffet","{\"on\":true,\"percent\":5,\"period\":100}");
    require(buffeted.status==200 && contains(buffeted.body,"\"buffet\":{\"on\":true,\"percent\":5,\"period\":100") && wb.buffet().on,"buffet on");
    require(wb.take_commands().empty(),"buffet is a knob, not a command");
    wb.set_buffet({false,10,80});require(!wb.buffet().on,"buffet off again");
    require(respond(site,request("GET /api/marks HTTP/1.1\r\nHost: localhost\r\n\r\n"),now).body=="[]","no marks");
    std::cout<<"http: ok\n";
}
// A real request over loopback, through the sockets.
#ifdef _WIN32
using Sock=SOCKET;
void close_sock(Sock s) {closesocket(s);}
#else
using Sock=int;
void close_sock(Sock s) {::close(s);}
#endif
Sock connect_to(int port) {
    const Sock s=::socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);
    sockaddr_in a{};a.sin_family=AF_INET;a.sin_port=htons(static_cast<std::uint16_t>(port));
    inet_pton(AF_INET,"127.0.0.1",&a.sin_addr);
    ::connect(s,reinterpret_cast<const sockaddr*>(&a),sizeof(a));
    return s;
}
std::string exchange(int port,const std::string& text) {
    const Sock s=::socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);
    sockaddr_in a{};a.sin_family=AF_INET;a.sin_port=htons(static_cast<std::uint16_t>(port));
    inet_pton(AF_INET,"127.0.0.1",&a.sin_addr);
    if (::connect(s,reinterpret_cast<const sockaddr*>(&a),sizeof(a))!=0) {close_sock(s);return {};}
    send(s,text.data(),static_cast<int>(text.size()),0);
    std::string reply;char buf[4096];
    for (;;) {const auto n=recv(s,buf,sizeof(buf),0);if (n<=0) break;reply.append(buf,static_cast<std::size_t>(n));}
    close_sock(s);
    return reply;
}
void loopback() {
#ifdef _WIN32
    WSADATA data{};WSAStartup(MAKEWORD(2,2),&data);
#endif
    const auto dir=scratch("loopback");
    Workbench wb;wb.load(dir);
    Server server;std::string error;
    ServerOptions options;options.port=0;options.bind_address="127.0.0.1";options.page=[]{return embedded_page();};
    require(server.start(wb,options,error),error.c_str());
    require(server.port()>0 && server.addresses()==std::vector<std::string>{"127.0.0.1"},"bound to loopback");
    Server second;std::string busy;
    ServerOptions same=options;same.port=server.port();
    require(!second.start(wb,same,busy) && contains(busy,"in use"),"a taken port is reported");
    const auto state=exchange(server.port(),"GET /api/state HTTP/1.1\r\nHost: 127.0.0.1\r\n\r\n");
    require(state.rfind("HTTP/1.1 200 OK\r\n",0)==0 && contains(state,"\"schema\":3") && contains(state,"\"listen\":{\"addresses\":[\"127.0.0.1\"],\"port\":"+std::to_string(server.port())),"state over loopback");
    const auto page=exchange(server.port(),"GET / HTTP/1.1\r\nHost: localhost\r\n\r\n");
    require(contains(page,"Content-Type: text/html") && contains(page,"<title>Yoke feel</title>"),"embedded page served");
    const std::string body="{\"which\":\"trying\"}";
    const auto shown=exchange(server.port(),"POST /api/show HTTP/1.1\r\nHost: localhost\r\nContent-Type: application/json\r\nX-Workbench: 1\r\nContent-Length: "+std::to_string(body.size())+"\r\n\r\n"+body);
    require(contains(shown,"200 OK") && wb.showing_trying(),"POST over loopback");
    const std::string big(5000,' ');
    require(contains(exchange(server.port(),"POST /api/show HTTP/1.1\r\nHost: localhost\r\nContent-Length: 5000\r\n\r\n"+big),"HTTP/1.1 413 "),"413 over loopback");
    require(contains(exchange(server.port(),"GET /api/state HTTP/1.1\r\nHost: attacker.example\r\n\r\n"),"HTTP/1.1 403 "),"403 over loopback");
    // Spare browser connections, a stalled request and more connections than
    // the server holds: Stop still answers at once.
    std::vector<Sock> idle;
    for (int i=0;i<10;++i) idle.push_back(connect_to(server.port()));
    const Sock stalled=connect_to(server.port());
    const std::string partial="POST /api/command HTTP/1.1\r\nHost: localhost\r\n";
    send(stalled,partial.data(),static_cast<int>(partial.size()),0);
    const std::string stop_body="{\"name\":\"stop_motors\"}";
    const auto asked=Clock::now();
    const auto stopped=exchange(server.port(),"POST /api/command HTTP/1.1\r\nHost: localhost\r\nContent-Type: application/json\r\nX-Workbench: 1\r\nContent-Length: "
        +std::to_string(stop_body.size())+"\r\n\r\n"+stop_body);
    const auto took=Clock::now()-asked;
    require(contains(stopped,"200 OK") && took<std::chrono::milliseconds(200),"stop_motors answers in well under 500 ms beside idle connections");
    std::cout<<"  stop_motors beside 11 idle connections: "<<std::chrono::duration_cast<std::chrono::milliseconds>(took).count()<<" ms\n";
    require(!wb.take_commands().empty(),"stop reached the sim thread's queue");
    for (int i=0;i<3;++i) require(contains(exchange(server.port(),"GET /api/state HTTP/1.1\r\nHost: localhost\r\n\r\n"),"200 OK"),"polls keep answering");
    {
        // A connection that sends nothing is dropped after about a second.
        const Sock quiet=connect_to(server.port());
        std::this_thread::sleep_for(std::chrono::milliseconds(1400));
        char byte=0;
        require(recv(quiet,&byte,1,0)==0,"an idle connection is closed after 1 s");
        close_sock(quiet);
    }
    wb.settings(parse(R"({"roll":{"units_per_nm":1600}})"),Clock::now());
    const auto before=Clock::now();
    server.stop();
    require(Clock::now()-before<std::chrono::milliseconds(500),"stop joins promptly with connections open");
    for (const auto s:idle) close_sock(s);
    close_sock(stalled);
    require(std::filesystem::exists(dir/draft_file),"the server writes the draft before it stops");
    std::filesystem::remove_all(dir);
#ifdef _WIN32
    WSACleanup();
#endif
    std::cout<<"loopback round trip: ok\n";
}
// The plugin's elevator model and solver against the page's
// (elevator_vectors.json, generated from elevator.html by elevator_vectors.mjs).
void elevator_model_vectors() {
    std::ifstream in(BRUNNER_ELEVATOR_VECTORS,std::ios::binary);
    require(in.good(),"elevator_vectors.json is beside the sources");
    if (!in) return;
    std::stringstream text;text<<in.rdbuf();
    const auto doc=parse_json(text.str());
    const auto num=[](const Json& o,const char* key) {
        const auto* v=o.get(key);
        if (!v || (v->kind!=Json::Kind::integer && v->kind!=Json::Kind::real)) throw std::runtime_error(std::string("elevator vector lacks ")+key);
        return v->kind==Json::Kind::integer?double(v->integer):v->real;
    };
    const auto flag=[](const Json& o,const char* key) {
        const auto* v=o.get(key);
        if (!v || v->kind!=Json::Kind::boolean) throw std::runtime_error(std::string("elevator vector lacks ")+key);
        return v->boolean;
    };
    const auto* figures=doc.get("figures");const auto* list=doc.get("vectors");
    require(figures && list && list->kind==Json::Kind::array && list->items.size()>=10,"elevator vectors present");
    if (!figures || !list) return;
    ElevatorFigures page{};
    for (const auto& spec:figure_table()) if (spec.field && spec.field!=&ElevatorFigures::unbalance_inlb && figures->get(spec.key)) page.*spec.field=num(*figures,spec.key);
    page.unbalance_inlb=30;
    require(same_figures(page,c172_figures),"the tests' C172 is the page's");
    int solved=0;
    for (const auto& v:list->items) {
        auto e=page;e.unbalance_inlb=num(v,"unbalance");
        const ElevatorInputs inputs{num(v,"trim"),num(v,"tail_pa"),num(v,"g")};
        const std::string name=v.get("name")?v.get("name")->text:"unnamed";
        const auto m=solve_pitch(e,inputs,std::nan(""),222);
        const double k=num(v,"stiffness_lb_per_in");
        const bool ok=std::abs(m.weight_lb-num(v,"weight_lb"))<1e-4 && std::abs(m.stiffness_lb_per_in-k)<1e-4*std::max(1.0,k)
            && std::abs(m.centre_counts-num(v,"centre_counts"))<=0.5+1e-6 && std::abs(m.rest_counts-num(v,"rest_counts"))<=0.5+1e-6;
        if (!ok) std::cerr<<name<<": plugin "<<m.weight_lb<<" lb, centre "<<m.centre_counts<<", "<<m.stiffness_lb_per_in<<" lb/in, rest "<<m.rest_counts<<'\n';
        require(ok,"elevator vector matches the page's model: "+name);
        const auto* solutions=v.get("solutions");
        if (!solutions) {require(false,"elevator vector has solutions: "+name);continue;}
        for (const auto& s:solutions->items) {
            const auto* at=s.get("x0");
            const double x0=at && at->kind==Json::Kind::null?std::nan(""):num(s,"x0");
            const auto p=solve_pitch(e,inputs,x0,static_cast<int>(num(s,"units_per_lb")));
            const bool same=p.strength==num(s,"strength") && p.centre==num(s,"centre") && p.bias==num(s,"bias") && p.ceiling==num(s,"ceiling")
                && within_margin(p.strength,p.centre,p.bias,pitch_travel_bound())
                && std::abs(p.force_lb-num(s,"force_lb"))<2e-6 && std::abs(p.drive_force_lb-num(s,"drive_force_lb"))<2e-6
                && std::abs(p.slope_lb_per_count-num(s,"slope_lb_per_count"))<2e-9 && p.saturated==flag(s,"saturated") && p.floored==flag(s,"floored") && p.bias_limited==flag(s,"bias_limited");
            if (!same) std::cerr<<name<<" at "<<x0<<": plugin strength "<<p.strength<<", centre "<<p.centre<<", bias "<<p.bias<<", model "<<p.force_lb
                <<" lb, drive "<<p.drive_force_lb<<" lb, slope "<<p.slope_lb_per_count<<", ceiling "<<p.ceiling<<'\n';
            require(same,"the plugin's solver matches the page's: "+name);
            ++solved;
        }
    }
    require(solved>=100,"every case solved at every yoke position");
    std::cout<<"elevator model and solver against the page: "<<solved<<" solutions\n";
}
// The tail's angle, stall and air from behind (decision 17) against the
// page's (elevator_alpha_vectors.json, generated from elevator.html's model by
// elevator_alpha_vectors.mjs): the model's cases, the page's flight chain read
// as the plugin would read its state, and the tail panels as X-Plane reports them.
void elevator_alpha_vectors() {
    std::ifstream in(BRUNNER_ELEVATOR_ALPHA_VECTORS,std::ios::binary);
    require(in.good(),"elevator_alpha_vectors.json is beside the sources");
    if (!in) return;
    std::stringstream text;text<<in.rdbuf();
    const auto doc=parse_json(text.str());
    const auto num=[](const Json& o,const char* key) {
        const auto* v=o.get(key);
        if (!v || (v->kind!=Json::Kind::integer && v->kind!=Json::Kind::real)) throw std::runtime_error(std::string("elevator alpha vector lacks ")+key);
        return v->kind==Json::Kind::integer?double(v->integer):v->real;
    };
    const auto number_of=[](const Json& v) {return v.kind==Json::Kind::integer?double(v.integer):v.kind==Json::Kind::real?v.real:std::nan("");};
    const auto flag=[](const Json& o,const char* key) {
        const auto* v=o.get(key);
        if (!v || v->kind!=Json::Kind::boolean) throw std::runtime_error(std::string("elevator alpha vector lacks ")+key);
        return v->boolean;
    };
    const auto* figures=doc.get("figures");const auto* constants=doc.get("constants");const auto* at=doc.get("force_at_counts");
    const auto* list=doc.get("vectors");const auto* chain=doc.get("chain");const auto* panels=doc.get("panels");
    require(figures && constants && at && list && chain && panels && list->items.size()>=30 && chain->items.size()>=10 && panels->items.size()>=10,
        "elevator alpha vectors present");
    if (!figures || !constants || !at || !list || !chain || !panels) return;
    require(num(*constants,"minimum_panel_pa")==tail_minimum_pa && num(*constants,"alpha_clamp_deg")==tail_alpha_clamp_deg,"the tail's gate and clamp are the page's");
    ElevatorFigures page=c172_figures;
    page.alpha_slope_per_rad=num(*figures,"alpha_slope_per_rad");page.stall_factor=num(*figures,"stall_factor");page.k_rev=num(*constants,"k_rev");
    const auto* surface=constants->get("surface");
    require(surface!=nullptr,"the page's surface stall");
    if (!surface) return;
    const auto close_to=[](double a,double b,double tolerance) {return std::abs(a-b)<=tolerance;};
    int solved_alpha=0;
    // One model case: the figures and inputs given, everything the page reports;
    // forces to 2e-7 of themselves, since the file rounds the tail's pressure to 1e-6 Pa.
    const auto check_case=[&](const std::string& name,const ElevatorFigures& e,const ElevatorInputs& inputs,const Json& v,bool used) {
        const auto s=elevator_state(e,inputs);
        const double rest=elevator_rest_rad(s);
        bool ok=close_to(rest/radians_per_degree,num(v,"rest_deg"),2e-6) && close_to(elevator_counts(s,rest),num(v,"rest_counts"),1e-3)
            && flag(v,"reversed")==tail_flow(s,used?0.0:rest).reversed;
        if (used) {
            const auto t=tail_flow(s,0);
            ok=ok && close_to(t.alpha/radians_per_degree,num(v,"alpha_used_deg"),1e-8) && close_to(t.hinge_stalled,num(v,"stalled_used"),1e-11)
                && close_to(t.eff,num(v,"kept"),1e-11) && close_to(elevator_moments(s,0).float_/newton_metres_per_inlb*s.gearing,num(v,"float_lb"),2e-7*std::max(1.0,std::abs(num(v,"float_lb"))));
        }
        const auto& force=v.get("force_lb")->items;const auto& stalled=v.get("stalled_at")->items;
        for (std::size_t i=0;i<at->items.size();++i) {
            const double d=elevator_rad(s,number_of(at->items[i]));
            ok=ok && close_to(grip_lb(s,d),number_of(force.at(i)),2e-7*std::max(1.0,std::abs(number_of(force.at(i)))))
                && close_to(tail_flow(s,d).hinge_stalled,number_of(stalled.at(i)),1e-9);
        }
        // The solver's rest is the model's, reached from the yoke, and its force
        // the model's; its registers stay inside the drive's margin, keep the
        // constant force within its strength and never go under the 20 % floor.
        for (const double x0:{-12000.0,-3000.0,0.0,4500.0,12000.0}) {
            const auto p=solve_pitch(e,inputs,x0,222);
            ok=ok && close_to(p.rest_counts,elevator_counts(s,elevator_rest_rad(s,elevator_rad(s,x0))),1e-9) && close_to(p.force_lb,grip_lb(s,elevator_rad(s,x0)),1e-12)
                && within_margin(p.strength,p.centre,p.bias,pitch_travel_bound()) && keeps_constant_force(p.strength,p.bias)
                && p.strength>=softest_spring_percent && p.tail_reversed==s.reversed;
        }
        // The page's solve at each yoke position: the rest reached from it, the
        // stiffness there, the drive's registers and line.
        if (const auto* solutions=v.get("solutions")) for (const auto& w:solutions->items) {
            const auto* x=w.get("x0");
            const double x0=x && x->kind==Json::Kind::null?std::nan(""):num(w,"x0");
            const auto p=solve_pitch(e,inputs,x0,222);
            const double k=num(w,"stiffness_lb_per_in");
            const bool same=close_to(p.rest_counts,num(w,"rest_counts"),1e-3) && close_to(p.stiffness_lb_per_in,k,1e-7*std::max(1.0,std::abs(k)))
                && p.strength==num(w,"strength") && p.centre==num(w,"centre") && p.bias==num(w,"bias") && p.ceiling==num(w,"ceiling")
                && close_to(p.force_lb,num(w,"force_lb"),2e-6) && close_to(p.drive_force_lb,num(w,"drive_force_lb"),2e-6)
                && close_to(p.slope_lb_per_count,num(w,"slope_lb_per_count"),2e-9)
                && p.saturated==flag(w,"saturated") && p.floored==flag(w,"floored") && p.bias_limited==flag(w,"bias_limited");
            if (!same) std::cerr<<name<<" at "<<x0<<": plugin rest "<<p.rest_counts<<", "<<p.stiffness_lb_per_in<<" lb/in, strength "<<p.strength
                <<", centre "<<p.centre<<", bias "<<p.bias<<", ceiling "<<p.ceiling<<", model "<<p.force_lb<<" lb, drive "<<p.drive_force_lb<<" lb\n";
            ok=ok && same;
            ++solved_alpha;
        }
        if (!ok) {
            std::cerr<<name<<": plugin rest "<<rest/radians_per_degree<<" deg, "<<elevator_counts(s,rest)<<" counts; at the counts";
            for (std::size_t i=0;i<at->items.size();++i) {const double d=elevator_rad(s,number_of(at->items[i]));
                std::cerr<<' '<<grip_lb(s,d)-number_of(force.at(i))<<'/'<<tail_flow(s,d).hinge_stalled-number_of(stalled.at(i));}
            std::cerr<<'\n';
        }
        require(ok,"elevator alpha vector matches the page's model: "+name);
    };
    int cases=0;
    for (const auto& v:list->items) {
        const std::string name=v.get("name")?v.get("name")->text:"unnamed";
        auto e=page;e.unbalance_inlb=num(v,"unbalance");e.hinge_slope_per_rad=num(v,"hinge_slope_per_rad");
        e.alpha_slope_per_rad=num(v,"alpha_slope_per_rad");e.stall_factor=num(v,"stall_factor");e.k_rev=num(v,"k_rev");
        const auto* f=v.get("surface");
        if (f && f->kind==Json::Kind::object) {e.tau=num(*f,"tau");e.stall_deg=num(*f,"stall");e.stall_width_deg=num(*f,"width");}
        else {e.tau=0;e.stall_deg=0;e.stall_width_deg=0;}
        require(valid_figures(e),"the case's figures are valid: "+name);
        check_case(name,e,{num(v,"trim"),num(v,"tail_pa"),num(v,"g"),num(v,"alpha_deg"),num(v,"stalled")},v,true);
        ++cases;
    }
    require(solved_alpha>=6*42,"every alpha case solved at the page's yoke positions");
    // The chain: the plugin reads X-Plane's angle and load factor where the page derives them.
    auto chained=page;chained.unbalance_inlb=30;
    chained.tau=num(*surface,"tau");chained.stall_deg=num(*surface,"stall");chained.stall_width_deg=num(*surface,"width");
    for (const auto& v:chain->items) {
        const std::string name=v.get("name")?v.get("name")->text:"unnamed";
        check_case("chain: "+name,chained,{num(v,"trim"),num(v,"tail_pa"),num(v,"load_factor"),num(v,"reported_deg"),0},v,false);
        ++cases;
    }
    // The sweeps across the air's direction: the page's values, then the
    // plugin's own steps held to the page's bounds, and every solution the
    // drive is sent within its registers' limits, through a stiffness that
    // passes 0 at ±90° and turns negative from behind.
    const auto* sweep=doc.get("sweep");const auto* sweeps=doc.get("sweeps");
    require(sweep && sweeps && sweeps->items.size()==12,"the elevator's sweeps are present");
    int swept=0;bool through_zero=false,negative=false;
    if (sweep && sweeps) {
        const double step=num(*sweep,"step_deg"),most_rest=num(*sweep,"rest_counts"),per_pa=num(*sweep,"per_pa");
        const auto* at_counts=sweep->get("at_counts");
        auto e=page;e.unbalance_inlb=30;
        e.tau=num(*surface,"tau");e.stall_deg=num(*surface,"stall");e.stall_width_deg=num(*surface,"width");
        for (const auto& w:sweeps->items) {
            const double pa=num(w,"tail_pa"),from=num(w,"from_deg"),way=num(w,"to_deg")>from?1.0:-1.0;
            const auto& rests=w.get("rest_counts")->items;const auto& stiff=w.get("stiffness_lb_per_in")->items;const auto& forces=w.get("force_lb")->items;
            const std::string where=std::to_string(static_cast<int>(pa))+" Pa from "+std::to_string(static_cast<int>(from))+" deg";
            double last_rest=0,last_k=0;std::array<double,2> last_f{};
            bool ok=rests.size()==81 && stiff.size()==81 && forces.size()==81 && at_counts && at_counts->items.size()==2;
            for (std::size_t i=0;ok && i<rests.size();++i) {
                const ElevatorInputs inputs{0,pa,1,from+way*step*static_cast<double>(i),0};
                const auto s=elevator_state(e,inputs);
                const double rest=elevator_counts(s,elevator_rest_rad(s));
                const auto r=solve_pitch(e,inputs,0,222);
                ok=close_to(rest,number_of(rests[i]),1e-4) && close_to(r.stiffness_lb_per_in,number_of(stiff[i]),1e-8*std::max(1.0,std::abs(r.stiffness_lb_per_in)));
                std::array<double,2> f{};
                for (std::size_t k=0;k<2;++k) {
                    f[k]=grip_lb(s,elevator_rad(s,number_of(at_counts->items[k])));
                    ok=ok && close_to(f[k],number_of(forces[i].items[k]),1e-8*std::max(1.0,std::abs(f[k])));
                }
                if (i>0) ok=ok && std::abs(rest-last_rest)<=most_rest && std::abs(r.stiffness_lb_per_in-last_k)<=pa*per_pa
                    && std::abs(f[0]-last_f[0])<=pa*per_pa && std::abs(f[1]-last_f[1])<=pa*per_pa;
                // What the drive is sent, at the yoke's neutral and both stops.
                for (const double x0:{0.0,-full_travel_counts(),full_travel_counts()}) {
                    const auto d=solve_pitch(e,inputs,x0,222);
                    ok=ok && d.strength>=softest_spring_percent && d.strength<=d.ceiling && d.ceiling<=strongest_spring_percent()
                        && std::abs(d.centre)<=full_travel_counts() && std::abs(d.bias)<=max_pitch_bias() && std::isfinite(d.drive_force_lb)
                        && valid_tuning(solved_tuning(Tuning{},d));
                }
                through_zero=through_zero || std::abs(r.stiffness_lb_per_in)<1e-9;negative=negative || r.stiffness_lb_per_in<0;
                if (!ok) std::cerr<<"elevator sweep "<<where<<", step "<<i<<": rest "<<rest<<", stiffness "<<r.stiffness_lb_per_in<<'\n';
                last_rest=rest;last_k=r.stiffness_lb_per_in;last_f=f;
            }
            require(ok,"the elevator turns over smoothly with the air's direction, as the page's: "+where);
            ++swept;
        }
    }
    require(swept==12 && through_zero && negative,"the sweeps pass the stiffness through 0 and below it, and the drive stays within its registers");
    // Across the gate: air from behind as the pressure rises from 2 to 6 Pa;
    // the turn's departure from 1 fades in from 2.5 to 5 Pa, so no step moves
    // the force at the sweep's counts more than the page's bound.
    const auto* gate=doc.get("gate");
    require(gate && sweep,"the elevator's gate sweep is present");
    if (gate && sweep) {
        const auto* at_counts=sweep->get("at_counts");
        auto e=page;e.unbalance_inlb=30;
        e.tau=num(*surface,"tau");e.stall_deg=num(*surface,"stall");e.stall_width_deg=num(*surface,"width");
        const double from_pa=num(*gate,"from_pa"),step_pa=num(*gate,"step_pa"),most=num(*gate,"most_force_lb"),alpha=num(*gate,"alpha_deg");
        const auto& rests=gate->get("rest_counts")->items;const auto& forces=gate->get("force_lb")->items;
        bool ok=rests.size()==81 && forces.size()==81 && at_counts && at_counts->items.size()==2;
        std::array<double,2> last{};
        for (std::size_t i=0;ok && i<rests.size();++i) {
            const ElevatorInputs inputs{0,from_pa+step_pa*static_cast<double>(i),1,alpha,0};
            const auto s=elevator_state(e,inputs);
            const double rest=elevator_counts(s,elevator_rest_rad(s));
            ok=close_to(rest,number_of(rests[i]),1e-4);
            std::array<double,2> f{};
            for (std::size_t k=0;k<2;++k) {
                f[k]=grip_lb(s,elevator_rad(s,number_of(at_counts->items[k])));
                ok=ok && close_to(f[k],number_of(forces[i].items[k]),1e-8*std::max(1.0,std::abs(f[k])))
                    && (i==0 || std::abs(f[k]-last[k])<=most);
            }
            const auto d=solve_pitch(e,inputs,0,222);
            ok=ok && d.strength>=softest_spring_percent && d.strength<=d.ceiling && valid_tuning(solved_tuning(Tuning{},d));
            if (!ok) std::cerr<<"elevator gate, step "<<i<<": rest "<<rest<<", force "<<f[0]<<", "<<f[1]<<'\n';
            last=f;
        }
        require(ok,"air from behind fades in across the tail's gate, as the page's");
    }
    // The panels, wings 8 and 9 as X-Plane's arrays hold them.
    int panel_reads=0;
    for (const auto& v:panels->items) {
        const std::string name=v.get("name")?v.get("name")->text:"unnamed";
        std::array<float,99> speed{},area{},aoa{},stall{};
        const auto& items=v.get("panels")->items;
        require(items.size()==18,"eighteen tail panels: "+name);
        for (std::size_t i=0;i<18 && i<items.size();++i) {
            const std::size_t slot=(i<9?80:90)+i%9;
            const auto& p=items[i].items;
            speed[slot]=static_cast<float>(number_of(p.at(0)));area[slot]=static_cast<float>(number_of(p.at(1)));
            aoa[slot]=static_cast<float>(number_of(p.at(2)));stall[slot]=static_cast<float>(number_of(p.at(3)));
        }
        const auto r=tail_side(number_of(*v.get("rho")),speed.data(),area.data(),aoa.data(),stall.data(),99,8,9);
        const bool empty=v.get("tail_pa")->kind==Json::Kind::null;
        bool ok=r.has_value()!=empty;
        if (r && !empty) ok=close_to(r->pa,num(v,"tail_pa"),1e-8) && close_to(r->alpha_deg,num(v,"alpha_deg"),1e-8) && close_to(r->stalled,num(v,"stalled"),1e-11);
        if (!ok) std::cerr<<name<<": plugin "<<(r?std::to_string(r->pa)+" Pa, "+std::to_string(r->alpha_deg)+" deg":std::string("empty"))<<'\n';
        require(ok,"the tail's panels panel_reads as the page's: "+name);
        ++panel_reads;
    }
    std::cout<<"elevator angle, stall and air from behind against the page: "<<cases<<" cases, "<<solved_alpha<<" solutions, "<<panel_reads<<" panel reads\n";
}
}
// The aileron model and solver (force/aileron.hpp) against aileron.html's, through
// aileron_vectors.json: torques to 1e-9 N·m as written, registers exactly.
void aileron_model_vectors() {
    std::ifstream in(BRUNNER_AILERON_VECTORS,std::ios::binary);
    require(in.good(),"aileron_vectors.json is beside the sources");
    if (!in) return;
    std::stringstream text;text<<in.rdbuf();
    const auto doc=parse_json(text.str());
    const auto num=[](const Json& o,const char* key) {
        const auto* v=o.get(key);
        if (!v || (v->kind!=Json::Kind::integer && v->kind!=Json::Kind::real)) throw std::runtime_error(std::string("aileron vector lacks ")+key);
        return v->kind==Json::Kind::integer?double(v->integer):v->real;
    };
    const auto flag=[](const Json& o,const char* key) {
        const auto* v=o.get(key);
        if (!v || v->kind!=Json::Kind::boolean) throw std::runtime_error(std::string("aileron vector lacks ")+key);
        return v->boolean;
    };
    const auto* figures=doc.get("figures");const auto* list=doc.get("vectors");
    require(figures && list && list->kind==Json::Kind::array && list->items.size()>=15,"aileron vectors present");
    if (!figures || !list) return;
    AileronFigures page{};
    for (const auto& spec:aileron_figure_table()) if (spec.field && spec.field!=&AileronFigures::unbalance_inlb) page.*spec.field=num(*figures,spec.key);
    require(same_figures(page,c172_ailerons),"the tests' C172 ailerons are the page's");
    require(num(*figures,"minimum_panel_pa")==aileron_minimum_panel_pa && num(*figures,"alpha_clamp_deg")==aileron_alpha_clamp_deg,"the gate and the clamp are the page's");
    const auto side=[&](const Json& v,const char* key) {
        const auto* o=v.get(key);
        if (!o) throw std::runtime_error(std::string("aileron vector lacks ")+key);
        return AileronSide{num(*o,"pa"),num(*o,"alpha_deg"),num(*o,"stalled")};
    };
    const auto close_to=[](double a,double b,double tolerance) {return std::abs(a-b)<=tolerance;};
    int solved=0;
    bool airless=false,weighted=false,saturated=false,floored_exact=false,floored_rest=false,limited=false,stops=false,anchored=false;
    for (const auto& v:list->items) {
        auto f=page;f.unbalance_inlb=num(v,"unbalance_inlb");
        if (v.get("hinge_slope_per_rad")) f.hinge_slope_per_rad=num(v,"hinge_slope_per_rad"); // a case's own, else the page's
        const AileronInputs inputs{side(v,"left"),side(v,"right")};
        const std::string name=v.get("name")?v.get("name")->text:"unnamed";
        const double rest=wheel_rest_rad(f,inputs);
        bool ok=aileron_indifferent(f,inputs)==flag(v,"indifferent") && close_to(rest/radians_per_degree,num(v,"rest_deg"),2e-6)
            && std::floor(wheel_exact_counts(f,rest)+0.5)==num(v,"rest_counts"); // Math.round
        // A rest at a stop keeps the model's torque there: the anchor the drive's centre is offset by.
        const double anchor=std::abs(wheel_exact_counts(f,rest))>=roll_travel_counts()-0.5?wheel_nm(f,inputs,rest):0;
        ok=ok && close_to(anchor,num(v,"anchor_nm"),2e-9);
        anchored=anchored || anchor!=0;
        const auto half=aileron_angles(f,f.wheel_deg*radians_per_degree/2);
        ok=ok && close_to(half.left/radians_per_degree,num(v,"half_wheel_left_deg"),2e-6) && close_to(half.right/radians_per_degree,num(v,"half_wheel_right_deg"),2e-6);
        for (const auto& t:v.get("torques")->items) {
            const auto w=wheel_torque(f,inputs,num(t,"wheel_deg")*radians_per_degree);
            ok=ok && close_to(w.left,num(t,"left_nm"),2e-9) && close_to(w.right,num(t,"right_nm"),2e-9) && close_to(w.weight,num(t,"weight_nm"),2e-9)
                && close_to(w.total,num(t,"total_nm"),2e-9) && close_to(rim_lb(f,w.total),num(t,"rim_lb"),2e-9);
        }
        if (!ok) std::cerr<<name<<": plugin rest "<<rest/radians_per_degree<<" deg\n";
        require(ok,"aileron vector matches the page's model: "+name);
        // Mirror: the wings swapped and the wheel's sign flipped negate the torque, the rest and the registers.
        const AileronInputs mirrored{inputs.right,inputs.left};
        require(close_to(wheel_rest_rad(f,mirrored),-rest,1e-9),"the mirrored rest is the rest's negative: "+name);
        for (const double t:{-1.2,-0.4,0.0,0.3,1.1}) require(close_to(wheel_nm(f,mirrored,-t),-wheel_nm(f,inputs,t),1e-9),"the mirrored torque: "+name);
        const auto* solutions=v.get("solutions");
        if (!solutions) {require(false,"aileron vector has solutions: "+name);continue;}
        for (const auto& s:solutions->items) {
            const auto* at=s.get("x0");
            const double x0=at && at->kind==Json::Kind::null?std::nan(""):num(s,"x0");
            const auto r=solve_roll(f,inputs,x0,num(s,"units_per_nm"),flag(s,"force_exact_rule"));
            const bool same=r.strength==num(s,"strength") && r.centre==num(s,"centre") && r.ceiling==num(s,"ceiling")
                && within_margin(r.strength,r.centre,0,roll_travel_bound())
                && close_to(r.model_nm,num(s,"model_nm"),2e-9) && close_to(r.drive_nm,num(s,"drive_nm"),2e-9)
                && close_to(r.slope_nm_per_count,num(s,"slope_nm_per_count"),2e-12) && r.saturated==flag(s,"saturated") && r.floored==flag(s,"floored")
                && r.force_exact==flag(s,"force_exact") && r.centre_limited==flag(s,"centre_limited");
            if (!same) std::cerr<<name<<" at "<<x0<<": plugin strength "<<r.strength<<", centre "<<r.centre<<", model "<<r.model_nm
                <<" N·m, drive "<<r.drive_nm<<" N·m, slope "<<r.slope_nm_per_count<<", ceiling "<<r.ceiling<<'\n';
            require(same,"the plugin's roll solver matches the page's: "+name);
            if (std::isfinite(x0)) {
                const auto m=solve_roll(f,mirrored,-x0,num(s,"units_per_nm"),flag(s,"force_exact_rule"));
                require(m.strength==r.strength && m.centre==-r.centre && close_to(m.model_nm,-r.model_nm,1e-9) && close_to(m.drive_nm,-r.drive_nm,1e-9),
                    "the mirrored registers are the registers' negative: "+name);
                stops=stops || std::abs(x0)==roll_travel_counts();
            }
            if (aileron_airless(inputs) && f.unbalance_inlb==0 && std::isfinite(x0) && flag(s,"force_exact_rule")) {
                // No air: zero torque, the centre on the wheel, no force at all.
                require(r.floored && r.force_exact && r.strength==softest_spring_percent && r.centre==std::lround(x0) && r.model_nm==0 && r.drive_nm==0,
                    "with no air the wheel stays where it is left: "+name);
                airless=true;
            }
            weighted=weighted || (aileron_airless(inputs) && f.unbalance_inlb!=0);
            saturated=saturated || r.saturated;floored_exact=floored_exact || r.force_exact;
            floored_rest=floored_rest || (r.floored && !r.force_exact);limited=limited || r.centre_limited;
            ++solved;
        }
    }
    require(airless && weighted && saturated && floored_exact && floored_rest && stops,"the vectors reach no air, weights, saturation, both floor rules and the stops");
    require(limited && anchored,"the vectors reach a clamped centre and a rest at a stop with its anchor torque");
    require(solved>=150,"every case solved at every wheel position");
    // The sweeps across the left aileron's air direction: the page's values,
    // the plugin's own steps held to the page's bounds, and the roll drive's
    // registers within their limits all the way through.
    const auto* sweep=doc.get("sweep");const auto* sweeps=doc.get("sweeps");
    require(sweep && sweeps && sweeps->items.size()==10,"the ailerons' sweeps are present");
    if (sweep && sweeps) {
        const double step=num(*sweep,"step_deg"),most_rest=num(*sweep,"rest_counts"),per_pa=num(*sweep,"per_pa");
        const auto* at_wheel=sweep->get("at_wheel_deg");
        const auto num_at=[](const Json& v) {return v.kind==Json::Kind::integer?double(v.integer):v.kind==Json::Kind::real?v.real:std::nan("");};
        for (const auto& w:sweeps->items) {
            auto f=page;f.unbalance_inlb=num(w,"unbalance_inlb");
            const double pa=num(w,"pa"),from=num(w,"from_deg"),way=num(w,"to_deg")>from?1.0:-1.0;
            const auto* right=w.get("right_alpha_deg");
            // With both sides alike the rest is a tie between the stops: none held.
            const auto* rest_list=w.get("rest_counts");const bool held_rest=rest_list && rest_list->kind==Json::Kind::array;
            const auto& torques=w.get("total_nm")->items;const auto& drives=w.get("drive_nm")->items;
            const auto* at_x=sweep->get("at_counts");
            const std::string where=std::to_string(static_cast<int>(pa))+" Pa from "+std::to_string(static_cast<int>(from))+" deg";
            bool ok=torques.size()==81 && drives.size()==81 && (!held_rest || rest_list->items.size()==81)
                && at_wheel && at_wheel->items.size()==2 && at_x && at_x->items.size()==2;
            double last_rest=0;std::array<double,2> last_t{},last_d{};
            for (std::size_t i=0;ok && i<torques.size();++i) {
                const double left=from+way*step*static_cast<double>(i);
                const AileronInputs inputs{{pa,left,0},{pa,right && right->kind!=Json::Kind::null?num(w,"right_alpha_deg"):left,0}};
                const double rest=wheel_exact_counts(f,wheel_rest_rad(f,inputs));
                ok=!held_rest || close_to(rest,num_at(rest_list->items[i]),1e-4);
                std::array<double,2> t{},dn{};
                for (std::size_t k=0;k<2;++k) {
                    t[k]=wheel_nm(f,inputs,num_at(at_wheel->items[k])*radians_per_degree);
                    dn[k]=solve_roll(f,inputs,num_at(at_x->items[k]),1000,true).drive_nm;
                    ok=ok && close_to(t[k],num_at(torques[i].items[k]),2e-9) && close_to(dn[k],num_at(drives[i].items[k]),2e-9);
                }
                if (i>0) ok=ok && (!held_rest || std::abs(rest-last_rest)<=most_rest)
                    && std::abs(t[0]-last_t[0])<=pa*per_pa && std::abs(t[1]-last_t[1])<=pa*per_pa
                    && std::abs(dn[0]-last_d[0])<=pa*per_pa && std::abs(dn[1]-last_d[1])<=pa*per_pa;
                for (const double x0:{0.0,-roll_travel_counts(),roll_travel_counts()}) for (const bool exact_rule:{true,false}) {
                    const auto r=solve_roll(f,inputs,x0,1000,exact_rule);
                    ok=ok && r.strength>=softest_spring_percent && r.strength<=r.ceiling && r.ceiling<=strongest_spring_percent()
                        && std::abs(r.centre)<=roll_travel_counts() && std::isfinite(r.drive_nm) && valid_tuning(solved_tuning(Tuning{},r));
                }
                if (!ok) std::cerr<<"aileron sweep "<<where<<", step "<<i<<": rest "<<rest<<'\n';
                last_rest=rest;last_t=t;last_d=dn;
            }
            require(ok,"the ailerons turn over smoothly with the air's direction, as the page's: "+where);
        }
    }
    // Across the gate: air from behind both sides as the pressure rises from
    // 2 to 6 Pa; the turn's departure from 1 fades in from 2.5 to 5 Pa, so no
    // step moves the torque or the drive more than the page's bound, except
    // the step onto the gate, where the ailerons' whole moment starts.
    const auto* gate=doc.get("gate");
    require(gate && sweep,"the ailerons' gate sweep is present");
    if (gate && sweep) {
        const auto* at_wheel=sweep->get("at_wheel_deg");const auto* at_x=sweep->get("at_counts");
        const auto gate_num=[](const Json& v) {return v.kind==Json::Kind::integer?double(v.integer):v.kind==Json::Kind::real?v.real:std::nan("");};
        auto f=page;f.unbalance_inlb=num(*gate,"unbalance_inlb");
        const double from_pa=num(*gate,"from_pa"),step_pa=num(*gate,"step_pa"),most=num(*gate,"most_nm"),alpha=num(*gate,"alpha_deg");
        const auto& torques=gate->get("total_nm")->items;const auto& drives=gate->get("drive_nm")->items;
        bool ok=torques.size()==81 && drives.size()==81 && at_wheel && at_wheel->items.size()==2 && at_x && at_x->items.size()==2;
        std::array<double,2> last_t{},last_d{};
        for (std::size_t i=0;ok && i<torques.size();++i) {
            const double pa=from_pa+step_pa*static_cast<double>(i);
            const bool onto_gate=i>0 && pa-step_pa<aileron_minimum_panel_pa-1e-9 && pa>=aileron_minimum_panel_pa-1e-9;
            const AileronInputs inputs{{pa,alpha,0},{pa,alpha,0}};
            std::array<double,2> t{},dn{};
            for (std::size_t k=0;k<2;++k) {
                t[k]=wheel_nm(f,inputs,gate_num(at_wheel->items[k])*radians_per_degree);
                dn[k]=solve_roll(f,inputs,gate_num(at_x->items[k]),1000,true).drive_nm;
                ok=ok && close_to(t[k],gate_num(torques[i].items[k]),2e-9) && close_to(dn[k],gate_num(drives[i].items[k]),2e-9)
                    && (i==0 || onto_gate || (std::abs(t[k]-last_t[k])<=most && std::abs(dn[k]-last_d[k])<=most));
            }
            if (!ok) std::cerr<<"aileron gate, step "<<i<<": torque "<<t[0]<<", "<<t[1]<<'\n';
            last_t=t;last_d=dn;
        }
        require(ok,"air from behind fades in across the ailerons' gate, as the page's");
    }
    // The figures the plan's table rests on (docs/brunner/aileron-and-aoa-plan.md):
    // 12 kt leaves the floor, 26 kt about 100 %, 55 and 110 kt saturate.
    const auto want=[&](double pa) {return solve_roll(page,{{pa,1,0},{pa,1,0}},3000,1000,true);};
    require(want(23.3).want>=19.5 && want(23.3).want<40,"12 kt leaves the floor");
    require(want(109.6).want>80 && want(109.6).want<130,"26 kt is about the drive's 100 %");
    require(solve_roll(page,{{490.5,2,0},{490.5,2,0}},3000,1000,true).saturated && solve_roll(page,{{1962,2,0},{1962,2,0}},3000,1000,true).saturated,"55 and 110 kt saturate");
    // Equal angles both sides: the rest is level; 5 degrees more on the left moves it.
    require(std::abs(wheel_rest_rad(page,{{1312,5,0},{1312,5,0}}))<1e-9,"equal angles, level rest");
    require(std::abs(wheel_rest_rad(page,{{1312,8,0},{1312,3,0}}))>radians_per_degree,"a 5 degree difference moves the rest");
    // A stalled wing keeps stall_factor of its aileron's torque.
    const AileronInputs clean{{700,0,0},{700,0,0}},stalled{{700,0,0},{700,0,1}};
    const double t=0.5;
    require(close_to(wheel_torque(page,stalled,t).right,page.stall_factor*wheel_torque(page,clean,t).right,1e-12) && close_to(wheel_torque(page,stalled,t).left,wheel_torque(page,clean,t).left,1e-12),
        "a stalled aileron keeps stall_factor of its torque, the other all of it");
    // Missing values count as none: no NaN reaches the drive.
    const double nan=std::nan("");
    const auto lost=solve_roll(page,{{nan,nan,nan},{100,nan,nan}},2000,1000,true);
    require(std::isfinite(lost.model_nm) && std::isfinite(lost.drive_nm) && lost.strength>=softest_spring_percent,"missing panel values cannot reach the drive");
    std::cout<<"aileron model and solver against the page: "<<solved<<" solutions\n";
}
// The demo's followed state (demo_state.hpp): what a capture script writes
// so the page steps through a film's frames.
void demo_state() {
    const auto s=parse_demo_state(R"({"seq":12,"scenario":"baron","ias":110,"trim":0.4,"tail_pa":1987.96,"load_factor":1.02,
        "tail_alpha":-2,"tail_stalled":0.25,"yoke":11.75,"wheel":-17.5,"left":{"pa":1962,"alpha_deg":2,"stalled":0}})");
    require(s.seq==12 && s.scenario=="baron" && s.ias==110 && s.trim==0.4 && s.tail_pa==1987.96 && s.load_factor==1.02,"The demo state reads the flight");
    require(s.tail_alpha==-2 && s.tail_stalled==0.25 && s.yoke==11.75 && s.wheel==-17.5,"…the tail's angle and stall, the yoke and the wheel");
    const auto both=demo_roll_inputs(s,100);
    require(both.left.pa==1962 && both.right.pa==1962 && both.right.alpha_deg==2,"One wing given serves both");
    const auto none=parse_demo_state(R"({"seq":1,"trim":3,"yoke":-250})");
    require(none.scenario.empty() && std::isnan(none.tail_pa) && none.load_factor==1 && none.trim==1 && none.yoke==-100,
        "Missing keys keep the demo's scenario and the airspeed's tail; trim and the yoke are held to their range");
    const auto still=demo_roll_inputs(none,600);
    require(still.left.pa==600 && still.right.pa==600 && still.left.alpha_deg==0,"No wings: the airspeed's air, level");
    const auto bad=[](const char* t) {try {parse_demo_state(t);} catch (const JsonError&) {return true;}return false;};
    require(bad("[1]") && bad(R"({"ias":"fast"})") && bad(R"({"left":3})") && bad("{"),"A bad state is refused, so the demo keeps the last");
}
int main() {
    try {elevator_model_vectors();elevator_alpha_vectors();aileron_model_vectors();demo_state();units();configuration();feel_files();json();profiles_parse();compare_and_edit();knobs();figures_in_flight();session_notices();
        commands_and_start_up();marks_history_and_files();views();http();loopback();}
    catch (const std::exception& e) {std::cerr<<"FAILED with exception: "<<e.what()<<'\n';return 1;}
    if (failures) {std::cerr<<failures<<" check(s) failed\n";return 1;}
    std::cout<<"workbench: all passed\n";
    return 0;
}
