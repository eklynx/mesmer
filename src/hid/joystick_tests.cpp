// Tests for the joystick-report path: descriptor parsing and decoding (hid/report_decoder.hpp), the yoke profile's
// glitch rules (brunner/yoke_profile.hpp), press detection and the command map (brunner/button_input.hpp) and the
// "button_commands" key of the state file (brunner/owner_state.hpp). The descriptor and reports are the CLS-E NG's,
// captured with hid/mac/joystick_probe_mac.cpp. No hardware. Spec: docs/dev/button-commands.md.
#include "brunner/button_input.hpp"
#include "brunner/button_runner.hpp"
#include "brunner/owner_state.hpp"
#include <chrono>
#include <deque>
#include <iostream>
#include <thread>

using namespace brunner;
namespace {
void check_at(bool okay,int line) {if (!okay) throw std::runtime_error("check failed line "+std::to_string(line));}
#define check(...) check_at((__VA_ARGS__),__LINE__)

const std::vector<std::uint8_t> ng_descriptor{
    0x05,0x01,0x09,0x04,0xa1,0x01,0x05,0x09,0x19,0x01,0x29,0x06,0x15,0x00,0x25,0x01,
    0x36,0x00,0x00,0x46,0x01,0x00,0x75,0x01,0x95,0x06,0x81,0x02,0x75,0x02,0x95,0x01,
    0x81,0x03,0x05,0x01,0x09,0x39,0x15,0x01,0x25,0x08,0x36,0x00,0x00,0x46,0x3b,0x01,
    0x66,0x14,0x00,0x55,0x00,0x75,0x04,0x95,0x01,0x81,0x42,0x75,0x04,0x95,0x01,0x81,
    0x03,0x05,0x01,0x15,0x00,0x26,0xff,0xff,0x36,0x00,0x00,0x46,0xff,0xff,0x65,0x00,
    0x09,0x30,0x75,0x10,0x95,0x01,0x81,0x02,0x09,0x31,0x75,0x10,0x95,0x01,0x81,0x02,
    0xc0};
const hid::ReportLayout& layout() {
    static const auto parsed=hid::parse_report_layout(ng_descriptor);
    check(parsed.has_value());
    return *parsed;
}
std::optional<hid::JoystickState> decode(std::initializer_list<std::uint8_t> bytes) {
    const std::vector<std::uint8_t> report(bytes);
    return hid::decode_report(layout(),report.data(),report.size());
}
hid::JoystickState state(std::uint32_t buttons,int hat=0,bool padding_clear=true) {
    hid::JoystickState s;s.buttons=buttons;s.hat=hat;s.padding_clear=padding_clear;return s;
}
ButtonEntry one(const char* command) {ButtonEntry e;e.form=ButtonEntry::Form::single;e.commands={command};return e;}
ButtonEntry many(std::vector<std::string> commands) {ButtonEntry e;e.form=ButtonEntry::Form::list;e.commands=std::move(commands);return e;}
const YokeProfile& profile() {return yoke_profile_for({0x25bb,0x008b});}

void the_descriptor_is_parsed() {
    check(layout().buttons.size()==6 && layout().hat.has_value() && layout().payload_bytes()==6);
    check(layout().buttons[0].first_bit==0 && layout().buttons[5].first_bit==5);
    check(layout().hat->first_bit==8 && layout().hat->bit_count==4);
    check(!hid::parse_report_layout({}).has_value());
    check(!hid::parse_report_layout({0x05,0x01,0x09,0x04,0xa1}).has_value());   // truncated
    check(!hid::parse_report_layout({0x06,0x00,0xff,0x0a,0x02,0xff,0xa1,0x01,0x85,0x3f,0x95,0x3f,0x75,0x08,0x15,0x00,
        0x25,0xff,0x0a,0x01,0xff,0x81,0x02,0x0a,0x01,0xff,0x91,0x02,0xc0}).has_value());   // the vendor bridge is no joystick
}
void reports_are_decoded() {
    check(*decode({0x00,0x00,0x60,0x7f,0x00,0x00})==state(0));
    check(*decode({0x01,0x00,0,0,0,0})==state(1));        // button 1, the trigger
    check(*decode({0x10,0x00,0,0,0,0})==state(1u<<4));    // button 5, rocker up
    check(*decode({0x20,0x00,0,0,0,0})==state(1u<<5));    // button 6, the hat button
    for (int hat=1;hat<=8;++hat) check(decode({0x00,static_cast<std::uint8_t>(hat),0,0,0,0})->hat==hat);
    check(decode({0x00,0x09,0,0,0,0})->hat==0);            // outside the logical range: centred
    check(!decode({0x00,0x00,0,0,0,0})->hat);
    check(decode({0x7d,0x00,0x05,0x00,0x00,0x00})->padding_clear==false);   // the captured glitch
    check(!decode({0x00,0x00}).has_value());                // too short
}
void the_glitch_is_dropped() {
    check(profile().invalid_reason(state((1u<<3)|(1u<<4))).size()>0);                 // the rocker, both sides
    check(profile().invalid_reason(state((1u<<4)|(1u<<5))).empty());                  // rocker up with the hat pushed in is a real press
    check(profile().invalid_reason(*decode({0x7d,0x00,0x05,0x00,0x00,0x00})).size()>0);
    check(profile().invalid_reason(state(1u<<3)).empty() && profile().invalid_reason(state(1u<<4)).empty());
    check(profile().invalid_reason(state(0,0,false)).size()>0);   // padding alone is enough
    ButtonInput input(profile());
    check(input.feed(state(0)).dropped.empty());
    const auto glitch=input.feed(*decode({0x7d,0x00,0x05,0x00,0x00,0x00}));
    check(!glitch.dropped.empty() && glitch.edges.empty());     // nothing fires from a glitched report
    check(input.feed(state(0)).edges.empty());                  // and the state it left is the last good one
}
void presses_fire_once() {
    ButtonInput input(profile());
    check(input.feed(state(0)).edges.empty());                    // baseline
    auto r=input.feed(state(1));
    check(r.edges.size()==1 && r.edges[0]==InputId{InputKind::button,1});
    check(input.feed(state(1)).edges.empty());                    // held: no repeat
    r=input.feed(state(0));                                       // release: its own edge
    check(r.edges.size()==1 && r.edges[0]==InputId{InputKind::button,1,true});
    r=input.feed(state(0,3));
    check(r.edges.size()==1 && r.edges[0]==InputId{InputKind::hat,3});
    check(input.feed(state(0,3)).edges.empty());
    r=input.feed(state(0,0));                                     // centring releases the position it left
    check(r.edges.size()==1 && r.edges[0]==InputId{InputKind::hat,3,true});
    check(input.feed(state(0,0)).edges.empty());
    input.feed(state(0,2));
    r=input.feed(state(0,4));                                     // straight from one position to another
    check(r.edges.size()==2 && r.edges[0]==InputId{InputKind::hat,2,true} && r.edges[1]==InputId{InputKind::hat,4});
    r=input.feed(state(5u,0));                                    // buttons 1 and 3 pressed, hat released
    check(r.edges.size()==3 && r.edges[0]==InputId{InputKind::hat,4,true} && r.edges[1]==InputId{InputKind::button,1} && r.edges[2]==InputId{InputKind::button,3});
    r=input.feed(state(2u,0));                                    // releases come before presses
    check(r.edges.size()==3 && r.edges[0].released && r.edges[1].released && !r.edges[2].released);
    ButtonInput held(profile());
    check(held.feed(state(2)).edges.empty());                     // already held at the start: baseline, no press
    r=held.feed(state(0));
    check(r.edges.size()==1 && r.edges[0]==InputId{InputKind::button,2,true});   // but its release is seen
}
void a_vanished_yoke_releases_everything() {
    ButtonInput input(profile());
    check(input.release_all().empty());                           // nothing known yet
    input.feed(state(0));
    input.feed(state(0b101,6));
    const auto edges=input.release_all();
    check(edges.size()==3 && edges[0]==InputId{InputKind::button,1,true} && edges[1]==InputId{InputKind::button,3,true} && edges[2]==InputId{InputKind::hat,6,true});
    check(input.release_all().empty());
    check(input.feed(state(1)).edges.empty());                    // a new baseline
}
void names_and_commands() {
    check(input_name({InputKind::button,3})=="button_3" && input_name({InputKind::hat,1})=="hat_n" && input_name({InputKind::hat,8})=="hat_nw");
    check(all_inputs(profile()).size()==14);
    for (const auto& id:all_inputs(profile())) check(parse_input_name(input_name(id),profile())==id);
    check(!parse_input_name("button_0",profile()) && !parse_input_name("button_7",profile()) && !parse_input_name("button_",profile()));
    check(input_name({InputKind::button,3,true})=="button_3_released" && input_name({InputKind::hat,2,true})=="hat_ne_released");
    for (auto id:all_inputs(profile())) {id.released=true;check(parse_input_name(input_name(id),profile())==id);}
    check(input_name({InputKind::button,3,false,true})=="button_3_hold" && input_name({InputKind::hat,1,false,true})=="hat_n_hold");
    for (auto id:all_inputs(profile())) {id.hold=true;check(parse_input_name(input_name(id),profile())==id);}
    check(!parse_input_name("button_3_released_hold",profile()) && !parse_input_name("button_3_hold_released",profile()) && !parse_input_name("button_9_hold",profile()) && !parse_input_name("_hold",profile()));
    check(!parse_input_name("button_7_released",profile()) && !parse_input_name("_released",profile()) && !parse_input_name("hat_up_released",profile()));
    check(!parse_input_name("hat_up",profile()) && !parse_input_name("Button_1",profile()) && !parse_input_name("",profile()));
    std::vector<std::string> notes;
    const auto none=ButtonCommands::from_state({},profile(),notes);
    check(none.size()==0 && notes.empty() && none.commands_for({InputKind::button,1}).empty());   // the default: nothing
    const auto some=ButtonCommands::from_state({{"button_1",one("sim/autopilot/servos_toggle")},{"button_2",ButtonEntry{}},
        {"button_3",one("")},{"hat_n",one("sim/flight_controls/pitch_trim_up")},{"button_9",one("x/y")},
        {"hat_e",one("has space")},{"button_2_released",one("sim/flight_controls/flaps_up")},
        {"hat_n_released",ButtonEntry{}},{"button_4_hold",one("sim/flight_controls/pitch_trim_down")}},profile(),notes);
    check(some.commands_for({InputKind::button,4,false,true})==std::vector<std::string>{"sim/flight_controls/pitch_trim_down"} && some.commands_for({InputKind::button,4}).empty());
    check(some.size()==4 && some.commands_for({InputKind::button,2,true})==std::vector<std::string>{"sim/flight_controls/flaps_up"});
    check(some.commands_for({InputKind::button,2}).empty() && some.commands_for({InputKind::hat,1,true}).empty());   // a release is its own entry
    check(some.commands_for({InputKind::button,1})==std::vector<std::string>{"sim/autopilot/servos_toggle"});
    check(some.commands_for({InputKind::button,2}).empty() && some.commands_for({InputKind::button,3}).empty());
    check(some.commands_for({InputKind::hat,1})==std::vector<std::string>{"sim/flight_controls/pitch_trim_up"});
    check(notes.size()==2);   // button_9 and "has space"
    notes.clear();
    const auto several=ButtonCommands::from_state({{"button_1",many({"a/one","","b/two","bad name","a/one"})},{"hat_s_hold",many({})},
        {"button_5_released",many({"c/three"})}},profile(),notes);
    check(several.commands_for({InputKind::button,1})==std::vector<std::string>{"a/one","b/two","a/one"});   // in the order written; empty strings skipped
    check(several.commands_for({InputKind::hat,5,false,true}).empty() && several.size()==2 && notes.size()==1);
    check(several.commands_for({InputKind::button,5,true})==std::vector<std::string>{"c/three"});          // a one-item list is the same as a string
}
void the_state_file_key() {
    check(OwnerState{}.button_commands.empty());
    check(serialize_owner_state(OwnerState{})=="{\"version\":1,\"allow_unverified_yoke\":false}\n");   // nothing added by default
    const auto read=read_owner_state("{\"version\":1,\"button_commands\":{\"button_1\":\"a/b\",\"hat_n\":null,\"button_2\":\"\",\"nope\":\"c/d\",\"button_3\":5,"
        "\"button_4\":[\"x/y\",\"z/w\"],\"button_5\":[\"q/r\"],\"button_6\":[],\"hat_s\":[\"a/b\",7,\"c/d\"]}}");
    const auto& map=read.state.button_commands;
    check(map.size()==8 && map.at("button_1")==one("a/b"));                                             // unknown names are kept as read
    check(map.at("hat_n")==ButtonEntry{} && map.at("button_4")==many({"x/y","z/w"}));
    check(map.at("button_5").form==ButtonEntry::Form::list && map.at("button_5").commands.size()==1);   // a one-item list stays a list
    check(map.at("button_6")==many({}) && map.at("hat_s")==many({"a/b","c/d"}));
    check(read.notes.size()==2);                                                                         // button_3 (a number) and hat_s's 7
    const auto again=read_owner_state(serialize_owner_state(read.state));
    check(again.state==read.state && again.notes.empty());                                              // a save keeps the map, each in the form read
    check(serialize_owner_state(read.state).find("\"button_5\":[\"q/r\"]")!=std::string::npos && serialize_owner_state(read.state).find("\"button_1\":\"a/b\"")!=std::string::npos);
    check(read_owner_state("{\"version\":1,\"button_commands\":[1]}").notes.size()==1);
    check(read_owner_state("{\"version\":1,\"button_commands\":[1]}").state.button_commands.empty());
    check(read_owner_state("{\"version\":1,\"button_commands\":{\"button_1\":{}}}").notes.size()==1);
    check(read_owner_state("{\"version\":1,\"auto_start\":true,\"button_commands\":{\"button_1\":\"a/b\"}}").state.auto_start==true);
}
// A hold action as text, "+cmd" for a begin and "-cmd" for an end, so a plan reads as one string.
std::string plan_text(const std::vector<HoldAction>& actions) {
    std::string text;
    for (const auto& a:actions) text+=(text.empty()?"":" ")+std::string(a.begin?"+":"-")+a.command;
    return text;
}
void holds_are_kept_per_command() {
    std::vector<std::string> notes;
    const auto commands=ButtonCommands::from_state({{"button_1_hold",one("a/one")},{"button_2_hold",one("b/two")},
        {"hat_n_hold",many({"rot/up"})},{"hat_ne_hold",many({"rot/up","rot/right"})},{"hat_e_hold",many({"rot/right"})},
        {"hat_nw_hold",many({"rot/up","rot/left"})},{"button_3_hold",one("a/one")},{"button_4",one("e/five")}},profile(),notes);
    check(notes.empty());
    const auto press=[](InputKind kind,int index) {return InputId{kind,index};};
    const auto release=[](InputKind kind,int index) {return InputId{kind,index,true};};
    const auto B=InputKind::button;const auto H=InputKind::hat;
    HoldTracker tracker;
    check(plan_text(tracker.plan({press(B,1)},commands))=="+a/one" && tracker.held("a/one"));
    check(plan_text(tracker.plan({press(B,1)},commands)).empty());                          // never begun twice
    check(plan_text(tracker.plan({release(B,1)},commands))=="-a/one" && !tracker.held("a/one"));
    // The hat sweeps up -> up-right -> right -> up: a command that carries over is never ended and begun again.
    check(plan_text(tracker.plan({press(H,1)},commands))=="+rot/up");
    check(plan_text(tracker.plan({release(H,1),press(H,2)},commands))=="+rot/right");        // up -> up-right: rot/up stays
    check(plan_text(tracker.plan({release(H,2),press(H,3)},commands))=="-rot/up");           // up-right -> right: rot/right stays
    check(plan_text(tracker.plan({release(H,3),press(H,1)},commands))=="-rot/right +rot/up"); // right -> up: ends first
    check(plan_text(tracker.plan({release(H,1),press(H,8)},commands))=="+rot/left");         // up -> up-left: rot/up stays
    check(plan_text(tracker.plan({release(H,8),press(H,2)},commands))=="-rot/left +rot/right"); // up-left -> up-right
    check(plan_text(tracker.plan({release(H,2)},commands))=="-rot/up -rot/right");           // centred: both end
    check(!tracker.held("rot/up") && !tracker.held("rot/right"));
    // Two controls with the same command: it stays begun until both let go.
    check(plan_text(tracker.plan({press(B,1),press(B,3)},commands))=="+a/one");
    check(plan_text(tracker.plan({release(B,1)},commands)).empty() && tracker.held("a/one"));
    check(plan_text(tracker.plan({release(B,3)},commands))=="-a/one");
    // Ends come before begins, whatever order the edges were queued in.
    tracker.plan({press(H,1)},commands);
    check(plan_text(tracker.plan({press(B,2),release(H,1),press(H,3)},commands))=="-rot/up +b/two +rot/right");
    tracker.plan({release(B,2),release(H,3)},commands);
    // A tap, begun and ended in one batch, after the other ends; and not at all when the command is held anyway.
    tracker.plan({press(B,1)},commands);
    check(plan_text(tracker.plan({release(B,1),press(B,2),release(B,2)},commands))=="-a/one +b/two -b/two");
    tracker.plan({press(H,1)},commands);
    check(plan_text(tracker.plan({press(H,2),release(H,2)},commands))=="+rot/right -rot/right" && tracker.held("rot/up"));   // rot/up is held anyway: only rot/right pulses
    check(plan_text(tracker.plan({release(H,1)},commands))=="-rot/up");
    // A hold entry with no commands, and the releases of what was never held, do nothing.
    check(plan_text(tracker.plan({press(B,4),release(B,4),release(B,2)},commands)).empty());
    // Stopping ends everything still begun, once.
    tracker.plan({press(B,1),press(H,2)},commands);
    check(plan_text(tracker.end_all())=="-a/one -rot/up -rot/right" && tracker.end_all().empty() && !tracker.held("a/one"));
}
void the_tolerance_key() {
    check(!OwnerState{}.reply_tolerance_ms && serialize_owner_state(OwnerState{}).find("tolerance")==std::string::npos);   // nothing added by default
    check(read_owner_state("{\"version\":1,\"reply_tolerance_ms\":2500}").state.reply_tolerance_ms==2500 && read_owner_state("{\"version\":1,\"reply_tolerance_ms\":2500}").notes.empty());
    check(read_owner_state("{\"version\":1,\"reply_tolerance_ms\":0}").state.reply_tolerance_ms==0);        // 0 is allowed: none
    check(read_owner_state("{\"version\":1,\"reply_tolerance_ms\":5000}").state.reply_tolerance_ms==5000);  // the ceiling
    for (const char* bad:{"5001","-1","1.5","\"1000\"","true","null"}) {
        const auto r=read_owner_state(std::string("{\"version\":1,\"reply_tolerance_ms\":")+bad+"}");
        check(!r.state.reply_tolerance_ms && r.notes.size()==1);                                              // ignored and named
    }
    OwnerState both;both.reply_tolerance_ms=1500;both.auto_start=true;
    check(read_owner_state(serialize_owner_state(both)).state==both);                                        // a save keeps it
}
void the_queue_hands_over() {
    PressQueue queue;
    check(queue.drain().empty());
    queue.push({InputKind::button,1});queue.push({InputKind::hat,2});
    const auto items=queue.drain();
    check(items.size()==2 && items[1]==InputId{InputKind::hat,2} && queue.drain().empty());
}

// A scripted joystick: hands out its reports, then times out; throws TransportError once told to.
struct FakeJoystick final : hid::JoystickDevice {
    std::deque<std::vector<std::uint8_t>> reports;
    bool fail_when_empty=false;
    std::uint16_t vendor() const override {return 0x25bb;}
    std::uint16_t product() const override {return 0x008b;}
    const std::vector<std::uint8_t>& descriptor() const override {return ng_descriptor;}
    bool read(std::vector<std::uint8_t>& payload,int) override {
        if (reports.empty()) {
            if (fail_when_empty) throw TransportError("Joystick interface removed");
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            return false;
        }
        payload=reports.front();reports.pop_front();return true;
    }
};
void the_runner_turns_reports_into_presses() {
    auto device=std::make_unique<FakeJoystick>();
    device->reports={{0,0,0x60,0x7f,0,0},{0x7d,0,0x05,0,0,0},{0x01,0,0x60,0x7f,0,0},{0x7d,0,0x05,0,0,0},{0x00,0,0x60,0x7f,0,0},{0x00,3,0x60,0x7f,0,0}};
    device->fail_when_empty=true;
    std::atomic<int> opens{0};
    FakeJoystick* raw=device.get();
    ButtonRunner runner([&](std::string& why)->std::unique_ptr<hid::JoystickDevice> {
        if (opens++==0) return std::move(device);
        why="no joystick interface";return nullptr;
    },10);
    check(runner.take_drops().empty() && runner.status().empty());
    runner.start();
    std::vector<InputId> got;
    for (int i=0;i<200 && got.size()<4;++i) {
        for (const auto& id:runner.presses().drain()) got.push_back(id);
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    runner.stop();
    (void)raw;
    check(got.size()==4 && got[0]==InputId{InputKind::button,1} && got[1]==InputId{InputKind::button,1,true} && got[2]==InputId{InputKind::hat,3}
          && got[3]==InputId{InputKind::hat,3,true});   // the last one: the yoke vanished with the hat held   // the glitches fired nothing
    const auto drops=runner.take_drops();
    check(drops.find("dropped 2 glitched reports")!=std::string::npos && drops.find("buttons 4 and 5 together: 2")!=std::string::npos);
    check(runner.take_drops().empty());                                                                  // reported once
    check(runner.status().find("removed")!=std::string::npos || runner.status().empty());                // the removal ended the session
}
void a_missing_joystick_is_not_an_error() {
    ButtonRunner runner([](std::string& why)->std::unique_ptr<hid::JoystickDevice> {why="no joystick interface";return nullptr;},5);
    runner.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(40));
    runner.stop();
    check(runner.status().empty() && runner.presses().drain().empty());
    ButtonRunner refused([](std::string& why)->std::unique_ptr<hid::JoystickDevice> {why="macOS refused access (Input Monitoring permission?)";return nullptr;},5);
    refused.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(40));
    refused.stop();
    check(refused.status().find("refused")!=std::string::npos);
}
}

int main() {
    try {
        the_descriptor_is_parsed();
        reports_are_decoded();
        the_glitch_is_dropped();
        presses_fire_once();
        a_vanished_yoke_releases_everything();
        names_and_commands();
        the_state_file_key();
        the_tolerance_key();
        holds_are_kept_per_command();
        the_queue_hands_over();
        the_runner_turns_reports_into_presses();
        a_missing_joystick_is_not_an_error();
        std::cout<<"joystick tests passed\n";
        return 0;
    } catch (const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
