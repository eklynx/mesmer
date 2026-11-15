// SOURCE OF TRUTH for turning the yoke's joystick-interface HID input reports into buttons and a hat.
// Specification: docs/dev/button-commands.md. Read this header, not report_decoder.cpp, to use it.
// Portable: no operating-system code, so every platform's joystick reader shares it.
//
// Happy path: parse_report_layout(descriptor) reads the interface's HID report descriptor once and
// returns a ReportLayout (where buttons 1..N, the hat and the constant padding bits sit). decode_report(layout,
// report) then turns each input report into a JoystickState: the pressed buttons as a bitmask (bit n-1 is
// button n), the hat as 0 (centred) or 1..8 (1 up, clockwise), and whether every padding bit was zero.
//
// Error cases (none thrown):
//   parse_report_layout  returns nullopt when the descriptor has no Button usages and no Hat Switch (not a
//                        joystick), is malformed, or is truncated. A descriptor that has more than 32 buttons
//                        keeps the first 32. Fields it does not use (X, Y, ...) are skipped, not an error.
//   decode_report        returns nullopt when the report is shorter than the layout needs. Hat values outside
//                        the logical range (the "null state") decode as 0.
// The report passed in is the payload: when the layout has a report ID the caller passes the bytes after it.
#pragma once
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace hid {
// One run of bits in the report payload.
struct BitField {
    int first_bit = 0;   // from the first payload bit, least significant bit of byte 0 first
    int bit_count = 0;
};
struct ReportLayout {
    bool has_report_id = false;
    int report_bits = 0;                       // the payload length in bits, rounded up to bytes when decoding
    std::vector<BitField> buttons;             // buttons[n-1] is button n (1 bit each)
    std::optional<BitField> hat;
    int hat_logical_min = 1, hat_logical_max = 8;
    std::vector<BitField> padding;             // the constant (padding) runs: all zero in a genuine report
    std::size_t payload_bytes() const { return static_cast<std::size_t>((report_bits + 7) / 8); }
};
struct JoystickState {
    std::uint32_t buttons = 0;     // bit n-1 set while button n is pressed
    int hat = 0;                   // 0 centred, 1 up, 2 up-right, ... 8 up-left (clockwise)
    bool padding_clear = true;     // false when any padding bit was set
    bool operator==(const JoystickState& o) const {
        return buttons == o.buttons && hat == o.hat && padding_clear == o.padding_clear;
    }
};
std::optional<ReportLayout> parse_report_layout(const std::vector<std::uint8_t>& descriptor);
std::optional<JoystickState> decode_report(const ReportLayout& layout, const std::uint8_t* report, std::size_t length);
}
