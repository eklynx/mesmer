// SOURCE OF TRUTH for what differs between yoke MODELS: travel, force ceilings,
// target torque and wheel travel. Specification: docs/yoke-presets.md.
//
// A preset belongs to a model, identified by its USB vendor/product id, never by
// firmware: a firmware update must not change these values. The firmware a
// preset has been verified on is only a note (see YokePreset::verified_firmware).
//
// Built-ins: "cls-e" (the original, verified) and "cls-e-ng". More models, or
// changes to these, come from BrunnerDirect.yokes.json next to the profiles file,
// checked as strictly as the profiles are. A file that does not validate is
// refused whole and the built-ins stay in force.
//
// A preset is a *ceiling and a geometry*, not a permission to exceed the
// controller: the force ceiling actually used is the lower of the preset's and
// the controller's own limit register (drive.hpp).
#pragma once
#include "json.hpp"
#include "usb_ids.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace brunner {
inline constexpr char yokes_file[]="BrunnerDirect.yokes.json";
struct PresetError : std::runtime_error {using std::runtime_error::runtime_error;};

struct YokePreset {
    std::string name;                       // "cls-e-ng": lower case, digits, '-'
    std::string title;                      // shown to the pilot
    std::vector<UsbId> usb_ids;             // the bridges that identify this model
    // A model's first writes are the owner's call: unless `verified`, driving it
    // needs the owner's acknowledgement (Developer > Allow unverified yoke models). Firmware never gates;
    // a firmware outside this list is noted in the log and the Force Monitor.
    bool verified=false;
    std::vector<std::uint16_t> verified_firmware;
    // Travel either way from centre, in encoder counts. The force models scale by it.
    double roll_travel_counts=13000,pitch_travel_counts=12000;
    double wheel_deg=86;                    // the wheel's travel each way
    double pitch_travel_mm=0;               // information only (0: unknown)
    // Directions. The spring's frame (0x2116, "drive counts") is sign * the encoder (0x6064).
    // The CLS-E's -1 on both axes has never been confirmed on a powered yoke, and a new
    // model's is unknown: if the first powered check shows the rest mirrored, flip it
    // here (no rebuild). input_reverse flips the pilot's input direction (right/forward positive).
    int roll_encoder_sign=-1,pitch_encoder_sign=-1;
    bool roll_input_reverse=true,pitch_input_reverse=false;
    // Drive force units per physical unit, the same on every model so far.
    int pitch_units_per_lb=222,roll_units_per_nm=1000;
    // The most force the model's spec allows, per axis, in drive force units.
    // What is used is the lower of this and the controller's own limit register.
    int roll_ceiling_units=4500,pitch_ceiling_units=4500;
    // Headroom kept under the ceiling (the CLS-E's 500 of 4500): what the drive
    // does at its limit is unknown, so nothing is asked of it that close.
    double margin_fraction=500.0/4500.0;
    int target_torque=50;                   // 0x6071, the only value written
    // Status words (0x6041) the CLS-E flow expects, as numbers (no hex in JSON). stopped:
    // referenced and motors off (the 0x0800 limit flag ignored); ready_mask: every one of
    // these bits set while a spring is live; acquired_mask: set while centring. A model whose
    // words differ refuses safely and every refusal prints the words it saw: set them here.
    int status_stopped=0x0431,status_ready_mask=0x1017,status_acquired_mask=0x1004;
};

