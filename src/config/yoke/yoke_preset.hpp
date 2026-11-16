// SOURCE OF TRUTH for what differs between yoke MODELS: travel, force ceilings,
// target torque and wheel travel. Specification: docs/dev/yoke-presets.md.
//
// A preset belongs to a model, identified by its USB vendor/product id, never by
// firmware: a firmware update must not change these values. The firmware a
// preset has been verified on is only a note (see YokePreset::verified_firmware).
//
// Models are the files in the plugin's yokes/ folder (config/yokes/): cls-e.json (the original,
// verified) and cls-e-ng.json. The compiled-in "built-ins" below are the fallback when that folder
// is missing or refused, and a test pins them equal to the files. The user's changes and new models
// go in BrunnerDirect.yokes.json next to the profiles file, patching by name; every file is checked as
// strictly as the profiles are, and one that does not validate is refused whole.
//
// A preset is a *ceiling and a geometry*, not a permission to exceed the
// controller: the force ceiling actually used is the lower of the preset's and
// the controller's own limit register (brunner/drive.hpp).
#pragma once
#include "config/yoke/yoke_errors.hpp"
#include "buttons/controls.hpp"
#include "buttons/input/can_layout.hpp"
#include "buttons/methods.hpp"
#include "config/json.hpp"
#include "hid/usb_ids.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace brunner {
inline constexpr char yokes_file[]="BrunnerDirect.yokes.json";
struct PresetError : std::runtime_error {using std::runtime_error::runtime_error;};

// What the plugin leaves the controllers at between sessions (after force feel stops, before a centring,
// in recovery and in "Reset yoke"): the model's own resting settings, so a model is left as it was found.
// The defaults are the CLS-E's. `mode` and `pitch_cutoff` of 0 mean "not written". `captured` means the
// preset gives none: the plugin reads these registers when a session starts and writes exactly that back.
// The NG's come from its register survey of 2026-11-12 (the state it rests in at power-on).
struct RestSettings {
    std::array<std::uint16_t,9> roll_curve{{100,350,400,450,500,600,750,900,1000}},pitch_curve{{100,350,400,450,500,600,750,900,1000}};
    int roll_spring_percent=100,pitch_spring_percent=100;   // 0x2114
    int pitch_gain_denominator=6,pitch_move_back=180;       // pitch's drag gains
    int pitch_cutoff=0;                                     // 0x211f, pitch only; 0: not written
    int mode=0;                                             // 0x2101:01 left at rest, bit 13 (reference at power-on) included; 0: not written
    bool captured=false;
    bool operator==(const RestSettings& o) const {
        return roll_curve==o.roll_curve && pitch_curve==o.pitch_curve && roll_spring_percent==o.roll_spring_percent
            && pitch_spring_percent==o.pitch_spring_percent && pitch_gain_denominator==o.pitch_gain_denominator
            && pitch_move_back==o.pitch_move_back && pitch_cutoff==o.pitch_cutoff && mode==o.mode && captured==o.captured;
    }
};

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
    // The encoder's raw counts at the two stops of each axis (low < 0 < high), which is what the
    // position sent to X-Plane is scaled by: low reads -1 and high +1 (each side by its own span, from
    // encoder zero). Unlike the travel above (how far the spring's centre may be written, and the
    // force models' scale) these are where the yoke physically stops. 0 and 0 (unset) means
    // -travel..+travel, the CLS-E's behaviour. Edit in BrunnerDirect.yokes.json.
    int roll_input_low=0,roll_input_high=0,pitch_input_low=0,pitch_input_high=0;
    double wheel_deg=86;                    // the wheel's travel each way
    double pitch_travel_mm=0;               // information only (0: unknown)
    // Directions, three facts per axis. encoder_sign: the model's counts (forward / right positive,
    // what the force models and the page use) are sign * the encoder (0x6064); it only reads the
    // encoder. spring_sign: the controller's spring frame (0x2116 and the constant force) is
    // sign * the encoder, a fact about the hardware. Every value the plugin writes to those registers
    // is the model's times encoder_sign * spring_sign (drive.hpp, controller_counts): the CLS-E's -1
    // and -1 make that 1, a model whose encoder runs the other way (the NG's pitch: +1 and -1) mirrors
    // its writes. None is confirmed on a powered yoke except where a model's notes say so; edit here
    // (no rebuild). input_reverse flips the pilot's input direction (right/forward positive).
    int roll_encoder_sign=-1,pitch_encoder_sign=-1;
    int roll_spring_sign=-1,pitch_spring_sign=-1;
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
    // How far the yoke may drift while a centring decides it has settled, in percent of the axis's own
    // travel (0.1 = +-0.1 %, decimals allowed; 0 to 5; 0 asks for the old fixed 2 counts). A sensitive
    // yoke wanders a little at rest: the centring settles when it holds within this of where the hold began.
    double centre_drift_percent=0.1;
    // Status words (0x6041) the CLS-E flow expects, as numbers (no hex in JSON). stopped:
    // referenced and motors off (the 0x0800 limit flag ignored); ready_mask: every one of
    // these bits set while a spring is live; acquired_mask: set while centring. A model whose
    // words differ refuses safely and every refusal prints the words it saw: set them here.
    int status_stopped=0x0431,status_ready_mask=0x1017,status_acquired_mask=0x1004;
    // The force-mode word (0x2101:01) the controllers rest in, with the reference request,
    // vibration and the other bits the flow sets and clears (bits 13, 11, 6, 5) left out.
    // The flow refuses any other mode, so a model whose resting mode differs needs its own.
    int spring_mode=0x0205;
    // The force mode a controller may be found in before the plugin has written one (what it powers on in),
    // in the same terms as spring_mode, for a mode a controller may have been left in. The flow still works in
    // spring_mode and writes it itself; this only widens what the first check accepts. 0: the same as spring_mode.
    int power_on_spring_mode=0;
    // True for a model whose controllers idle with their motors enabled (its own spring
    // running, status bit 0x0004 set). Centring then disables them first, with the standard
    // shutdown, instead of refusing; the CLS-E, which idles disabled, keeps the refusal.
    bool idle_enabled=false;
    RestSettings rest;
    // What the yoke's controls are called, as a map of lists keyed like button_registers: the key is a control (button_1..button_16,
    // hat_n..hat_nw) and the list its friendly names (buttons/controls.hpp). Giving the field in BrunnerDirect.yokes.json replaces
    // the model's whole map.
    std::map<std::string,std::vector<std::string>> button_aliases;
    // Where each of the yoke's buttons and hat positions is on the bridge (buttons/input/can_layout.hpp), as a map from the control's
    // name (button_1.., hat_n..) to the controller object and the mask or value that says it is pressed: the original CLS-E's are
    // bits 16 to 31 of node 2's 0x60fd, the NG's bits of its own record 0x2880:3 and the values of 0x2880:4. A control with no entry
    // is one the yoke does not have. Only objects on brunner::button_register's read-only allowlist may be named.
    buttons::CanLayout button_registers;
    // What the plugin does with this model's buttons (buttons/methods.hpp). A model that gives none runs button_commands. VJoy needs
    // a virtual joystick: where the operating system has none it is Direct.
    buttons::ActionMethod button_action_method=buttons::default_action_method;
};

