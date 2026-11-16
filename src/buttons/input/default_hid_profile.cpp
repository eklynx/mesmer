// The default HidProfile (buttons/input/hid_profile.hpp), used for every model today.
#include "buttons/input/hid_profile.hpp"

namespace buttons {
namespace {
class DefaultHidProfile final : public HidProfile {
public:
    std::string_view name() const override { return "default"; }
    std::string_view invalid_reason(const hid::JoystickState& state) const override {
        constexpr std::uint32_t rocker = (1u << 3) | (1u << 4);   // buttons 4 (down) and 5 (up) are one rocker: never both
        if ((state.buttons & rocker) == rocker) return "buttons 4 and 5 together";
        if (!state.padding_clear) return "padding bits set";
        return {};
    }
};
}
const HidProfile& hid_profile_for(const brunner::UsbId&) {
    static const DefaultHidProfile profile;
    return profile;
}
}