// ---- Built-in models ----
inline YokePreset builtin_cls_e() {
    YokePreset p;
    p.name="cls-e";p.title="Brunner CLS-E";
    p.usb_ids={{0x25bb,0x0081}};
    p.verified=true;p.verified_firmware={367};
    return p; // the defaults above are the CLS-E's
}
// The CLS-E NG. Travel is the midpoint of the controller's own limit flags
// (roll -7637/+7647, pitch -5458/+5478, seen 2026-11-12). Ceilings are the spec
// peaks in CLS-E units (pitch 190 N / 0.02 N per unit, roll 11 N.m / 0.001 N.m per
// unit); the controller's limit registers (2250 / 2150) are lower and win.
inline YokePreset builtin_cls_e_ng() {
    YokePreset p;
    p.name="cls-e-ng";p.title="Brunner CLS-E NG";
    p.usb_ids={{0x25bb,0x008b}};
    p.verified=false;
    p.roll_travel_counts=7642;p.pitch_travel_counts=5468;
    p.wheel_deg=90;p.pitch_travel_mm=160;
    p.roll_ceiling_units=11000;p.pitch_ceiling_units=9500;
    p.target_torque=25;
    // At rest, referenced with its own spring running, the NG reads 0x1427: it lacks the CLS-E's
    // bit 0x0010 in that state, so its ready mask is 0x1007 (seen 2026-11-12). Its stopped word
    // is not yet known; the CLS-E's is kept until a run shows it.
    p.status_ready_mask=0x1007;
    return p;
}
inline std::vector<YokePreset> builtin_yoke_presets() {return {builtin_cls_e(),builtin_cls_e_ng()};}

