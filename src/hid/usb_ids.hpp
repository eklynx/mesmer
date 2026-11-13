// SOURCE OF TRUTH for which USB devices Mesmer treats as the yoke's HID bridge.
// Specification: docs/hid-bridge.md. Matching rules beyond the id (usage page,
// report sizes, "exactly one") live in hid/hid_device.hpp; the firmware gate lives
// in reader.cpp. An id here is necessary, never sufficient, to move a motor.
#pragma once
#include <array>
#include <cstdint>
#include <vector>

namespace brunner {
struct UsbId {
    std::uint16_t vendor, product;
};
// Every bridge id Mesmer will open. Add an id only when its HID descriptor and
// controller protocol are the same as the first one's; hid/hid_device.hpp's
// select_bridge refuses a different descriptor, and any firmware but 367 is read-only (readonly.hpp).
inline constexpr std::array<UsbId, 2> bridge_ids{{
    {0x25bb, 0x0081}, // original CLS USB Bridge
    {0x25bb, 0x008b}, // CLS-E NG: same HID framing; reports firmware v642, so it runs read-only
}};
// The ids to open when no presets are given (the built-in models').
inline std::vector<UsbId> default_usb_ids() {return {bridge_ids.begin(),bridge_ids.end()};}
inline bool id_listed(const std::vector<UsbId>& ids,std::uint16_t vendor,std::uint16_t product) {
    for (const auto& id:ids) if (id.vendor==vendor && id.product==product) return true;
    return false;
}
constexpr bool is_bridge_id(std::uint16_t vendor, std::uint16_t product) {
    for (const auto& id : bridge_ids)
        if (id.vendor == vendor && id.product == product) return true;
    return false;
}
}
