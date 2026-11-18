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
    check(input_name({InputKind::button,3})=="button_3" && input_name({InputKind::button,16})=="button_16");
    check(input_name({InputKind::hat,1})=="hat_n" && input_name({InputKind::hat,8})=="hat_nw");
    check(all_inputs().size()==24);
    for (const auto& input:all_inputs()) check(parse_input(input_name(input))==input);
    check(parse_input("Button_4")==Input{InputKind::button,4} && parse_input("BUTTON_16")==Input{InputKind::button,16} && parse_input("Hat_SE")==Input{InputKind::hat,4});
    for (const char* bad:{"","button_0","button_17","button_","button_01x","button_1x","hat_","hat_up","trigger","hid_button_1","can_button_1"})
        check(!parse_input(bad));
    check(valid_friendly_name("trigger") && valid_friendly_name("left_thumb_2") && valid_friendly_name("TRIGGER") && valid_friendly_name("hid_thumb"));
    for (const char* bad:{"","x_released","x_hold","has space","dash-ed","hat_n","button_1","a123456789012345678901234567890123"})
        check(!valid_friendly_name(bad));
}
void no_aliases_every_input_is_its_own_control() {
    const ControlMap map;
    check(map.size()==24);
    for (const auto& input:all_inputs()) {
        const int control=map.control_of(input);
        check(map.resolve(input_name(input))==control && map.names(control).size()==1 && map.input_of(control)==input);
    }
    check(*map.resolve("BUTTON_2")==map.control_of({InputKind::button,2}));
    check(!map.resolve("trigger") && !map.resolve("hid_button_1") && !map.resolve(""));
    // The numbering: buttons, then hat positions.
    check(map.control_of({InputKind::button,1})==0 && map.control_of({InputKind::button,16})==15 && map.control_of({InputKind::hat,1})==16 && map.control_of({InputKind::hat,8})==23);
    // vJoy: a button is its own number, the hat positions take 17 up in order.
    check(map.vjoy_button(map.control_of({InputKind::button,5}))==5 && map.vjoy_button(map.control_of({InputKind::button,16}))==16);
    check(map.vjoy_button(map.control_of({InputKind::hat,1}))==17 && map.vjoy_button(map.control_of({InputKind::hat,8}))==24);
}
ControlMap::Aliases ng_aliases() {
    ControlMap::Aliases a;
    const char* names[]={"trigger","top_left","top_right","rocker_down","rocker_up","hat_push"};
    for (int n=1;n<=6;++n) a["button_"+std::to_string(n)]={names[n-1]};
    return a;
}
void aliases_name_controls() {
    const ControlMap map(ng_aliases());
    check(map.size()==24);                                                            // naming a control does not add one
    const int trigger=*map.resolve("trigger");
    check(*map.resolve("button_1")==trigger && *map.resolve("TRIGGER")==trigger && map.control_of({InputKind::button,1})==trigger);
    const auto names=map.names(trigger);
    check(names.size()==2 && names[0]=="button_1" && names[1]=="trigger");
    check(map.vjoy_button(trigger)==1 && map.vjoy_button(*map.resolve("rocker_up"))==5 && map.vjoy_button(*map.resolve("hat_push"))==6);
    check(map.vjoy_button(*map.resolve("hat_n"))==17 && map.vjoy_button(*map.resolve("hat_nw"))==24);
}
void a_hat_position_can_be_named() {
    auto a=ng_aliases();
    a["hat_n"]={"up","north"};                                                        // a control can have several names
    const ControlMap map(a);
    const int hat_n=*map.resolve("hat_n");
    check(*map.resolve("up")==hat_n && *map.resolve("NORTH")==hat_n && map.input_of(hat_n)==Input{InputKind::hat,1} && map.vjoy_button(hat_n)==17);
    check((map.names(hat_n)==std::vector<std::string>{"hat_n","north","up"}));        // the control's own name, then the friendly ones alphabetically
    check(map.size()==24);
}
void aliases_are_matched_in_any_case() {
    ControlMap::Aliases a;
    a["BUTTON_1"]={"Trigger","Left_Index"};
    const ControlMap map(a);
    check(*map.resolve("trigger")==*map.resolve("left_index") && *map.resolve("TRIGGER")==*map.resolve("button_1") && map.names(0).size()==3);
}
void bad_aliases_are_refused() {
    check(refuses({{"trigger",{"button_1"}}},"not a button or hat name"));                // the key is the control, not the friendly name
    check(refuses({{"hid_button_1",{"x"}}},"not a button or hat name") && refuses({{"button_17",{"x"}}},"not a button or hat name") && refuses({{"",{"x"}}}));
    check(refuses({{"button_1",{"x_released"}}},"not a usable name") && refuses({{"button_1",{"x_hold"}}},"not a usable name"));
    check(refuses({{"button_1",{"has space"}}},"not a usable name") && refuses({{"button_1",{""}}},"not a usable name"));
    check(refuses({{"button_1",{"a123456789012345678901234567890123"}}},"not a usable name"));
    check(refuses({{"button_1",{"button_2"}}},"not a usable name") && refuses({{"button_1",{"hat_n"}}},"not a usable name"));   // a canonical name is not a friendly one
    check(refuses({{"button_1",{"x","X"}}},"given twice"));                              // twice for one control
    check(refuses({{"button_1",{"x"}},{"button_2",{"x"}}},"given twice"));              // for two controls
    check(refuses({{"button_1",{"x"}},{"BUTTON_1",{"y"}}},"given twice"));              // a key twice, in two cases
    check(ControlMap({{"button_1",{}}}).size()==24);                                   // no names is fine
}

