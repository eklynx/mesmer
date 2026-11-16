// Tests for the joystick-report path: descriptor parsing and decoding (hid/report_decoder.hpp), the HID profile's glitch
// rules and the HID source thread (buttons/input/), and the "button_commands" and reply-tolerance keys of the state file
// (brunner/owner_state.hpp). What happens to the buttons after that (names, aliases, edges, holds) is buttons/buttons_tests.cpp. The descriptor and reports are the CLS-E NG's,
// captured with hid/mac/joystick_probe_mac.cpp. No hardware. Spec: docs/dev/button-commands.md.
#include "buttons/input/hid_profile.hpp"
#include "buttons/input/hid_source.hpp"
#include "brunner/owner_state.hpp"
#include <chrono>
#include <deque>
#include <iostream>
#include <mutex>
#include <thread>

using namespace brunner;
using buttons::ButtonState;
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
const buttons::HidProfile& profile() {return buttons::hid_profile_for({0x25bb,0x008b});}

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
// Windows gives the bits of each usage, not the descriptor (hid/win/joystick_win.cpp): from the NG's usages as
// Windows' parse places them, layout_from_fields builds exactly the layout the descriptor parses to.
void the_layout_is_built_from_usage_bits() {
    const auto run=[](int first,int count) {std::vector<int> bits;for (int b=first;b<first+count;++b) bits.push_back(b);return bits;};
    std::vector<hid::UsageBits> usages;
    for (int n=6;n>=1;--n) usages.push_back({0x09,n,{n-1},0,1});   // in any order
    usages.push_back({0x01,0x39,run(8,4),1,8});
    usages.push_back({0x01,0x30,run(16,16),0,65535});
    usages.push_back({0x01,0x31,run(32,16),0,65535});
    const auto built=hid::layout_from_fields(usages,48,false);
    check(built.has_value());
    const auto& parsed=layout();
    check(built->has_report_id==parsed.has_report_id && built->report_bits==parsed.report_bits);
    check(built->buttons.size()==parsed.buttons.size());
    for (std::size_t n=0;n<parsed.buttons.size();++n) check(built->buttons[n].first_bit==parsed.buttons[n].first_bit && built->buttons[n].bit_count==1);
    check(built->hat && built->hat->first_bit==parsed.hat->first_bit && built->hat->bit_count==parsed.hat->bit_count);
    check(built->hat_logical_min==parsed.hat_logical_min && built->hat_logical_max==parsed.hat_logical_max);
    check(built->padding.size()==parsed.padding.size());
    for (std::size_t k=0;k<parsed.padding.size();++k)
        check(built->padding[k].first_bit==parsed.padding[k].first_bit && built->padding[k].bit_count==parsed.padding[k].bit_count);
    // The captured glitch sets a padding bit either way.
    const std::vector<std::uint8_t> glitch{0x7d,0x00,0x05,0x00,0x00,0x00};
    check(!hid::decode_report(*built,glitch.data(),glitch.size())->padding_clear);
    // Buttons stop at the first missing usage; no buttons and no hat is no joystick; a bit outside the payload is refused.
    check(hid::layout_from_fields({{0x09,1,{0},0,1},{0x09,3,{2},0,1}},8,false)->buttons.size()==1);
    check(!hid::layout_from_fields({{0x01,0x30,run(0,16),0,65535}},16,false).has_value());
    check(!hid::layout_from_fields({{0x09,1,{8},0,1}},8,false).has_value());
    check(!hid::layout_from_fields({{0x09,1,{0},0,1}},0,false).has_value());
}
void the_glitch_is_dropped() {
    check(profile().invalid_reason(state((1u<<3)|(1u<<4))).size()>0);                 // the rocker, both sides
    check(profile().invalid_reason(state((1u<<4)|(1u<<5))).empty());                  // rocker up with the hat pushed in is a real press
    check(profile().invalid_reason(*decode({0x7d,0x00,0x05,0x00,0x00,0x00})).size()>0);
    check(profile().invalid_reason(state(1u<<3)).empty() && profile().invalid_reason(state(1u<<4)).empty());
    check(profile().invalid_reason(state(0,0,false)).size()>0);   // padding alone is enough
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
// What a read method's source hands over, recorded.
struct RecordingSink final : buttons::ButtonSink {
    std::mutex mutex;
    std::vector<ButtonState> states;int releases=0;
    void publish(const ButtonState& state,buttons::Clock::time_point) override {std::lock_guard<std::mutex> lock(mutex);states.push_back(state);}
    void release_all() override {std::lock_guard<std::mutex> lock(mutex);++releases;}
    std::size_t published() {std::lock_guard<std::mutex> lock(mutex);return states.size();}
    int released() {std::lock_guard<std::mutex> lock(mutex);return releases;}
};
void the_source_publishes_genuine_reports() {
    auto device=std::make_unique<FakeJoystick>();
    device->reports={{0,0,0x60,0x7f,0,0},{0x7d,0,0x05,0,0,0},{0x01,0,0x60,0x7f,0,0},{0x7d,0,0x05,0,0,0},{0x00,0,0x60,0x7f,0,0},{0x00,3,0x60,0x7f,0,0}};
    device->fail_when_empty=true;
    std::atomic<int> opens{0};
    buttons::HidSource source([&](std::string& why)->std::unique_ptr<hid::JoystickDevice> {
        if (opens++==0) return std::move(device);
        why="no joystick interface";return nullptr;
    },10);
    RecordingSink sink;
    check(source.take_drops().empty() && source.status().empty());
    source.start(sink);
    for (int i=0;i<200 && sink.released()<1;++i) std::this_thread::sleep_for(std::chrono::milliseconds(5));
    source.stop();
    // The four genuine reports, in HID numbering; the two glitches published nothing; the yoke vanishing released everything once.
    check(sink.states.size()==4 && sink.states[0]==ButtonState{0,0} && sink.states[1]==ButtonState{1,0} && sink.states[2]==ButtonState{0,0} && sink.states[3]==ButtonState{0,3});
    check(sink.releases>=1);
    const auto drops=source.take_drops();
    check(drops.find("dropped 2 glitched reports")!=std::string::npos && drops.find("buttons 4 and 5 together: 2")!=std::string::npos);
    check(source.take_drops().empty());                                                                  // reported once
    check(source.status().find("removed")!=std::string::npos || source.status().empty());                // the removal ended the session
}
void a_missing_joystick_is_not_an_error() {
    buttons::HidSource source([](std::string& why)->std::unique_ptr<hid::JoystickDevice> {why="no joystick interface";return nullptr;},5);
    RecordingSink sink;
    source.start(sink);
    std::this_thread::sleep_for(std::chrono::milliseconds(40));
    source.stop();
    check(source.status().empty() && sink.published()==0);
    buttons::HidSource refused([](std::string& why)->std::unique_ptr<hid::JoystickDevice> {why="macOS refused access (Input Monitoring permission?)";return nullptr;},5);
    RecordingSink other;
    refused.start(other);
    std::this_thread::sleep_for(std::chrono::milliseconds(40));
    refused.stop();
    check(refused.status().find("refused")!=std::string::npos);
}
}

int main() {
    try {
        the_descriptor_is_parsed();
        reports_are_decoded();
        the_layout_is_built_from_usage_bits();
        the_glitch_is_dropped();
        the_state_file_key();
        the_tolerance_key();
        the_source_publishes_genuine_reports();
        a_missing_joystick_is_not_an_error();
        std::cout<<"joystick tests passed\n";
        return 0;
    } catch (const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
