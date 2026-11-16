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
#include "buttons/controls.hpp"
#include "buttons/input/can_layout.hpp"
#include "buttons/methods.hpp"
#include "json.hpp"
#include "hid/usb_ids.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <map>
#include <optional>
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
    // How the yoke's controls are named and which of them are the same control, as a map of lists: each key and the names
    // in its list are one control (buttons/controls.hpp). Canonical names are the NG joystick's hid_button_1..6 and its hat
    // positions hat_n..hat_nw, and the CAN slots can_button_1..16 (the original CLS-E's, bits 16-31 of register 0x60fd);
    // any other key is a friendly name. Linking a hid_button to a can_button is how a button written under one numbering
    // fires under the other read method. Giving the field in BrunnerDirect.yokes.json replaces the model's whole map.
    std::map<std::string,std::vector<std::string>> button_aliases;
    // Where the yoke's buttons and hat are on the bridge, for the CanBus read method (buttons/input/can_layout.hpp): the original
    // CLS-E's are node 2's 0x60fd (bits 16 to 31), the NG's its own record 0x2880 (entry 3 the buttons, entry 4 the hat). Only
    // registers on brunner::button_register's read-only allowlist may be named.
    buttons::CanLayout button_registers;
    // How this model's buttons are read and what the plugin does with them (buttons/methods.hpp). A model that gives none is read
    // over the bridge and runs button_commands. VJoy needs a virtual joystick: where the operating system has none it is Direct.
    buttons::ReadMethod button_read_method=buttons::default_read_method;
    buttons::ActionMethod button_action_method=buttons::default_action_method;
};

