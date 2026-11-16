// Tests for the per-aircraft state files (config/state/aircraft_state.hpp, spec docs/dev/advanced-configuration.md "Button commands for
// one aircraft"): reading one, the version, what is ignored, and which file names an aircraft tries. No files, no X-Plane.
#include "config/state/aircraft_state.hpp"
#include <iostream>

using namespace brunner;
namespace {
void check_at(bool okay,int line) {if (!okay) throw std::runtime_error("check failed line "+std::to_string(line));}
#define check(...) check_at((__VA_ARGS__),__LINE__)
bool mentions(const std::vector<std::string>& lines,const char* part) {
    for (const auto& l:lines) if (l.find(part)!=std::string::npos) return true;
    return false;
}

void a_file_is_read() {
    const auto read=read_aircraft_state(R"({"version":1,"button_commands":{"button_1":"sim/view/default_view","hat_n_hold":["a/b","c/d"],"button_2":null}})","aircraft/C172.json");
    check(read.notes.empty() && read.state.button_commands.size()==3);
    const auto& single=read.state.button_commands.at("button_1");
    check(single.form==ButtonEntry::Form::single && single.commands==std::vector<std::string>{"sim/view/default_view"});
    const auto& list=read.state.button_commands.at("hat_n_hold");
    check(list.form==ButtonEntry::Form::list && list.commands==(std::vector<std::string>{"a/b","c/d"}));
    check(read.state.button_commands.at("button_2").form==ButtonEntry::Form::none);   // null is kept: on this aircraft the control runs nothing
    check(read_aircraft_state(R"({"version":1})","aircraft/x.json").state==AircraftState{});   // a file with no commands overrides nothing
}
void the_version_is_required() {
    for (const char* text:{R"({"button_commands":{"button_1":"a/b"}})",R"({"version":2,"button_commands":{"button_1":"a/b"}})",R"({"version":"1"})",R"({"version":1.5})"}) {
        const auto read=read_aircraft_state(text,"aircraft/x.json");
        check(read.state==AircraftState{} && read.notes.size()==1 && mentions(read.notes,"aircraft/x.json ignored: it needs \"version\":1"));   // nothing of it applies
    }
}
void a_bad_file_is_ignored_whole() {
    for (const char* text:{"","not json","[1]","{","\"version\""}) {
        const auto read=read_aircraft_state(text,"aircraft/x.json");
        check(read.state==AircraftState{} && read.notes.size()==1 && mentions(read.notes,"aircraft/x.json ignored: "));
    }
    check(mentions(read_aircraft_state("[1]","aircraft/x.json").notes,"it is not a JSON object"));
    check(mentions(read_aircraft_state("nope","aircraft/x.json").notes,"it is not valid JSON"));
}
void what_is_wrong_in_a_file_is_named_and_the_rest_applies() {
    auto read=read_aircraft_state(R"({"version":1,"allow_unverified_yoke":true,"button_commands":{"button_1":"a/b"}})","aircraft/C172.json");
    check(read.notes.size()==1 && mentions(read.notes,"aircraft/C172.json: unknown key \"allow_unverified_yoke\" ignored") && read.state.button_commands.size()==1);   // only the commands are an aircraft's
    read=read_aircraft_state(R"({"version":1,"button_commands":[1]})","aircraft/C172.json");
    check(read.notes.size()==1 && mentions(read.notes,"aircraft/C172.json: button_commands must be an object; ignored") && read.state.button_commands.empty());
    read=read_aircraft_state(R"({"version":1,"button_commands":{"button_1":5,"button_2":["a/b",7,"c/d"],"button_3":"x/y"}})","aircraft/C172.json");
    check(read.notes.size()==2 && mentions(read.notes,"button_commands.button_1 must be a command name") && mentions(read.notes,"button_commands.button_2 has a list item"));
    check(!read.state.button_commands.count("button_1") && read.state.button_commands.at("button_2").commands==(std::vector<std::string>{"a/b","c/d"}) && read.state.button_commands.count("button_3"));
}
void ids_are_made_into_safe_file_names() {
    check(aircraft_state_id("C172")=="C172" && aircraft_state_id("Cessna_172SP")=="Cessna_172SP" && aircraft_state_id("A-320.neo")=="A-320.neo");
    check(aircraft_state_id("B 737/800")=="B_737_800" && aircraft_state_id("  C172  ")=="C172");
    check(aircraft_state_id("../../etc/passwd")=="_.._etc_passwd");                      // never a path
    check(aircraft_state_id("a\\b:c*d").find_first_of("\\/:*")==std::string::npos);
    check(aircraft_state_id("")=="" && aircraft_state_id("   ")=="" && aircraft_state_id("...")=="" && aircraft_state_id("___")=="");   // nothing usable
    check(aircraft_state_id(".hidden")=="hidden");                                       // never a dot file
}
void the_names_to_try_are_the_acf_file_then_the_icao() {
    using Names=std::vector<std::string>;
    check(aircraft_state_file_names("C172","Cessna_172SP.acf")==(Names{"Cessna_172SP.json","C172.json"}));   // the file name first
    check(aircraft_state_file_names("","Cessna_172SP.acf")==(Names{"Cessna_172SP.json"}));          // no ICAO: just the .acf name
    check(aircraft_state_file_names("C172","")==(Names{"C172.json"}));                              // no file name: the ICAO
    check(aircraft_state_file_names("","")==Names{});
    check(aircraft_state_file_names("C172","C172.acf")==Names{"C172.json"});                        // the same name twice is tried once
    check(aircraft_state_file_names("","Cessna_172SP.ACF")==(Names{"Cessna_172SP.json"}));            // the extension in any case
    check(aircraft_state_file_names("C172","Aircraft/Laminar Research/Cessna 172 SP/Cessna_172SP.acf")==(Names{"Cessna_172SP.json","C172.json"}));   // a path is cut to its file
    check(aircraft_state_file_names("","C:\\X-Plane\\Aircraft\\x\\My Plane.acf")==(Names{"My_Plane.json"}));
    check(aircraft_state_file_names("B 737","")==(Names{"B_737.json"}));
}
}

int main() {
    try {
        a_file_is_read();
        the_version_is_required();
        a_bad_file_is_ignored_whole();
        what_is_wrong_in_a_file_is_named_and_the_rest_applies();
        ids_are_made_into_safe_file_names();
        the_names_to_try_are_the_acf_file_then_the_icao();
        std::cout<<"aircraft state tests passed\n";
        return 0;
    } catch (const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
