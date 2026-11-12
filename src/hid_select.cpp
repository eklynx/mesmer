// Platform-independent bridge selection. Contract: hid_device.hpp.
#include "hid_device.hpp"
#include "usb_ids.hpp"

namespace brunner {
namespace {
bool is_bridge(const HidCandidate& c) {
    return is_bridge_id(c.vendor, c.product) && c.usage_page == 0xff00 && c.usage == 0xff02
        && c.input_length == 64 && c.output_length == 64;
}
}
std::size_t select_bridge(const std::vector<HidCandidate>& candidates) {
    std::size_t found = candidates.size();
    unsigned matches = 0;
    for (std::size_t i = 0; i < candidates.size(); ++i)
        if (is_bridge(candidates[i])) {
            found = i;
            ++matches;
        }
    if (matches == 0) throw BridgeAbsent{};
    if (matches != 1) throw std::runtime_error("Expected one original CLS USB Bridge; found several");
    return found;
}
}
