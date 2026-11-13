// SOURCE OF TRUTH for read-only mode: what the plugin does with a yoke whose
// controller firmware it does not drive (the CLS-E NG reports 642; the plugin
// drives only 367). Specification: docs/read-only-mode.md.
//
// In this mode the reader only reads (ReadOnlyBridge in bridge.hpp has no write
// members) and the plugin never asks for centring, force feel or recovery. What
// remains is the yoke's position as X-Plane's pitch and roll, which needs no
// motor and no centring: the travel is learned by watching the encoder's
// extremes while the pilot moves the yoke to its stops.
//
// Happy path: both axes reach `read_only_min_span` counts of observed travel;
// each position then maps to -1..1 around the midpoint of what was seen.
// Not ready: no sample, a stale sample, an axis that is not referenced or is in
// fault, or an axis that has not moved far enough (the reason names how far each
// has come). The NG homes itself at power-on, sweeping past its own limits; that
// sweep is neither learned nor forwarded (see axis_usable). Nothing is written
// whatever the status says.
#pragma once
#include "core.hpp"
#include "json.hpp"
#include "yoke_preset.hpp"
#include <algorithm>
#include <array>
#include <cstdint>
#include <optional>
#include <string>

namespace brunner {
// Why a yoke is read-only, if it is. A model whose preset is not verified is only driven
// when the owner has acknowledged it (Developer > Allow unverified yoke models; saved
// state in owner_state.hpp).
// Firmware never decides this: a firmware outside the preset's verified list is noted
// in the log and the Force Monitor, nothing more.
inline std::optional<std::string> read_only_reason(const YokePreset& preset,bool allow_unverified) {
    if (preset.verified || allow_unverified) return std::nullopt;
    return "Yoke model "+preset.title+" is not verified: read-only. To drive it, turn on Developer > Allow unverified yoke models in the Yoke (Brunner) menu";
}
// The note for a firmware the preset was not verified on, or empty when it was.
inline std::string firmware_note(const YokePreset& preset,const std::array<std::uint16_t,2>& firmware) {
    for (const auto f:firmware) {
        if (firmware_verified(preset,f)) continue;
        std::string verified;
        for (const auto v:preset.verified_firmware) verified+=(verified.empty()?"":", ")+std::to_string(v);
        return "Firmware "+std::to_string(f)+" is not one the "+preset.title+" settings were verified on ("+(verified.empty()?std::string("none"):verified)+")";
    }
    return {};
}
// The status bits the CLS-E's own input gate uses (core.hpp flight_input). On the
// NG, bit 0x1000 is absent exactly while it homes at power-on (captured
// 2026-11-12) and present otherwise. The "internal limit" flag 0x0800 marks the
// controller's own end stops and is deliberately allowed.
inline constexpr std::uint16_t status_referenced=0x1000,status_fault=0x0008,status_homing_error=0x0100;
// An axis whose position is a pilot's: referenced, and no fault.
inline bool axis_usable(std::uint16_t status) {
    return (status&status_referenced)!=0 && (status&(status_fault|status_homing_error))==0;
}
// Observed travel an axis needs before its input is used: about a third of the
// original CLS-E's range, so a nudge cannot stretch to full deflection.
inline constexpr std::int64_t read_only_min_span=4000;

// One axis's observed extremes of the encoder.
struct TravelLearner {
    std::int32_t low=0,high=0;
    bool seen=false;
    // Returns true when the range widened (worth saving).
    bool observe(std::int32_t counts) {
        if (!seen) {low=high=counts;seen=true;return true;}
        bool widened=false;
        if (counts<low) {low=counts;widened=true;}
        if (counts>high) {high=counts;widened=true;}
        return widened;
    }
    std::int64_t span() const {return seen?static_cast<std::int64_t>(high)-low:0;}
    bool ready() const {return span()>=read_only_min_span;}
    int percent() const {return static_cast<int>(std::min<std::int64_t>(100,span()*100/read_only_min_span));}
    // -1..1 around the midpoint of the observed range, flipped when `reverse`.
    float ratio(std::int32_t counts,bool reverse) const {
        if (!ready()) throw std::invalid_argument("Travel not learned");
        const double middle=(static_cast<double>(low)+high)/2.0,half=span()/2.0;
        const auto r=std::clamp((counts-middle)/half,-1.0,1.0);
        return static_cast<float>(reverse?-r:r);
    }
};
using Travel=std::array<TravelLearner,2>; // roll, pitch (the order of Snapshot::axes)

struct ReadOnlyResult {FlightInput input;bool widened=false;};
// Feeds the sample to the learners and returns the input X-Plane should get.
// `reverse` is per axis (roll, pitch).
inline ReadOnlyResult read_only_flight_input(const Snapshot& s,Travel& travel,const std::array<bool,2>& reverse,Clock::time_point now) {
    ReadOnlyResult result;
    const auto unavailable=[&](std::string reason) {result.input=FlightInput{false,0,0,std::move(reason)};return result;};
    if (!s.read_only || !s.connected || s.cycles==0) return unavailable("No complete yoke sample");
    if (now<s.sampled || now-s.sampled>std::chrono::milliseconds(250)) return unavailable("Yoke data stale");
    // Homing, a fault or an unreferenced controller: the position is the machine's, not
    // the pilot's. Learn nothing from it and forward nothing.
    for (const auto& axis:s.axes) if (axis.status&(status_fault|status_homing_error)) return unavailable("Controller fault");
    for (const auto& axis:s.axes) if (!axis_usable(axis.status)) return unavailable("Read-only: the yoke is homing; wait for it to finish");
    for (unsigned i=0;i<2;++i) result.widened=travel[i].observe(s.axes[i].encoder) || result.widened;
    if (!travel[0].ready() || !travel[1].ready())
        return unavailable("Read-only: move the yoke to all four stops (roll "+std::to_string(travel[0].percent())+"%, pitch "+std::to_string(travel[1].percent())+"%)");
    result.input=FlightInput{true,travel[0].ratio(s.axes[0].encoder,reverse[0]),travel[1].ratio(s.axes[1].encoder,reverse[1]),
        "Read-only input (firmware "+std::to_string(s.axes[0].firmware)+"): no force feel"};
    return result;
}

// ---- The remembered range: Output/BrunnerDirect/read-only-travel.json ----
// {"version":2,"firmware":[642,642],"roll":{"low":-9000,"high":9100},"pitch":{"low":-7000,"high":7200}}
// Version 2 learns only from referenced samples. An unversioned file was written
// while the NG's homing sweep was still being learned, so it is ignored.
inline constexpr int travel_file_version=2;
inline std::string serialize_travel(const std::array<std::uint16_t,2>& firmware,const Travel& travel) {
    const auto axis=[](const TravelLearner& t) {return "{\"low\":"+std::to_string(t.low)+",\"high\":"+std::to_string(t.high)+"}";};
    return "{\"version\":"+std::to_string(travel_file_version)+",\"firmware\":["+std::to_string(firmware[0])+","+std::to_string(firmware[1])+"],\"roll\":"+axis(travel[0])+",\"pitch\":"+axis(travel[1])+"}\n";
}
// The saved range, only when it is well-formed and for the same firmware; anything
// else (garbage, another yoke, an inverted range) is ignored, never trusted.
inline std::optional<Travel> parse_travel(const std::string& text,const std::array<std::uint16_t,2>& firmware) {
    try {
        const auto root=parse_json(text);
        const auto* version=root.get("version");
        if (!version || version->kind!=Json::Kind::integer || version->integer!=travel_file_version) return std::nullopt;
        const auto* fw=root.get("firmware");
        if (!fw || fw->kind!=Json::Kind::array || fw->items.size()!=2) return std::nullopt;
        for (unsigned i=0;i<2;++i) if (fw->items[i].kind!=Json::Kind::integer || fw->items[i].integer!=firmware[i]) return std::nullopt;
        Travel travel;
        const char* names[2]={"roll","pitch"};
        for (unsigned i=0;i<2;++i) {
            const auto* axis=root.get(names[i]);
            const auto* low=axis?axis->get("low"):nullptr;
            const auto* high=axis?axis->get("high"):nullptr;
            if (!low || !high || low->kind!=Json::Kind::integer || high->kind!=Json::Kind::integer) return std::nullopt;
            const auto lo=low->integer,hi=high->integer;
            constexpr std::int64_t limit=1'000'000'000;
            if (lo>hi || lo<-limit || hi>limit) return std::nullopt;
            travel[i].low=static_cast<std::int32_t>(lo);travel[i].high=static_cast<std::int32_t>(hi);travel[i].seen=true;
        }
        return travel;
    } catch (const std::exception&) {return std::nullopt;}
}
}
