// The default YokeProfile (brunner/yoke_profile.hpp), used for every model today.
#include "brunner/yoke_profile.hpp"

namespace brunner {
namespace {
class DefaultYokeProfile final : public YokeProfile {
public:
    std::string_view name() const override { return "default"; }
    int button_count() const override { return 6; }
    std::string_view invalid_reason(const hid::JoystickState& state) const override {
        constexpr std::uint32_t rocker = (1u << 3) | (1u << 4);   // buttons 4 (down) and 5 (up) are one rocker: never both
        if ((state.buttons & rocker) == rocker) return "buttons 4 and 5 together";
        if (!state.padding_clear) return "padding bits set";
        return {};
    }
};
}
const YokeProfile& yoke_profile_for(const UsbId&) {
    static const DefaultYokeProfile profile;
    return profile;
}
}