// ---- Validation (every bound a file entry must meet) ----
inline void validate_preset(const YokePreset& p) {
    const auto fail=[&](const std::string& why) {throw PresetError("yoke preset \""+p.name+"\": "+why);};
    if (p.name.empty() || p.name.size()>32) fail("name must be 1 to 32 characters");
    for (const char c:p.name) if (!((c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='-')) fail("name may use only a-z, 0-9 and '-'");
    if (p.title.size()>64) fail("title is longer than 64 characters");
    if (p.usb_ids.empty() || p.usb_ids.size()>8) fail("needs 1 to 8 USB ids");
    for (const auto& id:p.usb_ids) if (id.vendor==0 || id.product==0) fail("a USB id is zero");
    if (p.verified_firmware.size()>16) fail("more than 16 verified firmware versions");
    for (const auto f:p.verified_firmware) if (f==0) fail("a verified firmware version is zero");
    const auto in=[](double v,double lo,double hi) {return std::isfinite(v) && v>=lo && v<=hi;};
    if (!in(p.roll_travel_counts,1000,30000) || !in(p.pitch_travel_counts,1000,30000)) fail("travel counts must be 1000 to 30000");
    if (!in(p.wheel_deg,10,180)) fail("wheel_deg must be 10 to 180");
    if (!in(p.pitch_travel_mm,0,1000)) fail("pitch_travel_mm must be 0 to 1000");
    if ((p.roll_encoder_sign!=1 && p.roll_encoder_sign!=-1) || (p.pitch_encoder_sign!=1 && p.pitch_encoder_sign!=-1)) fail("encoder signs must be 1 or -1");
    if (p.pitch_units_per_lb<50 || p.pitch_units_per_lb>1000) fail("pitch_units_per_lb must be 50 to 1000");
    if (p.roll_units_per_nm<100 || p.roll_units_per_nm>10000) fail("roll_units_per_nm must be 100 to 10000");
    if (p.roll_ceiling_units<100 || p.roll_ceiling_units>20000 || p.pitch_ceiling_units<100 || p.pitch_ceiling_units>20000)
        fail("force ceilings must be 100 to 20000 units");
    if (!in(p.margin_fraction,0.02,0.5)) fail("margin_fraction must be 0.02 to 0.5");
    if (p.target_torque<1 || p.target_torque>1000) fail("target_torque must be 1 to 1000");
    for (const int w:{p.status_stopped,p.status_ready_mask,p.status_acquired_mask}) if (w<1 || w>0xffff) fail("status words must be 1 to 65535");
}
// No two models may claim one USB id: the model must follow from the device.
inline void validate_presets(const std::vector<YokePreset>& presets) {
    for (std::size_t i=0;i<presets.size();++i) {
        validate_preset(presets[i]);
        for (std::size_t j=0;j<i;++j) {
            if (presets[i].name==presets[j].name) throw PresetError("duplicate yoke preset name \""+presets[i].name+"\"");
            for (const auto& a:presets[i].usb_ids) for (const auto& b:presets[j].usb_ids)
                if (a.vendor==b.vendor && a.product==b.product)
                    throw PresetError("yoke presets \""+presets[j].name+"\" and \""+presets[i].name+"\" claim the same USB id");
        }
    }
}

// ---- The presets file: BrunnerDirect.yokes.json ----
// {"schema":1,"yokes":[{"name":"cls-e-ng","roll_travel_counts":7800, ...}, {"name":"new-model", ...}]}
// An entry whose name is a built-in's starts from it and changes only the fields
// it gives. A new name must give its USB ids, both travels, both ceilings and
// the target torque (nothing is assumed for an unknown model); it is unverified
// unless it says otherwise. Fields: name, title, usb [{vendor,product}], verified,
// verified_firmware [..], roll_travel_counts, pitch_travel_counts, wheel_deg,
// pitch_travel_mm, pitch_units_per_lb, roll_units_per_nm, roll_ceiling_units,
// pitch_ceiling_units, margin_fraction, target_torque, status_stopped,
// status_ready_mask, status_acquired_mask, roll_encoder_sign,
// pitch_encoder_sign, roll_input_reverse, pitch_input_reverse. Unknown fields are refused.
namespace preset_detail {
inline double number(const Json& j,const std::string& what) {
    if (j.kind==Json::Kind::integer) return static_cast<double>(j.integer);
    if (j.kind==Json::Kind::real) return j.real;
    throw PresetError(what+" must be a number");
}
inline int whole(const Json& j,const std::string& what) {
    if (j.kind!=Json::Kind::integer || j.integer<-1000000 || j.integer>1000000) throw PresetError(what+" must be a whole number");
    return static_cast<int>(j.integer);
}
inline bool flag(const Json& j,const std::string& what) {
    if (j.kind!=Json::Kind::boolean) throw PresetError(what+" must be true or false");
    return j.boolean;
}
}
inline std::vector<YokePreset> parse_yoke_presets(std::string_view text,const std::vector<YokePreset>& builtins) {
    using namespace preset_detail;
    Json root;
    try {root=parse_json(text);} catch (const JsonError& e) {throw PresetError(std::string("not JSON: ")+e.what());}
    if (root.kind!=Json::Kind::object) throw PresetError("the presets file must be a JSON object");
    const auto* schema=root.get("schema");
    if (!schema || schema->kind!=Json::Kind::integer || schema->integer!=1) throw PresetError("unsupported presets schema (expected 1)");
    for (const auto& m:root.members) if (m.first!="schema" && m.first!="yokes") throw PresetError("unknown top-level field \""+m.first+"\"");
    const auto* yokes=root.get("yokes");
    if (!yokes || yokes->kind!=Json::Kind::array) throw PresetError("\"yokes\" must be a list");
    if (yokes->items.size()>32) throw PresetError("more than 32 yoke presets");
    std::vector<YokePreset> result=builtins;
    for (const auto& entry:yokes->items) {
        if (entry.kind!=Json::Kind::object) throw PresetError("each yoke must be an object");
        const auto* name=entry.get("name");
        if (!name || name->kind!=Json::Kind::string) throw PresetError("a yoke has no name");
        auto found=std::find_if(result.begin(),result.end(),[&](const YokePreset& p) {return p.name==name->text;});
        const bool is_new=found==result.end();
        YokePreset p;
        if (!is_new) p=*found;
        else {p.name=name->text;p.title=name->text;p.verified=false;p.verified_firmware.clear();}
        const std::string at="yoke \""+name->text+"\": ";
        static const char* required[]={"usb","roll_travel_counts","pitch_travel_counts","roll_ceiling_units","pitch_ceiling_units","target_torque"};
        if (is_new) for (const char* key:required) if (!entry.get(key)) throw PresetError(at+"a new model must give \""+key+"\"");
        for (const auto& m:entry.members) {
            const auto& key=m.first;const auto& v=m.second;const auto what=at+key;
            if (key=="name") continue;
            else if (key=="title") {if (v.kind!=Json::Kind::string) throw PresetError(what+" must be text");p.title=v.text;}
            else if (key=="usb") {
                if (v.kind!=Json::Kind::array) throw PresetError(what+" must be a list");
                p.usb_ids.clear();
                for (const auto& id:v.items) {
                    const auto* vendor=id.kind==Json::Kind::object?id.get("vendor"):nullptr;
                    const auto* product=id.kind==Json::Kind::object?id.get("product"):nullptr;
                    if (!vendor || !product) throw PresetError(what+" entries need vendor and product");
                    const int vn=whole(*vendor,what+".vendor"),pn=whole(*product,what+".product");
                    if (vn<1 || vn>0xffff || pn<1 || pn>0xffff) throw PresetError(what+" ids must be 1 to 65535");
                    p.usb_ids.push_back({static_cast<std::uint16_t>(vn),static_cast<std::uint16_t>(pn)});
                }
            }
            else if (key=="verified") p.verified=flag(v,what);
            else if (key=="verified_firmware") {
                if (v.kind!=Json::Kind::array) throw PresetError(what+" must be a list");
                p.verified_firmware.clear();
                for (const auto& f:v.items) {
                    const int n=whole(f,what);
                    if (n<1 || n>0xffff) throw PresetError(what+" entries must be 1 to 65535");
                    p.verified_firmware.push_back(static_cast<std::uint16_t>(n));
                }
            }
            else if (key=="roll_travel_counts") p.roll_travel_counts=number(v,what);
            else if (key=="pitch_travel_counts") p.pitch_travel_counts=number(v,what);
            else if (key=="wheel_deg") p.wheel_deg=number(v,what);
            else if (key=="pitch_travel_mm") p.pitch_travel_mm=number(v,what);
            else if (key=="pitch_units_per_lb") p.pitch_units_per_lb=whole(v,what);
            else if (key=="roll_units_per_nm") p.roll_units_per_nm=whole(v,what);
            else if (key=="roll_ceiling_units") p.roll_ceiling_units=whole(v,what);
            else if (key=="pitch_ceiling_units") p.pitch_ceiling_units=whole(v,what);
            else if (key=="margin_fraction") p.margin_fraction=number(v,what);
            else if (key=="target_torque") p.target_torque=whole(v,what);
            else if (key=="status_stopped") p.status_stopped=whole(v,what);
            else if (key=="status_ready_mask") p.status_ready_mask=whole(v,what);
            else if (key=="status_acquired_mask") p.status_acquired_mask=whole(v,what);
            else if (key=="roll_encoder_sign") p.roll_encoder_sign=whole(v,what);
            else if (key=="pitch_encoder_sign") p.pitch_encoder_sign=whole(v,what);
            else if (key=="roll_input_reverse") p.roll_input_reverse=flag(v,what);
            else if (key=="pitch_input_reverse") p.pitch_input_reverse=flag(v,what);
            else throw PresetError(at+"unknown field \""+key+"\"");
        }
        if (is_new) result.push_back(std::move(p)); else *found=std::move(p);
    }
    validate_presets(result);
    return result;
}

// ---- Lookup ----
inline const YokePreset* find_yoke_preset(const std::vector<YokePreset>& presets,UsbId id) {
    for (const auto& p:presets) for (const auto& u:p.usb_ids) if (u.vendor==id.vendor && u.product==id.product) return &p;
    return nullptr;
}
// Every USB id the presets name: the devices Mesmer will open.
inline std::vector<UsbId> preset_usb_ids(const std::vector<YokePreset>& presets) {
    std::vector<UsbId> ids;
    for (const auto& p:presets) ids.insert(ids.end(),p.usb_ids.begin(),p.usb_ids.end());
    return ids;
}
// Whether the firmware is one the model's values were verified on (a note, never a gate).
inline bool firmware_verified(const YokePreset& p,std::uint16_t firmware) {
    return std::find(p.verified_firmware.begin(),p.verified_firmware.end(),firmware)!=p.verified_firmware.end();
}
}
