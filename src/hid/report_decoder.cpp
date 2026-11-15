// Implementation of hid/report_decoder.hpp (read that header for the contract).
#include "hid/report_decoder.hpp"

namespace hid {
namespace {
constexpr int usage_page_generic_desktop = 0x01, usage_page_button = 0x09, usage_hat_switch = 0x39;
constexpr int max_buttons = 32;

struct Globals {
    int usage_page = 0, logical_min = 0, logical_max = 0, report_size = 0, report_count = 0;
};
struct Locals {
    std::vector<int> usages;
    int usage_min = -1, usage_max = -1;
    void clear() { usages.clear(); usage_min = usage_max = -1; }
};
std::int32_t signed_value(std::uint32_t value, int bytes) {
    if (bytes == 0) return 0;
    const std::uint32_t sign = 1u << (bytes * 8 - 1);
    return (value & sign) ? static_cast<std::int32_t>(value | ~((sign << 1) - 1)) : static_cast<std::int32_t>(value);
}
bool bit_set(const std::uint8_t* report, int bit) { return (report[bit / 8] >> (bit % 8)) & 1; }
}

std::optional<ReportLayout> parse_report_layout(const std::vector<std::uint8_t>& d) {
    ReportLayout layout;
    Globals g;
    Locals l;
    int bit = 0;
    bool any_input = false;
    for (std::size_t i = 0; i < d.size();) {
        const std::uint8_t prefix = d[i++];
        if (prefix == 0xfe) {   // long item: skip it whole
            if (i + 2 > d.size()) return std::nullopt;
            i += 2 + d[i];
            continue;
        }
        const int size = (prefix & 3) == 3 ? 4 : (prefix & 3), type = (prefix >> 2) & 3, tag = prefix >> 4;
        if (i + static_cast<std::size_t>(size) > d.size()) return std::nullopt;
        std::uint32_t raw = 0;
        for (int k = 0; k < size; ++k) raw |= static_cast<std::uint32_t>(d[i + k]) << (8 * k);
        i += static_cast<std::size_t>(size);
        if (type == 1) {          // global
            switch (tag) {
            case 0: g.usage_page = static_cast<int>(raw); break;
            case 1: g.logical_min = signed_value(raw, size); break;
            case 2: g.logical_max = signed_value(raw, size); break;
            case 7: g.report_size = static_cast<int>(raw); break;
            case 8: layout.has_report_id = true; break;
            case 9: g.report_count = static_cast<int>(raw); break;
            default: break;
            }
        } else if (type == 2) {   // local
            if (tag == 0) l.usages.push_back(static_cast<int>(raw));
            else if (tag == 1) l.usage_min = static_cast<int>(raw);
            else if (tag == 2) l.usage_max = static_cast<int>(raw);
        } else if (type == 0 && tag == 8) {   // Input
            any_input = true;
            const bool constant = raw & 1;
            const int total = g.report_size * g.report_count;
            if (total < 0 || g.report_size < 0 || g.report_count < 0 || total > 4096) return std::nullopt;
            if (constant) {
                layout.padding.push_back({bit, total});
            } else if (g.usage_page == usage_page_button && g.report_size == 1) {
                for (int k = 0; k < g.report_count && static_cast<int>(layout.buttons.size()) < max_buttons; ++k)
                    layout.buttons.push_back({bit + k, 1});
            } else if (g.usage_page == usage_page_generic_desktop && g.report_count == 1 && !l.usages.empty() &&
                       l.usages.front() == usage_hat_switch) {
                layout.hat = BitField{bit, g.report_size};
                layout.hat_logical_min = g.logical_min;
                layout.hat_logical_max = g.logical_max;
            }
            bit += total;
        }
        if (type == 0) l.clear();   // a main item ends the local state
    }
    if (!any_input || (layout.buttons.empty() && !layout.hat)) return std::nullopt;
    layout.report_bits = bit;
    return layout;
}

std::optional<ReportLayout> layout_from_fields(const std::vector<UsageBits>& usages, int report_bits, bool has_report_id) {
    if (report_bits <= 0) return std::nullopt;
    ReportLayout layout;
    layout.has_report_id = has_report_id;
    layout.report_bits = report_bits;
    std::vector<bool> claimed(static_cast<std::size_t>(report_bits), false);
    std::vector<int> button_bit(max_buttons + 1, -1);
    for (const auto& u : usages) {
        for (const int b : u.bits) {
            if (b < 0 || b >= report_bits) return std::nullopt;
            claimed[static_cast<std::size_t>(b)] = true;
        }
        if (u.usage_page == usage_page_button && u.usage >= 1 && u.usage <= max_buttons && u.bits.size() == 1)
            button_bit[static_cast<std::size_t>(u.usage)] = u.bits.front();
        else if (u.usage_page == usage_page_generic_desktop && u.usage == usage_hat_switch && !layout.hat && !u.bits.empty()) {
            bool contiguous = true;
            for (std::size_t k = 1; k < u.bits.size(); ++k) contiguous = contiguous && u.bits[k] == u.bits[k - 1] + 1;
            if (contiguous) {
                layout.hat = BitField{u.bits.front(), static_cast<int>(u.bits.size())};
                layout.hat_logical_min = u.logical_min;
                layout.hat_logical_max = u.logical_max;
            }
        }
    }
    for (int n = 1; n <= max_buttons && button_bit[static_cast<std::size_t>(n)] >= 0; ++n)
        layout.buttons.push_back({button_bit[static_cast<std::size_t>(n)], 1});
    for (int b = 0; b < report_bits;) {
        if (claimed[static_cast<std::size_t>(b)]) { ++b; continue; }
        const int first = b;
        while (b < report_bits && !claimed[static_cast<std::size_t>(b)]) ++b;
        layout.padding.push_back({first, b - first});
    }
    if (layout.buttons.empty() && !layout.hat) return std::nullopt;
    return layout;
}

std::optional<JoystickState> decode_report(const ReportLayout& layout, const std::uint8_t* report, std::size_t length) {
    if (length < layout.payload_bytes()) return std::nullopt;
    JoystickState state;
    for (std::size_t n = 0; n < layout.buttons.size(); ++n)
        if (bit_set(report, layout.buttons[n].first_bit)) state.buttons |= 1u << n;
    if (layout.hat) {
        int raw = 0;
        for (int k = 0; k < layout.hat->bit_count; ++k)
            if (bit_set(report, layout.hat->first_bit + k)) raw |= 1 << k;
        const int position = raw - layout.hat_logical_min + 1;
        const int positions = layout.hat_logical_max - layout.hat_logical_min + 1;
        state.hat = (raw >= layout.hat_logical_min && raw <= layout.hat_logical_max && positions == 8) ? position : 0;
    }
    for (const auto& run : layout.padding)
        for (int k = 0; k < run.bit_count; ++k)
            if (bit_set(report, run.first_bit + k)) state.padding_clear = false;
    return state;
}
}
