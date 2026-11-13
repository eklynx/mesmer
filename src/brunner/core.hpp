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
enum class Object : std::uint16_t { firmware=0x3018, status=0x6041, encoder=0x6064, position=0x2106 };
inline unsigned width(Object object) {
    switch (object) {
    case Object::firmware: case Object::status: return 2;
    case Object::encoder: case Object::position: return 4;
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
inline std::int32_t signed32(std::uint32_t value) {
    std::int32_t result;
    std::memcpy(&result, &value, sizeof(result));
    return result;
}
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
    std::int32_t encoder=0, position=0;
};
struct Snapshot {
    std::array<Axis,2> axes{}; // roll, pitch
    Clock::time_point sampled{}; // beginning of the complete sampling cycle
    std::uint64_t cycles=0;
    bool connected=false;
    std::uint16_t usb_vendor=0,usb_product=0; // the USB id of the bridge opened: the model follows from it
    std::string model,model_title,yoke_note;  // its preset, and a note when the firmware is outside what that preset was verified on
    bool bridge_present=false; // USB bridge enumerated; controllers may still be unpowered
    bool bridge_busy=false; // another program (CLS2Sim, Simux) holds the bridge open
    bool stop_unconfirmed=false; // a motor stop could not be verified; never retried automatically
    bool reference_active=false;
    bool session_reference=false;
    bool loading_active=false;
    bool flight_session=false;
    bool flight_suspended=false;
    bool loading_fault=false;
    bool power_cycle_needed=false;
    // Firmware other than the one the plugin drives: the reader only reads the
    // yoke (brunner/readonly.hpp) and writes nothing. `survey` is the last survey's JSON.
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
inline FlightInput flight_input(const Snapshot& s, const std::array<Calibration,2>& c,
                               bool centering_complete, Clock::time_point now) {
    auto unavailable=[](const char* reason) { return FlightInput{false,0,0,reason}; };
    if (!s.connected || s.cycles==0) return unavailable("No complete yoke sample");
    if (now<s.sampled || now-s.sampled>std::chrono::milliseconds(250)) return unavailable("Yoke data stale");
    for (const auto& a:s.axes) {
        if (a.status&0x0008) return unavailable("Controller fault");
        if (a.status&0x0100) return unavailable("Controller homing error");
        if (!(a.status&0x1000)) return unavailable("Controller reference not initialized");
    }
    if (!centering_complete) return unavailable("Centering not verified");
    for (const auto& cal:c) if (!cal.verified || cal.lower>=0 || cal.upper<=0)
        return unavailable("Axis calibration not verified");
    return {true,normalize(s.axes[0].position,c[0]),normalize(s.axes[1].position,c[1]),"Ready"};
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
// Commissioning input with the motors disabled after a successful centering
// test. 0x2106 freezes when stopped; use the observed live 0x6064 encoder.
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