// ---- Built-in models ----
// The original CLS-E's controls, from the pressed-one-by-one survey of 2026-11-10 (docs/buttons.md): slots 1-5 are the left
// grip, 7-11 the right grip, 12-14 the panel buttons; slot 6 is not fitted.
inline std::map<std::string,std::vector<std::string>> cls_e_button_aliases() {
    return {{"button_1",{"left_index"}},{"button_2",{"left_thumb_left"}},{"button_3",{"left_thumb_right"}},
            {"button_4",{"left_rocker_back"}},{"button_5",{"left_rocker_forward"}},
            {"button_7",{"right_index"}},{"button_8",{"right_thumb_left"}},{"button_9",{"right_thumb_right"}},
            {"button_10",{"right_rocker_back"}},{"button_11",{"right_rocker_forward"}},
            {"button_12",{"panel_middle"}},{"button_13",{"panel_right"}},{"button_14",{"panel_left"}}};
}
// The NG's controls: its own button record 0x2880 holds six buttons (1 trigger, 2 top left, 3 top right, 4 rocker down, 5 rocker up,
// 6 hat push) and the hat as the eight positions (checked on the NG, 2026-11-16, docs/dev/button-commands.md). The original CLS-E's
// 0x60fd carries buttons 1 to 5 of these too, but not the hat push or the hat.
inline std::map<std::string,std::vector<std::string>> cls_e_ng_button_aliases() {
    return {{"button_1",{"trigger"}},{"button_2",{"top_left"}},{"button_3",{"top_right"}},
            {"button_4",{"rocker_down"}},{"button_5",{"rocker_up"}},{"button_6",{"hat_push"}}};
}
inline YokePreset builtin_cls_e() {
    YokePreset p;
    p.name="cls-e";p.title="Brunner CLS-E";
    p.usb_ids={{0x25bb,0x0081}};
    p.verified=true;p.verified_firmware={367};
    p.button_aliases=cls_e_button_aliases();
    p.button_registers=buttons::cls_e_can_layout();
    p.button_action_method=buttons::ActionMethod::vjoy;   // the CLS-E: its buttons on a virtual joystick
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
    p.verified=true;p.verified_firmware={642};
    p.button_aliases=cls_e_ng_button_aliases();
    p.button_registers=buttons::cls_e_ng_can_layout();
    p.button_action_method=buttons::ActionMethod::direct;   // the NG: its buttons run X-Plane commands
    // Travel is the range the controller accepts for the resting point (0x2116), which bounds where the
    // force models may put the centre. Pitch clamps at +-5300 (measured 2026-11-13: a write of 5468 or 5369
    // read back 5300). Roll is inferred: the NG's own homing sweep stopped at roll -7412 and pitch -5320
    // (pitch's clamp plus a 20 count overshoot), so roll's clamp is about 7392; 7350 stays under it. The
    // controller's end-stop flag is further out (roll +-7640, pitch +-5460). Edit in BrunnerDirect.yokes.json.
    p.roll_travel_counts=7350;p.pitch_travel_counts=5300;
    // Where the yoke physically stops (the encoder's extremes with the yoke pushed to each stop, learned
    // 2026-11-13; the controller's own end-stop flag sets at roll -7637/+7647 and pitch -5458/+5478).
    p.roll_input_low=-7641;p.roll_input_high=7649;p.pitch_input_low=-5446;p.pitch_input_high=5461;
    p.wheel_deg=90;p.pitch_travel_mm=160;
    p.roll_ceiling_units=11000;p.pitch_ceiling_units=9500;
    p.target_torque=25;
    // At rest, referenced with its own spring running, the NG reads 0x1427: it lacks the CLS-E's
    // bit 0x0010 in that state, so its ready mask is 0x1007 (seen 2026-11-12). It idles with its
    // motors enabled; after the standard shutdown it reads 0x0421 (seen 2026-11-12, Log.txt of
    // the first drive attempt), the CLS-E's stopped word less bit 0x0010. Its force mode at rest
    // is 0x2605, and 0x0605 once the reference request (bit 13) is taken off.
    p.status_ready_mask=0x1007;
    p.status_stopped=0x0421;
    // The NG's own mode. The CLS-E's 0x0205 (the same less bit 0x0400) was tried on it on 2026-11-14 and left in
    // the controllers' registers; the first check still accepts that mode (power_on_spring_mode), and the first
    // session writes 0x0605 / 0x2605 back.
    p.spring_mode=0x0605;p.power_on_spring_mode=0x0205;
    p.idle_enabled=true;
    // Its pitch encoder runs the other way from the CLS-E's: pulling lowers it. The model's forward is
    // therefore +encoder (pitch_encoder_sign 1), the input is reversed, and the spring's frame stays
    // -encoder (as the CLS-E's; the yoke held where it was when a centre of sign*encoder was written),
    // so the plugin mirrors its pitch writes (spring_sign -1 against encoder_sign 1).
    p.pitch_encoder_sign=1;p.pitch_spring_sign=-1;
    p.pitch_input_reverse=true;
    // Its settings at rest, from the survey of 2026-11-12 (curve 50..950, spring 180 roll / 160 pitch, pitch
    // drag 1/3, move-back 200, cut-off 40, mode 0x2605). Writing the CLS-E's instead overwrote these, and the
    // NG kept them (with bit 13 cleared it no longer referenced itself at power-on).
    p.rest.roll_curve=p.rest.pitch_curve={{50,150,250,350,450,550,650,800,950}};
    p.rest.roll_spring_percent=180;p.rest.pitch_spring_percent=160;
    p.rest.pitch_gain_denominator=3;p.rest.pitch_move_back=200;p.rest.pitch_cutoff=40;
    p.rest.mode=0x2605;
    return p;
}
inline std::vector<YokePreset> builtin_yoke_presets() {return {builtin_cls_e(),builtin_cls_e_ng()};}

