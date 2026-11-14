// Tests for yoke presets (brunner/yoke_preset.hpp, spec docs/yoke-presets.md): the built-in
// models, the presets file and every bound it must meet. No hardware.
#include "brunner/yoke_overrides.hpp"
#include "brunner/yoke_preset.hpp"
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
    check(n.roll_travel_counts==7350 && n.pitch_travel_counts==5300 && n.wheel_deg==90 && n.pitch_travel_mm==160);
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
// The shipped files (config/yokes/) must stay equal to the built-ins: the CLS-E's are what the project
// was built on, and the fallback must never differ from what a healthy install loads.
bool same_preset(const YokePreset& a,const YokePreset& b) {
    return a.name==b.name && a.title==b.title && a.verified==b.verified && a.verified_firmware==b.verified_firmware
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
        && a.pitch_units_per_lb==b.pitch_units_per_lb && a.roll_units_per_nm==b.roll_units_per_nm;
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
    std::string clash=ng;clash.replace(clash.find("\"cls-e-ng\""),10,"\"other\"");clash.replace(clash.find("139"),3,"129");
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
    check(m.rest.captured);                                                            // no resting settings given: read from the yoke, put back as found
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
// Resting settings: the built-ins', a model that gives its own, and every bound they must meet.
void resting_settings() {
    const auto cls_e=builtin_cls_e().rest,ng=builtin_cls_e_ng().rest;
    // The CLS-E's are the plugin's historical constants, unchanged; the NG's are its survey of 2026-11-12.
    check(!cls_e.captured && cls_e.roll_spring_percent==100 && cls_e.pitch_spring_percent==100 && cls_e.pitch_gain_denominator==6
        && cls_e.pitch_move_back==180 && cls_e.pitch_cutoff==0 && cls_e.mode==0 && cls_e.roll_curve[0]==100 && cls_e.roll_curve[8]==1000);
    check(!ng.captured && ng.roll_spring_percent==180 && ng.pitch_spring_percent==160 && ng.pitch_gain_denominator==3
        && ng.pitch_move_back==200 && ng.pitch_cutoff==40 && ng.mode==0x2605 && ng.roll_curve[0]==50 && ng.pitch_curve[8]==950);
    // A new model that gives a rest block has it (and is not captured); it overrides field by field.
    const std::string model=R"({"name":"club-yoke","usb":[{"vendor":4660,"product":22136}],"roll_travel_counts":9000,"pitch_travel_counts":7000,
        "roll_ceiling_units":3000,"pitch_ceiling_units":3500,"target_torque":30,"rest":{"roll_spring_percent":50,"pitch_spring_percent":60}})";
    const auto merged=parse_yoke_presets(R"({"schema":1,"yokes":[)"+model+"]}",builtin_yoke_presets());
    const auto& m=named(merged,"club-yoke");
    check(!m.rest.captured && m.rest.roll_spring_percent==50 && m.rest.pitch_spring_percent==60 && m.rest.roll_curve==cls_e.roll_curve);
    const auto edited=parse_yoke_presets(R"({"schema":1,"yokes":[{"name":"cls-e-ng","rest":{"pitch_cutoff":0,"mode":0}}]})",builtin_yoke_presets());
    check(named(edited,"cls-e-ng").rest.pitch_cutoff==0 && named(edited,"cls-e-ng").rest.mode==0 && named(edited,"cls-e-ng").rest.roll_spring_percent==180);
    const auto in_ng=[](const std::string& rest) {return R"({"schema":1,"yokes":[{"name":"cls-e-ng","rest":{)"+rest+"}}]}";};
    check(!refused(in_ng("\"pitch_move_back\":180")) && !refused(in_ng("\"pitch_move_back\":720")) && !refused(in_ng("\"mode\":9733")) && !refused(in_ng("\"mode\":1541")));
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
            "\"usb\":[]","\"usb\":[{\"vendor\":0,\"product\":5}]","\"usb\":[{\"vendor\":5,\"product\":70000}]","\"usb\":[{\"vendor\":5}]","\"usb\":\"x\"",
            "\"roll_spring_sign\":0","\"pitch_spring_sign\":2","\"pitch_spring_sign\":true","\"pitch_encoder_sign\":0",
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
// Changing one direction from the feel page (brunner/yoke_overrides.hpp).
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
int main() {
    try {
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
