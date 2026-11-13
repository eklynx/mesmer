#pragma once
// The feel workbench demo's followed state (workbench_demo.cpp --state FILE):
// a small JSON file a capture script rewrites so the page can be stepped
// through a film's frames (docs/brunner/video/promo/tablet_capture.mjs). The
// demo re-reads it whenever its text changes and stands in for X-Plane and
// the yoke with it: what X-Plane would report for pitch and roll, the yoke
// and the wheel where the film has them. Demo only; nothing in the plugin
// includes this.
//
//   {"seq": 12, "scenario": "ready", "ias": 110, "bank": 0,
//    "trim": 0.4, "tail_pa": 1988, "load_factor": 1, "tail_alpha": -2, "tail_stalled": 0,
//    "yoke": 11.8,                       // percent of travel, aft positive (the page's)
//    "wheel": -17.5,                     // percent of travel, right positive
//    "left": {"pa": 1962, "alpha_deg": 2, "stalled": 0}, "right": {...}}
//
// Every key is optional. A missing tail_pa is the indicated airspeed's (no
// propwash), a missing side the right's or the airspeed's, level and unstalled.
#include "force/elevator.hpp"
#include "force/aileron.hpp"
#include "json.hpp"
#include <cmath>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace brunner::workbench {
struct DemoState {
    std::int64_t seq=0;
    std::string scenario;   // empty: keep the demo's
    double ias=0,bank=0,trim=0,load_factor=1,tail_alpha=0,tail_stalled=0,yoke=0,wheel=0;
    double tail_pa=std::numeric_limits<double>::quiet_NaN();
    std::optional<AileronSide> left,right;
};
inline DemoState parse_demo_state(std::string_view text) {
    const auto doc=parse_json(text);
    if (doc.kind!=Json::Kind::object) throw JsonError("the demo state is not an object");
    const auto num=[&](const Json* j,double fallback) {
        if (!j) return fallback;
        if (j->kind==Json::Kind::integer) return double(j->integer);
        if (j->kind==Json::Kind::real) return j->real;
        throw JsonError("a demo state value is not a number");
    };
    const auto at=[&](const char* key,double fallback) {return num(doc.get(key),fallback);};
    DemoState s;
    s.seq=static_cast<std::int64_t>(at("seq",0));
    if (const auto* n=doc.get("scenario")) {if (n->kind!=Json::Kind::string) throw JsonError("scenario is not a string");s.scenario=n->text;}
    s.ias=at("ias",0);s.bank=at("bank",0);s.trim=std::clamp(at("trim",0),-1.0,1.0);s.load_factor=at("load_factor",1);
    s.tail_alpha=at("tail_alpha",0);s.tail_stalled=std::clamp(at("tail_stalled",0),0.0,1.0);
    s.yoke=std::clamp(at("yoke",0),-100.0,100.0);s.wheel=std::clamp(at("wheel",0),-100.0,100.0);
    s.tail_pa=at("tail_pa",std::numeric_limits<double>::quiet_NaN());
    const auto side=[&](const char* key)->std::optional<AileronSide> {
        const auto* j=doc.get(key);
        if (!j) return std::nullopt;
        if (j->kind!=Json::Kind::object) throw JsonError("a wing is not an object");
        return AileronSide{num(j->get("pa"),0),num(j->get("alpha_deg"),0),std::clamp(num(j->get("stalled"),0),0.0,1.0)};
    };
    s.left=side("left");s.right=side("right");
    return s;
}
// The air over each aileron the state says: its own sides, else the
// airspeed's (pa_at_ias), level and unstalled; one side given serves both.
inline AileronInputs demo_roll_inputs(const DemoState& s,double pa_at_ias) {
    const AileronSide still{pa_at_ias,0,0};
    const AileronSide right=s.right?*s.right:s.left?*s.left:still;
    return {s.left?*s.left:right,right};
}
}
