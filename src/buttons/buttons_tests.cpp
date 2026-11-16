// Tests for the button package (src/buttons): control names and aliases, command resolution, edges, holds, the
// shared state and the actions, over fakes. No hardware, no operating-system API. Spec: docs/dev/button-commands.md.
#include "buttons/actions/direct_action.hpp"
#include "buttons/actions/vjoy_action.hpp"
#include "buttons/commands.hpp"
#include "buttons/controls.hpp"
#include "buttons/edges.hpp"
#include "buttons/hold.hpp"
#include "buttons/hub.hpp"
#include "buttons/methods.hpp"
#include "buttons/service.hpp"
#include <deque>
#include <iostream>
#include <set>
#include <thread>

using namespace buttons;
namespace {
void check_at(bool okay,int line) {if (!okay) throw std::runtime_error("check failed line "+std::to_string(line));}
#define check(...) check_at((__VA_ARGS__),__LINE__)
bool refuses(const ControlMap::Aliases& aliases,const char* part=nullptr) {
    try {ControlMap map(aliases);} catch (const ControlError& e) {return !part || std::string(e.what()).find(part)!=std::string::npos;}
    return false;
}

void canonical_names() {
    check(input_name({InputKind::hid_button,3})=="hid_button_3" && input_name({InputKind::hat,1})=="hat_n" && input_name({InputKind::hat,8})=="hat_nw");
    check(input_name({InputKind::can_button,11})=="can_button_11");
    check(all_inputs().size()==38);
    check(input_name({InputKind::can_hat,1})=="can_hat_n" && input_name({InputKind::can_hat,8})=="can_hat_nw" && parse_input("CAN_HAT_SE")==Input{InputKind::can_hat,4});
    check(!parse_input("can_hat_") && !parse_input("can_hat_up") && !valid_friendly_name("can_hat_n"));
    for (const auto& input:all_inputs()) check(parse_input(input_name(input))==input);
    check(parse_input("HID_Button_4")==Input{InputKind::hid_button,4} && parse_input("CAN_BUTTON_16")==Input{InputKind::can_button,16} && parse_input("Hat_SE")==Input{InputKind::hat,4});
    for (const char* bad:{"","button_1","hid_button_0","hid_button_7","can_button_0","can_button_17","hat_","hat_up","hid_button_","can_button_01x","trigger"})
        check(!parse_input(bad));
    check(valid_friendly_name("trigger") && valid_friendly_name("left_thumb_2") && valid_friendly_name("TRIGGER"));
    for (const char* bad:{"","hid_x","can_y","x_released","x_hold","has space","dash-ed","hat_n","hid_button_1","a123456789012345678901234567890123"})
        check(!valid_friendly_name(bad));
}
void no_aliases_every_input_is_its_own_control() {
    const ControlMap map;
    check(map.size()==38);
    for (const auto& input:all_inputs()) {
        const int control=map.control_of(input);
        check(map.resolve(input_name(input))==control && map.names(control).size()==1);
    }
    check(*map.resolve("HID_BUTTON_2")==map.control_of({InputKind::hid_button,2}));
    check(!map.resolve("trigger") && !map.resolve("button_1") && !map.resolve(""));
    // The numbering: hid buttons, hat positions, can slots.
    check(map.control_of({InputKind::hid_button,1})==0 && map.control_of({InputKind::hat,1})==6 && map.control_of({InputKind::can_button,1})==14 && map.control_of({InputKind::can_hat,1})==30);
    // vJoy: a can slot is its own number; the controls without one take 17 up, hid buttons first then the hat.
    check(map.vjoy_button(map.control_of({InputKind::can_button,5}))==5 && map.vjoy_button(map.control_of({InputKind::can_button,16}))==16);
    check(map.vjoy_button(map.control_of({InputKind::hid_button,1}))==17 && map.vjoy_button(map.control_of({InputKind::hid_button,6}))==22);
    check(map.vjoy_button(map.control_of({InputKind::hat,1}))==23 && map.vjoy_button(map.control_of({InputKind::hat,8}))==30);
    // 38 inputs need more than the 32 virtual buttons: what is left over has none (0), and the map is still built.
    check(map.vjoy_button(map.control_of({InputKind::can_hat,1}))==31 && map.vjoy_button(map.control_of({InputKind::can_hat,2}))==32 && map.vjoy_button(map.control_of({InputKind::can_hat,3}))==0 && map.vjoy_button(map.control_of({InputKind::can_hat,8}))==0);
}
ControlMap::Aliases ng_aliases() {
    ControlMap::Aliases a;
    const char* names[]={"trigger","top_left","top_right","rocker_down","rocker_up","hat_push"};
    for (int n=1;n<=6;++n) a[names[n-1]]={"hid_button_"+std::to_string(n),"can_button_"+std::to_string(n)};
    for (const char* d:{"n","ne","e","se","s","sw","w","nw"}) a[std::string("hat_")+d]={std::string("can_hat_")+d};   // the NG's hat is on the bridge too (0x2880:4)
    return a;
}
void aliases_link_hid_and_can() {
    const ControlMap map(ng_aliases());
    check(map.size()==38-14);                                                         // six button pairs and eight hat pairs became fourteen controls
    const int trigger=*map.resolve("trigger");
    check(*map.resolve("hid_button_1")==trigger && *map.resolve("can_button_1")==trigger && *map.resolve("TRIGGER")==trigger);
    check(map.control_of({InputKind::hid_button,1})==trigger && map.control_of({InputKind::can_button,1})==trigger);
    check(map.hid_source(trigger)==Input{InputKind::hid_button,1} && map.can_source(trigger)==Input{InputKind::can_button,1});
    const auto names=map.names(trigger);
    check(names.size()==3 && names[0]=="hid_button_1" && names[1]=="can_button_1" && names[2]=="trigger");
    check(map.vjoy_button(trigger)==1 && map.vjoy_button(*map.resolve("rocker_up"))==5 && map.vjoy_button(*map.resolve("hat_push"))==6);   // the can slot's number: the same under both read methods
    // The hat is linked to its CAN twin but has no CAN slot number, so it takes numbers from 17 up, in order.
    check(map.hid_source(*map.resolve("hat_n"))==Input{InputKind::hat,1} && map.can_source(*map.resolve("hat_n"))==Input{InputKind::can_hat,1});
    check(*map.resolve("can_hat_se")==*map.resolve("hat_se") && map.vjoy_button(*map.resolve("hat_n"))==17 && map.vjoy_button(*map.resolve("hat_nw"))==24);
    // A can slot with no hid input is its own control and keeps its own number.
    check(!map.hid_source(*map.resolve("can_button_9")) && map.vjoy_button(*map.resolve("can_button_9"))==9);
}
void a_hat_can_be_linked_later() {
    auto a=ng_aliases();
    a["hat_n"]={"can_button_15"};                                                     // a built-in name as a key
    a["up"]={"hat_n"};                                                                // and a friendly name for it
    const ControlMap map(a);
    const int hat_n=*map.resolve("hat_n");
    check(*map.resolve("can_button_15")==hat_n && *map.resolve("up")==hat_n && map.vjoy_button(hat_n)==15);
    check(map.hid_source(hat_n)==Input{InputKind::hat,1} && map.can_source(hat_n)==Input{InputKind::can_button,15});
    check(map.vjoy_button(*map.resolve("hat_ne"))==17);                               // the others still start at 17
}
void aliases_chain_and_nest() {
    ControlMap::Aliases a;
    a["trigger"]={"hid_button_1"};
    a["left_index"]={"trigger","can_button_1"};          // a friendly name listing another friendly name
    a["finger"]={"left_index"};
    const ControlMap map(a);
    const int c=*map.resolve("finger");
    check(*map.resolve("trigger")==c && *map.resolve("left_index")==c && *map.resolve("can_button_1")==c && *map.resolve("hid_button_1")==c);
    check(map.size()==37 && map.names(c).size()==5);
    // Cycles and repeated members are harmless.
    ControlMap::Aliases cycle;cycle["a"]={"b","hid_button_1"};cycle["b"]={"a","a"};
    check(ControlMap(cycle).size()==38 && *ControlMap(cycle).resolve("b")==*ControlMap(cycle).resolve("hid_button_1"));
}
void bad_aliases_are_refused() {
    check(refuses({{"hid_x",{"hid_button_1"}}},"not a usable name"));
    check(refuses({{"can_x",{"hid_button_1"}}},"not a usable name"));
    check(refuses({{"x_released",{"hid_button_1"}}},"not a usable name") && refuses({{"x_hold",{"hid_button_1"}}},"not a usable name"));
    check(refuses({{"has space",{"hid_button_1"}}},"not a usable name") && refuses({{"",{"hid_button_1"}}}));
    check(refuses({{"trigger",{"hid_button_9"}}},"neither a button name nor another alias"));
    check(refuses({{"trigger",{"nothing"}}},"neither a button name nor another alias"));
    check(refuses({{"trigger",{}}},"lists nothing"));
    check(refuses({{"a",{"b"}},{"b",{"a"}}},"not linked to any button"));                 // a group with no button in it
    check(refuses({{"x",{"hid_button_1","hid_button_2"}}},"one HID input"));            // two hid inputs
    check(refuses({{"x",{"hat_n","hid_button_1"}}},"one HID input"));                   // a hat position and a button are both hid inputs
    check(refuses({{"x",{"can_button_1","can_button_2"}}},"one CAN input"));
    check(refuses({{"x",{"can_button_1","can_hat_n"}}},"one CAN input"));            // a slot and a can hat position are both CAN inputs
    check(refuses({{"x",{"hid_button_1"}},{"X",{"hid_button_2"}}},"given twice"));
    check(refuses({{"x",{"hid_button_1"}},{"y",{"can_button_1"}},{"z",{"x","y","can_button_2"}}},"one CAN input"));   // a chain reaches two slots
}

// ---- the two settings ----
void the_methods() {
    check(parse_read_method("CanBus")==ReadMethod::can_bus && parse_read_method("CANBUS")==ReadMethod::can_bus && parse_read_method("canbus")==ReadMethod::can_bus);
    check(parse_read_method("HID")==ReadMethod::hid && parse_read_method("hid")==ReadMethod::hid && parse_read_method("Hid")==ReadMethod::hid);
    check(parse_action_method("VJoy")==ActionMethod::vjoy && parse_action_method("vjoy")==ActionMethod::vjoy && parse_action_method("DIRECT")==ActionMethod::direct);
    for (const char* bad:{"","can bus","can","usb","0","1","vjoy ","direct1"}) check(!parse_read_method(bad) && !parse_action_method(bad));
    check(!parse_read_method("vjoy") && !parse_action_method("hid") && !parse_action_method("canbus"));
    check(std::string(read_method_name(ReadMethod::can_bus))=="CanBus" && std::string(read_method_name(ReadMethod::hid))=="HID");
    check(std::string(action_method_name(ActionMethod::vjoy))=="VJoy" && std::string(action_method_name(ActionMethod::direct))=="Direct");
    // What a model that gives neither is, on every operating system (the shipped models say their own: brunner/preset_tests.cpp).
    static_assert(default_read_method==ReadMethod::can_bus && default_action_method==ActionMethod::direct,"a model that says nothing is CanBus + Direct");
}

// ---- edges ----
std::shared_ptr<const ControlMap> ng() {return std::make_shared<const ControlMap>(ng_aliases());}
ButtonState hid(std::uint32_t buttons,int hat=0) {return {buttons,hat};}
std::string edge_text(const std::vector<Edge>& edges) {
    std::string text;
    for (const auto& e:edges) text+=(text.empty()?"":" ")+std::string(e.released?"-":"+")+std::to_string(e.control);
    return text;
}
void edges_over_a_stream() {
    const auto map=ng();
    const auto id=[&](const char* name) {return *map->resolve(name);};
    EdgeDetector detector(map,ReadMethod::hid);
    check(detector.feed(hid(0)).empty());                                             // baseline
    check(edge_text(detector.feed(hid(1)))=="+"+std::to_string(id("trigger")));
    check(detector.feed(hid(1)).empty());                                             // held: no repeat
    check(edge_text(detector.feed(hid(0)))=="-"+std::to_string(id("trigger")));      // its own release edge
    check(edge_text(detector.feed(hid(0,3)))=="+"+std::to_string(id("hat_e")));
    check(detector.feed(hid(0,3)).empty());
    check(edge_text(detector.feed(hid(0,0)))=="-"+std::to_string(id("hat_e")));      // centring releases the position it left
    detector.feed(hid(0,2));
    check(edge_text(detector.feed(hid(0,4)))=="-"+std::to_string(id("hat_ne"))+" +"+std::to_string(id("hat_se")));   // one position to the next
    check(edge_text(detector.feed(hid(0b101,0)))=="-"+std::to_string(id("hat_se"))+" +"+std::to_string(id("trigger"))+" +"+std::to_string(id("top_right")));   // releases first
    // A button held when the stream starts is a baseline, not a press, but its release is seen.
    EdgeDetector held(map,ReadMethod::hid);
    check(held.feed(hid(2)).empty());
    check(edge_text(held.feed(hid(0)))=="-"+std::to_string(id("top_left")));
    // Everything last seen pressed is released at once when the source goes away, and the next state is a baseline again.
    EdgeDetector gone(map,ReadMethod::hid);
    gone.feed(hid(0));gone.feed(hid(0b110,6));   // buttons 2 and 3, hat position 6 (south-west)
    check(edge_text(gone.release_all())=="-"+std::to_string(id("top_left"))+" -"+std::to_string(id("top_right"))+" -"+std::to_string(id("hat_sw")));
    check(gone.release_all().empty() && gone.feed(hid(1)).empty());
    // Bits beyond the read method's inputs are ignored.
    EdgeDetector wide(map,ReadMethod::hid);
    wide.feed(hid(0));
    check(wide.feed(hid(0xffffff80u)).empty());
}
void both_read_methods_name_the_same_control() {
    const auto map=ng();
    EdgeDetector from_hid(map,ReadMethod::hid),from_can(map,ReadMethod::can_bus);
    from_hid.feed(hid(0));from_can.feed(hid(0));
    // The NG's trigger is hid_button_1 and can_button_1: pressed either way, the same control.
    check(edge_text(from_hid.feed(hid(1)))==edge_text(from_can.feed(hid(1))) && !edge_text(from_can.feed(hid(0))).empty());
    // A CAN slot with no HID input is still a control.
    EdgeDetector can(map,ReadMethod::can_bus);
    can.feed(hid(0));
    check(edge_text(can.feed(hid(1u<<8)))=="+"+std::to_string(*map->resolve("can_button_9")));
    // The hat under CanBus is the can_hat position: the same control the HID hat presses, so the same commands and virtual button.
    check(edge_text(can.feed(hid(1u<<8,5)))=="+"+std::to_string(*map->resolve("hat_s")) && *map->resolve("can_hat_s")==*map->resolve("hat_s"));
    check(edge_text(can.feed(hid(1u<<8,0)))=="-"+std::to_string(*map->resolve("hat_s")));
    check(edge_text(can.feed(hid(0xffffu)))!="" && can.feed(hid(0x1ffffu)).empty());   // 16 slots, no 17th
}

// ---- command resolution ----
bool has_note(const std::vector<std::string>& notes,const char* part) {
    for (const auto& n:notes) if (n.find(part)!=std::string::npos) return true;
    return false;
}
void commands_resolve_through_names() {
    const auto map=ng();
    std::vector<std::string> notes;
    const auto commands=ButtonCommands::resolve({{"trigger",{"a/one"}},{"top_left_released",{"b/two"}},{"ROCKER_UP_HOLD",{"c/three","c/four"}},{"hat_n",{"d/five"}}},*map,ReadMethod::hid,notes);
    check(commands.size()==4);
    check(commands.commands_for(*map->resolve("trigger"),Variant::press)==std::vector<std::string>{"a/one"});
    check(commands.commands_for(*map->resolve("top_left"),Variant::release)==std::vector<std::string>{"b/two"});
    check(commands.commands_for(*map->resolve("rocker_up"),Variant::hold)==(std::vector<std::string>{"c/three","c/four"}));
    check(commands.commands_for(*map->resolve("hat_n"),Variant::press)==std::vector<std::string>{"d/five"});
    check(commands.commands_for(*map->resolve("trigger"),Variant::release).empty() && commands.commands_for(*map->resolve("top_right"),Variant::press).empty());
    check(notes.empty());                                                       // under HID every one of these is seen: nothing to say
    notes.clear();
    // A command written under any name of a control: hid_, can_ and friendly names are one entry.
    const auto any=ButtonCommands::resolve({{"hid_button_4",{"x/y"}}},*map,ReadMethod::can_bus,notes);
    check(any.commands_for(*map->resolve("can_button_4"),Variant::press)==std::vector<std::string>{"x/y"} && any.commands_for(*map->resolve("rocker_down"),Variant::press)==std::vector<std::string>{"x/y"});
    check(notes.empty());                                                              // linked, so CanBus sees it
}
void commands_merge_and_say_so() {
    const auto map=ng();
    std::vector<std::string> notes;
    const auto commands=ButtonCommands::resolve({{"trigger",{"c/third"}},{"hid_button_1",{"b/second"}},{"can_button_1",{"a/first"}},{"trigger_hold",{"h/one"}}},*map,ReadMethod::hid,notes);
    // All of them run, in the alphabetical order of the entry names (can_button_1, hid_button_1, trigger).
    check(commands.commands_for(*map->resolve("trigger"),Variant::press)==(std::vector<std::string>{"a/first","b/second","c/third"}));
    check(notes.size()==1 && has_note(notes,"can_button_1, hid_button_1, trigger are the same control") && has_note(notes,"all of their commands run"));
    // The same control under different variants is not a collision.
    notes.clear();
    ButtonCommands::resolve({{"trigger",{"a/b"}},{"hid_button_1_released",{"c/d"}},{"can_button_1_hold",{"e/f"}}},*map,ReadMethod::hid,notes);
    check(notes.empty());
    // Empty entries do not collide.
    ButtonCommands::resolve({{"trigger",{"a/b"}},{"hid_button_1",{}},{"can_button_1",{""}}},*map,ReadMethod::hid,notes);
    check(notes.empty());
}
void commands_note_what_cannot_be_seen_or_used() {
    auto aliases=ng_aliases();
    aliases["hat_push"]={"hid_button_6"};                                              // a control the bridge cannot see (as on a yoke with no link)
    const auto map=std::make_shared<const ControlMap>(aliases);
    std::vector<std::string> notes;
    const auto c=ButtonCommands::resolve({{"hat_push",{"a/b"}},{"hat_e",{"m/n"}},{"hat_e_released",{"c/d"}},{"can_button_9",{"e/f"}},{"nonsense",{"g/h"}},{"button_1",{"i/j"}},{"hid_button_9",{"k/l"}},
        {"trigger_hold",{"has space"}},{"top_left",{"ok/ok","bad name"}}},*map,ReadMethod::can_bus,notes);
    check(has_note(notes,"hat_push has no CAN slot linked on this yoke yet") && has_note(notes,"does not fire with this yoke's CanBus read"));
    check(!has_note(notes,"hat_e has no CAN slot") && !has_note(notes,"hat_e_released has no CAN slot"));   // the hat is on the bridge
    check(has_note(notes,"\"nonsense\" is not a control of this yoke") && has_note(notes,"\"button_1\" is not a control") && has_note(notes,"\"hid_button_9\" is not a control"));
    check(has_note(notes,"trigger_hold has \"has space\"") && has_note(notes,"top_left has \"bad name\""));
    check(!has_note(notes,"can_button_9 has no"));                                    // a CAN-only slot is seen under CanBus
    check(c.commands_for(*map->resolve("top_left"),Variant::press)==std::vector<std::string>{"ok/ok"} && c.commands_for(*map->resolve("trigger"),Variant::hold).empty());
    // Under HID a CAN-only slot is the one that cannot be seen.
    notes.clear();
    ButtonCommands::resolve({{"can_button_9",{"e/f"}},{"trigger",{"a/b"}}},*map,ReadMethod::hid,notes);
    check(notes.size()==1 && has_note(notes,"can_button_9 has no HID input on this yoke") && has_note(notes,"this yoke's HID read"));
}

// ---- holds, per command ----
std::string hold_text(const std::vector<HoldAction>& actions) {
    std::string text;
    for (const auto& a:actions) text+=(text.empty()?"":" ")+std::string(a.begin?"+":"-")+a.command;
    return text;
}
void holds_are_kept_per_command() {
    const auto map=ng();
    std::vector<std::string> notes;
    const auto commands=ButtonCommands::resolve({{"trigger_hold",{"a/one"}},{"top_left_hold",{"b/two"}},{"top_right_hold",{"a/one"}},
        {"hat_n_hold",{"rot/up"}},{"hat_ne_hold",{"rot/up","rot/right"}},{"hat_e_hold",{"rot/right"}},{"hat_nw_hold",{"rot/up","rot/left"}},{"rocker_down",{"e/five"}}},*map,ReadMethod::hid,notes);
    check(notes.empty());
    const auto id=[&](const char* name) {return *map->resolve(name);};
    const auto press=[&](const char* name) {return Edge{id(name),false};};
    const auto release=[&](const char* name) {return Edge{id(name),true};};
    HoldTracker tracker;
    check(hold_text(tracker.plan({press("trigger")},commands))=="+a/one" && tracker.held("a/one"));
    check(hold_text(tracker.plan({press("trigger")},commands)).empty());              // never begun twice
    check(hold_text(tracker.plan({release("trigger")},commands))=="-a/one" && !tracker.held("a/one"));
    // The hat sweeps up -> up-right -> right -> up: a command that carries over is never ended and begun again.
    check(hold_text(tracker.plan({press("hat_n")},commands))=="+rot/up");
    check(hold_text(tracker.plan({release("hat_n"),press("hat_ne")},commands))=="+rot/right");
    check(hold_text(tracker.plan({release("hat_ne"),press("hat_e")},commands))=="-rot/up");
    check(hold_text(tracker.plan({release("hat_e"),press("hat_n")},commands))=="-rot/right +rot/up");
    check(hold_text(tracker.plan({release("hat_n"),press("hat_nw")},commands))=="+rot/left");
    check(hold_text(tracker.plan({release("hat_nw"),press("hat_ne")},commands))=="-rot/left +rot/right");
    check(hold_text(tracker.plan({release("hat_ne")},commands))=="-rot/up -rot/right" && !tracker.held("rot/up"));
    // Two controls with the same command: it stays begun until both let go.
    check(hold_text(tracker.plan({press("trigger"),press("top_right")},commands))=="+a/one");
    check(hold_text(tracker.plan({release("trigger")},commands)).empty() && tracker.held("a/one"));
    check(hold_text(tracker.plan({release("top_right")},commands))=="-a/one");
    // Ends come before begins, whatever order the edges were queued in.
    tracker.plan({press("hat_n")},commands);
    check(hold_text(tracker.plan({press("top_left"),release("hat_n"),press("hat_e")},commands))=="-rot/up +b/two +rot/right");
    tracker.plan({release("top_left"),release("hat_e")},commands);
    // A tap, begun and ended in one batch, after the other ends; and not at all when the command is held anyway.
    tracker.plan({press("trigger")},commands);
    check(hold_text(tracker.plan({release("trigger"),press("top_left"),release("top_left")},commands))=="-a/one +b/two -b/two");
    tracker.plan({press("hat_n")},commands);
    check(hold_text(tracker.plan({press("hat_ne"),release("hat_ne")},commands))=="+rot/right -rot/right" && tracker.held("rot/up"));
    check(hold_text(tracker.plan({release("hat_n")},commands))=="-rot/up");
    // Entries without a hold, and releases of what was never held, do nothing.
    check(hold_text(tracker.plan({press("rocker_down"),release("rocker_down"),release("top_left")},commands)).empty());
    // Stopping ends everything still begun, once.
    tracker.plan({press("trigger"),press("hat_ne")},commands);
    check(hold_text(tracker.end_all())=="-a/one -rot/up -rot/right" && tracker.end_all().empty() && !tracker.held("a/one"));
}

// ---- the hub ----
void the_hub() {
    const auto map=ng();
    const auto id=[&](const char* name) {return *map->resolve(name);};
    const auto t0=Clock::now();
    ButtonHub hub;
    check(!hub.vjoy_word(t0) && hub.take_edges().empty());                             // nothing known
    hub.configure(map,ReadMethod::hid);
    hub.publish(hid(0),t0);
    check(hub.take_edges().empty() && *hub.vjoy_word(t0)==0);
    hub.publish(hid(0b10001,0),t0);                                                    // trigger and rocker up
    check(edge_text(hub.take_edges())=="+"+std::to_string(id("trigger"))+" +"+std::to_string(id("rocker_up")) && hub.take_edges().empty());
    check(*hub.vjoy_word(t0)==((1u<<0)|(1u<<4)));                                      // vJoy buttons are the CAN slot numbers: 1 and 5
    hub.publish(hid(1,5),t0);                                                          // the hat: no CAN slot, so a number from 17 up
    check(*hub.vjoy_word(t0)==((1u<<0)|(1u<<(map->vjoy_button(id("hat_s"))-1))) && map->vjoy_button(id("hat_s"))==21);
    hub.take_edges();
    // HID has no freshness limit: a quiet joystick sends nothing, and its buttons stay as they were.
    check(hub.vjoy_word(t0+std::chrono::seconds(30)).has_value());
    // Losing the source releases everything, and the level is unknown.
    hub.release_all();
    const auto gone=hub.take_edges();
    check(gone.size()==2 && gone[0].released && gone[1].released && !hub.vjoy_word(t0));
    // CanBus is polled: its level goes stale after 250 ms.
    hub.configure(map,ReadMethod::can_bus);
    hub.publish(hid(0),t0);hub.publish(hid(1u<<8),t0);
    check(edge_text(hub.take_edges())=="+"+std::to_string(id("can_button_9")) && *hub.vjoy_word(t0)==(1u<<8));
    check(*hub.vjoy_word(t0+std::chrono::milliseconds(250)) == (1u<<8) && !hub.vjoy_word(t0+std::chrono::milliseconds(251)));
    // Reconfiguring (the yoke's model became known) starts again.
    hub.configure(std::make_shared<const ControlMap>(),ReadMethod::hid);
    check(hub.take_edges().empty() && !hub.vjoy_word(t0) && hub.controls()->size()==38);
    // Many threads publishing and taking at once lose nothing and do not deadlock.
    hub.configure(map,ReadMethod::hid);
    hub.publish(hid(0),t0);
    std::thread writer([&] {for (int i=0;i<500;++i) hub.publish(hid(i&1?1:0),t0);});
    std::size_t seen=0;
    for (int i=0;i<500;++i) {seen+=hub.take_edges().size();hub.vjoy_word(t0);}
    writer.join();
    seen+=hub.take_edges().size();
    check(seen==499);                                                                  // 500 alternating states after a released baseline: 499 edges
}

// ---- the Direct action ----
struct RecordingRunner final : CommandRunner {
    std::vector<std::string> calls;std::set<std::string> unknown;
    bool note(const char* what,const std::string& name) {calls.push_back(std::string(what)+" "+name);return !unknown.count(name);}
    bool once(const std::string& name) override {return note("once",name);}
    bool begin(const std::string& name) override {return note("begin",name);}
    bool end(const std::string& name) override {return note("end",name);}
    std::string take() {std::string text;for (const auto& c:calls) text+=(text.empty()?"":"; ")+c;calls.clear();return text;}
};
void the_direct_action_runs_commands() {
    const auto map=ng();
    std::vector<std::string> notes;
    const auto commands=std::make_shared<const ButtonCommands>(ButtonCommands::resolve({{"trigger",{"a/one","a/two"}},{"trigger_released",{"r/one"}},{"top_left_hold",{"h/one"}},
        {"hid_button_3",{"c/x"}},{"top_right",{"c/y"}},{"hat_n_hold",{"rot/up"}}},*map,ReadMethod::hid,notes));
    check(has_note(notes,"hid_button_3, top_right are the same control"));
    RecordingRunner runner;
    DirectAction action(runner);
    action.set_commands(map,commands);
    ButtonHub hub;hub.configure(map,ReadMethod::hid);
    const Log log=[](const std::string&) {};
    const auto t=Clock::now();
    hub.publish(hid(0),t);action.update(hub,t,log);
    check(runner.take().empty());
    hub.publish(hid(0b1),t);action.update(hub,t,log);                                  // the trigger: its commands once, in order
    check(runner.take()=="once a/one; once a/two");
    hub.publish(hid(0b1),t);action.update(hub,t,log);check(runner.take().empty());   // held: no repeat
    hub.publish(hid(0b0),t);action.update(hub,t,log);check(runner.take()=="once r/one");   // its release
    hub.publish(hid(0b110),t);action.update(hub,t,log);                                // top_left (a hold), top_right (merged with hid_button_3)
    check(runner.take()=="once c/x; once c/y; begin h/one");                           // the same control's two entries, alphabetical: hid_button_3 then top_right
    hub.publish(hid(0b110,1),t);action.update(hub,t,log);check(runner.take()=="begin rot/up");
    hub.publish(hid(0b000,0),t);action.update(hub,t,log);
    check(runner.take()=="end h/one; end rot/up");                                     // every end
    // Unknown commands are skipped without stopping the rest, and a lost source ends what was held.
    runner.unknown={"a/one"};
    hub.publish(hid(0b1),t);action.update(hub,t,log);check(runner.take()=="once a/one; once a/two");
    hub.publish(hid(0b101000,0),t);action.update(hub,t,log);runner.take();
    hub.publish(hid(0b10,0),t);action.update(hub,t,log);runner.take();
    hub.release_all();action.update(hub,t,log);
    check(runner.take()=="end h/one");                                                 // top_left was held when the source went away
    action.close();check(runner.take().empty());                                       // nothing left to end
    // Changing the commands (the yoke's model became known) ends what the old ones held.
    hub.publish(hid(0),t);action.update(hub,t,log);runner.take();                      // a new baseline after the release
    hub.publish(hid(0b10),t);action.update(hub,t,log);
    check(runner.take()=="begin h/one");
    action.set_commands(map,std::make_shared<const ButtonCommands>());
    check(runner.take()=="end h/one");
    hub.publish(hid(0b10,0),t);hub.publish(hid(0),t);action.update(hub,t,log);        // and the new (empty) commands run nothing
    check(runner.take().empty());
}

// ---- the virtual joystick action ----
struct RecordingJoystick final : VirtualJoystick {
    std::vector<std::string> calls;Status state=Status::available;bool missing=false;
    std::string load() override {return missing?"vJoy is not installed":std::string();}
    bool enabled() override {return true;}
    Status status() override {return state;}
    bool acquire() override {state=Status::owned;return true;}
    void relinquish() override {state=Status::available;}
    int buttons() override {return 32;}
    bool set(unsigned button,bool down) override {calls.push_back((down?"down ":"up ")+std::to_string(button));return true;}
    bool reset() override {calls.push_back("reset");return true;}
    void unload() override {calls.push_back("unload");}
};
void the_vjoy_action_presses_a_virtual_joystick() {
    const auto map=ng();
    auto owned=std::make_unique<RecordingJoystick>();
    auto* joystick=owned.get();
    VJoyAction action(std::move(owned));
    ButtonHub hub;hub.configure(map,ReadMethod::hid);
    std::vector<std::string> said;
    const Log log=[&](const std::string& line) {said.push_back(line);};
    const auto t=Clock::now();
    action.update(hub,t,log);                                                          // nothing known: the device is taken, every button up
    check(!said.empty() && said[0].find("pressing vJoy device 1's 32 buttons")!=std::string::npos);
    joystick->calls.clear();
    hub.publish(hid(0),t);hub.publish(hid(0b10001,0),t);action.update(hub,t,log);      // trigger (1) and rocker up (5)
    check((joystick->calls==std::vector<std::string>{"down 1","down 5"}));
    joystick->calls.clear();
    hub.publish(hid(0b10001,5),t);action.update(hub,t,log);                            // the hat: no CAN slot, so vJoy button 17 + 4 = 21
    check((joystick->calls==std::vector<std::string>{"down 21"}));
    joystick->calls.clear();
    hub.release_all();action.update(hub,t,log);                                        // the source went away: all up
    check(joystick->calls==std::vector<std::string>{"reset"});
    action.close();action.unload();
    check(joystick->calls.back()=="unload");
}

// ---- the service ----
struct ServiceRig {
    RecordingRunner runner;
    std::vector<std::string> said;
    Log log=[this](const std::string& line) {said.push_back(line);};
    RecordingJoystick* joystick=nullptr;
    std::unique_ptr<ButtonService> service;
    ServiceRig(ButtonSettings settings,const ButtonCommands::Entries& entries,bool have_vjoy,HidSource::Opener opener=nullptr) {
        if (!opener) opener=[](std::string& why)->std::unique_ptr<hid::JoystickDevice> {why="no joystick interface";return nullptr;};
        service=std::make_unique<ButtonService>(runner,opener,[this,have_vjoy]()->std::unique_ptr<VirtualJoystick> {
            if (!have_vjoy) return nullptr;
            auto j=std::make_unique<RecordingJoystick>();joystick=j.get();return j;
        });
        service->set_commands(entries);
        service->set_yoke("",{},settings,log);   // the placeholder, with the methods a yoke would give
    }
    bool said_part(const char* part) const {return has_note(said,part);}
};
void the_service_chooses_what_to_run() {
    const ButtonCommands::Entries one{{"trigger",{"a/b"}}};
    // The methods are the connected yoke's, applied when its model is known. VJoy without a virtual joystick falls back to Direct and
    // says so; with one it stays.
    {
        ServiceRig rig({ReadMethod::can_bus,ActionMethod::vjoy},one,false);
        check(rig.said.empty());                                                       // the placeholder is quiet, fallback included
        rig.service->start(rig.log);
        rig.service->set_yoke("Brunner CLS-E",{},{ReadMethod::can_bus,ActionMethod::vjoy},rig.log);
        check(rig.service->action_method()==ActionMethod::direct && rig.service->read_method()==ReadMethod::can_bus);
        check(rig.said_part("Brunner CLS-E: read method CanBus, action Direct") && rig.said_part("asks for the VJoy action") && rig.said_part("does not have yet; using Direct"));
    }
    {
        ServiceRig rig({ReadMethod::can_bus,ActionMethod::direct},{},true);
        check(!rig.service->wants_can());                                              // Direct with no commands uses nothing
        rig.service->set_yoke("Brunner CLS-E",{},{ReadMethod::can_bus,ActionMethod::vjoy},rig.log);
        check(rig.service->action_method()==ActionMethod::vjoy && rig.service->wants_can());   // a virtual joystick uses the buttons with no commands at all
        check(rig.said_part("read method CanBus, action VJoy") && !rig.said_part("does not have yet"));
        // A different yoke connects: its own methods replace the first's, and the virtual joystick is let go.
        rig.said.clear();
        rig.service->set_yoke("Brunner CLS-E NG",{},{ReadMethod::can_bus,ActionMethod::direct},rig.log);
        check(rig.service->action_method()==ActionMethod::direct && !rig.service->wants_can() && rig.said_part("Brunner CLS-E NG: read method CanBus, action Direct"));
        rig.service->set_yoke("Brunner CLS-E",{},{ReadMethod::can_bus,ActionMethod::vjoy},rig.log);   // and back
        check(rig.service->action_method()==ActionMethod::vjoy && rig.service->wants_can());
    }
    {   // The read method follows the yoke too: the joystick interface is opened only while the yoke's method is HID.
        int opened=0;
        ServiceRig rig({ReadMethod::can_bus,ActionMethod::direct},one,false,[&](std::string&)->std::unique_ptr<hid::JoystickDevice> {++opened;return nullptr;});
        rig.service->start(rig.log);
        rig.service->set_yoke("Brunner CLS-E",{},{ReadMethod::can_bus,ActionMethod::direct},rig.log);
        std::this_thread::sleep_for(std::chrono::milliseconds(60));
        check(opened==0);
        rig.service->set_yoke("Brunner CLS-E NG",{},{ReadMethod::hid,ActionMethod::direct},rig.log);
        std::this_thread::sleep_for(std::chrono::milliseconds(60));
        check(opened>0 && !rig.service->wants_can());
        rig.service->set_yoke("Brunner CLS-E",{},{ReadMethod::can_bus,ActionMethod::direct},rig.log);
        check(rig.service->wants_can());
        rig.service->stop();
    }
    // The CAN read is only wanted when something uses it: CanBus and (a virtual joystick or some command).
    check(ServiceRig({ReadMethod::can_bus,ActionMethod::direct},one,false).service->wants_can());
    check(!ServiceRig({ReadMethod::can_bus,ActionMethod::direct},{},false).service->wants_can());
    check(!ServiceRig({ReadMethod::hid,ActionMethod::vjoy},{},true).service->wants_can());
    // CanBus never touches the joystick interface: the HID source is not started, so its opener is never called, even with commands bound.
    {
        int opened=0;
        ServiceRig rig({ReadMethod::can_bus,ActionMethod::direct},one,false,[&](std::string&)->std::unique_ptr<hid::JoystickDevice> {++opened;return nullptr;});
        rig.service->start(rig.log);
        for (int i=0;i<6;++i) {std::this_thread::sleep_for(std::chrono::milliseconds(20));rig.service->update(Clock::now(),brunner::Snapshot{},rig.log);}
        rig.service->stop();
        check(opened==0 && !rig.said_part("Joystick buttons"));
    }
    // With nothing to do, no source starts at all and the log says so.
    {
        int opened=0;
        ServiceRig rig({ReadMethod::hid,ActionMethod::direct},{},false,[&](std::string&)->std::unique_ptr<hid::JoystickDevice> {++opened;return nullptr;});
        rig.service->start(rig.log);
        rig.service->set_yoke("Brunner CLS-E NG",{},{ReadMethod::hid,ActionMethod::direct},rig.log);
        std::this_thread::sleep_for(std::chrono::milliseconds(60));
        rig.service->stop();
        check(opened==0 && rig.said_part("not read"));
    }
}
void the_service_resolves_names_when_the_yoke_is_known() {
    // Commands written under the NG's HID names and a friendly name, resolved through the NG's aliases, for the CAN read.
    ServiceRig rig({ReadMethod::can_bus,ActionMethod::direct},{{"hid_button_4",{"x/down"}},{"rocker_up",{"x/up"}},{"hat_n",{"x/hat"}},{"nonsense",{"x/y"}}},false);
    rig.said.clear();
    rig.service->set_yoke("",{},rig.log);                                              // the placeholder before the yoke is known: quiet
    check(rig.said.empty());
    rig.service->start(rig.log);
    rig.said.clear();
    rig.service->set_yoke("Brunner CLS-E NG",ng_aliases(),rig.log);
    check(rig.said_part("\"nonsense\" is not a control of this yoke") && !rig.said_part("has no CAN slot linked"));   // the NG's hat is on the bridge
    check(rig.said_part("Brunner CLS-E NG has 24 controls; 3 with commands"));
    // The CAN read publishes slot 4: it fires hid_button_4's command, because the NG's alias links the two.
    const auto t=Clock::now();
    rig.service->sink().publish({0,0},t);
    rig.service->update(t,brunner::Snapshot{},rig.log);
    rig.service->sink().publish({1u<<3,0},t);
    rig.service->update(t,brunner::Snapshot{},rig.log);
    check(rig.runner.take()=="once x/down");
    rig.service->sink().publish({1u<<4,0},t);                                              // slot 5 is rocker_up
    rig.service->update(t,brunner::Snapshot{},rig.log);
    check(rig.runner.take()=="once x/up");
    rig.service->sink().publish({0,0},t);
    rig.service->update(t,brunner::Snapshot{},rig.log);
    rig.runner.take();
    rig.service->sink().publish({0,1},t);                                                  // the hat pushed up, as the bridge reports it (can_hat_n)
    rig.service->update(t,brunner::Snapshot{},rig.log);
    check(rig.runner.take()=="once x/hat");
    // The CAN refusal of the reader's snapshot is told once, and told again when it clears.
    brunner::Snapshot refused;refused.button_refusal="Controller rejected SDO read";
    rig.said.clear();
    rig.service->update(t,refused,rig.log);rig.service->update(t,refused,rig.log);
    check(rig.said.size()==1 && rig.said_part("Yoke buttons not read: Controller rejected SDO read"));
    brunner::Snapshot fine;fine.buttons=std::uint16_t{0};
    rig.service->update(t,fine,rig.log);
    check(rig.said_part("Yoke buttons read again"));
    // An alias map that does not hold together is reported and the plain names are used.
    rig.said.clear();
    rig.service->set_yoke("Odd",{{"x",{"hid_button_1","hid_button_2"}}},rig.log);
    check(rig.said_part("controls of Odd are not usable") && rig.said_part("Odd has 38 controls"));
    // Stopping releases everything: what was held is ended.
    rig.service->set_yoke("Brunner CLS-E NG",ng_aliases(),rig.log);
    rig.service->stop();
    check(rig.runner.take().empty());
}
void the_service_reads_hid_reports() {
    struct Joystick final : hid::JoystickDevice {
        std::deque<std::vector<std::uint8_t>> reports;
        std::uint16_t vendor() const override {return 0x25bb;}
        std::uint16_t product() const override {return 0x008b;}
        const std::vector<std::uint8_t>& descriptor() const override {return desc;}
        bool read(std::vector<std::uint8_t>& payload,int) override {
            if (reports.empty()) {std::this_thread::sleep_for(std::chrono::milliseconds(5));return false;}
            payload=reports.front();reports.pop_front();return true;
        }
        std::vector<std::uint8_t> desc{0x05,0x01,0x09,0x04,0xa1,0x01,0x05,0x09,0x19,0x01,0x29,0x06,0x15,0x00,0x25,0x01,0x36,0x00,0x00,0x46,0x01,0x00,0x75,0x01,0x95,0x06,0x81,0x02,
            0x75,0x02,0x95,0x01,0x81,0x03,0x05,0x01,0x09,0x39,0x15,0x01,0x25,0x08,0x36,0x00,0x00,0x46,0x3b,0x01,0x66,0x14,0x00,0x55,0x00,0x75,0x04,0x95,0x01,0x81,0x42,
            0x75,0x04,0x95,0x01,0x81,0x03,0x05,0x01,0x15,0x00,0x26,0xff,0xff,0x36,0x00,0x00,0x46,0xff,0xff,0x65,0x00,0x09,0x30,0x75,0x10,0x95,0x01,0x81,0x02,0x09,0x31,0x75,0x10,0x95,0x01,0x81,0x02,0xc0};
    };
    std::atomic<int> opens{0};
    Joystick* raw=nullptr;
    ServiceRig rig({ReadMethod::hid,ActionMethod::direct},{{"trigger",{"a/b"}},{"hat_e_hold",{"rot/right"}}},false,[&](std::string& why)->std::unique_ptr<hid::JoystickDevice> {
        if (opens++==0) {auto j=std::make_unique<Joystick>();raw=j.get();return j;}
        why="no joystick interface";return nullptr;
    });
    rig.service->set_yoke("Brunner CLS-E NG",ng_aliases(),rig.log);
    rig.service->start(rig.log);
    const auto wait_for_source=[&] {for (int i=0;i<200 && !raw;++i) std::this_thread::sleep_for(std::chrono::milliseconds(5));};
    wait_for_source();
    check(raw!=nullptr);
    // The device is read from its own thread, so what is pushed here is seen a moment later: poll the service until it has run.
    raw->reports={{0,0,0,0,0,0},{1,0,0,0,0,0},{0x7d,0,5,0,0,0},{0,0,0,0,0,0},{0,3,0,0,0,0}};
    std::string seen;
    for (int i=0;i<200;++i) {
        rig.service->update(Clock::now(),brunner::Snapshot{},rig.log);
        seen+=rig.runner.take()+";";
        if (seen.find("begin rot/right")!=std::string::npos) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    check(seen.find("once a/b")!=std::string::npos && seen.find("begin rot/right")!=std::string::npos);   // the glitch (0x7d) in between fired nothing
    rig.service->stop();
    check(rig.runner.take()=="end rot/right");                                          // stopping ends the held hat
}
}

int main() {
    try {
        canonical_names();
        no_aliases_every_input_is_its_own_control();
        aliases_link_hid_and_can();
        a_hat_can_be_linked_later();
        aliases_chain_and_nest();
        bad_aliases_are_refused();
        the_methods();
        edges_over_a_stream();
        both_read_methods_name_the_same_control();
        commands_resolve_through_names();
        commands_merge_and_say_so();
        commands_note_what_cannot_be_seen_or_used();
        holds_are_kept_per_command();
        the_hub();
        the_direct_action_runs_commands();
        the_vjoy_action_presses_a_virtual_joystick();
        the_service_chooses_what_to_run();
        the_service_resolves_names_when_the_yoke_is_known();
        the_service_reads_hid_reports();
        std::cout<<"buttons tests passed\n";
        return 0;
    } catch (const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
