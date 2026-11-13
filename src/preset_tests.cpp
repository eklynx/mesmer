// Tests for yoke presets (yoke_preset.hpp, spec docs/yoke-presets.md): the built-in
// models, the presets file and every bound it must meet. No hardware.
#include "yoke_preset.hpp"
#include <fstream>
#include <functional>
#include <iostream>
#include <sstream>

using namespace brunner;
namespace {
void check_at(bool okay,int line) {if (!okay) throw std::runtime_error("check failed line "+std::to_string(line));}
#define check(x) check_at((x),__LINE__)
bool refused(const std::string& text) {
    try {parse_yoke_presets(text,builtin_yoke_presets());} catch (const PresetError&) {return true;} catch (...) {}
    return false;
}
const YokePreset& named(const std::vector<YokePreset>& v,const std::string& name) {
    for (const auto& p:v) if (p.name==name) return p;
    throw std::runtime_error("no preset "+name);
}

void the_built_ins() {
    const auto presets=builtin_yoke_presets();
    validate_presets(presets);
    check(presets.size()==2);
    // The CLS-E: every value the force models were built on.
    const auto& e=named(presets,"cls-e");
    check(e.verified && e.verified_firmware==std::vector<std::uint16_t>{367});
    check(e.roll_travel_counts==13000 && e.pitch_travel_counts==12000 && e.wheel_deg==86);
    check(e.roll_ceiling_units==4500 && e.pitch_ceiling_units==4500 && e.target_torque==50);
    check(e.pitch_units_per_lb==222 && e.roll_units_per_nm==1000);
    check(std::fabs(e.margin_fraction-500.0/4500.0)<1e-12);
    check(e.usb_ids.size()==1 && e.usb_ids[0].vendor==0x25bb && e.usb_ids[0].product==0x0081);
    // The NG: its measured travel, spec ceilings, own torque; not yet verified.
    const auto& n=named(presets,"cls-e-ng");
    check(!n.verified && n.verified_firmware.empty());
    check(n.roll_travel_counts==7642 && n.pitch_travel_counts==5468 && n.wheel_deg==90 && n.pitch_travel_mm==160);
    check(n.roll_ceiling_units==11000 && n.pitch_ceiling_units==9500);   // 11 N.m / 0.001, 190 N / 0.02
    check(n.target_torque==25 && n.pitch_units_per_lb==222 && n.roll_units_per_nm==1000);
    check(n.usb_ids.size()==1 && n.usb_ids[0].vendor==0x25bb && n.usb_ids[0].product==0x008b);
    // Lookup is by the device, never by firmware.
    check(find_yoke_preset(presets,{0x25bb,0x0081})->name=="cls-e");
    check(find_yoke_preset(presets,{0x25bb,0x008b})->name=="cls-e-ng");
    check(!find_yoke_preset(presets,{0x25bb,0x0082}) && !find_yoke_preset(presets,{0x1234,0x0081}));
    const auto ids=preset_usb_ids(presets);
    check(ids.size()==2 && is_bridge_id(ids[0].vendor,ids[0].product) && is_bridge_id(ids[1].vendor,ids[1].product));
    // Firmware is only a note.
    check(firmware_verified(e,367) && !firmware_verified(e,368) && !firmware_verified(n,642));
}
// The shipped example (config/BrunnerDirect.yokes.example.json) must stay equal to the built-in
// NG, so copying it as BrunnerDirect.yokes.json changes nothing.
void the_example_file_is_the_built_in() {
    std::ifstream in(BRUNNER_YOKES_EXAMPLE,std::ios::binary);
    check(in.good());
    std::stringstream text;text<<in.rdbuf();
    const auto merged=parse_yoke_presets(text.str(),builtin_yoke_presets());
    check(merged.size()==2);
    const auto& a=named(merged,"cls-e-ng");const auto b=builtin_cls_e_ng();
    check(a.title==b.title && a.verified==b.verified && a.verified_firmware==b.verified_firmware);
    check(a.usb_ids.size()==1 && a.usb_ids[0].vendor==b.usb_ids[0].vendor && a.usb_ids[0].product==b.usb_ids[0].product);
    check(a.roll_travel_counts==b.roll_travel_counts && a.pitch_travel_counts==b.pitch_travel_counts && a.wheel_deg==b.wheel_deg && a.pitch_travel_mm==b.pitch_travel_mm);
    check(a.roll_ceiling_units==b.roll_ceiling_units && a.pitch_ceiling_units==b.pitch_ceiling_units && a.target_torque==b.target_torque);
    check(std::fabs(a.margin_fraction-b.margin_fraction)<1e-3);   // the file's 0.1111 against 500/4500
    check(a.status_ready_mask==b.status_ready_mask && a.status_stopped==b.status_stopped && a.status_acquired_mask==b.status_acquired_mask);
    check(a.roll_encoder_sign==b.roll_encoder_sign && a.pitch_encoder_sign==b.pitch_encoder_sign);
    check(a.roll_input_reverse==b.roll_input_reverse && a.pitch_input_reverse==b.pitch_input_reverse);
    check(a.pitch_units_per_lb==b.pitch_units_per_lb && a.roll_units_per_nm==b.roll_units_per_nm);
}
void an_edit_of_a_built_in() {
    const auto merged=parse_yoke_presets(R"({"schema":1,"yokes":[{"name":"cls-e-ng","roll_travel_counts":7800,"verified":true,"verified_firmware":[642,643]}]})",builtin_yoke_presets());
    check(merged.size()==2);
    const auto& n=named(merged,"cls-e-ng");
    check(n.roll_travel_counts==7800 && n.verified && n.verified_firmware==(std::vector<std::uint16_t>{642,643}));
    check(n.pitch_travel_counts==5468 && n.target_torque==25 && n.roll_ceiling_units==11000 && n.usb_ids[0].product==0x008b); // the rest is kept
    check(named(merged,"cls-e").roll_travel_counts==13000);                  // the other model is untouched
    // An empty list of yokes is the built-ins.
    check(parse_yoke_presets(R"({"schema":1,"yokes":[]})",builtin_yoke_presets()).size()==2);
}
void a_new_model() {
    const std::string model=R"({"name":"club-yoke","title":"Club Yoke","usb":[{"vendor":4660,"product":22136}],
        "roll_travel_counts":9000,"pitch_travel_counts":7000,"roll_ceiling_units":3000,"pitch_ceiling_units":3500,"target_torque":30})";
    const auto merged=parse_yoke_presets(R"({"schema":1,"yokes":[)"+model+"]}",builtin_yoke_presets());
    check(merged.size()==3);
    const auto& m=named(merged,"club-yoke");
    check(m.title=="Club Yoke" && m.usb_ids.size()==1 && m.usb_ids[0].vendor==4660 && m.usb_ids[0].product==22136);
    check(!m.verified && m.verified_firmware.empty());                                 // never assumed verified
    check(m.pitch_units_per_lb==222 && m.roll_units_per_nm==1000 && m.wheel_deg==86 && std::fabs(m.margin_fraction-500.0/4500.0)<1e-12);
    check(find_yoke_preset(merged,{4660,22136})->name=="club-yoke");
    check(preset_usb_ids(merged).size()==3);                                            // its bridge is now opened too
    // A new model must give everything that cannot be assumed: leave out each required
    // member in turn (and check the complete entry is accepted, so the refusal is for the gap).
    const std::vector<std::pair<std::string,std::string>> members{
        {"usb","[{\"vendor\":4660,\"product\":22136}]"},{"roll_travel_counts","9000"},{"pitch_travel_counts","7000"},
        {"roll_ceiling_units","3000"},{"pitch_ceiling_units","3500"},{"target_torque","30"}};
    const auto entry=[&](const std::string& leave_out) {
        std::string text=R"({"schema":1,"yokes":[{"name":"gap-test")";
        for (const auto& m:members) if (m.first!=leave_out) text+=",\""+m.first+"\":"+m.second;
        return text+"}]}";
    };
    check(!refused(entry("")));
    for (const auto& m:members) check(refused(entry(m.first)));
    // Claiming a built-in's USB id is refused: the model must follow from the device.
    check(refused(R"({"schema":1,"yokes":[{"name":"clash","usb":[{"vendor":9659,"product":139}],
        "roll_travel_counts":9000,"pitch_travel_counts":7000,"roll_ceiling_units":3000,"pitch_ceiling_units":3500,"target_torque":30}]})"));
}
void bounds_and_malformed_files() {
    const auto edit=[](const std::string& fields) {return R"({"schema":1,"yokes":[{"name":"cls-e-ng",)"+fields+"}]}";};
    check(!refused(edit("\"roll_travel_counts\":1000")) && !refused(edit("\"roll_travel_counts\":30000")));   // the edges are fine
    for (const auto* bad:{"\"roll_travel_counts\":999","\"roll_travel_counts\":30001","\"pitch_travel_counts\":999","\"pitch_travel_counts\":30001",
            "\"wheel_deg\":9","\"wheel_deg\":181","\"pitch_travel_mm\":-1","\"pitch_travel_mm\":1001",
            "\"pitch_units_per_lb\":49","\"pitch_units_per_lb\":1001","\"roll_units_per_nm\":99","\"roll_units_per_nm\":10001",
            "\"roll_ceiling_units\":99","\"roll_ceiling_units\":20001","\"pitch_ceiling_units\":99","\"pitch_ceiling_units\":20001",
            "\"margin_fraction\":0.01","\"margin_fraction\":0.51","\"target_torque\":0","\"target_torque\":1001",
            "\"verified_firmware\":[0]","\"verified_firmware\":[65536]","\"verified_firmware\":[1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17]",
            "\"usb\":[]","\"usb\":[{\"vendor\":0,\"product\":5}]","\"usb\":[{\"vendor\":5,\"product\":70000}]","\"usb\":[{\"vendor\":5}]","\"usb\":\"x\"",
            "\"roll_travel_counts\":\"7000\"","\"wheel_deg\":true","\"verified\":1","\"title\":5","\"target_torque\":25.5","\"mystery\":1"})
        check(refused(edit(bad)));
    // Names.
    for (const auto* bad_name:{"","UPPER","has space","under_score",
            "this-name-is-far-too-long-to-be-accepted-here"})
        check(refused(std::string(R"({"schema":1,"yokes":[{"name":")")+bad_name+R"(","usb":[{"vendor":1,"product":2}],"roll_travel_counts":9000,
            "pitch_travel_counts":7000,"roll_ceiling_units":3000,"pitch_ceiling_units":3500,"target_torque":30}]})"));
    // The file itself.
    for (const auto* bad_file:{"","not json","[]","{}","{\"schema\":1}","{\"schema\":2,\"yokes\":[]}","{\"schema\":\"1\",\"yokes\":[]}",
            "{\"schema\":1,\"yokes\":{}}","{\"schema\":1,\"yokes\":[5]}","{\"schema\":1,\"yokes\":[{}]}","{\"schema\":1,\"yokes\":[],\"extra\":1}",
            "{\"schema\":1,\"yokes\":[{\"name\":5}]}"})
        check(refused(bad_file));
    std::string many=R"({"schema":1,"yokes":[)";
    for (int i=0;i<33;++i) many+=std::string(i?",":"")+R"({"name":"cls-e"})";
    check(refused(many+"]}"));
    // Two models claiming one id are refused whichever way they arrive.
    auto clash=builtin_yoke_presets();clash[1].usb_ids.push_back(clash[0].usb_ids[0]);
    bool threw=false;try {validate_presets(clash);} catch (const PresetError&) {threw=true;}
    check(threw);
    auto twice=builtin_yoke_presets();twice[1].name=twice[0].name;
    threw=false;try {validate_presets(twice);} catch (const PresetError&) {threw=true;}
    check(threw);
}
}
int main() {
    try {
        the_built_ins();
        the_example_file_is_the_built_in();
        an_edit_of_a_built_in();
        a_new_model();
        bounds_and_malformed_files();
        std::cout<<"preset tests passed\n";
        return 0;
    } catch (const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
