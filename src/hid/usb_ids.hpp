// SOURCE OF TRUTH for which USB devices Mesmer treats as the yoke's HID bridge.
// Specification: docs/dev/hid-bridge.md. Matching rules beyond the id (usage page,
// report sizes, "exactly one") live in hid/hid_device.hpp; which models are driven
// and which are read-only is brunner/reader.cpp and brunner/readonly.hpp. An id here
// is necessary, never sufficient, to move a motor.
#pragma once
#include <array>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <string>
#include <vector>

namespace brunner {
struct UsbId {
    std::uint16_t vendor, product;
};
// Every bridge id Mesmer will open. Add an id only when its HID descriptor and
// controller protocol are the same as the first one's; hid/hid_device.hpp's
// select_bridge refuses a different descriptor.
inline constexpr std::array<UsbId, 2> bridge_ids{{
    {0x25bb, 0x0081}, // original CLS USB Bridge
    {0x25bb, 0x008b}, // CLS-E NG: same HID framing and protocol; its preset is not yet verified (config/yoke/yoke_preset.hpp)
}};
// Brunner's USB vendor id. A bridge from this vendor whose product id is not listed is an unknown
// yoke: it is opened read-only, never driven (docs/dev/read-only-mode.md). Another vendor's device is never opened.
inline constexpr std::uint16_t brunner_vendor=0x25bb;
inline bool vendor_listed(const std::vector<std::uint16_t>& vendors,std::uint16_t vendor) {
    for (const auto v:vendors) if (v==vendor) return true;
    return false;
}
// One id field as it is written everywhere (presets, saved files, logs, documents): "0x25BB", upper-case digits.
inline std::string hex_id_text(std::uint16_t value) {
    char text[8];std::snprintf(text,sizeof(text),"0x%04X",static_cast<unsigned>(value));return text;
}
// "0x25BB:0x008B", a whole USB id as it is written in messages and documents.
inline std::string usb_text(const UsbId& id) {return hex_id_text(id.vendor)+":"+hex_id_text(id.product);}
// The value of "0x25BB" (either case of the digits and of the x, 1 to 4 digits, the prefix is required);
// nullopt for anything else, including a decimal number, a missing prefix, zero and values over 0xffff.
inline std::optional<std::uint16_t> parse_hex_id(const std::string& text) {
    if (text.size()<3 || text.size()>6 || text[0]!='0' || (text[1]!='x' && text[1]!='X')) return std::nullopt;
    unsigned value=0;
    for (std::size_t i=2;i<text.size();++i) {
        const char c=text[i];
        const unsigned digit=c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:c>='A'&&c<='F'?c-'A'+10:16;
        if (digit>15) return std::nullopt;
        value=value*16+digit;
    }
    if (value==0) return std::nullopt;
    return static_cast<std::uint16_t>(value);
}
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
