// Development harness for the feel workbench page: the real Workbench and
// server, with a simulated aircraft instead of X-Plane and the yoke. It never
// touches hardware.
//   brunner_workbench_demo [--port 8087] [--scenario ready|force_feel_off|yoke_off|fault|busy|baron]
//                          [--page workbench.html] [--dir <folder>] [--seconds N] [--public]
//                          [--profiles BrunnerDirect.profiles.json]
//                          [--ias KT] [--yoke PERCENT] [--tail-alpha DEG] [--tail-stalled SHARE]
// --page serves the file from disk on every request, so edits show on reload.
// --public listens on every interface (for an iPad); the default is 127.0.0.1.
// --profiles is the aircraft profiles file (scripts/brunner_profiles.py
// writes one from ansible/brunner.yml), re-read within 2 s of a change.
// --ias and --yoke hold the circuit's airspeed and the hand's yoke (percent,
// aft positive); --tail-alpha and --tail-stalled stand in for X-Plane's tail
// angle and tail-plane stall, so the page can show a stalled tail or air from
// behind (an angle beyond 90 degrees).
// Without it pitch has no profile, as in the plugin: no model, no target.
#include "ui/workbench/workbench_server.hpp"
#include "brunner/loading.hpp"
#include <csignal>
#include <iostream>
#include <optional>
#include <thread>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <winsock2.h>
#endif

