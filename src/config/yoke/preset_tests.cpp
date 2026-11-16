// Tests for yoke presets (config/yoke/yoke_preset.hpp, spec docs/dev/yoke-presets.md): the built-in
// models, the presets file and every bound it must meet. No hardware.
#include "config/yoke/yoke_overrides.hpp"
#include "config/yoke/yoke_preset.hpp"
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
const YokePreset& named(const std::vector<YokePreset>& v,const char* name) {
    for (const auto& p:v) if (p.name==name) return p;
    throw std::runtime_error(std::string("no preset ")+name);
}

// A USB id is written one way everywhere: "0x25BB", "0x25BB:0x008B".
void usb_ids_are_hex() {
    check(hex_id_text(0x25bb)=="0x25BB" && hex_id_text(0x8b)=="0x008B" && usb_text({0x25bb,0x008b})=="0x25BB:0x008B");
    // Either case of the digits (and of the x) is read; upper case is what is written.
    for (const auto* same:{"0x25BB","0x25bb","0x25Bb","0X25BB","0X25bb"}) check(parse_hex_id(same)==0x25BB);
    check(parse_hex_id("0x8b")==0x8b && parse_hex_id("0x8B")==0x8b && parse_hex_id("0x008b")==0x8b && parse_hex_id("0xFFFF")==0xffff && parse_hex_id("0xffff")==0xffff);
    for (const auto* bad:{"","0x","0x0","0x0000","0x10000","25bb","9659","0x25bg","x25bb","0xx25b"," 0x25BB","0x-1"})
        check(!parse_hex_id(bad));
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
    check(e.usb_ids.size()==1 && e.usb_ids[0].vendor==0x25BB && e.usb_ids[0].product==0x0081);
    // The NG: its measured travel, spec ceilings, own torque; verified on firmware 642.
    const auto& n=named(presets,"cls-e-ng");
    check(n.verified && n.verified_firmware==std::vector<std::uint16_t>{642});
    check(n.roll_travel_counts==7350 && n.pitch_travel_counts==5300 && n.wheel_deg==90 && n.pitch_travel_mm==160);
    check(n.roll_ceiling_units==11000 && n.pitch_ceiling_units==9500);   // 11 N.m / 0.001, 190 N / 0.02
    check(n.target_torque==25 && n.pitch_units_per_lb==222 && n.roll_units_per_nm==1000);
    check(n.usb_ids.size()==1 && n.usb_ids[0].vendor==0x25BB && n.usb_ids[0].product==0x008b);
    // Lookup is by the device, never by firmware.
    check(find_yoke_preset(presets,{0x25bb,0x0081})->name=="cls-e");
    check(find_yoke_preset(presets,{0x25bb,0x008b})->name=="cls-e-ng");
    check(!find_yoke_preset(presets,{0x25bb,0x0082}) && !find_yoke_preset(presets,{0x1234,0x0081}));
    const auto ids=preset_usb_ids(presets);
    check(ids.size()==2 && is_bridge_id(ids[0].vendor,ids[0].product) && is_bridge_id(ids[1].vendor,ids[1].product));
    // Firmware is only a note.
    check(firmware_verified(e,367) && !firmware_verified(e,368) && firmware_verified(n,642) && !firmware_verified(n,643));
}
// The shipped files (config/yokes/) must stay equal to the built-ins: the CLS-E's are what the project
// was built on, and the fallback must never differ from what a healthy install loads.
bool same_preset(const YokePreset& a,const YokePreset& b) {
    return a.name==b.name && a.title==b.title && a.spring_mode==b.spring_mode && a.power_on_spring_mode==b.power_on_spring_mode && a.verified==b.verified && a.verified_firmware==b.verified_firmware
        && a.usb_ids.size()==b.usb_ids.size() && a.usb_ids[0].vendor==b.usb_ids[0].vendor && a.usb_ids[0].product==b.usb_ids[0].product
        && a.roll_travel_counts==b.roll_travel_counts && a.pitch_travel_counts==b.pitch_travel_counts && a.wheel_deg==b.wheel_deg
        && a.pitch_travel_mm==b.pitch_travel_mm && a.roll_ceiling_units==b.roll_ceiling_units && a.pitch_ceiling_units==b.pitch_ceiling_units
        && a.target_torque==b.target_torque && a.centre_drift_percent==b.centre_drift_percent && std::fabs(a.margin_fraction-b.margin_fraction)<1e-3
        && a.status_ready_mask==b.status_ready_mask && a.status_stopped==b.status_stopped && a.status_acquired_mask==b.status_acquired_mask
        && a.spring_mode==b.spring_mode && a.idle_enabled==b.idle_enabled && a.rest==b.rest
        && a.roll_encoder_sign==b.roll_encoder_sign && a.pitch_encoder_sign==b.pitch_encoder_sign
        && a.roll_spring_sign==b.roll_spring_sign && a.pitch_spring_sign==b.pitch_spring_sign
        && a.roll_input_low==b.roll_input_low && a.roll_input_high==b.roll_input_high && a.pitch_input_low==b.pitch_input_low && a.pitch_input_high==b.pitch_input_high
        && a.roll_input_reverse==b.roll_input_reverse && a.pitch_input_reverse==b.pitch_input_reverse
        && a.pitch_units_per_lb==b.pitch_units_per_lb && a.roll_units_per_nm==b.roll_units_per_nm
        && a.button_aliases==b.button_aliases && a.button_registers==b.button_registers
        && a.button_action_method==b.button_action_method;
}
std::string shipped_text(const char* file) {
    std::ifstream in(std::string(BRUNNER_YOKES_DIR)+"/"+file,std::ios::binary);
    check(in.good());
    std::stringstream text;text<<in.rdbuf();
    return text.str();
}
void the_shipped_files_are_the_built_ins() {
    const auto loaded=load_yoke_models({shipped_text("cls-e.json"),shipped_text("cls-e-ng.json")},std::nullopt);
    check(loaded.from_files && loaded.notes.empty() && loaded.presets.size()==2);
    check(same_preset(named(loaded.presets,"cls-e"),builtin_cls_e()));
    check(same_preset(named(loaded.presets,"cls-e-ng"),builtin_cls_e_ng()));
}
void loading_the_models() {
    const auto cls_e=shipped_text("cls-e.json"),ng=shipped_text("cls-e-ng.json");
    // The same file twice, or two files claiming one USB id, is refused whole: the fallback is in force.
    auto twice=load_yoke_models({cls_e,cls_e},std::nullopt);
    check(!twice.from_files && twice.notes.size()==1 && twice.presets.size()==2);
    std::string clash=ng;clash.replace(clash.find("\"cls-e-ng\""),10,"\"other\"");clash.replace(clash.find("0x008B"),6,"0x0081");
    {   // lower-case digits in a file name the same ids, so it still loads
        auto ng_lower=ng;ng_lower.replace(ng_lower.find("0x25BB"),6,"0x25bb");ng_lower.replace(ng_lower.find("0x008B"),6,"0x008b");
        check(load_yoke_models({cls_e,ng_lower},std::nullopt).from_files);
    }
    check(!load_yoke_models({cls_e,clash},std::nullopt).from_files);
    // A malformed file, an empty folder: fallback with a note, never half applied.
    const auto bad=load_yoke_models({cls_e,"{not json"},std::nullopt);
    check(!bad.from_files && bad.notes.size()==1 && same_preset(named(bad.presets,"cls-e-ng"),builtin_cls_e_ng()));
    const auto none=load_yoke_models({},std::nullopt);
    check(!none.from_files && none.presets.size()==2);
    // A shipped file cannot patch another model by name: it is parsed against an empty set.
    check(!load_yoke_models({cls_e,"{\"schema\":1,\"yokes\":[{\"name\":\"cls-e\",\"verified\":false}]}"},std::nullopt).from_files);
    // The user's overrides patch a shipped model by name; a refused overrides file changes nothing.
    const std::string patch="{\"schema\":1,\"yokes\":[{\"name\":\"cls-e-ng\",\"pitch_input_reverse\":false}]}";
    const auto patched=load_yoke_models({cls_e,ng},patch);
    check(patched.from_files && patched.overrides_applied && !named(patched.presets,"cls-e-ng").pitch_input_reverse && patched.presets.size()==2);
    const auto refused_patch=load_yoke_models({cls_e,ng},std::string("{\"schema\":1,\"yokes\":[{\"name\":\"cls-e-ng\",\"nonsense\":1}]}"));
    check(refused_patch.from_files && !refused_patch.overrides_applied && refused_patch.notes.size()==1
        && named(refused_patch.presets,"cls-e-ng").pitch_input_reverse);
    // Overrides also patch the fallback.
    check(!named(load_yoke_models({},patch).presets,"cls-e-ng").pitch_input_reverse);
}
void an_edit_of_a_built_in() {
    const auto merged=parse_yoke_presets(R"({"schema":1,"yokes":[{"name":"cls-e-ng","roll_travel_counts":7800,"verified":true,"verified_firmware":[642,643]}]})",builtin_yoke_presets());
    check(merged.size()==2);
    const auto& n=named(merged,"cls-e-ng");
    check(n.roll_travel_counts==7800 && n.verified && n.verified_firmware==(std::vector<std::uint16_t>{642,643}));
    check(n.pitch_travel_counts==5300 && n.target_torque==25 && n.roll_ceiling_units==11000 && n.usb_ids[0].product==0x008b); // the rest is kept
    check(named(merged,"cls-e").roll_travel_counts==13000);                  // the other model is untouched
    // An empty list of yokes is the built-ins.
    check(parse_yoke_presets(R"({"schema":1,"yokes":[]})",builtin_yoke_presets()).size()==2);
}
void a_new_model() {
    const std::string model=R"({"name":"club-yoke","title":"Club Yoke","usb":[{"vendorId":"0x1234","productId":"0x5678"}],
        "roll_travel_counts":9000,"pitch_travel_counts":7000,"roll_ceiling_units":3000,"pitch_ceiling_units":3500,"target_torque":30})";
    const auto merged=parse_yoke_presets(R"({"schema":1,"yokes":[)"+model+"]}",builtin_yoke_presets());
    check(merged.size()==3);
    const auto& m=named(merged,"club-yoke");
    check(m.title=="Club Yoke" && m.usb_ids.size()==1 && m.usb_ids[0].vendor==0x1234 && m.usb_ids[0].product==0x5678);
    check(!m.verified && m.verified_firmware.empty());                                 // never assumed verified
    check(m.pitch_units_per_lb==222 && m.roll_units_per_nm==1000 && m.wheel_deg==86 && std::fabs(m.margin_fraction-500.0/4500.0)<1e-12);
    check(find_yoke_preset(merged,{0x1234,0x5678})->name=="club-yoke");
    check(preset_usb_ids(merged).size()==3);                                            // its bridge is now opened too
    check(m.rest.captured);                                                            // no resting settings given: read from the yoke, put back as found
    // A new model must give everything that cannot be assumed: leave out each required
    // member in turn (and check the complete entry is accepted, so the refusal is for the gap).
    const std::vector<std::pair<std::string,std::string>> members{
        {"usb","[{\"vendorId\":\"0x1234\",\"productId\":\"0x5678\"}]"},{"roll_travel_counts","9000"},{"pitch_travel_counts","7000"},
        {"roll_ceiling_units","3000"},{"pitch_ceiling_units","3500"},{"target_torque","30"}};
    const auto entry=[&](const std::string& leave_out) {
        std::string text=R"({"schema":1,"yokes":[{"name":"gap-test")";
        for (const auto& member:members) if (member.first!=leave_out) text+=",\""+member.first+"\":"+member.second;
        return text+"}]}";
    };
    check(!refused(entry("")));
    for (const auto& member:members) check(refused(entry(member.first)));
    // Claiming a built-in's USB id is refused: the model must follow from the device.
    check(refused(R"({"schema":1,"yokes":[{"name":"clash","usb":[{"vendorId":"0x25BB","productId":"0x008B"}],
        "roll_travel_counts":9000,"pitch_travel_counts":7000,"roll_ceiling_units":3000,"pitch_ceiling_units":3500,"target_torque":30}]})"));
}
// Resting settings: the built-ins', a model that gives its own, and every bound they must meet.
void resting_settings() {
    const auto cls_e=builtin_cls_e().rest,ng=builtin_cls_e_ng().rest;
    // The CLS-E's are the plugin's historical constants, unchanged; the NG's are its survey of 2026-11-12.
    check(!cls_e.captured && cls_e.roll_spring_percent==100 && cls_e.pitch_spring_percent==100 && cls_e.pitch_gain_denominator==6
        && cls_e.pitch_move_back==180 && cls_e.pitch_cutoff==0 && cls_e.mode==0 && cls_e.roll_curve[0]==100 && cls_e.roll_curve[8]==1000);
    check(!ng.captured && ng.roll_spring_percent==180 && ng.pitch_spring_percent==160 && ng.pitch_gain_denominator==3
        && ng.pitch_move_back==200 && ng.pitch_cutoff==40 && ng.mode==0x2605 && ng.roll_curve[0]==50 && ng.pitch_curve[8]==950);
    // A new model that gives a rest block has it (and is not captured); it overrides field by field.
    const std::string model=R"({"name":"club-yoke","usb":[{"vendorId":"0x1234","productId":"0x5678"}],"roll_travel_counts":9000,"pitch_travel_counts":7000,
        "roll_ceiling_units":3000,"pitch_ceiling_units":3500,"target_torque":30,"rest":{"roll_spring_percent":50,"pitch_spring_percent":60}})";
    const auto merged=parse_yoke_presets(R"({"schema":1,"yokes":[)"+model+"]}",builtin_yoke_presets());
    const auto& m=named(merged,"club-yoke");
    check(!m.rest.captured && m.rest.roll_spring_percent==50 && m.rest.pitch_spring_percent==60 && m.rest.roll_curve==cls_e.roll_curve);
    const auto edited=parse_yoke_presets(R"({"schema":1,"yokes":[{"name":"cls-e-ng","rest":{"pitch_cutoff":0,"mode":0}}]})",builtin_yoke_presets());
    check(named(edited,"cls-e-ng").rest.pitch_cutoff==0 && named(edited,"cls-e-ng").rest.mode==0 && named(edited,"cls-e-ng").rest.roll_spring_percent==180);
    const auto in_ng=[](const std::string& rest) {return R"({"schema":1,"yokes":[{"name":"cls-e-ng","rest":{)"+rest+"}}]}";};
    check(!refused(in_ng("\"pitch_move_back\":180")) && !refused(in_ng("\"pitch_move_back\":720")) && !refused(in_ng("\"mode\":9733")) && !refused(in_ng("\"mode\":1541")) && refused(in_ng("\"mode\":8709")));   // 0x2605 and 0x0605 go with spring_mode 0x0605; 0x2205 does not
    for (const auto* bad:{"\"roll_spring_percent\":0","\"pitch_spring_percent\":1001","\"pitch_gain_denominator\":0","\"pitch_gain_denominator\":7",
            "\"pitch_move_back\":179","\"pitch_move_back\":721","\"pitch_cutoff\":102","\"pitch_cutoff\":-1",
            "\"mode\":1537","\"mode\":70000","\"mode\":9732",                       // not the spring mode plus the flow's bits
            "\"roll_curve\":[100]","\"roll_curve\":[1,350,400,450,500,600,750,900,1000]","\"pitch_curve\":[100,350,400,450,500,600,750,900,1001]",
            "\"roll_curve\":\"x\"","\"mystery\":1","\"mode\":1.5"})
        check(refused(in_ng(bad)));
    check(refused(R"({"schema":1,"yokes":[{"name":"cls-e-ng","rest":5}]})"));
}
void bounds_and_malformed_files() {
    const auto edit=[](const std::string& fields) {return R"({"schema":1,"yokes":[{"name":"cls-e-ng",)"+fields+"}]}";};
    check(!refused(edit("\"roll_travel_counts\":1000")) && !refused(edit("\"roll_travel_counts\":30000")));   // the edges are fine
    // The centre drift is a decimal percent of travel: 0.1 by default, 0 to 5, a whole number is fine too.
    check(builtin_cls_e().centre_drift_percent==0.1 && builtin_cls_e_ng().centre_drift_percent==0.1);
    for (const auto* good:{"\"centre_drift_percent\":0","\"centre_drift_percent\":0.05","\"centre_drift_percent\":0.125","\"centre_drift_percent\":5"}) check(!refused(edit(good)));
    {const auto p=parse_yoke_presets(edit("\"centre_drift_percent\":0.125"),builtin_yoke_presets());check(named(p,"cls-e-ng").centre_drift_percent==0.125 && named(p,"cls-e").centre_drift_percent==0.1);}
    for (const auto* bad:{"\"centre_drift_percent\":-0.01","\"centre_drift_percent\":5.01","\"centre_drift_percent\":\"0.1\""}) check(refused(edit(bad)));
    for (const auto* bad:{"\"roll_travel_counts\":999","\"roll_travel_counts\":30001","\"pitch_travel_counts\":999","\"pitch_travel_counts\":30001",
            "\"wheel_deg\":9","\"wheel_deg\":181","\"pitch_travel_mm\":-1","\"pitch_travel_mm\":1001",
            "\"pitch_units_per_lb\":49","\"pitch_units_per_lb\":1001","\"roll_units_per_nm\":99","\"roll_units_per_nm\":10001",
            "\"roll_ceiling_units\":99","\"roll_ceiling_units\":20001","\"pitch_ceiling_units\":99","\"pitch_ceiling_units\":20001",
            "\"margin_fraction\":0.01","\"margin_fraction\":0.51","\"target_torque\":0","\"target_torque\":1001",
            "\"verified_firmware\":[0]","\"verified_firmware\":[65536]","\"verified_firmware\":[1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17]",
            "\"usb\":[]","\"usb\":[{\"vendorId\":\"0x0\",\"productId\":\"0x5\"}]","\"usb\":[{\"vendorId\":\"0x5\",\"productId\":\"0x10000\"}]","\"usb\":[{\"vendorId\":\"0x5\"}]","\"usb\":\"x\"",
            // decimal numbers, a missing 0x, the old key names and non-hex digits are all refused
            "\"usb\":[{\"vendorId\":9659,\"productId\":139}]","\"usb\":[{\"vendorId\":\"25bb\",\"productId\":\"0x8b\"}]","\"usb\":[{\"vendor\":9659,\"product\":139}]","\"usb\":[{\"vendorId\":\"0xzz\",\"productId\":\"0x8b\"}]",
            "\"roll_spring_sign\":0","\"pitch_spring_sign\":2","\"pitch_spring_sign\":true","\"pitch_encoder_sign\":0",
            "\"roll_travel_counts\":\"7000\"","\"wheel_deg\":true","\"verified\":1","\"title\":5","\"target_torque\":25.5","\"mystery\":1"})
        check(refused(edit(bad)));
    // Names.
    for (const auto* bad_name:{"","UPPER","has space","under_score",
            "this-name-is-far-too-long-to-be-accepted-here"})
        check(refused(std::string(R"({"schema":1,"yokes":[{"name":")")+bad_name+R"(","usb":[{"vendorId":"0x1","productId":"0x2"}],"roll_travel_counts":9000,
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
// Where each yoke physically stops, which scales the input to X-Plane (the preset's *_input_low/high).
void the_input_stops() {
    const auto cls_e=builtin_cls_e(),ng=builtin_cls_e_ng();
    check(cls_e.roll_input_low==0 && cls_e.roll_input_high==0 && cls_e.pitch_input_low==0 && cls_e.pitch_input_high==0);   // unset: the travel either way
    check(ng.roll_input_low==-7641 && ng.roll_input_high==7649 && ng.pitch_input_low==-5446 && ng.pitch_input_high==5461);
    const auto in_ng=[](const std::string& fields) {return R"({"schema":1,"yokes":[{"name":"cls-e-ng",)"+fields+"}]}";};
    const auto patched=parse_yoke_presets(in_ng("\"pitch_input_low\":-5000,\"pitch_input_high\":5200"),builtin_yoke_presets());
    check(named(patched,"cls-e-ng").pitch_input_low==-5000 && named(patched,"cls-e-ng").pitch_input_high==5200 && named(patched,"cls-e-ng").roll_input_low==-7641);
    check(!refused(in_ng("\"pitch_input_low\":0,\"pitch_input_high\":0")));          // both unset is allowed: the travel
    for (const auto* bad:{"\"pitch_input_low\":-5446,\"pitch_input_high\":0","\"pitch_input_low\":0,\"pitch_input_high\":5461",
            "\"pitch_input_low\":5446,\"pitch_input_high\":5461","\"pitch_input_low\":-5446,\"pitch_input_high\":-5461",
            "\"pitch_input_low\":-999,\"pitch_input_high\":5461","\"roll_input_low\":-7641,\"roll_input_high\":999",
            "\"roll_input_low\":-30001,\"roll_input_high\":7649","\"roll_input_low\":-7641,\"roll_input_high\":30001",
            "\"pitch_input_low\":\"-5446\",\"pitch_input_high\":5461"})
        check(refused(in_ng(bad)));
}
// Changing one direction from the feel page (config/yoke/yoke_overrides.hpp).
void changing_a_direction() {
    const std::vector<std::string> shipped{shipped_text("cls-e.json"),shipped_text("cls-e-ng.json")};
    using F=DirectionChange::Field;
    const DirectionChange pitch_encoder{true,F::encoder_sign,1},roll_reverse{false,F::input_reverse,0};
    // No file yet: the change becomes the first entry, and loads as that value on top of the shipped model.
    const auto first=patch_direction("",shipped,"cls-e-ng",pitch_encoder);
    check(first.ok());
    const auto loaded=load_yoke_models(shipped,first.text);
    check(loaded.overrides_applied && named(loaded.presets,"cls-e-ng").pitch_encoder_sign==1 && named(loaded.presets,"cls-e-ng").roll_encoder_sign==-1);
    // An existing file: the other keys, the other entries and an earlier change to another field all stay.
    const std::string existing=R"({"schema":1,"yokes":[{"name":"cls-e","roll_travel_counts":12500},{"name":"cls-e-ng","roll_travel_counts":7000,"pitch_input_reverse":false}]})";
    const auto second=patch_direction(existing,shipped,"cls-e-ng",roll_reverse);
    check(second.ok());
    const auto kept=load_yoke_models(shipped,second.text);
    check(named(kept.presets,"cls-e-ng").roll_travel_counts==7000 && !named(kept.presets,"cls-e-ng").roll_input_reverse
        && !named(kept.presets,"cls-e-ng").pitch_input_reverse && named(kept.presets,"cls-e").roll_travel_counts==12500);
    // A key already in the entry is replaced in place, not repeated.
    const auto third=patch_direction(second.text,shipped,"cls-e-ng",DirectionChange{true,F::input_reverse,1});
    check(third.ok() && third.text.find("pitch_input_reverse")==third.text.rfind("pitch_input_reverse"));
    check(named(load_yoke_models(shipped,third.text).presets,"cls-e-ng").pitch_input_reverse);
    // Refusals leave the old file to stand: a bad value, bad JSON, no yokes list, a model nobody names.
    check(!patch_direction("",shipped,"cls-e-ng",DirectionChange{true,F::encoder_sign,2}).ok());
    check(!patch_direction("",shipped,"cls-e-ng",DirectionChange{true,F::encoder_sign,0}).ok());
    check(!patch_direction("",shipped,"cls-e-ng",DirectionChange{true,F::input_reverse,-1}).ok());
    check(!patch_direction("{not json",shipped,"cls-e-ng",pitch_encoder).ok());
    check(!patch_direction("{\"schema\":1}",shipped,"cls-e-ng",pitch_encoder).ok());
    check(!patch_direction("",shipped,"no-such-yoke",pitch_encoder).ok());
    // A file the loader would refuse for another reason is not rewritten around.
    check(!patch_direction(R"({"schema":1,"yokes":[{"name":"cls-e-ng","mystery":1}]})",shipped,"cls-e-ng",pitch_encoder).ok());
    // The spring sign is its own field: the NG reads its pitch encoder +1 against a -1 spring frame, the CLS-E -1 and -1.
    check(builtin_cls_e_ng().pitch_encoder_sign==1 && builtin_cls_e_ng().pitch_spring_sign==-1 && builtin_cls_e_ng().roll_spring_sign==-1);
    check(builtin_cls_e().pitch_encoder_sign==-1 && builtin_cls_e().pitch_spring_sign==-1 && builtin_cls_e().roll_spring_sign==-1);
    const auto spring=patch_direction("",shipped,"cls-e-ng",DirectionChange{true,F::spring_sign,1});
    check(spring.ok() && named(load_yoke_models(shipped,spring.text).presets,"cls-e-ng").pitch_spring_sign==1 && direction_key(DirectionChange{false,F::spring_sign,1})=="roll_spring_sign");
    check(!patch_direction("",shipped,"cls-e-ng",DirectionChange{true,F::spring_sign,0}).ok());
    check(direction_key(pitch_encoder)=="pitch_encoder_sign" && direction_key(roll_reverse)=="roll_input_reverse");
}
}
// The yoke's control names (buttons/controls.hpp): what each shipped model says, and how a file may change it.
void button_aliases() {
    const auto ng=builtin_cls_e_ng(),cls_e=builtin_cls_e();
    check(ng.button_aliases.size()==6 && ng.button_aliases.at("button_1")==(std::vector<std::string>{"trigger"}));
    check(cls_e.button_aliases.size()==13 && cls_e.button_aliases.at("button_1")==(std::vector<std::string>{"left_index"}) && !cls_e.button_aliases.count("button_6"));
    const buttons::ControlMap ng_map(ng.button_aliases),cls_e_map(cls_e.button_aliases);
    check(*ng_map.resolve("button_5")==*ng_map.resolve("rocker_up") && *ng_map.resolve("hat_push")==*ng_map.resolve("button_6"));
    check(*cls_e_map.resolve("panel_left")==*cls_e_map.resolve("button_14"));
    // A file may give the map: it replaces the model's whole map (other fields are kept).
    const auto replaced=parse_yoke_presets(R"({"schema":1,"yokes":[{"name":"cls-e-ng","button_aliases":{"button_1":["fire"],"hat_n":["north","up"]}}]})",builtin_yoke_presets());
    const auto& n=named(replaced,"cls-e-ng");
    check(n.button_aliases.size()==2 && n.button_aliases.at("hat_n")==(std::vector<std::string>{"north","up"}) && n.pitch_travel_counts==5300);
    check(named(replaced,"cls-e").button_aliases==builtin_cls_e().button_aliases);   // the other model is untouched
    // Where each control is on the bridge: a map from the control's name to the object and the mask or value that says pressed. The
    // original CLS-E's are bits 16 to 31 of 0x60fd (no slot 6, no hat), the NG's bits of 0x2880:3 and the values of 0x2880:4.
    const auto& cls_e_signals=builtin_cls_e().button_registers;
    check(cls_e_signals==buttons::cls_e_can_layout() && cls_e_signals.size()==13 && !cls_e_signals.count({buttons::InputKind::button,6}) && !cls_e_signals.count({buttons::InputKind::hat,1}));
    const auto& slot_1=cls_e_signals.at({buttons::InputKind::button,1});
    check(slot_1==(buttons::CanSignal{2,0x60fd,0,true,0x00010000}) && cls_e_signals.at({buttons::InputKind::button,14}).operand==0x20000000u);
    const auto& ng_signals=builtin_cls_e_ng().button_registers;
    check(ng_signals==buttons::cls_e_ng_can_layout() && ng_signals.size()==14);
    check(ng_signals.at({buttons::InputKind::button,6})==(buttons::CanSignal{2,0x2880,3,true,0x20}));
    check(ng_signals.at({buttons::InputKind::hat,3})==(buttons::CanSignal{2,0x2880,4,false,3}));
    // A mask is pressed when all its bits are set, a value when it is the value read.
    check(slot_1.pressed(0x00010000u) && slot_1.pressed(0xffffffffu) && !slot_1.pressed(0x00020000u) && !slot_1.pressed(0));
    check(ng_signals.at({buttons::InputKind::hat,3}).pressed(3) && !ng_signals.at({buttons::InputKind::hat,3}).pressed(2) && !ng_signals.at({buttons::InputKind::hat,3}).pressed(0));
    const buttons::CanSignal two_bits{2,0x2880,3,true,0x03};
    check(two_bits.pressed(0x03) && two_bits.pressed(0x07) && !two_bits.pressed(0x01));
    const auto moved=parse_yoke_presets(R"({"schema":1,"yokes":[{"name":"cls-e","button_registers":{"button_2":{"node":1,"object":"0x2880","sub":3,"mask":"0x06"},"hat_s":{"node":2,"object":"0x2880","sub":4,"value":5}}}]})",builtin_yoke_presets());
    const auto& replaced_signals=named(moved,"cls-e").button_registers;
    check(replaced_signals.size()==2 && replaced_signals.at({buttons::InputKind::button,2})==(buttons::CanSignal{1,0x2880,3,true,0x06}));
    check(replaced_signals.at({buttons::InputKind::hat,5})==(buttons::CanSignal{2,0x2880,4,false,5}) && named(moved,"cls-e-ng").button_registers==buttons::cls_e_ng_can_layout());   // the other model is untouched
    check(named(parse_yoke_presets(R"({"schema":1,"yokes":[{"name":"cls-e","button_registers":{}}]})",builtin_yoke_presets()),"cls-e").button_registers.empty());   // none: a yoke with no buttons
    // What the plugin does with the buttons: the CLS-E presses a virtual joystick, the NG runs X-Plane commands, a model that says
    // nothing runs commands; any case, two words, nothing else.
    check(builtin_cls_e().button_action_method==buttons::ActionMethod::vjoy);
    check(builtin_cls_e_ng().button_action_method==buttons::ActionMethod::direct);
    const std::string club=R"({"name":"club-yoke","usb":[{"vendorId":"0x1234","productId":"0x5678"}],"roll_travel_counts":9000,"pitch_travel_counts":7000,"roll_ceiling_units":3000,"pitch_ceiling_units":3500,"target_torque":30})";
    const auto club_yoke=named(parse_yoke_presets(R"({"schema":1,"yokes":[)"+club+"]}",builtin_yoke_presets()),"club-yoke");
    check(club_yoke.button_action_method==buttons::ActionMethod::direct);
    const auto methods=parse_yoke_presets(R"({"schema":1,"yokes":[{"name":"cls-e","button_action_method":"DIRECT"},{"name":"cls-e-ng","button_action_method":"VJoy"}]})",builtin_yoke_presets());
    check(named(methods,"cls-e").button_action_method==buttons::ActionMethod::direct && named(methods,"cls-e-ng").button_action_method==buttons::ActionMethod::vjoy);
    // A mapping that cannot be used never refuses the file: the bad entry is skipped and named, the rest of the file applies.
    const auto parse_cls_e=[&](const std::string& fields,std::vector<std::string>& skipped) {
        return named(parse_yoke_presets(R"({"schema":1,"yokes":[{"name":"cls-e",)"+fields+"}]}",builtin_yoke_presets(),skipped),"cls-e");
    };
    const auto mentions=[](const std::vector<std::string>& lines,const char* part) {
        for (const auto& l:lines) if (l.find(part)!=std::string::npos) return true;
        return false;
    };
    for (const char* bad:{R"("button_action_method":"HID")",R"("button_action_method":"v joy")",R"("button_action_method":"")",R"("button_action_method":1)",R"("button_action_method":null)"}) {
        std::vector<std::string> skipped;
        const auto cls=parse_cls_e(std::string(R"("title":"Kept anyway",)")+bad,skipped);
        check(cls.button_action_method==buttons::ActionMethod::vjoy && cls.title=="Kept anyway");   // the model keeps its own; the rest applies
        check(skipped.size()==1 && mentions(skipped,"must be VJoy or Direct") && mentions(skipped,"the model keeps VJoy"));
    }
    const std::string good=R"("node":2,"object":"0x2880","sub":3,"mask":"0x01")";
    const auto registers=[&](const std::string& extra,std::vector<std::string>& skipped) {
        return parse_cls_e(R"("button_registers":{"button_1":{)"+good+"}"+extra+"}",skipped);
    };
    {
        std::vector<std::string> skipped;
        const auto cls=registers("",skipped);
        check(skipped.empty() && cls.button_registers.size()==1 && cls.button_registers.at({buttons::InputKind::button,1}).operand==1);
    }
    struct BadRegister {const char* entry;const char* says;};
    for (const auto& bad:std::vector<BadRegister>{
            {R"(,"trigger":{"node":2,"object":"0x2880","sub":3,"mask":"0x01"})","is not a button or hat name"},
            {R"(,"button_17":{"node":2,"object":"0x2880","sub":3,"mask":"0x01"})","is not a button or hat name"},
            {R"(,"BUTTON_1":{"node":2,"object":"0x2880","sub":3,"mask":"0x02"})","is given twice"},
            {R"(,"button_2":1)","must be an object with node"},
            {R"(,"button_2":{"object":"0x2880","sub":3,"mask":"0x01"})","needs \"node\""},
            {R"(,"button_2":{"node":3,"object":"0x2880","sub":3,"mask":"0x01"})","node must be 1 or 2"},
            {R"(,"button_2":{"node":0,"object":"0x2880","sub":3,"mask":"0x01"})","node must be 1 or 2"},
            {R"(,"button_2":{"node":2,"object":"0x6040","sub":0,"mask":"0x01"})","is not an object the yoke's buttons may be read from"},
            {R"(,"button_2":{"node":2,"object":"0x2880","sub":3})","exactly one of"},
            {R"(,"button_2":{"node":2,"object":"0x2880","sub":3,"mask":"0x01","value":1})","exactly one of"},
            {R"(,"button_2":{"node":2,"object":"0x2880","sub":3,"mask":"0x100"})","mask is wider than the object"},
            {R"(,"button_2":{"node":2,"object":"0x2880","sub":3,"mask":"0x00"})","mask must not be 0"},
            {R"(,"button_2":{"node":2,"object":"0x2880","sub":3,"mask":1})","hex string"},
            {R"(,"button_2":{"node":2,"object":"0x2880","sub":3,"mask":"01"})","hex string"},
            {R"(,"hat_n":{"node":2,"object":"0x2880","sub":4,"value":256})","value is wider than the object"},
            {R"(,"hat_n":{"node":2,"object":"0x2880","sub":4,"value":-1})","must not be negative"},
            {R"(,"button_2":{"node":2,"object":"0x2880","sub":3,"mask":"0x01","extra":1})","unknown field"}}) {
        std::vector<std::string> skipped;
        const auto cls=registers(bad.entry,skipped);
        check(skipped.size()==1 && mentions(skipped,bad.says) && mentions(skipped,"skipped"));
        check(cls.button_registers.size()==1 && cls.button_registers.count({buttons::InputKind::button,1}));   // the good entry applied, the bad one did not
    }
    {   // The field itself not being a map: skipped whole, the model keeps its own.
        std::vector<std::string> skipped;
        const auto cls=parse_cls_e(R"("button_registers":[1])",skipped);
        check(skipped.size()==1 && mentions(skipped,"must be an object") && cls.button_registers==buttons::cls_e_can_layout());
    }
    // An empty map is allowed: canonical names only.
    check(named(parse_yoke_presets(R"({"schema":1,"yokes":[{"name":"cls-e","button_aliases":{}}]})",builtin_yoke_presets()),"cls-e").button_aliases.empty());
    // A new model without the field has none.
    const std::string model=R"({"name":"club-yoke","usb":[{"vendorId":"0x1234","productId":"0x5678"}],"roll_travel_counts":9000,"pitch_travel_counts":7000,"roll_ceiling_units":3000,"pitch_ceiling_units":3500,"target_torque":30})";
    check(named(parse_yoke_presets(R"({"schema":1,"yokes":[)"+model+"]}",builtin_yoke_presets()),"club-yoke").button_aliases.empty());
    // Aliases: each bad key or name is skipped and named, the good ones apply.
    const auto aliases=[&](const std::string& fields,std::vector<std::string>& skipped) {return parse_cls_e(R"("button_aliases":)"+fields,skipped);};
    {
        std::vector<std::string> skipped;
        const auto cls=aliases(R"({"button_1":["fire"],"hat_n":["north"]})",skipped);
        check(skipped.empty() && cls.button_aliases.size()==2 && cls.button_aliases.at("button_1")==(std::vector<std::string>{"fire"}));
    }
    struct BadAlias {const char* fields;const char* says;std::size_t kept;};
    for (const auto& bad:std::vector<BadAlias>{
            {R"({"button_1":["fire"],"trigger":["x"]})","is not a button or hat name",1},
            {R"({"button_1":["fire"],"button_17":["x"]})","is not a button or hat name",1},
            {R"({"button_1":["fire"],"BUTTON_1":["y"]})","is given twice",1},
            {R"({"button_1":["fire"],"button_2":"x"})","must be a list",1},
            {R"({"button_1":["fire",1]})","not a name in quotes",1},
            {R"({"button_1":["fire","x_hold"]})","not a usable name",1},
            {R"({"button_1":["fire","x_released"]})","not a usable name",1},
            {R"({"button_1":["fire","button_2"]})","not a usable name",1},
            {R"({"button_1":["fire"],"button_2":["fire"]})","is given twice",2},
            {R"({"button_1":["fire","FIRE"]})","is given twice",1}}) {
        std::vector<std::string> skipped;
        const auto cls=aliases(bad.fields,skipped);
        check(skipped.size()==1 && mentions(skipped,bad.says) && mentions(skipped,"skipped"));
        check(cls.button_aliases.size()==bad.kept && cls.button_aliases.at("button_1")==(std::vector<std::string>{"fire"}));   // the good name stays
    }
    {   // The bad name is left out of its list only: the other names of that control stay.
        std::vector<std::string> skipped;
        const auto cls=aliases(R"({"button_3":["a","b b","c"]})",skipped);
        check(skipped.size()==1 && cls.button_aliases.at("button_3")==(std::vector<std::string>{"a","c"}));
    }
    {   // The field not being a map: skipped whole, the model keeps its own.
        std::vector<std::string> skipped;
        const auto cls=aliases("[1]",skipped);
        check(skipped.size()==1 && mentions(skipped,"must be an object") && cls.button_aliases==builtin_cls_e().button_aliases);
    }
    {   // Several problems are all named, and they are named by model, so the pilot knows where to look.
        std::vector<std::string> skipped;
        parse_cls_e(R"("button_aliases":{"nope":["x"],"button_2":["x_hold"]},"button_action_method":"HID")",skipped);
        check(skipped.size()==3 && mentions(skipped,"yoke \"cls-e\": button_aliases.nope") && mentions(skipped,"yoke \"cls-e\": button_action_method"));
    }
    // Anything else wrong still refuses the whole file: only the button mappings are forgiving.
    for (const char* bad:{R"("roll_travel_counts":5)",R"("unknown_field":1)",R"("usb":[])"}) {
        bool refused=false;
        try {std::vector<std::string> skipped;parse_cls_e(std::string(R"("button_aliases":{"nope":["x"]},)")+bad,skipped);} catch (const PresetError&) {refused=true;}
        check(refused);
    }
    // The load carries the skipped lines out for the log and the screen, from a file that applied.
    {
        const auto loaded=load_yoke_models({},std::string(R"({"schema":1,"yokes":[{"name":"cls-e","button_aliases":{"nope":["x"],"button_1":["fire"]}}]})"));
        check(loaded.overrides_applied && loaded.skipped.size()==1 && mentions(loaded.skipped,"nope") && named(loaded.presets,"cls-e").button_aliases.at("button_1")==(std::vector<std::string>{"fire"}));
        const auto refused=load_yoke_models({},std::string(R"({"schema":1,"yokes":[{"name":"cls-e","button_aliases":{"nope":["x"]},"roll_travel_counts":5}]})"));
        check(!refused.overrides_applied && refused.skipped.empty());   // a refused file applied nothing, so there is nothing to report
    }
}
int main() {
    try {
        button_aliases();
        usb_ids_are_hex();
        the_built_ins();
        the_shipped_files_are_the_built_ins();
        loading_the_models();
        an_edit_of_a_built_in();
        a_new_model();
        resting_settings();
        bounds_and_malformed_files();
        changing_a_direction();
        the_input_stops();
        std::cout<<"preset tests passed\n";
        return 0;
    } catch (const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
