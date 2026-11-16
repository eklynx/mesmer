// SOURCE OF TRUTH for what a yoke model's joystick HID reports may contain. Specification: docs/dev/button-commands.md.
// Read this header, not default_hid_profile.cpp, to use or add a profile. Applies to the HID read method only.
//
// Happy path: hid_profile_for(usb_id) returns the profile for the model that was opened; today every model gets the same
// default profile (all yokes are assumed to behave alike). It judges each decoded report with invalid_reason(): empty means
// genuine.
//
// Error cases (none thrown): invalid_reason() names the rule a report broke ("buttons 4 and 5 together", "padding bits set")
// and the caller drops the report. hid_profile_for never fails; an unknown id gets the default profile. To support a model
// that differs, add a profile in its own folder under buttons/input/ and return it from hid_profile_for; nothing else changes.
#pragma once
#include "hid/report_decoder.hpp"
#include "hid/usb_ids.hpp"
#include <string_view>

namespace buttons {
class HidProfile {
public:
    virtual ~HidProfile() = default;
    virtual std::string_view name() const = 0;
    // Empty when the report is genuine, else the rule it broke (a short phrase for the log).
    virtual std::string_view invalid_reason(const hid::JoystickState& state) const = 0;
};
const HidProfile& hid_profile_for(const brunner::UsbId& id);
}
