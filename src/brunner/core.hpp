#pragma once
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <optional>
#include <stdexcept>
#include <cstdio>
#include <string>
#include "force/aileron.hpp"
#include "brunner/drive.hpp"

namespace brunner {
using Clock = std::chrono::steady_clock;
using Report = std::array<std::uint8_t, 64>;
// What a sampling cycle reads. The yoke's buttons are node 2's 0x60fd:00,
// whose bits 16-31 are slots 1-16 (decision 9; CLS2Sim 5.9 MicroDriveDevice,
// scripts/brunner_buttons.py). Two objects are deliberately not read. CLS2Sim's
// slots 17-32, bits 0-15 of 0x2088:00, are empty on this yoke: its 13
// controls are all in slots 1-14 (pressed one by one, 2026-11-10). The
// drive's control position, 0x2106:00, freezes when the motors stop, so
// position is the live encoder alone. Each read is about 4 ms of the cycle.
enum class Object : std::uint16_t { firmware=0x3018, status=0x6041, encoder=0x6064, digital_inputs=0x60fd };
inline unsigned width(Object object) {
    switch (object) {
    case Object::firmware: case Object::status: return 2;
    case Object::encoder: case Object::digital_inputs: return 4;
    }
    throw std::invalid_argument("Unsupported read object");
}
inline std::uint16_t u16(const std::uint8_t* p) {
    return static_cast<std::uint16_t>(p[0] | (static_cast<unsigned>(p[1]) << 8));
}
inline std::uint32_t u32(const std::uint8_t* p) {
    return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8)
        | (static_cast<std::uint32_t>(p[2]) << 16) | (static_cast<std::uint32_t>(p[3]) << 24);
}
// The yoke's button slots in node 2's 0x60fd (CLS2Sim's mapping, as
// button_word in scripts/brunner_buttons.py): its low half is the drive's
// own inputs, not buttons.
inline std::uint16_t slots_1_to_16(std::uint32_t digital_inputs) {return static_cast<std::uint16_t>(digital_inputs>>16);}
inline std::int32_t signed32(std::uint32_t value) {
    std::int32_t result;
    std::memcpy(&result, &value, sizeof(result));
    return result;
}
// The two errors that end a reader's cycle whatever it was reading: the
// reader stopping, and the USB link failing (the link is gone or the
// controllers are silent; nothing about their state is concluded from it).
struct OperationCancelled : std::runtime_error {OperationCancelled():std::runtime_error("Reader stopped") {}};
struct TransportError : std::runtime_error {explicit TransportError(const char* what):std::runtime_error(what) {}};
// Deliberately only SDO uploads. No generic write or motor-command API.
inline Report read_request(unsigned node, Object object, std::uint16_t counter) {
    if (node != 1 && node != 2) throw std::invalid_argument("Unsupported node");
    (void)width(object);
    const auto index = static_cast<std::uint16_t>(object);
    Report r{};
    r[0]=0x3f; r[1]=0x11; r[2]=static_cast<std::uint8_t>(counter);
    r[3]=static_cast<std::uint8_t>(counter>>8);
    r[4]=static_cast<std::uint8_t>(node); r[5]=0x86;
    r[6]=0x40; r[7]=static_cast<std::uint8_t>(index); r[8]=static_cast<std::uint8_t>(index>>8);
    return r;
}
inline std::optional<std::uint32_t> read_reply(const Report& r, unsigned node, Object object) {
    if (node != 1 && node != 2) throw std::invalid_argument("Unsupported node");
    if (r[0]!=0x3f) throw std::runtime_error("Unexpected HID report ID");
    const unsigned kind=r[1]>>4, count=r[1]&15;
    if (kind>2 || count>(kind==0?0u:kind==1?6u:4u)) throw std::runtime_error("Invalid HID frame layout");
    std::optional<std::uint32_t> result;
    for (unsigned i=0; i<count; ++i) {
        const auto* slot=r.data()+4+i*(kind==2?14:10);
        const auto header=u16(slot);
        if ((header>>12)>8) throw std::runtime_error("Invalid CAN data length");
        if ((header&0x7ffu)!=0x580u+node) continue;
        const auto* data=slot+2;
        if ((header>>12)!=8) throw std::runtime_error("Short SDO reply");
        if (u16(data+1)!=static_cast<std::uint16_t>(object) || data[3]!=0) continue;
        if (data[0]==0x80) throw std::runtime_error("Controller rejected SDO read");
        if (data[0]!=(width(object)==2?0x4b:0x43)) throw std::runtime_error("Unexpected SDO width or command");
        if (result) throw std::runtime_error("Duplicate SDO reply");
        result=width(object)==2 ? u16(data+4) : u32(data+4);
    }
    return result;
}
struct Axis {
    std::uint16_t firmware=0, status=0;
    std::int32_t encoder=0;
};
struct Snapshot {
    std::array<Axis,2> axes{}; // roll, pitch
    // The yoke's buttons, slots 1-16: bits 16-31 of node 2's 0x60fd, read in
    // every sampling cycle (sample_buttons in buttons.hpp). Unset until the
    // first read in this USB session, and from a read that failed (why, in
    // button_refusal) until one succeeds, tried from button_retry on: the
    // buttons never stop pitch, roll or force feel.
    std::optional<std::uint16_t> buttons;
    std::string button_refusal;
    Clock::time_point button_retry{};
    Clock::time_point sampled{}; // beginning of the complete sampling cycle
    std::uint64_t cycles=0;
    bool connected=false;
    std::uint16_t usb_vendor=0,usb_product=0; // the USB id of the bridge opened: the model follows from it
    std::string model,model_title,yoke_note;  // its preset, and a note when the firmware is outside what that preset was verified on
    bool bridge_present=false; // USB bridge enumerated; controllers may still be unpowered
    bool bridge_busy=false; // another program (CLS2Sim, Simux) holds the bridge open
    bool bridge_disabled=false; // plugged in but switched off in Windows (Device Manager's Disable device)
    bool stop_unconfirmed=false; // a motor stop could not be verified; never retried automatically
    bool reference_active=false;
    bool session_reference=false;
    bool loading_active=false;
    bool flight_session=false;
    bool flight_suspended=false;
    bool loading_fault=false;
    bool power_cycle_needed=false;
    // A yoke the plugin does not drive (an unknown model, or an unverified one): the reader only
    // reads the yoke (brunner/readonly.hpp) and writes nothing. `survey` is the last survey's JSON.
    bool read_only=false;
    std::string survey;
    unsigned surveys=0; // how many surveys have been taken; a change means a new `survey`
    // Requests that failed in this USB session; a change marks a new failure
    // even while the displayed fault reads the same.
    unsigned failures=0; // a refusal that switching the yoke off and on clears
    std::string force_unavailable; // force feel refused with nothing changed: the plain line; the yoke still flies
    // Why the last session stopped, in the pilot's words (the page's whole
    // sentence, the corner window's short line), and whether Retry can help.
    // Empty when nothing failed; cleared when the next session starts.
    std::string stop_reason,stop_reason_short;bool retry_helps=true;
    bool tuning_valid=false;
    Tuning applied_tuning{}; // last verified hardware readback; valid only with tuning_valid
    std::array<int,2> force_bias{}; // last readback, roll/pitch; not measured hand force
    // A force-feel session's pitch: the solver's output for the yoke's position
    // read in its last update, whether it follows (no blend under way), and the
    // measured live-update rate (0 outside a session).
    bool pitch_solved=false,pitch_engaged=false;PitchSolution pitch_solution{};double update_hz=0;
    // Its roll: the aileron model's solution for the wheel's position, likewise.
    bool roll_solved=false;RollSolution roll_solution{};
    // At the end of the last loading session: whether pitch's constant force
    // read back as zero; unset from a session's start until its end reports.
    std::optional<bool> pitch_force_cleared;
    // Buffet's amplitude last read back, roll/pitch, whether a clear of what
    // it set failed, and a vibration time it changed and could not put back,
    // with the value it found (force/buffet.hpp).
    std::array<std::uint32_t,2> buffet_units{};bool buffet_uncleared=false;
    std::array<std::optional<std::uint32_t>,2> buffet_time_unrestored{};
    bool force_bias_valid=false;
    int loading_seconds_remaining=0;
    std::string loading_result;
    std::string reference_result;
    std::string loading_inspection;
    std::string message="Disconnected";
};
struct Calibration {
    std::int32_t lower=0, upper=0;
    bool reverse=false;
    bool verified=false;
};
struct FlightInput { bool ready=false; float roll=0, pitch=0; std::string reason; };
inline float normalize(std::int32_t counts, const Calibration& cal) {
    if (!cal.verified || cal.lower>=0 || cal.upper<=0) throw std::invalid_argument("Calibration not verified");
    const auto span=counts<0 ? -static_cast<double>(cal.lower) : static_cast<double>(cal.upper);
    const auto ratio=std::clamp(static_cast<double>(counts)/span, -1.0, 1.0);
    return static_cast<float>(cal.reverse ? -ratio : ratio);
}
// The input scaling per axis (roll, pitch) from the active drive: the encoder's raw counts at the stops, so
// the stops read -1 and +1 (the preset's *_input_low/high; the travel either way when a model gives none),
// reversed as the drive says. Used for the yoke's own input to X-Plane once it is centred.
inline std::array<Calibration,2> input_calibration() {
    const auto& d=active_drive();
    return {{{d.roll_input_low,d.roll_input_high,d.roll_input_reverse,true},{d.pitch_input_low,d.pitch_input_high,d.pitch_input_reverse,true}}};
}
// Bit 11 reports an internal limit, not loss of a completed reference. Keep
// live position input at the end stop while still rejecting other state changes.
inline bool referenced_stopped_status(std::uint16_t word) {return (word & ~0x0800u)==active_drive().status_stopped;}
// A spring is live: every bit of the model's ready mask is set.
inline bool status_ready(std::uint16_t word) {return (word&active_drive().status_ready_mask)==active_drive().status_ready_mask;}
inline std::string status_hex(std::uint16_t word) {
    char text[16];std::snprintf(text,sizeof(text),"0x%04x",static_cast<unsigned>(word));return text;
}
inline std::string status_words(const Snapshot& s) {return "status "+status_hex(s.axes[0].status)+"/"+status_hex(s.axes[1].status);}
// Pitch and roll for X-Plane after a successful centring, motors on or off:
// the live 0x6064 encoder (the drive's control position, 0x2106, freezes when
// stopped and is not read).
// The reference is valid only for this continuous reader session. A cold/reset
// status, I/O failure or reconnect invalidates it; no reference is persisted.
inline FlightInput stopped_flight_input(const Snapshot& s,const std::array<Calibration,2>& c,Clock::time_point now) {
    if (!s.session_reference || s.reference_active) return {false,0,0,"Run reference and centering before input"};
    if (!s.connected || s.cycles==0 || now<s.sampled || now-s.sampled>std::chrono::milliseconds(250))
        return {false,0,0,"Yoke data stale or unavailable"};
    for (const auto& a:s.axes) if (s.loading_active?(!status_ready(a.status) || (a.status&0x2108)!=0):!referenced_stopped_status(a.status))
        return {false,0,0,"Controller state changed; reference invalid ("+status_words(s)+")"};
    for (const auto& cal:c) if (!cal.verified || cal.lower>=0 || cal.upper<=0)
        return {false,0,0,"Axis calibration not verified"};
    return {true,normalize(s.axes[0].encoder,c[0]),normalize(s.axes[1].encoder,c[1]),
        s.loading_active?(s.flight_session?"Flight loading active":"Light spring test active (ten minutes maximum)"):"Pitch and roll active; motors disabled"};
}
} // namespace brunner