// ---- the setting ----
void the_action_methods() {
    check(parse_action_method("VJoy")==ActionMethod::vjoy && parse_action_method("vjoy")==ActionMethod::vjoy && parse_action_method("DIRECT")==ActionMethod::direct);
    check(parse_action_method("Disabled")==ActionMethod::disabled && parse_action_method("disabled")==ActionMethod::disabled && parse_action_method("DISABLED")==ActionMethod::disabled);
    for (const char* bad:{"","can bus","hid","canbus","usb","0","1","vjoy ","direct1","none","None","disable","disabled "}) check(!parse_action_method(bad));
    check(std::string(action_method_name(ActionMethod::vjoy))=="VJoy" && std::string(action_method_name(ActionMethod::direct))=="Direct"
        && std::string(action_method_name(ActionMethod::disabled))=="Disabled");
    // The owner's choice in the state file beats the model's; with none, the model's stands.
    check(effective_action_method(ActionMethod::vjoy,std::nullopt)==ActionMethod::vjoy);
    check(effective_action_method(ActionMethod::vjoy,ActionMethod::disabled)==ActionMethod::disabled);
    check(effective_action_method(ActionMethod::disabled,ActionMethod::direct)==ActionMethod::direct);
    // What a model that gives none is, on every operating system (the shipped models say their own: config/yoke/preset_tests.cpp).
    static_assert(default_action_method==ActionMethod::direct,"a model that says nothing runs commands");
}

// ---- edges ----
std::shared_ptr<const ControlMap> ng() {return std::make_shared<const ControlMap>(ng_aliases());}
ButtonState state(std::uint32_t buttons,int hat=0) {return {buttons,hat};}
std::string edge_text(const std::vector<Edge>& edges) {
    std::string text;
    for (const auto& e:edges) text+=(text.empty()?"":" ")+std::string(e.released?"-":"+")+std::to_string(e.control);
    return text;
}
void edges_over_a_stream() {
    const auto map=ng();
    const auto id=[&](const char* name) {return *map->resolve(name);};
    EdgeDetector detector(map);
    check(detector.feed(state(0)).empty());                                           // baseline
    check(edge_text(detector.feed(state(1)))=="+"+std::to_string(id("trigger")));
    check(detector.feed(state(1)).empty());                                           // held: no repeat
    check(edge_text(detector.feed(state(0)))=="-"+std::to_string(id("trigger")));    // its own release edge
    check(edge_text(detector.feed(state(0,3)))=="+"+std::to_string(id("hat_e")));
    check(detector.feed(state(0,3)).empty());
    check(edge_text(detector.feed(state(0,0)))=="-"+std::to_string(id("hat_e")));    // centring releases the position it left
    detector.feed(state(0,2));
    check(edge_text(detector.feed(state(0,4)))=="-"+std::to_string(id("hat_ne"))+" +"+std::to_string(id("hat_se")));   // one position to the next
    check(edge_text(detector.feed(state(0b101,0)))=="-"+std::to_string(id("hat_se"))+" +"+std::to_string(id("trigger"))+" +"+std::to_string(id("top_right")));   // releases first
    // A button held when the stream starts is a baseline, not a press, but its release is seen.
    EdgeDetector held(map);
    check(held.feed(state(2)).empty());
    check(edge_text(held.feed(state(0)))=="-"+std::to_string(id("top_left")));
    // Everything last seen pressed is released at once when the source goes away, and the next state is a baseline again.
    EdgeDetector gone(map);
    gone.feed(state(0));gone.feed(state(0b110,6));   // buttons 2 and 3, hat position 6 (south-west)
    check(edge_text(gone.release_all())=="-"+std::to_string(id("top_left"))+" -"+std::to_string(id("top_right"))+" -"+std::to_string(id("hat_sw")));
    check(gone.release_all().empty() && gone.feed(state(1)).empty());
}
void all_sixteen_buttons_and_the_hat_are_seen() {
    const auto map=ng();
    EdgeDetector detector(map);
    detector.feed(state(0));
    // A button the NG has no name for is still a control; the 16 slots are the limit.
    check(edge_text(detector.feed(state(1u<<8)))=="+"+std::to_string(*map->resolve("button_9")));
    check(edge_text(detector.feed(state(1u<<8,5)))=="+"+std::to_string(*map->resolve("hat_s")));
    check(edge_text(detector.feed(state(1u<<8,0)))=="-"+std::to_string(*map->resolve("hat_s")));
    check(!edge_text(detector.feed(state(0xffffu))).empty() && detector.feed(state(0x1ffffu)).empty());   // bit 17 is no button
    check(pressed_controls(*map,state(0b11,8))==(std::vector<int>{*map->resolve("trigger"),*map->resolve("top_left"),*map->resolve("hat_nw")}));
}

