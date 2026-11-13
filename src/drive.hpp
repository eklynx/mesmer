// SOURCE OF TRUTH for the numbers the force models and the drive writes use:
// the active Drive, derived from a yoke model's preset (yoke_preset.hpp) and
// the controller's own limit registers. Specification: docs/yoke-presets.md.
//
// Every use reads active_drive(); nothing is passed around, so no call can
// silently use another model's numbers. Until a yoke is identified the active
// drive is the CLS-E's (cls_e_drive()), which reproduces every constant the
// models were built on; tests that need another set use ScopedDrive.
//
// Force ceilings: for each axis the lower of the preset's ceiling (the spec) and
// the controller's limit register (read, never written); one budget for both axes
// follows from the lower of the two, less the preset's margin. Forces stop there:
// the models clamp, they do not ask for more.
//
// Thread safety: the reader sets the active drive once, when it identifies the
// yoke, before any force is computed; the plugin thread only reads it. Drives
// live as long as the process (a handful at most), so a reader of an older
// drive never dangles. A mixed moment (a value computed under the old drive,
// checked under the new) can only be refused by the checks, never accepted wrongly.
#pragma once
#include "yoke_preset.hpp"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>

namespace brunner {
struct Drive {
    std::string model="cls-e";                  // the preset's name
    double roll_travel_counts=13000,pitch_travel_counts=12000;
    double wheel_deg=86;
    std::uint32_t target_torque=50;
    std::uint16_t status_stopped=0x0431,status_ready_mask=0x1017,status_acquired_mask=0x1004; // see YokePreset
    std::uint16_t spring_mode=0x0205;                      // the resting force mode, see YokePreset
    bool idle_enabled=false;                               // idles with motors enabled, see YokePreset
    int roll_encoder_sign=-1,pitch_encoder_sign=-1;       // spring frame = sign * encoder
    bool roll_input_reverse=true,pitch_input_reverse=false;
    // What each controller's limit register read when this drive was made. The
    // plugin never writes it; the writes' checks compare against it.
    std::uint32_t roll_limit_register=4500,pitch_limit_register=4500;
    // Effective ceilings per axis: min(preset ceiling, register).
    int roll_ceiling_units=4500,pitch_ceiling_units=4500;
    // The one budget for both axes: the lower ceiling, and the headroom kept under it.
    int limit_units=4500,margin_units=500;
    bool operator==(const Drive& o) const {
        return model==o.model && roll_travel_counts==o.roll_travel_counts && pitch_travel_counts==o.pitch_travel_counts
            && wheel_deg==o.wheel_deg && target_torque==o.target_torque && roll_encoder_sign==o.roll_encoder_sign && status_stopped==o.status_stopped
            && spring_mode==o.spring_mode && idle_enabled==o.idle_enabled
            && status_ready_mask==o.status_ready_mask && status_acquired_mask==o.status_acquired_mask
            && pitch_encoder_sign==o.pitch_encoder_sign && roll_input_reverse==o.roll_input_reverse && pitch_input_reverse==o.pitch_input_reverse && roll_limit_register==o.roll_limit_register
            && pitch_limit_register==o.pitch_limit_register && roll_ceiling_units==o.roll_ceiling_units
            && pitch_ceiling_units==o.pitch_ceiling_units && limit_units==o.limit_units && margin_units==o.margin_units;
    }
};

// The drive for a model whose controllers' limit registers read these values. A
// register of zero (unread, or a controller that reports none) is refused: a
// ceiling must never be assumed.
inline Drive make_drive(const YokePreset& p,std::uint32_t roll_limit_register,std::uint32_t pitch_limit_register) {
    validate_preset(p);
    if (roll_limit_register==0 || pitch_limit_register==0 || roll_limit_register>65535 || pitch_limit_register>65535)
        throw PresetError("yoke preset \""+p.name+"\": a controller's limit register is not usable");
    Drive d;
    d.model=p.name;
    d.roll_travel_counts=p.roll_travel_counts;d.pitch_travel_counts=p.pitch_travel_counts;
    d.wheel_deg=p.wheel_deg;d.target_torque=static_cast<std::uint32_t>(p.target_torque);
    d.status_stopped=static_cast<std::uint16_t>(p.status_stopped);d.status_ready_mask=static_cast<std::uint16_t>(p.status_ready_mask);
    d.status_acquired_mask=static_cast<std::uint16_t>(p.status_acquired_mask);
    d.spring_mode=static_cast<std::uint16_t>(p.spring_mode);d.idle_enabled=p.idle_enabled;
    d.roll_encoder_sign=p.roll_encoder_sign;d.pitch_encoder_sign=p.pitch_encoder_sign;
    d.roll_input_reverse=p.roll_input_reverse;d.pitch_input_reverse=p.pitch_input_reverse;
    d.roll_limit_register=roll_limit_register;d.pitch_limit_register=pitch_limit_register;
    d.roll_ceiling_units=std::min<int>(p.roll_ceiling_units,static_cast<int>(roll_limit_register));
    d.pitch_ceiling_units=std::min<int>(p.pitch_ceiling_units,static_cast<int>(pitch_limit_register));
    d.limit_units=std::min(d.roll_ceiling_units,d.pitch_ceiling_units);
    d.margin_units=static_cast<int>(std::lround(d.limit_units*p.margin_fraction));
    return d;
}
// The CLS-E's: reproduces, exactly, the constants the models were built on.
inline const Drive& cls_e_drive() {
    static const Drive d=make_drive(builtin_cls_e(),4500,4500);
    return d;
}

namespace drive_detail {
inline std::atomic<const Drive*>& slot() {
    static std::atomic<const Drive*> s{&cls_e_drive()};
    return s;
}
inline std::mutex& pool_mutex() {static std::mutex m;return m;}
inline std::deque<Drive>& pool() {static std::deque<Drive> p;return p;}
}
inline const Drive& active_drive() {return *drive_detail::slot().load(std::memory_order_acquire);}
// Makes `d` the active drive and returns the process-lifetime copy now in force.
inline const Drive& set_active_drive(const Drive& d) {
    std::lock_guard<std::mutex> lock(drive_detail::pool_mutex());
    auto& pool=drive_detail::pool();
    const Drive* kept=nullptr;
    for (const auto& existing:pool) if (existing==d) {kept=&existing;break;}
    if (!kept) {pool.push_back(d);kept=&pool.back();}
    drive_detail::slot().store(kept,std::memory_order_release);
    return *kept;
}
// For tests: the active drive for a scope, then whatever was active before.
class ScopedDrive {
    const Drive* previous_;
 public:
    explicit ScopedDrive(const Drive& d):previous_(&active_drive()) {set_active_drive(d);}
    ~ScopedDrive() {drive_detail::slot().store(previous_,std::memory_order_release);}
    ScopedDrive(const ScopedDrive&)=delete;
    ScopedDrive& operator=(const ScopedDrive&)=delete;
};
}
