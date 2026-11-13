#pragma once
#include <cstddef>

namespace brunner {
// The yoke's left rocker looks down at the panel in two steps (the owner,
// 2026-11-10: the right hand stays on the mouse): back goes from ahead to half
// down to the panel, forward comes back up the same way. Half and the panel
// are X-Plane's Quick Looks 2 and 1, saved by the pilot; ahead is X-Plane's
// default view. The plugin notes whichever of the three X-Plane was last sent
// to, by any button or key, so a step goes from there; a view moved by the
// mouse alone is not seen.
enum class Look {ahead,half,panel};
constexpr std::size_t look_views=3;
inline Look look_down(Look from) {return from==Look::ahead?Look::half:Look::panel;}
inline Look look_up(Look from) {return from==Look::panel?Look::half:Look::ahead;}
// X-Plane's command for each view, in Look's order.
inline const char* look_command(Look look) {
    switch (look) {
    case Look::ahead:return "sim/view/default_view";
    case Look::half:return "sim/view/quick_look_1";
    case Look::panel:return "sim/view/quick_look_0";
    }
    return "sim/view/default_view";
}
} // namespace brunner
