// SOURCE OF TRUTH for what a yoke model's joystick reports may contain. Specification:
// docs/dev/button-commands.md. Read this header, not default_yoke_profile.cpp, to use or add a profile.
//
// Happy path: yoke_profile_for(usb_id) returns the profile for the model that was opened; today every
// model gets the same default profile (all yokes are assumed to behave alike). The profile says how many
// buttons the yoke has and judges each decoded report with invalid_reason(): empty means genuine.
//
// Error cases (none thrown): invalid_reason() names the rule a report broke ("buttons 4 and 5 together",
// "padding bits set") and the caller drops the report. yoke_profile_for never fails; an unknown id gets the
// default profile. To support a model that differs, add a profile in its own folder under brunner/ and
// return it from yoke_profile_for; nothing else changes.
#pragma once
#include "hid/report_decoder.hpp"
#include "hid/usb_ids.hpp"
#include <string_view>

namespace brunner {
class YokeProfile {
public:
    virtual ~YokeProfile() = default;
    virtual std::string_view name() const = 0;
    // Buttons 1..button_count() exist; the hat always has eight positions.
    virtual int button_count() const = 0;
    // Empty when the report is genuine, else the rule it broke (a short phrase for the log).
    virtual std::string_view invalid_reason(const hid::JoystickState& state) const = 0;
};
const YokeProfile& yoke_profile_for(const UsbId& id);
}
