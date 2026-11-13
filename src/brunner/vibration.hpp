#pragma once
#include "brunner/reference.hpp"

namespace brunner {
// The drive's own vibration generator (docs/brunner/vibration.md), from
// CLS2Sim 5.9 MicroDriveDevice.PrepareUpdate (RVA 0xb39d0, IL 0x6dbc..0x84bc).
// Confirmed in its code: two generators per drive, each an amplitude in the
// drive's force units (the units of its 4500 protection limit) and a time;
// CLS2Sim turns them on with bit 4 of 0x2101:01 and off by clearing it.
// The slow generator carries CLS2Sim's stick shaker and ground rumble, the
// fast one its engine vibration. Buffet uses the slow one only.
inline constexpr Register vibration_slow_force{0x2108,0,2},vibration_slow_time{0x2109,0,2};
inline constexpr Register vibration_fast_force{0x2099,0,2},vibration_fast_time{0x209a,0,2};
inline constexpr std::uint16_t vibration_enable_bit=1u<<4;
inline bool vibration_same(Register a,Register b) {return a.index==b.index && a.sub==b.sub && a.bytes==b.bytes;}
inline bool vibration_register(Register r) {
    return r.sub==0 && r.bytes==2 && (r.index==0x2108 || r.index==0x2109 || r.index==0x2099 || r.index==0x209a);
}
// The plugin's bound on the slow amplitude: a tenth of the protection limit,
// half of CLS2Sim's own largest stick-shaker amplitude for this drive class
// (20% of 0x211b's 4500 = 900). Untested with the motors on.
inline constexpr std::uint32_t buffet_ceiling_units=450;
// CLS2Sim's own range for the slow time from its percentage (130*p+10, so
// 10..140); its raw path never writes under 10. Units inferred, not confirmed.
inline constexpr std::uint32_t buffet_period_min=10,buffet_period_max=140;
// Buffet writes only the slow amplitude and time; zero is always allowed so
// any end can clear them. The fast generator is never written.
inline bool vibration_write_allowed(unsigned node,RegisterWrite op) {
    if (node<1 || node>2) return false;
    if (vibration_same(op.reg,vibration_slow_force)) return op.value<=buffet_ceiling_units;
    if (vibration_same(op.reg,vibration_slow_time)) return op.value>=buffet_period_min && op.value<=buffet_period_max;
    return false;
}
}
