// Platform-independent bridge selection. Contract: hid/hid_device.hpp.
#include "hid/hid_device.hpp"
#include "hid/yoke_buttons.hpp"
#include "hid/usb_ids.hpp"

namespace brunner {
namespace {
bool has_bridge_descriptor(const HidCandidate& c) {
    return c.usage_page == 0xff00 && c.usage == 0xff02 && c.input_length == 64 && c.output_length == 64;
}
}
std::size_t select_bridge(const std::vector<HidCandidate>& candidates,const std::vector<UsbId>& ids,
    const std::vector<std::uint16_t>& unknown_vendors) {
    // Tier 1: a known model's bridge. Tier 2, only if tier 1 is empty: an unknown model of a listed vendor.
    for (const bool known:{true,false}) {
        std::size_t found = candidates.size();
        unsigned matches = 0;
        for (std::size_t i = 0; i < candidates.size(); ++i) {
            const auto& c = candidates[i];
            const bool qualifies = known ? id_listed(ids, c.vendor, c.product) : vendor_listed(unknown_vendors, c.vendor);
            if (qualifies && has_bridge_descriptor(c)) {
                found = i;
                ++matches;
            }
        }
        if (matches > 1) throw std::runtime_error("Expected one original CLS USB Bridge; found several");
        if (matches == 1) return found;
    }
    throw BridgeAbsent{};
}
#ifndef _WIN32
// Windows-only notions (hid/hid_device.hpp), so every non-Windows build and test that links the
// reader has them: a Mac has nothing to switch the bridge back on with.
bool bridge_disabled_in_windows() {return false;}
std::string turn_bridge_on_in_windows() {return "Turning the yoke's USB on is a Windows feature";}
// No virtual joystick off Windows: every call does nothing (hid/yoke_buttons.hpp).
struct YokeButtons::Impl {};
YokeButtons::YokeButtons() {}
YokeButtons::~YokeButtons()=default;
void YokeButtons::update(const Snapshot&,Clock::time_point,const Log&) {}
void YokeButtons::close() {}
void YokeButtons::unload() {}
#endif
}