// ---- command resolution ----
bool has_note(const std::vector<std::string>& notes,const char* part) {
    for (const auto& n:notes) if (n.find(part)!=std::string::npos) return true;
    return false;
}
void commands_resolve_through_names() {
    const auto map=ng();
    std::vector<std::string> notes;
    const auto commands=ButtonCommands::resolve({{"trigger",{"a/one"}},{"top_left_released",{"b/two"}},{"ROCKER_UP_HOLD",{"c/three","c/four"}},{"hat_n",{"d/five"}}},*map,notes);
    check(commands.size()==4);
    check(commands.commands_for(*map->resolve("trigger"),Variant::press)==std::vector<std::string>{"a/one"});
    check(commands.commands_for(*map->resolve("top_left"),Variant::release)==std::vector<std::string>{"b/two"});
    check(commands.commands_for(*map->resolve("rocker_up"),Variant::hold)==(std::vector<std::string>{"c/three","c/four"}));
    check(commands.commands_for(*map->resolve("hat_n"),Variant::press)==std::vector<std::string>{"d/five"});
    check(commands.commands_for(*map->resolve("trigger"),Variant::release).empty() && commands.commands_for(*map->resolve("top_right"),Variant::press).empty());
    check(notes.empty());
    // A command written under any name of a control: the button's own name and the friendly name are one entry.
    const auto any=ButtonCommands::resolve({{"button_4",{"x/y"}}},*map,notes);
    check(any.commands_for(*map->resolve("rocker_down"),Variant::press)==std::vector<std::string>{"x/y"});
    check(notes.empty());
}
void commands_merge_and_say_so() {
    const auto map=ng();
    std::vector<std::string> notes;
    const auto commands=ButtonCommands::resolve({{"trigger",{"c/third"}},{"button_1",{"a/first"}},{"trigger_hold",{"h/one"}}},*map,notes);
    // All of them run, in the alphabetical order of the entry names (button_1, trigger).
    check(commands.commands_for(*map->resolve("trigger"),Variant::press)==(std::vector<std::string>{"a/first","c/third"}));
    check(notes.size()==1 && has_note(notes,"button_1, trigger are the same control") && has_note(notes,"all of their commands run"));
    // The same control under different variants is not a collision.
    notes.clear();
    ButtonCommands::resolve({{"trigger",{"a/b"}},{"button_1_released",{"c/d"}},{"button_1_hold",{"e/f"}}},*map,notes);
    check(notes.empty());
    // Empty entries do not collide.
    ButtonCommands::resolve({{"trigger",{"a/b"}},{"button_1",{}},{"BUTTON_1",{""}}},*map,notes);
    check(notes.empty());
}
void commands_note_what_cannot_be_used() {
    const auto map=ng();
    std::vector<std::string> notes;
    const auto c=ButtonCommands::resolve({{"nonsense",{"g/h"}},{"hid_button_1",{"i/j"}},{"button_17",{"k/l"}},
        {"trigger_hold",{"has space"}},{"top_left",{"ok/ok","bad name"}}},*map,notes);
    check(has_note(notes,"\"nonsense\" is not a control of this yoke") && has_note(notes,"\"hid_button_1\" is not a control") && has_note(notes,"\"button_17\" is not a control"));
    check(has_note(notes,"trigger_hold has \"has space\"") && has_note(notes,"top_left has \"bad name\""));
    check(notes.size()==5);
    check(c.commands_for(*map->resolve("top_left"),Variant::press)==std::vector<std::string>{"ok/ok"} && c.commands_for(*map->resolve("trigger"),Variant::hold).empty());
}