// ---- Validation (every bound a file entry must meet) ----
// Why a button signal cannot be used, or nullopt (buttons/input/can_layout.hpp): the node, the object (read-only allowlist) and the
// width of the mask or value. The parser skips an entry with a problem; validate_preset holds the built-in presets to the same rule.
inline std::optional<std::string> button_signal_problem(const buttons::CanSignal& signal) {
    if (signal.node<1 || signal.node>2) return yoke_errors::signal_node;
    const auto bytes=signal.bytes();
    if (bytes==0) return yoke_errors::signal_object;
    const std::uint64_t limit=bytes>=4?0xffffffffull:(1ull<<(bytes*8))-1;
    if (signal.operand>limit) return yoke_errors::signal_too_wide(signal.is_mask,bytes);
    if (signal.is_mask && signal.operand==0) return yoke_errors::signal_mask_zero;
    return std::nullopt;
}
inline void validate_preset(const YokePreset& p) {
    const auto fail=[&](const std::string& why) {throw PresetError(yoke_errors::preset_invalid(p.name,why));};
    if (p.name.empty() || p.name.size()>32) fail(yoke_errors::name_length);
    for (const char c:p.name) if (!((c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='-')) fail(yoke_errors::name_characters);
    if (p.title.size()>64) fail(yoke_errors::title_length);
    if (p.usb_ids.empty() || p.usb_ids.size()>8) fail(yoke_errors::usb_id_count);
    for (const auto& id:p.usb_ids) if (id.vendor==0 || id.product==0) fail(yoke_errors::usb_id_zero);
    if (p.verified_firmware.size()>16) fail(yoke_errors::firmware_count);
    for (const auto f:p.verified_firmware) if (f==0) fail(yoke_errors::firmware_zero);
    const auto in=[](double v,double lo,double hi) {return std::isfinite(v) && v>=lo && v<=hi;};
    if (!in(p.roll_travel_counts,1000,30000) || !in(p.pitch_travel_counts,1000,30000)) fail(yoke_errors::travel_counts);
    const auto input_ends=[&](int low,int high,const char* axis) {
        if (low==0 && high==0) return;   // unset: -travel..+travel
        if (low>-1000 || low<-30000 || high<1000 || high>30000) fail(yoke_errors::input_ends(axis));
    };
    input_ends(p.roll_input_low,p.roll_input_high,"roll");
    input_ends(p.pitch_input_low,p.pitch_input_high,"pitch");
    if (!in(p.wheel_deg,10,180)) fail(yoke_errors::wheel_deg);
    if (!in(p.pitch_travel_mm,0,1000)) fail(yoke_errors::pitch_travel_mm);
    if ((p.roll_encoder_sign!=1 && p.roll_encoder_sign!=-1) || (p.pitch_encoder_sign!=1 && p.pitch_encoder_sign!=-1)) fail(yoke_errors::encoder_signs);
    if ((p.roll_spring_sign!=1 && p.roll_spring_sign!=-1) || (p.pitch_spring_sign!=1 && p.pitch_spring_sign!=-1)) fail(yoke_errors::spring_signs);
    if (p.pitch_units_per_lb<50 || p.pitch_units_per_lb>1000) fail(yoke_errors::pitch_units_per_lb);
    if (p.roll_units_per_nm<100 || p.roll_units_per_nm>10000) fail(yoke_errors::roll_units_per_nm);
    if (p.roll_ceiling_units<100 || p.roll_ceiling_units>20000 || p.pitch_ceiling_units<100 || p.pitch_ceiling_units>20000)
        fail(yoke_errors::force_ceilings);
    if (!in(p.margin_fraction,0.02,0.5)) fail(yoke_errors::margin_fraction);
    if (p.target_torque<1 || p.target_torque>1000) fail(yoke_errors::target_torque);
    if (!in(p.centre_drift_percent,0,5)) fail(yoke_errors::centre_drift_percent);
    for (const int w:{p.status_stopped,p.status_ready_mask,p.status_acquired_mask}) if (w<1 || w>0xffff) fail(yoke_errors::status_words);
    {
        const auto& r=p.rest;
        for (const auto v:r.roll_curve) if (v<5 || v>1000) fail(yoke_errors::rest_curve_points);
        for (const auto v:r.pitch_curve) if (v<5 || v>1000) fail(yoke_errors::rest_curve_points);
        if (r.roll_spring_percent<1 || r.roll_spring_percent>1000 || r.pitch_spring_percent<1 || r.pitch_spring_percent>1000) fail(yoke_errors::rest_spring_percents);
        if (r.pitch_gain_denominator<1 || r.pitch_gain_denominator>6) fail(yoke_errors::rest_pitch_gain_denominator);
        if (r.pitch_move_back<180 || r.pitch_move_back>720) fail(yoke_errors::rest_pitch_move_back);
        if (r.pitch_cutoff<0 || r.pitch_cutoff>101) fail(yoke_errors::rest_pitch_cutoff);
        const int flow_bits=(1<<13)|(1<<11)|(1<<6)|(1<<5);
        if (r.mode!=0 && (r.mode<1 || r.mode>0xffff || (r.mode&~flow_bits)!=p.spring_mode))
            fail(yoke_errors::rest_mode);
    }
    if (p.power_on_spring_mode!=0 && (p.power_on_spring_mode<1 || p.power_on_spring_mode>0xffff || (p.power_on_spring_mode&((1<<13)|(1<<11)|(1<<6)|(1<<5)))!=0))
        fail(yoke_errors::power_on_spring_mode);
    if (p.spring_mode<1 || p.spring_mode>0xffff || (p.spring_mode&((1<<13)|(1<<11)|(1<<6)|(1<<5)))!=0)
        fail(yoke_errors::spring_mode);
    if (p.button_aliases.size()>64) fail(yoke_errors::too_many_aliases);
    for (const auto& alias:p.button_aliases) if (alias.second.size()>16) fail(yoke_errors::alias_lists_too_many(alias.first));
    try {buttons::ControlMap controls(p.button_aliases);} catch (const buttons::ControlError& e) {fail(e.what());}
    for (const auto& entry:p.button_registers)
        if (const auto why=button_signal_problem(entry.second)) fail(yoke_errors::button_register(buttons::input_name(entry.first),*why));
}
// No two models may claim one USB id: the model must follow from the device.
inline void validate_presets(const std::vector<YokePreset>& presets) {
    for (std::size_t i=0;i<presets.size();++i) {
        validate_preset(presets[i]);
        for (std::size_t j=0;j<i;++j) {
            if (presets[i].name==presets[j].name) throw PresetError(yoke_errors::duplicate_name(presets[i].name));
            for (const auto& a:presets[i].usb_ids) for (const auto& b:presets[j].usb_ids)
                if (a.vendor==b.vendor && a.product==b.product)
                    throw PresetError(yoke_errors::same_usb_id(presets[j].name,presets[i].name));
        }
    }
}

// ---- The presets file: BrunnerDirect.yokes.json ----
// {"schema":1,"yokes":[{"name":"cls-e-ng","roll_travel_counts":7800, ...}, {"name":"new-model", ...}]}
// An entry whose name is a built-in's starts from it and changes only the fields
// it gives. A new name must give its USB ids, both travels, both ceilings and
// the target torque (nothing is assumed for an unknown model); it is unverified
// unless it says otherwise. Fields: name, title, usb [{vendorId,productId}] (each a hex string such as "0x25BB"), verified,
// verified_firmware [..], roll_travel_counts, pitch_travel_counts, roll_input_low, roll_input_high,
// pitch_input_low, pitch_input_high, wheel_deg,
// pitch_travel_mm, pitch_units_per_lb, roll_units_per_nm, roll_ceiling_units,
// pitch_ceiling_units, margin_fraction, target_torque, centre_drift_percent, status_stopped,
// status_ready_mask, status_acquired_mask, spring_mode, power_on_spring_mode, idle_enabled, rest {roll_curve, pitch_curve,
// roll_spring_percent, pitch_spring_percent, pitch_gain_denominator, pitch_move_back, pitch_cutoff, mode},
// roll_encoder_sign,
// pitch_encoder_sign, roll_spring_sign, pitch_spring_sign, roll_input_reverse, pitch_input_reverse,
// button_aliases {"button_4": ["rocker_down", "other_alias"], ...}, button_action_method "VJoy" | "Direct" (any case), button_registers {"button_1": {"node": 2, "object":
// "0x2880", "sub": 3, "mask": "0x01"}, "hat_n": {"node": 2, "object": "0x2880", "sub": 4, "value": 1}, ...}. Unknown fields are refused.
namespace preset_detail {
inline double number(const Json& j,const std::string& what) {
    if (j.kind==Json::Kind::integer) return static_cast<double>(j.integer);
    if (j.kind==Json::Kind::real) return j.real;
    throw PresetError(yoke_errors::must_be_a_number(what));
}
inline int whole(const Json& j,const std::string& what) {
    if (j.kind!=Json::Kind::integer || j.integer<-1000000 || j.integer>1000000) throw PresetError(yoke_errors::must_be_a_whole_number(what));
    return static_cast<int>(j.integer);
}
inline std::uint16_t hex_id(const Json& j,const std::string& what) {
    const auto id=j.kind==Json::Kind::string?parse_hex_id(j.text):std::nullopt;
    if (!id) throw PresetError(yoke_errors::must_be_a_hex_id(what));
    return *id;
}
inline bool flag(const Json& j,const std::string& what) {
    if (j.kind!=Json::Kind::boolean) throw PresetError(yoke_errors::must_be_a_boolean(what));
    return j.boolean;
}
}
// Happy path: the models with the file's changes applied. A bad button mapping (button_aliases, button_registers, button_action_method)
// never refuses the file: the bad entry is skipped, the rest applies, and one line per skipped entry is added to `skipped`
// (the plugin logs them and shows them on screen). Anything else wrong still throws PresetError and refuses the whole file.
inline std::vector<YokePreset> parse_yoke_presets(std::string_view text,const std::vector<YokePreset>& builtins,std::vector<std::string>& skipped) {
    using namespace preset_detail;
    Json root;
    try {root=parse_json(text);} catch (const JsonError& e) {throw PresetError(yoke_errors::not_json(e.what()));}
    if (root.kind!=Json::Kind::object) throw PresetError(yoke_errors::file_not_an_object);
    const auto* schema=root.get("schema");
    if (!schema || schema->kind!=Json::Kind::integer || schema->integer!=1) throw PresetError(yoke_errors::unsupported_schema);
    for (const auto& m:root.members) if (m.first!="schema" && m.first!="yokes") throw PresetError(yoke_errors::unknown_top_level_field(m.first));
    const auto* yokes=root.get("yokes");
    if (!yokes || yokes->kind!=Json::Kind::array) throw PresetError(yoke_errors::yokes_not_a_list);
    if (yokes->items.size()>32) throw PresetError(yoke_errors::too_many_presets);
    std::vector<YokePreset> result=builtins;
    for (const auto& entry:yokes->items) {
        if (entry.kind!=Json::Kind::object) throw PresetError(yoke_errors::yoke_not_an_object);
        const auto* name=entry.get("name");
        if (!name || name->kind!=Json::Kind::string) throw PresetError(yoke_errors::yoke_has_no_name);
        auto found=std::find_if(result.begin(),result.end(),[&](const YokePreset& p) {return p.name==name->text;});
        const bool is_new=found==result.end();
        YokePreset p;
        if (!is_new) p=*found;
        else {p.name=name->text;p.title=name->text;p.verified=false;p.verified_firmware.clear();}
        const std::string at="yoke \""+name->text+"\": ";
        static const char* required[]={"usb","roll_travel_counts","pitch_travel_counts","roll_ceiling_units","pitch_ceiling_units","target_torque"};
        if (is_new) for (const char* key:required) if (!entry.get(key)) throw PresetError(yoke_errors::new_model_must_give(at,key));
        bool has_rest=false;
        for (const auto& m:entry.members) {
            const auto& key=m.first;const auto& v=m.second;const auto what=at+key;
            if (key=="name") continue;
            else if (key=="title") {if (v.kind!=Json::Kind::string) throw PresetError(yoke_errors::must_be_text(what));p.title=v.text;}
            else if (key=="usb") {
                if (v.kind!=Json::Kind::array) throw PresetError(yoke_errors::must_be_a_list(what));
                p.usb_ids.clear();
                for (const auto& id:v.items) {
                    const auto* vendor=id.kind==Json::Kind::object?id.get("vendorId"):nullptr;
                    const auto* product=id.kind==Json::Kind::object?id.get("productId"):nullptr;
                    if (!vendor || !product) throw PresetError(yoke_errors::usb_entries_need_ids(what));
                    const auto vn=hex_id(*vendor,what+".vendorId"),pn=hex_id(*product,what+".productId");
                    p.usb_ids.push_back({vn,pn});
                }
            }
            else if (key=="verified") p.verified=flag(v,what);
            else if (key=="verified_firmware") {
                if (v.kind!=Json::Kind::array) throw PresetError(yoke_errors::must_be_a_list(what));
                p.verified_firmware.clear();
                for (const auto& f:v.items) {
                    const int n=whole(f,what);
                    if (n<1 || n>0xffff) throw PresetError(yoke_errors::entries_range_1_65535(what));
                    p.verified_firmware.push_back(static_cast<std::uint16_t>(n));
                }
            }
            else if (key=="roll_travel_counts") p.roll_travel_counts=number(v,what);
            else if (key=="roll_input_low") p.roll_input_low=whole(v,what);
            else if (key=="roll_input_high") p.roll_input_high=whole(v,what);
            else if (key=="pitch_input_low") p.pitch_input_low=whole(v,what);
            else if (key=="pitch_input_high") p.pitch_input_high=whole(v,what);
            else if (key=="pitch_travel_counts") p.pitch_travel_counts=number(v,what);
            else if (key=="wheel_deg") p.wheel_deg=number(v,what);
            else if (key=="pitch_travel_mm") p.pitch_travel_mm=number(v,what);
            else if (key=="pitch_units_per_lb") p.pitch_units_per_lb=whole(v,what);
            else if (key=="roll_units_per_nm") p.roll_units_per_nm=whole(v,what);
            else if (key=="roll_ceiling_units") p.roll_ceiling_units=whole(v,what);
            else if (key=="pitch_ceiling_units") p.pitch_ceiling_units=whole(v,what);
            else if (key=="margin_fraction") p.margin_fraction=number(v,what);
            else if (key=="target_torque") p.target_torque=whole(v,what);
            else if (key=="centre_drift_percent") p.centre_drift_percent=number(v,what);
            else if (key=="status_stopped") p.status_stopped=whole(v,what);
            else if (key=="status_ready_mask") p.status_ready_mask=whole(v,what);
            else if (key=="status_acquired_mask") p.status_acquired_mask=whole(v,what);
            else if (key=="spring_mode") p.spring_mode=whole(v,what);
            else if (key=="power_on_spring_mode") p.power_on_spring_mode=whole(v,what);
            else if (key=="idle_enabled") p.idle_enabled=flag(v,what);
            else if (key=="button_action_method") {
                const auto method=v.kind==Json::Kind::string?buttons::parse_action_method(v.text):std::nullopt;
                if (method) p.button_action_method=*method;
                else skipped.push_back(yoke_errors::action_method_skipped(what,buttons::action_method_name(p.button_action_method)));
            }
            else if (key=="button_registers") {
                if (v.kind!=Json::Kind::object) {skipped.push_back(yoke_errors::registers_not_an_object(what));continue;}
                buttons::CanLayout layout;
                for (const auto& r:v.members) {
                    const auto rw=what+"."+r.first;
                    try {
                        const auto input=buttons::parse_input(r.first);
                        if (!input) throw PresetError(yoke_errors::not_a_control_name(rw));
                        if (layout.count(*input)) throw PresetError(yoke_errors::given_twice(rw));
                        if (r.second.kind!=Json::Kind::object) throw PresetError(yoke_errors::register_not_an_object(rw));
                        const auto* node=r.second.get("node");const auto* object=r.second.get("object");const auto* sub=r.second.get("sub");
                        const auto* mask=r.second.get("mask");const auto* value=r.second.get("value");
                        if (!node || !object || !sub) throw PresetError(yoke_errors::register_needs_object(rw));
                        if ((mask!=nullptr)==(value!=nullptr)) throw PresetError(yoke_errors::register_needs_mask_or_value(rw));
                        for (const auto& extra:r.second.members)
                            if (extra.first!="node" && extra.first!="object" && extra.first!="sub" && extra.first!="mask" && extra.first!="value")
                                throw PresetError(yoke_errors::register_unknown_field(rw,extra.first));
                        buttons::CanSignal signal;
                        const int node_number=whole(*node,rw+".node"),sub_index=whole(*sub,rw+".sub");
                        if (node_number<1 || node_number>2) throw PresetError(yoke_errors::register_node(rw));
                        if (sub_index<0 || sub_index>255) throw PresetError(yoke_errors::register_sub(rw));
                        signal.node=static_cast<std::uint8_t>(node_number);
                        signal.index=hex_id(*object,rw+".object");
                        signal.sub=static_cast<std::uint8_t>(sub_index);
                        if (mask) {
                            const auto digits=mask->kind==Json::Kind::string?mask->text:std::string();
                            if (digits.size()<3 || digits.size()>10 || digits[0]!='0' || (digits[1]!='x' && digits[1]!='X') || digits.find_first_not_of("0123456789abcdefABCDEF",2)!=std::string::npos)
                                throw PresetError(yoke_errors::register_mask_hex(rw));
                            signal.is_mask=true;signal.operand=static_cast<std::uint32_t>(std::stoul(digits.substr(2),nullptr,16));
                        } else {
                            const int number=whole(*value,rw+".value");
                            if (number<0) throw PresetError(yoke_errors::register_value_negative(rw));
                            signal.is_mask=false;signal.operand=static_cast<std::uint32_t>(number);
                        }
                        if (const auto why=button_signal_problem(signal)) throw PresetError(yoke_errors::register_problem(rw,*why));
                        layout[*input]=signal;
                    } catch (const PresetError& e) {skipped.push_back(yoke_errors::skipped(e.what()));}
                }
                p.button_registers=layout;
            }
            else if (key=="button_aliases") {
                if (v.kind!=Json::Kind::object) {skipped.push_back(yoke_errors::aliases_not_an_object(what));continue;}
                p.button_aliases.clear();
                std::set<std::string> taken;   // every alias given so far, lower-case: a name belongs to one control
                for (const auto& alias:v.members) {
                    const auto aw=what+"."+alias.first;
                    const auto input=buttons::parse_input(alias.first);
                    if (!input) {skipped.push_back(yoke_errors::skipped(yoke_errors::not_a_control_name(aw)));continue;}
                    const auto control=buttons::input_name(*input);
                    if (p.button_aliases.count(control)) {skipped.push_back(yoke_errors::skipped(yoke_errors::given_twice(aw)));continue;}
                    if (alias.second.kind!=Json::Kind::array) {skipped.push_back(yoke_errors::alias_not_a_list(aw));continue;}
                    std::vector<std::string> names;
                    for (const auto& n:alias.second.items) {
                        if (n.kind!=Json::Kind::string) {skipped.push_back(yoke_errors::alias_entry_not_a_name(aw));continue;}
                        if (!buttons::valid_friendly_name(n.text)) {skipped.push_back(yoke_errors::alias_not_usable(aw,n.text));continue;}
                        if (!taken.insert(buttons::lower_case(n.text)).second) {skipped.push_back(yoke_errors::alias_given_twice(aw,n.text));continue;}
                        if (names.size()>=16) {skipped.push_back(yoke_errors::alias_past_the_limit(aw,n.text));continue;}
                        names.push_back(n.text);
                    }
                    p.button_aliases[control]=std::move(names);
                }
            }
            else if (key=="rest") {
                if (v.kind!=Json::Kind::object) throw PresetError(yoke_errors::must_be_an_object(what));
                has_rest=true;p.rest.captured=false;
                const auto curve=[&](const Json& j,const std::string& w) {
                    if (j.kind!=Json::Kind::array || j.items.size()!=9) throw PresetError(yoke_errors::curve_needs_nine(w));
                    std::array<std::uint16_t,9> out{};
                    for (std::size_t i=0;i<9;++i) {const int n=whole(j.items[i],w);if (n<0 || n>0xffff) throw PresetError(yoke_errors::curve_entries_range(w));out[i]=static_cast<std::uint16_t>(n);}
                    return out;
                };
                for (const auto& r:v.members) {
                    const auto& rk=r.first;const auto& rv=r.second;const auto rw=what+"."+rk;
                    if (rk=="roll_curve") p.rest.roll_curve=curve(rv,rw);
                    else if (rk=="pitch_curve") p.rest.pitch_curve=curve(rv,rw);
                    else if (rk=="roll_spring_percent") p.rest.roll_spring_percent=whole(rv,rw);
                    else if (rk=="pitch_spring_percent") p.rest.pitch_spring_percent=whole(rv,rw);
                    else if (rk=="pitch_gain_denominator") p.rest.pitch_gain_denominator=whole(rv,rw);
                    else if (rk=="pitch_move_back") p.rest.pitch_move_back=whole(rv,rw);
                    else if (rk=="pitch_cutoff") p.rest.pitch_cutoff=whole(rv,rw);
                    else if (rk=="mode") p.rest.mode=whole(rv,rw);
                    else throw PresetError(yoke_errors::unknown_field_in(at,key,rk));
                }
            }
            else if (key=="roll_encoder_sign") p.roll_encoder_sign=whole(v,what);
            else if (key=="pitch_encoder_sign") p.pitch_encoder_sign=whole(v,what);
            else if (key=="roll_spring_sign") p.roll_spring_sign=whole(v,what);
            else if (key=="pitch_spring_sign") p.pitch_spring_sign=whole(v,what);
            else if (key=="roll_input_reverse") p.roll_input_reverse=flag(v,what);
            else if (key=="pitch_input_reverse") p.pitch_input_reverse=flag(v,what);
            else throw PresetError(yoke_errors::unknown_field(at,key));
        }
        // A new model that gives no resting settings has the plugin read them from the yoke and put them back.
        if (is_new && !has_rest) p.rest.captured=true;
        if (is_new) result.push_back(std::move(p)); else *found=std::move(p);
    }
    validate_presets(result);
    return result;
}

inline std::vector<YokePreset> parse_yoke_presets(std::string_view text,const std::vector<YokePreset>& builtins) {
    std::vector<std::string> skipped;   // for a caller with nowhere to show them
    return parse_yoke_presets(text,builtins,skipped);
}

// ---- Where the models come from: the shipped yokes/ folder, then the user's overrides ----
// Each file in the plugin's yokes/ folder (config/yokes/ in the source) is a complete presets file for
// one model. A shipped file is parsed against an empty set, so it cannot patch another model; names and
// USB ids must be unique across the folder. Any refusal refuses the whole folder.
inline std::vector<YokePreset> parse_shipped_yokes(const std::vector<std::string>& texts,std::vector<std::string>& skipped) {
    if (texts.empty()) throw PresetError(yoke_errors::yokes_folder_empty);
    std::vector<YokePreset> all;
    for (const auto& text:texts) {
        const auto one=parse_yoke_presets(text,{},skipped);
        all.insert(all.end(),one.begin(),one.end());
    }
    validate_presets(all);
    return all;
}
struct LoadedYokes {
    std::vector<YokePreset> presets;
    bool from_files=false;                 // false: the built-in fallback is in force
    bool overrides_applied=false;
    std::vector<std::string> notes;        // one line each for the log: what was refused and why
    std::vector<std::string> skipped;      // one line each for a button mapping skipped (the file still applied): logged and shown on screen
};
// Happy path: the shipped files, patched by the overrides file when there is one; a button mapping that could not be used is skipped
// and named in `skipped`, the rest of the file still applies. Errors (none thrown):
// a refused folder falls back to the built-ins; a refused overrides file leaves the shipped models as
// they are. Both leave a note.
inline LoadedYokes load_yoke_models(const std::vector<std::string>& shipped,const std::optional<std::string>& overrides) {
    LoadedYokes out;
    try {
        std::vector<std::string> skipped;
        out.presets=parse_shipped_yokes(shipped,skipped);
        out.skipped.insert(out.skipped.end(),skipped.begin(),skipped.end());
        out.from_files=true;
    } catch (const std::exception& e) {
        out.presets=builtin_yoke_presets();
        out.notes.push_back(yoke_errors::files_refused(e.what()));
    }
    if (overrides) {
        try {
            std::vector<std::string> skipped;
            out.presets=parse_yoke_presets(*overrides,out.presets,skipped);
            out.skipped.insert(out.skipped.end(),skipped.begin(),skipped.end());   // only from a file that applied
            out.overrides_applied=true;
        } catch (const std::exception& e) {
            out.notes.push_back(yoke_errors::overrides_refused(e.what()));
        }
    }
    return out;
}

// ---- Lookup ----
inline const YokePreset* find_yoke_preset_named(const std::vector<YokePreset>& presets,const std::string& name) {
    for (const auto& p:presets) if (p.name==name) return &p;
    return nullptr;
}
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
