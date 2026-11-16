// make_virtual_joystick() (buttons/actions/vjoy_action.hpp): the operating system's virtual joystick, or nullptr.
#include "buttons/actions/vjoy_action.hpp"
#ifdef _WIN32
#include "buttons/actions/win/vjoy_win.hpp"
#endif

namespace buttons {
#ifdef _WIN32
std::unique_ptr<VirtualJoystick> make_virtual_joystick() { return std::make_unique<VJoy>(); }
#else
// No virtual joystick driver on this operating system yet: the seam for one is here.
std::unique_ptr<VirtualJoystick> make_virtual_joystick() { return nullptr; }
#endif
}