// ---- an aircraft's commands override, they do not add ----
void an_aircraft_overrides_the_state_file() {
    const auto map=ng();
    std::vector<std::string> notes;
    const ButtonCommands::Entries base{{"trigger",{"base/trigger"}},{"top_left",{"base/left"}},{"top_left_hold",{"base/left_hold"}},{"rocker_up",{"base/up"}},{"hat_n",{"base/n"}}};
    const auto id=[&](const char* name) {return *map->resolve(name);};
    // Replaces for that control and variant, and leaves the rest as the state file had them.
    auto c=ButtonCommands::resolve(base,{{"trigger",{"air/trigger"}},{"hat_n_hold",{"air/hat_hold"}}},"aircraft C172",*map,notes);
    check(c.commands_for(id("trigger"),Variant::press)==std::vector<std::string>{"air/trigger"});            // replaced, not "base/trigger" then "air/trigger"
    check(c.commands_for(id("top_left"),Variant::press)==std::vector<std::string>{"base/left"});            // not mentioned: the state file's
    check(c.commands_for(id("top_left"),Variant::hold)==std::vector<std::string>{"base/left_hold"});
    check(c.commands_for(id("hat_n"),Variant::press)==std::vector<std::string>{"base/n"});                  // the hold override leaves the press alone
    check(c.commands_for(id("hat_n"),Variant::hold)==std::vector<std::string>{"air/hat_hold"});             // and an aircraft entry the state file lacks is used
    check(c.size()==6);                                                                                     // 5 from the state file, one added by a variant it lacked, one replaced
    check(has_note(notes,"aircraft C172 button_commands: replace the state file's commands for 1 control"));
    // Two names of one control are one control: the aircraft's "button_1" replaces the state file's "trigger".
    notes.clear();
    c=ButtonCommands::resolve(base,{{"button_1",{"air/one"}}},"aircraft C172",*map,notes);
    check(c.commands_for(id("trigger"),Variant::press)==std::vector<std::string>{"air/one"});
    // Several commands replace several; an empty entry (null, "" or []) removes the control's command on this aircraft.
    notes.clear();
    c=ButtonCommands::resolve(base,{{"rocker_up",{"air/a","air/b"}},{"top_left",{}},{"top_left_hold",{""}}},"aircraft C172",*map,notes);
    check(c.commands_for(id("rocker_up"),Variant::press)==(std::vector<std::string>{"air/a","air/b"}));
    check(c.commands_for(id("top_left"),Variant::press).empty() && c.commands_for(id("top_left"),Variant::hold).empty());
    check(c.commands_for(id("trigger"),Variant::press)==std::vector<std::string>{"base/trigger"});
    check(has_note(notes,"replace the state file's commands for 3 controls"));
    // No aircraft entries, or an aircraft that names nothing the yoke has: exactly the state file's.
    notes.clear();
    c=ButtonCommands::resolve(base,{},"aircraft C172",*map,notes);
    check(c.size()==5 && notes.empty());
    c=ButtonCommands::resolve(base,{{"nonsense",{"air/x"}},{"button_17",{"air/y"}}},"aircraft C172",*map,notes);
    check(c.size()==5 && has_note(notes,"aircraft C172 button_commands: \"nonsense\" is not a control of this yoke") && !has_note(notes,"replace the state file's"));
    // The aircraft's own entries merge among themselves like any, with their own label on the note; the state file's are untouched.
    notes.clear();
    c=ButtonCommands::resolve(base,{{"trigger",{"air/b"}},{"button_1",{"air/a"}}},"aircraft C172",*map,notes);
    check(c.commands_for(id("trigger"),Variant::press)==(std::vector<std::string>{"air/a","air/b"}));       // alphabetical by entry name, like the state file's
    check(has_note(notes,"aircraft C172 button_commands: button_1, trigger are the same control"));
    // The three entries of a control are separate: overriding one never unsets or replaces the other two. A press override leaves the state
    // file's `_released` and `_hold` running; each of them is replaced (or switched off with null) only by an entry of its own.
    notes.clear();
    const ButtonCommands::Entries three{{"trigger",{"base/press"}},{"trigger_released",{"base/release"}},{"trigger_hold",{"base/hold"}}};
    c=ButtonCommands::resolve(three,{{"trigger",{"air/press"}}},"aircraft C172",*map,notes);
    check(c.commands_for(id("trigger"),Variant::press)==std::vector<std::string>{"air/press"});
    check(c.commands_for(id("trigger"),Variant::release)==std::vector<std::string>{"base/release"} && c.commands_for(id("trigger"),Variant::hold)==std::vector<std::string>{"base/hold"});
    c=ButtonCommands::resolve(three,{{"trigger_released",{"air/release"}}},"aircraft C172",*map,notes);                  // a release override leaves the press and the hold
    check(c.commands_for(id("trigger"),Variant::release)==std::vector<std::string>{"air/release"});
    check(c.commands_for(id("trigger"),Variant::press)==std::vector<std::string>{"base/press"} && c.commands_for(id("trigger"),Variant::hold)==std::vector<std::string>{"base/hold"});
    c=ButtonCommands::resolve(three,{{"trigger_hold",{"air/hold"}}},"aircraft C172",*map,notes);                         // and so does a hold override
    check(c.commands_for(id("trigger"),Variant::hold)==std::vector<std::string>{"air/hold"});
    check(c.commands_for(id("trigger"),Variant::press)==std::vector<std::string>{"base/press"} && c.commands_for(id("trigger"),Variant::release)==std::vector<std::string>{"base/release"});
    c=ButtonCommands::resolve(three,{{"trigger",{}},{"trigger_hold",{}}},"aircraft C172",*map,notes);                    // switching off is per entry too: the release stays
    check(c.commands_for(id("trigger"),Variant::press).empty() && c.commands_for(id("trigger"),Variant::hold).empty());
    check(c.commands_for(id("trigger"),Variant::release)==std::vector<std::string>{"base/release"});
    // A state file with nothing, and an aircraft: the aircraft's commands stand alone.
    notes.clear();
    c=ButtonCommands::resolve({},{{"trigger",{"air/trigger"}}},"aircraft C172",*map,notes);
    check(c.size()==1 && c.commands_for(id("trigger"),Variant::press)==std::vector<std::string>{"air/trigger"} && notes.empty());
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
        {"hat_n_hold",{"rot/up"}},{"hat_ne_hold",{"rot/up","rot/right"}},{"hat_e_hold",{"rot/right"}},{"hat_nw_hold",{"rot/up","rot/left"}},{"rocker_down",{"e/five"}}},*map,notes);
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
    hub.configure(map);
    hub.publish(state(0),t0);
    check(hub.take_edges().empty() && *hub.vjoy_word(t0)==0);
    hub.publish(state(0b10001,0),t0);                                                  // trigger and rocker up
    check(edge_text(hub.take_edges())=="+"+std::to_string(id("trigger"))+" +"+std::to_string(id("rocker_up")) && hub.take_edges().empty());
    check(*hub.vjoy_word(t0)==((1u<<0)|(1u<<4)));                                      // vJoy buttons are the button numbers: 1 and 5
    hub.publish(state(1,5),t0);                                                        // the hat: a number from 17 up
    check(*hub.vjoy_word(t0)==((1u<<0)|(1u<<(map->vjoy_button(id("hat_s"))-1))) && map->vjoy_button(id("hat_s"))==21);
    hub.take_edges();
    // The registers are read every sampling cycle, so the level goes stale after 250 ms.
    check(*hub.vjoy_word(t0+std::chrono::milliseconds(250))==((1u<<0)|(1u<<20)) && !hub.vjoy_word(t0+std::chrono::milliseconds(251)));
    // Losing the source releases everything, and the level is unknown.
    hub.release_all();
    const auto gone=hub.take_edges();
    check(gone.size()==2 && gone[0].released && gone[1].released && !hub.vjoy_word(t0));
    // Reconfiguring (the yoke's model became known) starts again.
    hub.publish(state(0),t0);hub.publish(state(1u<<8),t0);
    check(edge_text(hub.take_edges())=="+"+std::to_string(id("button_9")) && *hub.vjoy_word(t0)==(1u<<8));
    hub.configure(std::make_shared<const ControlMap>());
    check(hub.take_edges().empty() && !hub.vjoy_word(t0) && hub.controls()->size()==24);
    // Many threads publishing and taking at once lose nothing and do not deadlock.
    hub.configure(map);
    hub.publish(state(0),t0);
    std::thread writer([&] {for (int i=0;i<500;++i) hub.publish(state(i&1?1:0),t0);});
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
        {"button_3",{"c/x"}},{"top_right",{"c/y"}},{"hat_n_hold",{"rot/up"}}},*map,notes));
    check(has_note(notes,"button_3, top_right are the same control"));
    RecordingRunner runner;
    DirectAction action(runner);
    action.set_commands(map,commands);
    ButtonHub hub;hub.configure(map);
    const Log log=[](const std::string&) {};
    const auto t=Clock::now();
    hub.publish(state(0),t);action.update(hub,t,log);
    check(runner.take().empty());
    hub.publish(state(0b1),t);action.update(hub,t,log);                                // the trigger: its commands once, in order
    check(runner.take()=="once a/one; once a/two");
    hub.publish(state(0b1),t);action.update(hub,t,log);check(runner.take().empty());   // held: no repeat
    hub.publish(state(0b0),t);action.update(hub,t,log);check(runner.take()=="once r/one");   // its release
    hub.publish(state(0b110),t);action.update(hub,t,log);                              // top_left (a hold), top_right (merged with button_3)
    check(runner.take()=="once c/x; once c/y; begin h/one");                           // the same control's two entries, alphabetical: button_3 then top_right
    hub.publish(state(0b110,1),t);action.update(hub,t,log);check(runner.take()=="begin rot/up");
    hub.publish(state(0b000,0),t);action.update(hub,t,log);
    check(runner.take()=="end h/one; end rot/up");                                     // every end
    // Unknown commands are skipped without stopping the rest, and a lost source ends what was held.
    runner.unknown={"a/one"};
    hub.publish(state(0b1),t);action.update(hub,t,log);check(runner.take()=="once a/one; once a/two");
    hub.publish(state(0b101000,0),t);action.update(hub,t,log);runner.take();
    hub.publish(state(0b10,0),t);action.update(hub,t,log);runner.take();
    hub.release_all();action.update(hub,t,log);
    check(runner.take()=="end h/one");                                                 // top_left was held when the source went away
    action.close();check(runner.take().empty());                                       // nothing left to end
    // Changing the commands (the yoke's model became known) ends what the old ones held.
    hub.publish(state(0),t);action.update(hub,t,log);runner.take();                    // a new baseline after the release
    hub.publish(state(0b10),t);action.update(hub,t,log);
    check(runner.take()=="begin h/one");
    action.set_commands(map,std::make_shared<const ButtonCommands>());
    check(runner.take()=="end h/one");
    hub.publish(state(0b10,0),t);hub.publish(state(0),t);action.update(hub,t,log);     // and the new (empty) commands run nothing
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
    ButtonHub hub;hub.configure(map);
    std::vector<std::string> said;
    const Log log=[&](const std::string& line) {said.push_back(line);};
    const auto t=Clock::now();
    action.update(hub,t,log);                                                          // nothing known: the device is taken, every button up
    check(!said.empty() && said[0].find("pressing vJoy device 1's 32 buttons")!=std::string::npos);
    joystick->calls.clear();
    hub.publish(state(0),t);hub.publish(state(0b10001,0),t);action.update(hub,t,log);  // trigger (1) and rocker up (5)
    check((joystick->calls==std::vector<std::string>{"down 1","down 5"}));
    joystick->calls.clear();
    hub.publish(state(0b10001,5),t);action.update(hub,t,log);                          // the hat: vJoy button 17 + 4 = 21
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
    ServiceRig(ActionMethod action,const ButtonCommands::Entries& entries,bool have_vjoy) {
        service=std::make_unique<ButtonService>(runner,[this,have_vjoy]()->std::unique_ptr<VirtualJoystick> {
            if (!have_vjoy) return nullptr;
            auto j=std::make_unique<RecordingJoystick>();joystick=j.get();return j;
        });
        service->set_commands(entries);
        service->set_yoke("",{},action,log);   // the placeholder, with the action a yoke would give
    }
    bool said_part(const char* part) const {return has_note(said,part);}
};
void the_service_chooses_what_to_run() {
    const ButtonCommands::Entries one{{"trigger",{"a/b"}}};
    // The action is the connected yoke's, applied when its model is known. VJoy without a virtual joystick falls back to Direct and
    // says so; with one it stays.
    {
        ServiceRig rig(ActionMethod::vjoy,one,false);
        check(rig.said.empty());                                                       // the placeholder is quiet, fallback included
        rig.service->start(rig.log);
        rig.service->set_yoke("Brunner CLS-E",{},ActionMethod::vjoy,rig.log);
        check(rig.service->action_method()==ActionMethod::direct);
        check(rig.said_part("Brunner CLS-E: action Direct") && rig.said_part("asks for the VJoy action") && rig.said_part("does not have yet; using Direct"));
    }
    {
        ServiceRig rig(ActionMethod::direct,{},true);
        check(!rig.service->wants_buttons());                                          // Direct with no commands uses nothing
        rig.service->set_yoke("Brunner CLS-E",{},ActionMethod::vjoy,rig.log);
        check(rig.service->action_method()==ActionMethod::vjoy && rig.service->wants_buttons());   // a virtual joystick uses the buttons with no commands at all
        check(rig.said_part("Brunner CLS-E: action VJoy") && !rig.said_part("does not have yet"));
        // A different yoke connects: its own action replaces the first's, and the virtual joystick is let go.
        rig.said.clear();
        rig.service->set_yoke("Brunner CLS-E NG",{},ActionMethod::direct,rig.log);
        check(rig.service->action_method()==ActionMethod::direct && !rig.service->wants_buttons() && rig.said_part("Brunner CLS-E NG: action Direct"));
        rig.service->set_yoke("Brunner CLS-E",{},ActionMethod::vjoy,rig.log);          // and back
        check(rig.service->action_method()==ActionMethod::vjoy && rig.service->wants_buttons());
    }
    // The buttons are only read when something uses them: a virtual joystick or some command.
    check(ServiceRig(ActionMethod::direct,one,false).service->wants_buttons());
    check(!ServiceRig(ActionMethod::direct,{},false).service->wants_buttons());
    check(ServiceRig(ActionMethod::vjoy,{},true).service->wants_buttons());
    // With nothing to do, nothing is read and the log says so.
    {
        ServiceRig rig(ActionMethod::direct,{},false);
        rig.service->start(rig.log);
        rig.service->set_yoke("Brunner CLS-E NG",{},ActionMethod::direct,rig.log);
        check(rig.said_part("not read") && !rig.service->wants_buttons());
    }
}
// Disabled: nothing is read, pressed or run, whatever commands exist; a held command is ended when it takes effect.
void the_disabled_method_reads_and_runs_nothing() {
    const ButtonCommands::Entries held{{"trigger_hold",{"h/one"}}};
    ServiceRig rig(ActionMethod::direct,held,true);
    rig.service->start(rig.log);
    rig.service->set_yoke("Brunner CLS-E NG",ng_aliases(),ActionMethod::direct,rig.log);
    check(rig.service->wants_buttons());
    const auto t=Clock::now();
    const auto press=[&](std::uint32_t bits) {rig.service->sink().publish({bits,0},t);rig.service->update(t,brunner::Snapshot{},rig.log);};
    press(0);rig.runner.take();
    press(0b01);
    check(rig.runner.take()=="begin h/one");
    rig.said.clear();
    rig.service->set_yoke("Brunner CLS-E NG",ng_aliases(),ActionMethod::disabled,rig.log);
    check(rig.runner.take()=="end h/one");                                             // what was held is ended
    check(rig.service->action_method()==ActionMethod::disabled && !rig.service->wants_buttons());
    check(rig.said_part("Brunner CLS-E NG: action Disabled") && rig.said_part("the buttons are disabled; button_commands are ignored"));
    check(!rig.said_part("with commands"));                                            // no summary of commands that will not run
    press(0);press(0b01);press(0);
    check(rig.runner.take().empty());                                                  // nothing runs, though the commands are still there
    check(rig.joystick==nullptr);                                                      // and no virtual joystick was made
    // An aircraft's commands are ignored too, and the log says so once for the file.
    rig.said.clear();
    rig.service->set_aircraft("aircraft C172",{{"trigger",{"air/trigger"}}},rig.log);
    check(rig.said_part("aircraft C172: the buttons are disabled; its button_commands are ignored") && !rig.service->wants_buttons());
    press(0b01);
    check(rig.runner.take().empty());
    // Back to Direct: the commands work again.
    rig.service->set_aircraft("",{},rig.log);
    rig.service->set_yoke("Brunner CLS-E NG",ng_aliases(),ActionMethod::direct,rig.log);
    check(rig.service->action_method()==ActionMethod::direct && rig.service->wants_buttons());
    press(0);rig.runner.take();press(0b01);
    check(rig.runner.take()=="begin h/one");
    // Without any commands the log says the yoke's buttons are not read.
    ServiceRig bare(ActionMethod::direct,{},true);
    bare.service->start(bare.log);
    bare.said.clear();
    bare.service->set_yoke("Brunner CLS-E NG",ng_aliases(),ActionMethod::disabled,bare.log);
    check(bare.said_part("the buttons are disabled; the yoke's buttons are not read") && !bare.service->wants_buttons());
    // From a virtual joystick: it is let go and unloaded, and a fallback never changes Disabled.
    ServiceRig vjoy(ActionMethod::vjoy,{},true);
    vjoy.service->start(vjoy.log);
    vjoy.service->set_yoke("Brunner CLS-E",{},ActionMethod::vjoy,vjoy.log);
    check(vjoy.service->wants_buttons() && vjoy.joystick);
    vjoy.service->set_yoke("Brunner CLS-E",{},ActionMethod::disabled,vjoy.log);
    check(vjoy.service->action_method()==ActionMethod::disabled && !vjoy.service->wants_buttons());
    check(!vjoy.said_part("does not have yet"));
}
void the_service_applies_an_aircrafts_commands() {
    ServiceRig rig(ActionMethod::direct,{{"button_1",{"base/trigger"}},{"button_2",{"base/left"}}},false);
    rig.service->start(rig.log);
    rig.service->set_yoke("Brunner CLS-E NG",ng_aliases(),rig.log);
    const auto t=Clock::now();
    const auto press=[&](std::uint32_t bits) {rig.service->sink().publish({bits,0},t);rig.service->update(t,brunner::Snapshot{},rig.log);};
    press(0);press(0b01);
    check(rig.runner.take()=="once base/trigger");
    press(0);rig.runner.take();
    // An aircraft loads: its commands replace the state file's for that control, at once, and the others stay.
    rig.said.clear();
    rig.service->set_aircraft("aircraft C172",{{"trigger",{"air/trigger"}}},rig.log);
    check(rig.said_part("aircraft C172: 2 controls with commands") && rig.said_part("replace the state file's commands for 1 control"));
    press(0);press(0b01);
    check(rig.runner.take()=="once air/trigger");
    press(0);press(0b10);
    check(rig.runner.take()=="once base/left");
    press(0);
    // Another aircraft, with no file: back to the state file's.
    rig.service->set_aircraft("",{},rig.log);
    press(0b01);
    check(rig.runner.take()=="once base/trigger");
    // An aircraft's commands alone are enough to want the buttons read.
    ServiceRig empty(ActionMethod::direct,{},false);
    empty.service->set_yoke("Brunner CLS-E NG",ng_aliases(),empty.log);
    check(!empty.service->wants_buttons());
    empty.service->set_aircraft("aircraft C172",{{"trigger",{"air/trigger"}}},empty.log);
    check(empty.service->wants_buttons());
    empty.service->set_aircraft("",{},empty.log);
    check(!empty.service->wants_buttons());
    // Before the yoke is known the aircraft's notes and summary are held back, as the state file's are.
    ServiceRig early(ActionMethod::direct,{{"button_1",{"base/trigger"}}},false);
    early.said.clear();
    early.service->set_aircraft("aircraft C172",{{"trigger",{"air/trigger"}}},early.log);
    check(early.said.empty());
}
void the_service_resolves_names_when_the_yoke_is_known() {
    // Commands written under a button's own name and a friendly name, resolved through the NG's aliases.
    ServiceRig rig(ActionMethod::direct,{{"button_4",{"x/down"}},{"rocker_up",{"x/up"}},{"hat_n",{"x/hat"}},{"nonsense",{"x/y"}}},false);
    rig.said.clear();
    rig.service->set_yoke("",{},rig.log);                                              // the placeholder before the yoke is known: quiet
    check(rig.said.empty());
    rig.service->start(rig.log);
    rig.said.clear();
    rig.service->set_yoke("Brunner CLS-E NG",ng_aliases(),rig.log);
    check(rig.said_part("\"nonsense\" is not a control of this yoke"));
    check(rig.said_part("Brunner CLS-E NG has 24 controls; 3 with commands"));
    // The reader publishes button 4: it fires button_4's command, which is also rocker_down.
    const auto t=Clock::now();
    rig.service->sink().publish({0,0},t);
    rig.service->update(t,brunner::Snapshot{},rig.log);
    rig.service->sink().publish({1u<<3,0},t);
    rig.service->update(t,brunner::Snapshot{},rig.log);
    check(rig.runner.take()=="once x/down");
    rig.service->sink().publish({1u<<4,0},t);                                              // button 5 is rocker_up
    rig.service->update(t,brunner::Snapshot{},rig.log);
    check(rig.runner.take()=="once x/up");
    rig.service->sink().publish({0,0},t);
    rig.service->update(t,brunner::Snapshot{},rig.log);
    rig.runner.take();
    rig.service->sink().publish({0,1},t);                                                  // the hat pushed up
    rig.service->update(t,brunner::Snapshot{},rig.log);
    check(rig.runner.take()=="once x/hat");
    // The refusal in the reader's snapshot is told once, and told again when it clears.
    brunner::Snapshot refused;refused.button_refusal="Controller rejected SDO read";
    rig.said.clear();
    rig.service->update(t,refused,rig.log);rig.service->update(t,refused,rig.log);
    check(rig.said.size()==1 && rig.said_part("Yoke buttons not read: Controller rejected SDO read"));
    brunner::Snapshot fine;fine.buttons=std::uint16_t{0};
    rig.service->update(t,fine,rig.log);
    check(rig.said_part("Yoke buttons read again"));
    // An alias map that does not hold together is reported and the plain names are used.
    rig.said.clear();
    rig.service->set_yoke("Odd",{{"button_1",{"x"}},{"button_2",{"x"}}},rig.log);
    check(rig.said_part("controls of Odd are not usable") && rig.said_part("Odd has 24 controls"));
    // Stopping releases everything: what was held is ended.
    rig.service->set_yoke("Brunner CLS-E NG",ng_aliases(),rig.log);
    rig.service->stop();
    check(rig.runner.take().empty());
}
}

int main() {
    try {
        canonical_names();
        no_aliases_every_input_is_its_own_control();
        aliases_name_controls();
        a_hat_position_can_be_named();
        aliases_are_matched_in_any_case();
        bad_aliases_are_refused();
        the_action_methods();
        the_disabled_method_reads_and_runs_nothing();
        edges_over_a_stream();
        all_sixteen_buttons_and_the_hat_are_seen();
        commands_resolve_through_names();
        commands_merge_and_say_so();
        commands_note_what_cannot_be_used();
        an_aircraft_overrides_the_state_file();
        holds_are_kept_per_command();
        the_hub();
        the_direct_action_runs_commands();
        the_vjoy_action_presses_a_virtual_joystick();
        the_service_chooses_what_to_run();
        the_service_resolves_names_when_the_yoke_is_known();
        the_service_applies_an_aircrafts_commands();
        std::cout<<"buttons tests passed\n";
        return 0;
    } catch (const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
