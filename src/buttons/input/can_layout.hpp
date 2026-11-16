// SOURCE OF TRUTH for where a yoke model keeps its buttons on the bridge, for the CanBus read method. Specification:
// docs/dev/button-commands.md ("Read method: CanBus", "What each yoke has"). Portable plain data; a yoke's preset holds one
// (YokePreset::button_registers) and the reader hands it to the CanSource at the start of each USB session.
//
// A layout names up to two read-only controller registers on node 2, both from brunner::button_register's allowlist: the buttons,
// where can_button_n is bit `shift + n - 1` of the register (n from 1 to `bits`), and optionally the hat, a register holding 0
// (centred) or 1 to 8 clockwise from up, which is can_hat_n .. can_hat_nw. The original CLS-E has only the buttons, in
// 0x60fd:00 (bits 16 to 31 are its slots 1 to 16). The CLS-E NG has both in its own record: 0x2880:03 holds the buttons 1 to 6 in bits
// 0 to 5 and 0x2880:04 the hat. Each read is one SDO request, about 2 ms of a 20 ms sampling cycle.
//
// Error cases: none at run time (the layout is validated when the preset is parsed: a register not on the allowlist, a width
// that does not match it, or `shift`/`bits` outside the register refuses the preset).
#pragma once
#include <cstdint>
#include <optional>

namespace buttons {
struct CanRegister {
    std::uint16_t index = 0;
    std::uint8_t sub = 0;
    std::uint8_t bytes = 0;   // 1, 2 or 4, as the register is read
    bool operator==(const CanRegister& o) const { return index == o.index && sub == o.sub && bytes == o.bytes; }
};
// The registers a yoke's buttons may be read from, and their width in bytes (0: not one): the allowlist, READ ONLY, mirrored by
// brunner::button_register in brunner/reference.hpp, which is what lets the bridge send the request at all. 0x60fd:00 is the
// controller's digital inputs; 0x2880 is the CLS-E NG's own button record (entries 3 and 4).
inline std::uint8_t can_register_bytes(std::uint16_t index, std::uint8_t sub) {
    return index == 0x60fd && sub == 0 ? 4 : index == 0x2880 && (sub == 3 || sub == 4) ? 1 : 0;
}
struct CanLayout {
    CanRegister buttons{0x60fd, 0, 4};
    int shift = 16;   // can_button_1 is this bit of the register
    int bits = 16;    // how many buttons: 1 to 16
    std::optional<CanRegister> hat;
    bool operator==(const CanLayout& o) const { return buttons == o.buttons && shift == o.shift && bits == o.bits && hat == o.hat; }
};
// The original CLS-E's (the default for a model that gives none).
inline CanLayout cls_e_can_layout() { return {}; }
// The CLS-E NG's own button record, node 2's 0x2880.
inline CanLayout cls_e_ng_can_layout() {
    CanLayout layout;
    layout.buttons = {0x2880, 3, 1};
    layout.shift = 0;
    layout.bits = 6;
    layout.hat = CanRegister{0x2880, 4, 1};
    return layout;
}
}
