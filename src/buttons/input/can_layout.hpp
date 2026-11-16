// SOURCE OF TRUTH for where a yoke model keeps each of its buttons and hat positions on the bridge. Specification:
// docs/dev/button-commands.md ("Where the buttons are read", "What each yoke has"). Portable plain data; a yoke's preset holds one
// (YokePreset::button_registers) and the reader hands it to the CanSource at the start of each USB session.
//
// A layout is a map: the key is a control's canonical input (button_1..button_16, hat_n..hat_nw), the value is the CanSignal that says
// when that control is pressed: a controller object (node, index, sub-index; read-only, from the allowlist below, whose entry gives the
// width) and either a *mask* (pressed when all of its bits are set in the value read) or a *value* (pressed when the value read is
// exactly that). A control with no entry is one the yoke does not have: it is never pressed. The original CLS-E's buttons are bits 16
// to 31 of node 2's 0x60fd:00 (a mask each, no hat). The CLS-E NG's are bits 0 to 5 of its own record 0x2880:03 (a mask each) and
// its hat positions are the values 1 to 8 of 0x2880:04. Each distinct object is read once per sampling cycle (one SDO request, about
// 2 ms of the 20 ms cycle), however many controls use it.
//
// Error cases: none at run time (the layout is validated when the preset is parsed: a key that is not a button or hat name, a node
// other than 1 or 2, an object not on the allowlist, a mask of zero or wider than the object, a value wider than the object, or
// both or neither of mask and value refuses the preset).
#pragma once
#include "buttons/controls.hpp"
#include <cstdint>
#include <map>

namespace buttons {
// The objects a yoke's buttons may be read from, and their width in bytes (0: not one): the allowlist, READ ONLY, mirrored by
// brunner::button_register in brunner/reference.hpp, which is what lets the bridge send the request at all. 0x60fd:00 is the
// controller's digital inputs; 0x2880 is the CLS-E NG's own button record (entries 3 and 4).
inline std::uint8_t can_register_bytes(std::uint16_t index, std::uint8_t sub) {
    return index == 0x60fd && sub == 0 ? 4 : index == 0x2880 && (sub == 3 || sub == 4) ? 1 : 0;
}
struct CanSignal {
    std::uint8_t node = 2;       // 1 or 2
    std::uint16_t index = 0;     // the object
    std::uint8_t sub = 0;
    bool is_mask = true;         // a mask: all these bits set; otherwise a value: exactly this
    std::uint32_t operand = 0;   // the mask or the value
    std::uint8_t bytes() const { return can_register_bytes(index, sub); }
    // Whether `word`, read from this signal's object, says the control is pressed.
    bool pressed(std::uint32_t word) const { return is_mask ? (word & operand) == operand : word == operand; }
    bool operator==(const CanSignal& o) const { return node == o.node && index == o.index && sub == o.sub && is_mask == o.is_mask && operand == o.operand; }
};
using CanLayout = std::map<Input, CanSignal>;

// The original CLS-E's (the default for a model that gives none): slots 1 to 5 and 7 to 14, bits 16 to 31 of node 2's 0x60fd.
inline CanLayout cls_e_can_layout() {
    CanLayout layout;
    for (int n = 1; n <= 14; ++n)
        if (n != 6) layout[{InputKind::button, n}] = CanSignal{2, 0x60fd, 0, true, 1u << (15 + n)};
    return layout;
}
// The CLS-E NG's own button record, node 2's 0x2880: entry 3 the six buttons (a bit each), entry 4 the hat (a value per position).
inline CanLayout cls_e_ng_can_layout() {
    CanLayout layout;
    for (int n = 1; n <= 6; ++n) layout[{InputKind::button, n}] = CanSignal{2, 0x2880, 3, true, 1u << (n - 1)};
    for (int k = 1; k <= hat_positions; ++k) layout[{InputKind::hat, k}] = CanSignal{2, 0x2880, 4, false, static_cast<std::uint32_t>(k)};
    return layout;
}
}