using namespace brunner;
using namespace brunner::workbench;
namespace {
std::atomic<bool> quit{false};
void on_signal(int) {quit=true;}
struct Scenario {const char* name;const char* status;const char* view;const char* motors;bool stock_c172;const char* aircraft;const char* path;};
constexpr char c172_path[]="Aircraft/Laminar Research/Cessna 172 SP/Cessna_172SP.acf",baron_path[]="Aircraft/Laminar Research/Beechcraft Baron 58/Baron_58.acf";
constexpr Scenario scenarios[]{
    {"ready","Ready - force feel on","ready","on",true,"Cessna 172 SP",c172_path},
    {"force_feel_off","Ready - force feel off","force_feel_off","off",true,"Cessna 172 SP",c172_path},
    {"yoke_off","Yoke off or unplugged","yoke_off","off",true,"Cessna 172 SP",c172_path},
    {"fault","Yoke fault - motors stopped. Retry from the Yoke menu","fault","stopped",true,"Cessna 172 SP",c172_path},
    {"busy","Yoke centring - hands off","busy","on",true,"Cessna 172 SP",c172_path},
    {"baron","Ready - force feel on","ready","on",false,"Beechcraft Baron 58",baron_path},
};
const Scenario* find(const std::string& name) {
    for (const auto& s:scenarios) if (name==s.name) return &s;
    return nullptr;
}
std::string read_page(const std::filesystem::path& path) {
    std::ifstream in(path,std::ios::binary);
    if (!in) return "<!doctype html><title>Yoke feel</title><p>Cannot read "+path.string()+"</p>";
    std::ostringstream s;s<<in.rdbuf();return s.str();
}
int usage() {
    std::cerr<<"usage: brunner_workbench_demo [--port N] [--scenario ready|force_feel_off|yoke_off|fault|busy|baron]"
               " [--page file.html] [--dir folder] [--seconds N] [--public] [--profiles file.json]"
               " [--ias KT] [--yoke PERCENT] [--tail-alpha DEG] [--tail-stalled SHARE]\n";
    return 2;
}
}
int main(int argc,char** argv) {
    int port=8087,seconds=0;bool everywhere=false;
    std::optional<double> held_ias,held_yoke;double tail_alpha=0,tail_stalled=0;
    const Scenario* scenario=&scenarios[0];
    std::filesystem::path page_path,profiles_path,dir=std::filesystem::temp_directory_path()/"brunner-workbench-demo";
    for (int i=1;i<argc;++i) {
        const std::string arg=argv[i];
        const auto value=[&]()->std::string {if (i+1>=argc) throw std::invalid_argument(arg+" needs a value");return argv[++i];};
        try {
            if (arg=="--port") port=std::stoi(value());
            else if (arg=="--seconds") seconds=std::stoi(value());
            else if (arg=="--page") page_path=value();
            else if (arg=="--dir") dir=value();
            else if (arg=="--profiles") profiles_path=value();
            else if (arg=="--public") everywhere=true;
            else if (arg=="--ias") held_ias=std::stod(value());
            else if (arg=="--yoke") held_yoke=std::stod(value());
            else if (arg=="--tail-alpha") tail_alpha=std::stod(value());
            else if (arg=="--tail-stalled") tail_stalled=std::stod(value());
            else if (arg=="--scenario") {scenario=find(value());if (!scenario) return usage();}
            else return usage();
        } catch (const std::exception& e) {std::cerr<<e.what()<<'\n';return usage();}
    }
#ifdef _WIN32
    WSADATA data{};WSAStartup(MAKEWORD(2,2),&data);
#endif
    std::signal(SIGINT,on_signal);std::signal(SIGTERM,on_signal);
    std::filesystem::create_directories(dir);
    Workbench bench;
    for (const auto& line:bench.load(dir)) std::cout<<line<<'\n';
    ServerOptions options;
    options.port=port;options.bind_address=everywhere?"0.0.0.0":"127.0.0.1";
    if (page_path.empty()) options.page=[]{return embedded_page();};
    else options.page=[page_path]{return read_page(page_path);};
    Server server;std::string error;
    if (!server.start(bench,options,error)) {std::cerr<<"Feel tuning unavailable: "<<error<<'\n';return 1;}
    std::cout<<"Feel workbench demo ("<<scenario->name<<") at http://"<<options.bind_address<<':'<<server.port()<<"/  files in "<<dir.string()<<std::endl;

    const auto start=Clock::now();
    auto next=start;
    // The plugin's profile polling, in miniature.
    std::filesystem::file_time_type profiles_time{};
    Clock::time_point profiles_checked{};
    const char* flown_path=nullptr;
    const auto poll=[&](Clock::time_point now) {
        if (profiles_path.empty()) return;
        const bool new_flight=flown_path!=scenario->path;
        if (!new_flight && now-profiles_checked<std::chrono::seconds(2)) return;
        profiles_checked=now;
        std::error_code ec;
        const auto time=std::filesystem::last_write_time(profiles_path,ec);
        if (ec) {bench.profile_refused(profiles_path.string()+" is missing");return;}
        if (!new_flight && time==profiles_time) return;
        profiles_time=time;flown_path=scenario->path;
        try {
            const auto set=parse_profiles(read_bounded(profiles_path,256*1024));
            bench.profile_refused("");bench.set_profile(now,profile_for(set,scenario->path),new_flight);
        } catch (const std::exception& e) {bench.profile_refused(e.what());}
    };
    while (!quit) {
        const auto now=Clock::now();
        const double t=std::chrono::duration<double>(now-start).count();
        if (seconds>0 && t>=seconds) break;
        for (const auto c:bench.take_commands()) {
            if (c==Command::stop_motors) scenario=find("fault");
            else if (c==Command::force_feel_on || c==Command::retry) scenario=find("ready");
            std::cout<<"command: "<<(c==Command::stop_motors?"stop_motors":c==Command::force_feel_on?"force_feel_on":"retry")<<std::endl;
        }
        poll(now);
        // A 60 s circuit: IAS climbs 0-110 kt and back; the propeller adds tail flow on the C172.
        const double phase=std::fmod(t,60.0)/60.0,ias=held_ias?*held_ias:110*(1-std::abs(2*phase-1));
        const auto tail=scenario->stock_c172?TailAir{double(pa_from_kt(static_cast<int>(std::lround(ias+12)))),TailSource::surface}:tail_air(-1,ias);
        const double roll_pa=pa_from_kt(static_cast<int>(std::lround(ias)));
        const double trim=.3,bank=20*std::sin(t/7),flaps=ias<65?10:0,vs=500*std::sin(t/11);
        const bool session=std::string(scenario->name)=="ready" || std::string(scenario->name)=="baron";
        FrameInputs in;
        in.valid=true;in.force_feel=std::string(scenario->name)!="force_feel_off";in.connected=std::string(scenario->name)!="yoke_off";
        in.inputs={trim,tail.pa,1.0,tail_alpha,tail_stalled};
        const auto applied=bench.frame(now,in);
        FlightSettings f;apply(applied.settings,f);
        Live live;
        live.status=scenario->status;live.view=scenario->view;live.motors=scenario->motors;
        live.aircraft=scenario->aircraft;live.aircraft_file=std::filesystem::path(scenario->path).filename().string();live.aileron_panels=scenario->stock_c172;
        live.ias_kt=ias;live.bank_deg=bank;live.flaps_deg=flaps;live.vertical_fpm=vs;
        // The pilot's hand moves the yoke about the model's rest.
        const double yoke=held_yoke?*held_yoke:15*std::sin(t/3);
        live.pitch.yoke_percent=yoke;live.pitch.inputs=in.inputs;live.pitch.tail_source=tail.source;
        if (applied.has_profile) {
            live.pitch.solution=solve_pitch(applied.figures,in.inputs,-yoke*counts_per_percent,applied.knobs.units_per_lb);
            if (session) {
                live.pitch.now_percent=live.pitch.solution->strength;
                live.pitch.phase=t<0.5?"blending":"following";live.pitch.update_hz=6;
            }
        }
        if (scenario->stock_c172) {live.pitch.aoa_deg=4-ias/40+std::sin(t/5);live.pitch.deflection_deg=-3*std::sin(t/3);live.pitch.stalled=ias>5 && ias<45?1:0;}
        // The wheel follows the hand about the aileron model's rest.
        const double wheel=20*std::sin(t/4);
        live.roll.yoke_percent=wheel;
        if (applied.has_profile) {
            const AileronSide side{roll_pa,scenario->stock_c172?4-ias/40+std::sin(t/5):0,0};
            live.roll.inputs=AileronInputs{side,side};
            live.roll.source=scenario->stock_c172?AileronSource::surface:AileronSource::airspeed;
            live.roll.solution=solve_roll(applied.ailerons,*live.roll.inputs,wheel*roll_travel_counts()/100,
                applied.roll_knobs.units_per_nm,true);
            if (session) {live.roll.now_percent=live.roll.solution->strength;live.roll.phase=t<0.5?"blending":"following";}
        }
        if (scenario->stock_c172) {
            live.roll.airflow_pa=roll_pa;
            live.roll.aoa_deg=4-ias/40+std::sin(t/5);live.roll.deflection_deg=4*std::sin(t/4);live.roll.stalled=ias>5 && ias<45?1:0;
        }
        const auto curve=pitch_setup_for(true,f.pitch_force_gain_divisor,f.pitch_move_back_gain).curve;
        for (std::size_t i=0;i<curve.size();++i) live.pitch_curve[i]=curve[i];
        bench.publish(now,live);
        for (const auto& e:bench.take_events()) std::cout<<e<<std::endl;
        next+=std::chrono::milliseconds(100);
        std::this_thread::sleep_until(next);
    }
    server.stop();
    for (const auto& e:bench.take_events()) std::cout<<e<<'\n';
#ifdef _WIN32
    WSACleanup();
#endif
    return 0;
}