// ---- Built-in models ----
// The original CLS-E's controls, from the pressed-one-by-one survey of 2026-11-10 (docs/buttons.md): slots 1-5 are the left
// grip, 7-11 the right grip, 12-14 the panel buttons; slot 6 is not fitted. It has no joystick interface, so no hid_ names.
inline std::map<std::string,std::vector<std::string>> cls_e_button_aliases() {
    return {{"left_index",{"can_button_1"}},{"left_thumb_left",{"can_button_2"}},{"left_thumb_right",{"can_button_3"}},
            {"left_rocker_back",{"can_button_4"}},{"left_rocker_forward",{"can_button_5"}},
            {"right_index",{"can_button_7"}},{"right_thumb_left",{"can_button_8"}},{"right_thumb_right",{"can_button_9"}},
            {"right_rocker_back",{"can_button_10"}},{"right_rocker_forward",{"can_button_11"}},
            {"panel_middle",{"can_button_12"}},{"panel_right",{"can_button_13"}},{"panel_left",{"can_button_14"}}};
}
// The NG's controls, as its joystick interface numbers them (1 trigger, 2 top left, 3 top right, 4 rocker down, 5 rocker up, 6 hat push,
// and the hat's eight positions), linked to the same controls on the bridge: its own button record 0x2880 holds the six buttons
// as can_button_1..6 and the hat as can_hat_n..can_hat_nw (checked on the NG, 2026-11-16, docs/dev/button-commands.md). The original
// CLS-E's 0x60fd carries buttons 1 to 5 of these too, but not the hat push or the hat.
inline std::map<std::string,std::vector<std::string>> cls_e_ng_button_aliases() {
    std::map<std::string,std::vector<std::string>> aliases{
        {"trigger",{"hid_button_1","can_button_1"}},{"top_left",{"hid_button_2","can_button_2"}},{"top_right",{"hid_button_3","can_button_3"}},
        {"rocker_down",{"hid_button_4","can_button_4"}},{"rocker_up",{"hid_button_5","can_button_5"}},{"hat_push",{"hid_button_6","can_button_6"}}};
    for (const char* direction:{"n","ne","e","se","s","sw","w","nw"})
        aliases[std::string("hat_")+direction]={std::string("can_hat_")+direction};
    return aliases;
}
inline YokePreset builtin_cls_e() {
    YokePreset p;
    p.name="cls-e";p.title="Brunner CLS-E";
    p.usb_ids={{0x25bb,0x0081}};
    p.verified=true;p.verified_firmware={367};
    p.button_aliases=cls_e_button_aliases();
    p.button_registers=buttons::cls_e_can_layout();
    p.button_read_method=buttons::ReadMethod::can_bus;p.button_action_method=buttons::ActionMethod::vjoy;   // the CLS-E: its buttons on a virtual joystick
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
    p.button_read_method=buttons::ReadMethod::can_bus;p.button_action_method=buttons::ActionMethod::direct;   // the NG: its buttons run X-Plane commands
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
    const auto input_ends=[&](int low,int high,const char* axis) {
        if (low==0 && high==0) return;   // unset: -travel..+travel
        if (low>-1000 || low<-30000 || high<1000 || high>30000) fail(std::string(axis)+" input low must be -30000 to -1000 and high 1000 to 30000, or both left out");
    };
    input_ends(p.roll_input_low,p.roll_input_high,"roll");
    input_ends(p.pitch_input_low,p.pitch_input_high,"pitch");
    if (!in(p.wheel_deg,10,180)) fail("wheel_deg must be 10 to 180");
    if (!in(p.pitch_travel_mm,0,1000)) fail("pitch_travel_mm must be 0 to 1000");
    if ((p.roll_encoder_sign!=1 && p.roll_encoder_sign!=-1) || (p.pitch_encoder_sign!=1 && p.pitch_encoder_sign!=-1)) fail("encoder signs must be 1 or -1");
    if ((p.roll_spring_sign!=1 && p.roll_spring_sign!=-1) || (p.pitch_spring_sign!=1 && p.pitch_spring_sign!=-1)) fail("spring signs must be 1 or -1");
    if (p.pitch_units_per_lb<50 || p.pitch_units_per_lb>1000) fail("pitch_units_per_lb must be 50 to 1000");
    if (p.roll_units_per_nm<100 || p.roll_units_per_nm>10000) fail("roll_units_per_nm must be 100 to 10000");
    if (p.roll_ceiling_units<100 || p.roll_ceiling_units>20000 || p.pitch_ceiling_units<100 || p.pitch_ceiling_units>20000)
        fail("force ceilings must be 100 to 20000 units");
    if (!in(p.margin_fraction,0.02,0.5)) fail("margin_fraction must be 0.02 to 0.5");
    if (p.target_torque<1 || p.target_torque>1000) fail("target_torque must be 1 to 1000");
    if (!in(p.centre_drift_percent,0,5)) fail("centre_drift_percent must be 0 to 5");
    for (const int w:{p.status_stopped,p.status_ready_mask,p.status_acquired_mask}) if (w<1 || w>0xffff) fail("status words must be 1 to 65535");
    {
        const auto& r=p.rest;
        for (const auto v:r.roll_curve) if (v<5 || v>1000) fail("rest curve points must be 5 to 1000");
        for (const auto v:r.pitch_curve) if (v<5 || v>1000) fail("rest curve points must be 5 to 1000");
        if (r.roll_spring_percent<1 || r.roll_spring_percent>1000 || r.pitch_spring_percent<1 || r.pitch_spring_percent>1000) fail("rest spring percents must be 1 to 1000");
        if (r.pitch_gain_denominator<1 || r.pitch_gain_denominator>6) fail("rest pitch_gain_denominator must be 1 to 6");
        if (r.pitch_move_back<180 || r.pitch_move_back>720) fail("rest pitch_move_back must be 180 to 720");
        if (r.pitch_cutoff<0 || r.pitch_cutoff>101) fail("rest pitch_cutoff must be 0 (not written) or up to 101");
        const int flow_bits=(1<<13)|(1<<11)|(1<<6)|(1<<5);
        if (r.mode!=0 && (r.mode<1 || r.mode>0xffff || (r.mode&~flow_bits)!=p.spring_mode))
            fail("rest mode must be 0 (not written) or the spring_mode with only the flow's bits 13, 11, 6 and 5 added");
    }
    if (p.power_on_spring_mode!=0 && (p.power_on_spring_mode<1 || p.power_on_spring_mode>0xffff || (p.power_on_spring_mode&((1<<13)|(1<<11)|(1<<6)|(1<<5)))!=0))
        fail("power_on_spring_mode must be 0 (the same as spring_mode) or 1 to 65535 with bits 13, 11, 6 and 5 clear");
    if (p.spring_mode<1 || p.spring_mode>0xffff || (p.spring_mode&((1<<13)|(1<<11)|(1<<6)|(1<<5)))!=0)
        fail("spring_mode must be 1 to 65535 with bits 13, 11, 6 and 5 clear (they are the flow's, not the resting mode's)");
    if (p.button_aliases.size()>64) fail("more than 64 button aliases");
    for (const auto& alias:p.button_aliases) if (alias.second.size()>16) fail("button alias \""+alias.first+"\" lists more than 16 names");
    try {buttons::ControlMap controls(p.button_aliases);} catch (const buttons::ControlError& e) {fail(e.what());}
    {
        const auto& layout=p.button_registers;
        const auto named=[&](const buttons::CanRegister& r,const char* what) {
            if (buttons::can_register_bytes(r.index,r.sub)!=r.bytes || r.bytes==0) fail(std::string("button_registers.")+what+" is not a register the yoke's buttons may be read from (0x60fd:0, 0x2880:3 or 0x2880:4)");
        };
        named(layout.buttons,"buttons");
        if (layout.bits<1 || layout.bits>16 || layout.shift<0 || layout.shift+layout.bits>layout.buttons.bytes*8)
            fail("button_registers.buttons needs 1 to 16 bits that lie inside the register");
        if (layout.hat) {
            named(*layout.hat,"hat");
            if (layout.hat->bytes!=1 || *layout.hat==layout.buttons) fail("button_registers.hat must be a one-byte register other than the buttons'");
        }
    }
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
// unless it says otherwise. Fields: name, title, usb [{vendorId,productId}] (each a hex string such as "0x25BB"), verified,
// verified_firmware [..], roll_travel_counts, pitch_travel_counts, roll_input_low, roll_input_high,
// pitch_input_low, pitch_input_high, wheel_deg,
// pitch_travel_mm, pitch_units_per_lb, roll_units_per_nm, roll_ceiling_units,
// pitch_ceiling_units, margin_fraction, target_torque, centre_drift_percent, status_stopped,
// status_ready_mask, status_acquired_mask, spring_mode, power_on_spring_mode, idle_enabled, rest {roll_curve, pitch_curve,
// roll_spring_percent, pitch_spring_percent, pitch_gain_denominator, pitch_move_back, pitch_cutoff, mode},
// roll_encoder_sign,
// pitch_encoder_sign, roll_spring_sign, pitch_spring_sign, roll_input_reverse, pitch_input_reverse,
// button_aliases {"friendlyName": ["hid_button_4", "can_button_11", "other_alias"], ...}, button_read_method "CanBus" | "HID" and button_action_method "VJoy" | "Direct" (any case), button_registers {"buttons": {"object":
// "0x2880", "sub": 3, "shift": 0, "bits": 6}, "hat": {"object": "0x2880", "sub": 4}}. Unknown fields are refused.
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
inline std::uint16_t hex_id(const Json& j,const std::string& what) {
    const auto id=j.kind==Json::Kind::string?parse_hex_id(j.text):std::nullopt;
    if (!id) throw PresetError(what+" must be a hex string \"0x0001\" to \"0xFFFF\"");
    return *id;
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
        bool has_rest=false;
        for (const auto& m:entry.members) {
            const auto& key=m.first;const auto& v=m.second;const auto what=at+key;
            if (key=="name") continue;
            else if (key=="title") {if (v.kind!=Json::Kind::string) throw PresetError(what+" must be text");p.title=v.text;}
            else if (key=="usb") {
                if (v.kind!=Json::Kind::array) throw PresetError(what+" must be a list");
                p.usb_ids.clear();
                for (const auto& id:v.items) {
                    const auto* vendor=id.kind==Json::Kind::object?id.get("vendorId"):nullptr;
                    const auto* product=id.kind==Json::Kind::object?id.get("productId"):nullptr;
                    if (!vendor || !product) throw PresetError(what+" entries need vendorId and productId");
                    const auto vn=hex_id(*vendor,what+".vendorId"),pn=hex_id(*product,what+".productId");
                    p.usb_ids.push_back({vn,pn});
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
            else if (key=="button_read_method" || key=="button_action_method") {
                if (v.kind!=Json::Kind::string) throw PresetError(what+" must be text");
                if (key=="button_read_method") {
                    const auto method=buttons::parse_read_method(v.text);
                    if (!method) throw PresetError(what+" must be CanBus or HID");
                    p.button_read_method=*method;
                } else {
                    const auto method=buttons::parse_action_method(v.text);
                    if (!method) throw PresetError(what+" must be VJoy or Direct");
                    p.button_action_method=*method;
                }
            }
            else if (key=="button_registers") {
                if (v.kind!=Json::Kind::object) throw PresetError(what+" must be an object");
                const auto reg=[&](const Json& j,const std::string& w) {
                    if (j.kind!=Json::Kind::object) throw PresetError(w+" must be an object with object and sub");
                    const auto* object=j.get("object");const auto* sub=j.get("sub");
                    if (!object || !sub) throw PresetError(w+" needs \"object\" and \"sub\"");
                    const auto index=hex_id(*object,w+".object");
                    const int entry=whole(*sub,w+".sub");
                    if (entry<0 || entry>255) throw PresetError(w+".sub must be 0 to 255");
                    return buttons::CanRegister{index,static_cast<std::uint8_t>(entry),buttons::can_register_bytes(index,static_cast<std::uint8_t>(entry))};
                };
                buttons::CanLayout layout;
                layout.hat.reset();
                bool has_buttons=false;
                for (const auto& r:v.members) {
                    const auto rw=what+"."+r.first;
                    if (r.first=="buttons") {
                        has_buttons=true;
                        layout.buttons=reg(r.second,rw);layout.shift=0;layout.bits=0;
                        for (const auto& extra:r.second.members) {
                            if (extra.first=="shift") layout.shift=whole(extra.second,rw+".shift");
                            else if (extra.first=="bits") layout.bits=whole(extra.second,rw+".bits");
                            else if (extra.first!="object" && extra.first!="sub") throw PresetError(at+"unknown field \""+key+".buttons."+extra.first+"\"");
                        }
                    }
                    else if (r.first=="hat") {
                        layout.hat=reg(r.second,rw);
                        for (const auto& extra:r.second.members) if (extra.first!="object" && extra.first!="sub") throw PresetError(at+"unknown field \""+key+".hat."+extra.first+"\"");
                    }
                    else throw PresetError(at+"unknown field \""+key+"."+r.first+"\"");
                }
                if (!has_buttons) throw PresetError(what+" needs \"buttons\"");
                p.button_registers=layout;
            }
            else if (key=="button_aliases") {
                if (v.kind!=Json::Kind::object) throw PresetError(what+" must be an object of lists of names");
                p.button_aliases.clear();
                for (const auto& alias:v.members) {
                    if (alias.second.kind!=Json::Kind::array) throw PresetError(what+"."+alias.first+" must be a list of names");
                    std::vector<std::string> names;
                    for (const auto& n:alias.second.items) {
                        if (n.kind!=Json::Kind::string) throw PresetError(what+"."+alias.first+" must list names in quotes");
                        names.push_back(n.text);
                    }
                    p.button_aliases[alias.first]=std::move(names);
                }
            }
            else if (key=="rest") {
                if (v.kind!=Json::Kind::object) throw PresetError(what+" must be an object");
                has_rest=true;p.rest.captured=false;
                const auto curve=[&](const Json& j,const std::string& w) {
                    if (j.kind!=Json::Kind::array || j.items.size()!=9) throw PresetError(w+" must be a list of 9 numbers");
                    std::array<std::uint16_t,9> out{};
                    for (std::size_t i=0;i<9;++i) {const int n=whole(j.items[i],w);if (n<0 || n>0xffff) throw PresetError(w+" entries must be 0 to 65535");out[i]=static_cast<std::uint16_t>(n);}
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
                    else throw PresetError(at+"unknown field \""+key+"."+rk+"\"");
                }
            }
            else if (key=="roll_encoder_sign") p.roll_encoder_sign=whole(v,what);
            else if (key=="pitch_encoder_sign") p.pitch_encoder_sign=whole(v,what);
            else if (key=="roll_spring_sign") p.roll_spring_sign=whole(v,what);
            else if (key=="pitch_spring_sign") p.pitch_spring_sign=whole(v,what);
            else if (key=="roll_input_reverse") p.roll_input_reverse=flag(v,what);
            else if (key=="pitch_input_reverse") p.pitch_input_reverse=flag(v,what);
            else throw PresetError(at+"unknown field \""+key+"\"");
        }
        // A new model that gives no resting settings has the plugin read them from the yoke and put them back.
        if (is_new && !has_rest) p.rest.captured=true;
        if (is_new) result.push_back(std::move(p)); else *found=std::move(p);
    }
    validate_presets(result);
    return result;
}

// ---- Where the models come from: the shipped yokes/ folder, then the user's overrides ----
// Each file in the plugin's yokes/ folder (config/yokes/ in the source) is a complete presets file for
// one model. A shipped file is parsed against an empty set, so it cannot patch another model; names and
// USB ids must be unique across the folder. Any refusal refuses the whole folder.
inline std::vector<YokePreset> parse_shipped_yokes(const std::vector<std::string>& texts) {
    if (texts.empty()) throw PresetError("the yokes folder has no files");
    std::vector<YokePreset> all;
    for (const auto& text:texts) {
        const auto one=parse_yoke_presets(text,{});
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
};
// Happy path: the shipped files, patched by the overrides file when there is one. Errors (none thrown):
// a refused folder falls back to the built-ins; a refused overrides file leaves the shipped models as
// they are. Both leave a note.
inline LoadedYokes load_yoke_models(const std::vector<std::string>& shipped,const std::optional<std::string>& overrides) {
    LoadedYokes out;
    try {
        out.presets=parse_shipped_yokes(shipped);
        out.from_files=true;
    } catch (const std::exception& e) {
        out.presets=builtin_yoke_presets();
        out.notes.push_back(std::string("Yoke model files refused, built-in models in force: ")+e.what());
    }
    if (overrides) {
        try {
            out.presets=parse_yoke_presets(*overrides,out.presets);
            out.overrides_applied=true;
        } catch (const std::exception& e) {
            out.notes.push_back(std::string("Yoke overrides file refused, ignored: ")+e.what());
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
